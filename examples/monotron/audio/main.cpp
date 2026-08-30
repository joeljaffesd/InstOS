#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"
#include "../dsp.hpp"

#define SAMPLERATE 48000

// Monotron, sounding half -- Jaffrack's MonotronProcessor. No controls of
// its own: it renders whatever parameter set the panel box casts at it.
//
// Signal path, per sample, exactly as upstream:
//   lfo -> frequency modulation -> quasi-bandlimited FM saw carrier
//       -> ribbon gate -> damped feedback delay -> bypass gate
// Direct cable, static IPs: graphics/ (panel, maker) .1 -> audio/ (taker) .2.

// The DSP half: a member of the Firmware subclass below. The app's loop()
// pokes the public members when a parameter update arrives; per-sample state
// stays private to the audio path.
class MonotronEngine : public InstOS::AudioEngine {
public:
	// component setters are safe to call from loop() (parameter writes only)
	monotron::LFO mLFO;
	monotron::Delay mDelay;

	// written by loop(), read by the audio path
	volatile float mTargetHz = kFreqMid;
	volatile float mIntensity = 0.f;
	volatile int mKeyboardMute = 1;
	volatile int mBypass = 1;

	MonotronEngine() {
		mDelay.init((float) SAMPLERATE);
		mDelay.setEnabled(true); // always in circuit; `bypass` gates the output,
					 // as upstream does, so delay tails survive it
		mLFO.setFrequency(kRateMid, (float) SAMPLERATE);
		mLFO.setMode(0);
	}

private:
	monotron::FMSaw mOsc;

	// audio path only
	float mHz = kFreqMid;

	// The chain peaks near 1.7 with the delay wound up (measured), so this
	// keeps it clear of full scale; UAC clamps rather than wraps if it is
	// ever exceeded.
	static constexpr float kOutputLevel = 0.4f;
	// ~5 ms one-pole on pitch: control lands at frame rate, and stepping
	// straight to each new ribbon value would zipper
	static constexpr float kHzSmooth = 0.004f;

	void processAudio(const float** inputBuffers, float** outputBuffers,
			  unsigned nFrames) override {
		float fIntensity = mIntensity;
		float fGate = mKeyboardMute ? 0.f : 1.f;
		float fBypass = mBypass ? 0.f : 1.f;

		for (unsigned i = 0; i < nFrames; i++) {
			mHz += (mTargetHz - mHz) * kHzSmooth;

			float fLFO = mLFO.process();
			float fFrequency = mHz + (fLFO * fIntensity * mHz);

			float fCarrier = mOsc.process(fFrequency, (float) SAMPLERATE) * fGate;

			// the delay runs even while bypassed or muted, so its tail
			// keeps ringing when the ribbon is released
			float fSample = mDelay.process(fCarrier) * fBypass * kOutputLevel;

			outputBuffers[0][i] = fSample;
			outputBuffers[1][i] = fSample;
		}
	}
};

class MonotronTaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::Ethernet mNet{"192.168.10.2"};
	InstOS::Barebone::Taker<MonotronState> mTaker;

	MonotronEngine mEngine;

	MonotronState mState = {};

	int mLastMute = 1;
	int mLastBypass = 1;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mAudio.setEngine(&mEngine);
		return TRUE;
	}

	void loop() override {
		if (mTaker.get(mState)) {
			// mirrors MonotronProcessor::updateDSP()
			mEngine.mLFO.setFrequency(mState.rate, (float) SAMPLERATE);
			mEngine.mLFO.setMode(mState.lfoMode);
			mEngine.mDelay.setCutoff(mState.cutoff);
			mEngine.mDelay.setDelayTime(mState.delayTime);
			mEngine.mDelay.setFeedback(mState.feedback);

			mEngine.mTargetHz = mState.frequency;
			mEngine.mIntensity = mState.intensity;
			mEngine.mKeyboardMute = mState.keyboardMute;
			mEngine.mBypass = mState.bypass;

			// discrete edges only; the knobs move far too often to log
			if (mState.keyboardMute != mLastMute || mState.bypass != mLastBypass) {
				mLastMute = mState.keyboardMute;
				mLastBypass = mState.bypass;
				CLogger::Get()->Write("monotron", LogNotice,
						      "%s, ribbon %s at %u Hz (update %u)",
						      mState.bypass ? "bypassed" : "active",
						      mState.keyboardMute ? "up" : "down",
						      (unsigned) mState.frequency,
						      mState.changes);
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
	static MonotronTaker mMonotronTaker;
	mMonotronTaker.start();
}
