#pragma once

#include <InstOS.hpp>
#include <circle/net/netsubsystem.h>
#include <circle/net/ipaddress.h>

namespace InstOS {

// Ethernet module: brings up Circle's TCP/IP stack on the CM4's integrated
// GENET gigabit MAC (the NANO-B's RJ45). Static IP by default — instruments
// shouldn't wait on a DHCP server, and a direct box-to-box cable has none.
//
// Circle's network stack runs as cooperative scheduler tasks, so this
// module's update() yields to the scheduler every loop pass — without that,
// nothing is ever received or sent.
class Ethernet : public Module {
public:
	inline static Ethernet* instance = nullptr; // for dependents' init checks

	// e.g. Ethernet{"192.168.10.1"} — /24 netmask, no gateway needed for
	// link-local box-to-box use
	Ethernet(const char* pIPAddress, const char* pNetMask = "255.255.255.0")
	: mIP(pIPAddress), mNetMask(pNetMask)
	{
		instance = this;
	}

	const char* name() const override { return "ethernet"; }

	CNetSubSystem* net() { return &mNet; }
	boolean isUp() { return mActive && mNet.IsRunning(); }

	boolean init() override {
		// bWaitForActivate=FALSE: don't stall boot on an unplugged cable;
		// the stack comes up in the background once link is established
		mActive = mNet.Initialize(FALSE);
		if (!mActive) {
			CLogger::Get()->Write(name(), LogError, "Network init failed");
			return FALSE;
		}

		// Report what the STACK resolved, not what we asked for: DHCP is
		// decided inside Initialize(), so a mismatch here is the visible
		// symptom of the construction-order hazard described below.
		CNetConfig* pConfig = mNet.GetConfig();
		CString Address;
		pConfig->GetIPAddress()->Format(&Address);

		if (pConfig->IsDHCPUsed()) {
			CLogger::Get()->Write(name(), LogWarning,
					      "No static IP configured -- using DHCP");
		} else {
			CLogger::Get()->Write(name(), LogNotice, "Static IP %s",
					      (const char*) Address);
		}
		return TRUE;
	}

	void update() override {
		if (mActive) {
			CScheduler::Get()->Yield(); // let the net tasks breathe

			if (!mUpLogged && mNet.IsRunning()) {
				mUpLogged = TRUE;
				CLogger::Get()->Write(name(), LogNotice, "Link is up");
			}
		}
	}

	// "a.b.c.d" -> u8[4]; malformed input yields zeros (logged at init)
	static void parseIP(const char* pString, u8* pOut) {
		for (unsigned i = 0; i < 4; i++) pOut[i] = 0;
		unsigned nField = 0;
		for (const char* p = pString; *p != '\0' && nField < 4; p++) {
			if (*p == '.') {
				nField++;
			} else if (*p >= '0' && *p <= '9') {
				pOut[nField] = static_cast<u8>(pOut[nField] * 10 + (*p - '0'));
			}
		}
	}

private:
	// The address bytes MUST be populated before mNet is constructed.
	// CNetSubSystem's constructor copies them into its own CNetConfig, and
	// CNetSubSystem::Initialize() then re-derives static-vs-DHCP from that
	// copy (a 0.0.0.0 address means DHCP). Parsing in the Ethernet
	// constructor BODY runs too late: members are built in declaration order,
	// so mNet would copy all-zero bytes and the stack would silently fall
	// back to DHCP -- retrying forever on a direct box-to-box cable that has
	// no server. Hence a type that parses in its own constructor, declared
	// ahead of mNet.
	struct Addr {
		u8 b[4];
		Addr(const char* pString) { parseIP(pString, b); }
	};

	Addr mIP;
	Addr mNetMask;
	// no gateway/DNS: fine for a single subnet; extend if routing is ever needed
	CNetSubSystem mNet{mIP.b, mNetMask.b, nullptr, nullptr, "instos"};
	boolean mActive = FALSE;
	boolean mUpLogged = FALSE;
};

} // namespace InstOS
