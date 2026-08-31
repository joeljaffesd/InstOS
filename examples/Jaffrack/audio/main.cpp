#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"
#include "../../monotron/dsp.hpp"
#include <MicroNAM.h>
#include <assets/models/BassModel.h>
#include <assets/audio/huckFinn.h>

#define SAMPLERATE 48000
#define BLOCKSIZE 128

static_assert(BassModelWeightsCount == 13802, "BassModel must be standard architecture");

// Jaffrack, sounding half — InstOS's remake of upstream Jaffrack's cuttleSend. This
// box has the USB audio interface and owns no controls at all: the graphics/
// box casts a ControlState (mutes, HuckFinn transport, the full monotron
// panel) and this box renders the mix:
//
//   bass input (ch 0) -> NAM amp (StandardNet, BassModel) * bassGate
//   +  monotron voice  +  HuckFinn one-shot
//   --> * globalGate --> both outputs
//
// The HuckFinn sample fires once at boot (the boot-up sound) and re-fires
// from the top on every trigger from the panel.
//
// The scope ring (cast back to the graphics box, feeding its scope AND its
// tuner) carries the PRE-GLOBAL-MUTE mix — identical to the speaker feed in
// every way except that global mute does not touch it. So muting the rig
// keeps the scope and tuner fully alive: exactly the "tune while muted"
// stage workflow. (Bass mute DOES affect the feed, like everything else in
// the mix.) Gates are one-pole smoothed to avoid clicks.
// Direct cable, static IPs: audio/ .1 <-> graphics/ .2.

// The DSP half: the whole mixer runs on the audio path; loop() only shuttles
// the latest ControlState into the public volatiles.
class MixEngine : public InstOS::AudioEngine {
public:
	// written by loop() from the ControlState, read by the audio path
	volatile int mGlobalMute = 0;
	volatile int mBassMute = 0;
	// bump to (re)fire the sample; starts at 1 so boot fires it (boot-up
	// sound). Local counter only — never assigned from the wire value.
	volatile unsigned mHuckTrigger = 1;
	volatile float mTargetHz = kFreqMid;
	volatile float mIntensity = 0.f;
	volatile int mKeyboardMute = 1;
	volatile int mBypass = 1;

	// component setters are safe to call from loop() (parameter writes only)
	monotron::LFO mLFO;
	monotron::Delay mDelay;

	// the scope ring, drained by the Maker's snapshots
	SharedState* mpScope = nullptr;

	// one-time DSP setup, on the audio path before the first block
	void init() override {
		mNet.reset();
		mNet.load_weights(BassModelWeights);
		CLogger::Get()->Write("nam", LogNotice, "BassModel loaded (%u weights)",
				      BassModelWeightsCount);
	}

	MixEngine() {
		mDelay.init((float) SAMPLERATE);
		mDelay.setEnabled(true); // always in circuit; `bypass` gates the
					 // output so delay tails survive it
		mLFO.setFrequency(kRateMid, (float) SAMPLERATE);
		mLFO.setMode(0);
	}

private:
	monotron::FMSaw mOsc;

	// block-templated: forward() processes exactly BLOCKSIZE samples per call
	MicroNAM::StandardNet<BLOCKSIZE> mNet;
	float mBassBuf[BLOCKSIZE];

	// audio path only
	float mHz = kFreqMid;
	float mGlobalGate = 1.f, mBassGate = 1.f;
	unsigned mHuckPos = 0;
	boolean mHuckActive = FALSE;
	unsigned mHuckSeen = 0; // last trigger acted on

	static constexpr float kMonotronLevel = 0.4f; // chain peaks near 1.7 (measured)
	static constexpr float kHuckLevel = 0.8f;
	static constexpr float kHzSmooth = 0.004f;    // ~5 ms one-pole on pitch
	static constexpr float kGateSmooth = 0.01f;   // ~2 ms one-pole on gates

	void processAudio(const float** inputBuffers, float** outputBuffers,
			  unsigned nFrames) override {
		float fIntensity = mIntensity;
		float fRibbonGate = mKeyboardMute ? 0.f : 1.f;
		float fBypassGate = mBypass ? 0.f : 1.f;
		float fGlobalTarget = mGlobalMute ? 0.f : 1.f;
		float fBassTarget = mBassMute ? 0.f : 1.f;

		// one-shot (re)trigger: a repress mid-playback restarts from the top
		unsigned nTrigger = mHuckTrigger;
		if (nTrigger != mHuckSeen) {
			mHuckSeen = nTrigger;
			mHuckPos = 0;
			mHuckActive = TRUE;
		}

		// bass through the amp model, one whole block; runs even while
		// bass-muted so the net's state stays warm and CPU cost constant
		if (nFrames == BLOCKSIZE) {
			mNet.forward(inputBuffers[0], mBassBuf);
		} else {
			for (unsigned i = 0; i < nFrames; i++) mBassBuf[i] = 0.f;
		}

		for (unsigned i = 0; i < nFrames; i++) {
			float fBass = mBassBuf[i];

			// monotron voice, per-sample chain exactly as examples/monotron
			mHz += (mTargetHz - mHz) * kHzSmooth;
			float fLFO = mLFO.process();
			float fFrequency = mHz + (fLFO * fIntensity * mHz);
			float fCarrier = mOsc.process(fFrequency, (float) SAMPLERATE) * fRibbonGate;
			float fMonotron = mDelay.process(fCarrier) * fBypassGate * kMonotronLevel;

			// HuckFinn one-shot: plays through once, then goes quiet
			float fHuck = 0.f;
			if (mHuckActive) {
				fHuck = (float) HuckFinnPCM[mHuckPos] * (1.f / 32768.f);
				if (++mHuckPos >= HuckFinnCount) {
					mHuckActive = FALSE;
				}
			}

			mBassGate += (fBassTarget - mBassGate) * kGateSmooth;
			mGlobalGate += (fGlobalTarget - mGlobalGate) * kGateSmooth;

			// the feed: the mix before the one gate it must ignore
			float fFeed = fBass * mBassGate + fMonotron
				      + fHuck * kHuckLevel;

			if (mpScope != nullptr) {
				mpScope->writeSample(fFeed); // scope/tuner: mute-proof
			}

			float fMix = fFeed * mGlobalGate; // speakers only
			outputBuffers[0][i] = fMix;
			outputBuffers[1][i] = fMix;
		}
	}
};

class AudioBox : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	// 24-bit: many UAC2 interfaces (e.g. UA Volt) expose capture at 24-bit only
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::Ethernet mNet{"192.168.10.1"};
	InstOS::Barebone::Maker<SharedState> mMaker{"192.168.10.2"};
	InstOS::Barebone::Taker<ControlState> mTaker{kControlPort};

	MixEngine mEngine;
	SharedState mScope;
	ControlState mControl = {};

	int mLastMute = -1, mLastBass = -1;
	unsigned mLastHuck = 0;
	boolean mHuckSynced = FALSE;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mControl.monotron.keyboardMute = 1;
		mControl.monotron.frequency = kFreqMid;

		mEngine.mpScope = &mScope;
		mAudio.setEngine(&mEngine);
		mMaker.attach(&mScope);
		return TRUE;
	}

	void loop() override {
		if (mTaker.get(mControl)) {
			mEngine.mGlobalMute = mControl.globalMute;
			mEngine.mBassMute = mControl.bassMute;
			// Edge-triggered with sync-on-first-frame: the graphics box's
			// counter survives audio reboots (devloop!), so its absolute
			// value means nothing here — the first frame only latches a
			// baseline, and later CHANGES fire the engine's local counter.
			if (!mHuckSynced) {
				mHuckSynced = TRUE;
				mLastHuck = mControl.huckTrigger;
			} else if (mControl.huckTrigger != mLastHuck) {
				mLastHuck = mControl.huckTrigger;
				mEngine.mHuckTrigger = mEngine.mHuckTrigger + 1;
			}

			// mirrors examples/monotron/audio's parameter apply
			const MonotronState& m = mControl.monotron;
			mEngine.mLFO.setFrequency(m.rate, (float) SAMPLERATE);
			mEngine.mLFO.setMode(m.lfoMode);
			mEngine.mDelay.setCutoff(m.cutoff);
			mEngine.mDelay.setDelayTime(m.delayTime);
			mEngine.mDelay.setFeedback(m.feedback);
			mEngine.mTargetHz = m.frequency;
			mEngine.mIntensity = m.intensity;
			mEngine.mKeyboardMute = m.keyboardMute;
			mEngine.mBypass = m.bypass;

			// discrete edges only; knobs move far too often to log
			if (   mControl.globalMute != mLastMute
			    || mControl.bassMute != mLastBass) {
				mLastMute = mControl.globalMute;
				mLastBass = mControl.bassMute;
				CLogger::Get()->Write("jaffrack", LogNotice,
						      "mute %d, bass mute %d, huck trigger %u (update %u)",
						      mControl.globalMute, mControl.bassMute,
						      mControl.huckTrigger, mControl.changes);
			}
		}

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
	static AudioBox mAudioBox;
	mAudioBox.start(true); // full clock: the whole mix runs on the main loop
}
