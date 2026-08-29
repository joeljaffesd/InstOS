#pragma once

#include <InstOS.hpp>
#include <modules/Ethernet.hpp>
#include <circle/net/socket.h>
#include <circle/net/in.h>
#include <Barebone.hpp> // external/barebone: the portable cuttlebone wire protocol

namespace InstOS {
namespace Barebone {

// Cuttlebone-compatible defaults: 1400-byte packets on UDP port 63059
constexpr unsigned kPacketSize = 1400;
constexpr u16 kDefaultPort = 63059;

// One-directional state casting, cuttlebone-style: a Maker broadcasts a POD
// STATE struct as chunked UDP packets every frame; a Taker reassembles and
// keeps the latest complete frame. Bit-compatible with upstream Cuttlebone
// peers (a laptop running a stock Taker<STATE> receives our frames as-is).
// Each STATE must have exactly ONE writing machine; for bidirectional links
// run a second Maker/Taker pair on another port in the other direction.
//
// Requires an Ethernet module declared before it. No threads: upstream's
// send/receive loops live in these modules' update() pumps.

template <class STATE>
class Maker : public Module {
public:
	Maker(const char* pTargetIP, u16 nPort = kDefaultPort, unsigned nFPS = 30)
	: mPort(nPort), mSendIntervalUs(1000000 / (nFPS ? nFPS : 30)) {
		u8 IP[4];
		Ethernet::parseIP(pTargetIP, IP);
		mTarget.Set(IP);
	}

	// point at the app's live state; a snapshot is taken at each send
	void attach(const STATE* pState) { mpState = pState; }

	const char* name() const override { return "maker"; }

	boolean init() override {
		if (Ethernet::instance == nullptr) {
			CLogger::Get()->Write(name(), LogError,
					      "Requires an Ethernet module declared before it");
			return FALSE;
		}

		mpSocket = new CSocket(Ethernet::instance->net(), IPPROTO_UDP);

		CString Target;
		mTarget.Format(&Target);
		CLogger::Get()->Write(name(), LogNotice,
				      "Casting %u-byte state to %s:%u (%u parts/frame)",
				      (unsigned) sizeof(STATE), (const char*) Target,
				      (unsigned) mPort, (unsigned) PacketMakerT::TOTAL_PART_COUNT);
		return TRUE;
	}

	void update() override {
		if (mpState == nullptr || mpSocket == nullptr
		    || !Ethernet::instance->isUp()) {
			return;
		}

		unsigned nNow = CTimer::Get()->GetClockTicks();
		if (nNow - mLastSendTicks < mSendIntervalUs) {
			return;
		}
		mLastSendTicks = nNow;

		// snapshot, then chunk-and-send one whole frame
		memcpy(&mSnapshot, mpState, sizeof(STATE));
		PacketMakerT PacketMaker(mSnapshot, mFrameNumber++);
		barebone::Packet<kPacketSize> Packet;
		while (PacketMaker.fill(Packet)) {
			mpSocket->SendTo(&Packet,
					 barebone::Packet<kPacketSize>::HEADER_SIZE
					 + Packet.header.partSize,
					 MSG_DONTWAIT, mTarget, mPort);
		}
	}

private:
	using PacketMakerT = barebone::PacketMaker<STATE, barebone::Packet<kPacketSize>>;

	const STATE* mpState = nullptr;
	STATE mSnapshot;
	CSocket* mpSocket = nullptr;
	CIPAddress mTarget;
	u16 mPort;
	unsigned mSendIntervalUs;
	unsigned mLastSendTicks = 0;
	unsigned mFrameNumber = 0;
};

template <class STATE>
class Taker : public Module {
public:
	Taker(u16 nPort = kDefaultPort) : mPort(nPort) {}

	const char* name() const override { return "taker"; }

	// copies the newest complete frame into rState; TRUE if it was new
	boolean get(STATE& rState) {
		if (!mHasNew) {
			return FALSE;
		}
		mHasNew = FALSE;
		memcpy(&rState, &mFront, sizeof(STATE));
		return TRUE;
	}

	boolean init() override {
		if (Ethernet::instance == nullptr) {
			CLogger::Get()->Write(name(), LogError,
					      "Requires an Ethernet module declared before it");
			return FALSE;
		}

		mpSocket = new CSocket(Ethernet::instance->net(), IPPROTO_UDP);
		if (mpSocket->Bind(mPort) < 0) {
			CLogger::Get()->Write(name(), LogError, "Cannot bind port %u",
					      (unsigned) mPort);
			return FALSE;
		}

		CLogger::Get()->Write(name(), LogNotice,
				      "Listening on :%u for %u-byte states (%u parts/frame)",
				      (unsigned) mPort, (unsigned) sizeof(STATE),
				      (unsigned) PacketTakerT::TOTAL_PART_COUNT);
		return TRUE;
	}

	void update() override {
		if (mpSocket == nullptr) {
			return;
		}

		barebone::Packet<kPacketSize> Packet;
		int nBytes;
		while ((nBytes = mpSocket->Receive(&Packet, sizeof Packet, MSG_DONTWAIT)) > 0) {
			if (nBytes < (int) barebone::Packet<kPacketSize>::HEADER_SIZE) {
				continue;
			}
			if (Packet.header.partNumber >= PacketTakerT::TOTAL_PART_COUNT) {
				continue; // not our STATE (or a corrupt part)
			}

			if (mpPacketTaker == nullptr
			    || Packet.header.frameNumber != mpPacketTaker->frameNumber) {
				// new frame started: restart assembly (latest wins;
				// an incomplete previous frame is simply dropped)
				delete mpPacketTaker;
				mpPacketTaker = new PacketTakerT(mAssembly, Packet.header.frameNumber);
			}

			mpPacketTaker->take(Packet);
			if (mpPacketTaker->isComplete()) {
				memcpy(&mFront, &mAssembly, sizeof(STATE));
				mHasNew = TRUE;
				delete mpPacketTaker;
				mpPacketTaker = nullptr;

				if (!mFirstFrameLogged) {
					mFirstFrameLogged = TRUE;
					CLogger::Get()->Write(name(), LogNotice,
							      "Receiving state frames");
				}
			}
		}
	}

private:
	using PacketTakerT = barebone::PacketTaker<STATE, barebone::Packet<kPacketSize>>;

	STATE mAssembly;
	STATE mFront;
	PacketTakerT* mpPacketTaker = nullptr;
	CSocket* mpSocket = nullptr;
	u16 mPort;
	boolean mHasNew = FALSE;
	boolean mFirstFrameLogged = FALSE;
};

} // namespace Barebone
} // namespace InstOS
