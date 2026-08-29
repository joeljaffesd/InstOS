#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"

#define SAMPLERATE 48000

// Net-synth two, sounding half: one saw oscillator per key, all thirteen
// running all the time and simply gated to silence when their key is up.
// Standing voices cost a multiply-add each and remove voice allocation
// entirely -- with a fixed-pitch keyboard there is nothing to allocate, and
// a key that arrives already held (say the first packet after a dropout)
// sounds correctly rather than needing a note-on that never came.
// Direct cable, static IPs: graphics/ (screen, maker) .1 -> audio/ (taker) .2.
class KeyboardTaker : public InstOS::Firmware, public InstOS::AudioProcessor {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::Ethernet mNet{"192.168.10.2"};
	InstOS::Barebone::Taker<SynthState> mTaker;

	SynthState mState = {};

	// written by loop(), read by the audio path
	volatile unsigned mTargetKeys = 0;

	// audio path only
	float mPhase[kKeyCount] = {};
	float mGain[kKeyCount] = {};
	float mFrequency[kKeyCount] = {};

	// Per-voice level. Thirteen voices at once can sum past full scale, but
	// saws at different pitches rarely peak together, and UAC clamps rather
	// than wraps -- so the worst case is mild clipping, not a fold-over.
	static constexpr float kVoiceLevel = 0.12f;
	// ~2 ms one-pole on each gate: stepping straight to 0 or 1 would click
	static constexpr float kGainSmooth = 0.01f;

	unsigned mLastKeys = 0;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		for (unsigned i = 0; i < (unsigned) kKeyCount; i++) {
			mFrequency[i] = keyFrequency((int) i);
		}

		CLogger::Get()->Write("netsynth", LogNotice,
				      "Keyboard spans %u..%u Hz over %u keys",
				      (unsigned) mFrequency[0],
				      (unsigned) mFrequency[kKeyCount - 1], kKeyCount);

		mAudio.setProcessor(this);
		return TRUE;
	}

	void loop() override {
		if (mTaker.get(mState)) {
			mTargetKeys = mState.keys;

			if (mState.keys != mLastKeys) {
				mLastKeys = mState.keys;
				CLogger::Get()->Write("netsynth", LogNotice,
						      "keys %04x (update %u)",
						      mState.keys, mState.changes);
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

	void processAudio(const float** inputBuffers, float** outputBuffers,
			  unsigned nFrames) override {
		unsigned nKeys = mTargetKeys;

		for (unsigned i = 0; i < nFrames; i++) {
			float fMix = 0.f;

			for (unsigned k = 0; k < (unsigned) kKeyCount; k++) {
				float fTarget = (nKeys & (1u << k)) ? 1.f : 0.f;
				mGain[k] += (fTarget - mGain[k]) * kGainSmooth;

				if (fTarget == 0.f && mGain[k] < 0.0001f) {
					// fully released: park the phase so the next
					// press always starts from the same place
					mGain[k] = 0.f;
					mPhase[k] = 0.f;
					continue;
				}

				mPhase[k] += mFrequency[k] * (1.f / (float) SAMPLERATE);
				if (mPhase[k] >= 1.f) {
					mPhase[k] -= 1.f;
				}

				fMix += (2.f * mPhase[k] - 1.f) * mGain[k];
			}

			fMix *= kVoiceLevel;
			outputBuffers[0][i] = fMix;
			outputBuffers[1][i] = fMix;
		}
	}
};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static KeyboardTaker mKeyboardTaker;
	mKeyboardTaker.start();
}
