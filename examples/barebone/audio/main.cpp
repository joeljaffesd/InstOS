#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"

// The sending half of the split oscilloscope: this box has the USB audio
// interface. Input passes through to the outputs while filling the shared
// ring, which is cast over the Ethernet cable to the graphics/ box (which
// has the screen). Note this example casts the other way round from the
// others: here the AUDIO box is the maker.
// Direct cable, static IPs: audio/ (maker) .1 -> graphics/ (taker) .2.

// The DSP half: passthrough that also writes each sample into the shared
// state the Maker module casts over the wire.
class ScopeEngine : public InstOS::AudioEngine {
public:
	SharedState* mpState = nullptr; // set by the app before streaming starts

private:
	void processAudio(const float** inputBuffers, float** outputBuffers,
			  unsigned nFrames) override {
		for (unsigned i = 0; i < nFrames; i++) {
			float fSample = inputBuffers[0][i];
			outputBuffers[0][i] = fSample; // monitor through L
			outputBuffers[1][i] = fSample; // and R
			mpState->writeSample(fSample);
		}
	}
};

class ScopeMaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	InstOS::UAC mAudio{48000, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::Ethernet mNet{"192.168.10.1"};
	InstOS::Barebone::Maker<SharedState> mMaker{"192.168.10.2"};

	SharedState mState;
	ScopeEngine mEngine;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mEngine.mpState = &mState;
		mAudio.setEngine(&mEngine);
		mMaker.attach(&mState);
		return TRUE;
	}

	void loop() override {
		// non-blocking heartbeat; the Maker casts from its module update()
		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static ScopeMaker mScopeMaker;
	mScopeMaker.start();
}
