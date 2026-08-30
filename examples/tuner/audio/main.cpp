#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"

#include <math.h>

#define SAMPLERATE 48000

// Tuner, listening half: this box has the USB audio interface. Input channel
// 0 passes through to both outputs while YIN pitch estimates are cast over
// the Ethernet cable to the graphics/ box (which has the screen and does the
// note mapping, smoothing and rendering).
// Direct cable, static IPs: audio/ (maker) .1 -> graphics/ (taker) .2.
//
// Performance compromises vs. TUITUNE's original detector (doubles, 48 kHz):
// floats throughout, and analysis runs on a 2x-decimated signal (24 kHz,
// 2048-sample window ~= the same 85 ms) at ~14 Hz. That cuts the YIN
// difference function to ~1M MACs per pass, comfortable next to the audio
// pump; parabolic interpolation keeps cents resolution ample.

// The DSP half: passthrough + sample ring (single writer / single reader,
// same contract as the oscilloscope example).
class TunerEngine : public InstOS::AudioEngine {
public:
	static constexpr unsigned kRingSize = 16384; // power of two, > window
	float mRing[kRingSize] = {};
	volatile unsigned mWriteIndex = 0;

	// copy the most recent n samples into out (oldest first)
	void latest(float* pOut, unsigned n) const {
		unsigned w = mWriteIndex;
		unsigned nStart = w - n;
		for (unsigned i = 0; i < n; i++) {
			pOut[i] = mRing[(nStart + i) & (kRingSize - 1)];
		}
	}

private:
	void processAudio(const float** inputBuffers, float** outputBuffers,
			  unsigned nFrames) override {
		unsigned nIndex = mWriteIndex;
		for (unsigned i = 0; i < nFrames; i++) {
			float fSample = inputBuffers[0][i];
			outputBuffers[0][i] = fSample; // monitor through L
			outputBuffers[1][i] = fSample; // and R
			mRing[nIndex] = fSample;
			nIndex = (nIndex + 1) & (kRingSize - 1);
		}
		mWriteIndex = nIndex;
	}
};

// YIN (de Cheveigne & Kawahara 2002) with parabolic interpolation, in float.
// Returns frequency in Hz, or 0 if no confident pitch.
static float yinPitch(const float* x, unsigned n, float fSampleRate) {
	const unsigned tauMax = n / 2;                          // ~23 Hz floor
	const unsigned tauMin = (unsigned) (fSampleRate / 1600.0f); // 1.6 kHz cap
	static float d[2048], cmnd[2048];                       // sized for n = 4096

	for (unsigned tau = 1; tau < tauMax; tau++) {
		float sum = 0.0f;
		for (unsigned i = 0; i < tauMax; i++) {
			float diff = x[i] - x[i + tau];
			sum += diff * diff;
		}
		d[tau] = sum;
	}
	float running = 0.0f;
	cmnd[0] = 1.0f;
	for (unsigned tau = 1; tau < tauMax; tau++) {
		running += d[tau];
		cmnd[tau] = running > 0.0f ? d[tau] * (float) tau / running : 1.0f;
	}

	constexpr float kThreshold = 0.12f;
	unsigned tau = 0;
	for (unsigned t = tauMin > 2 ? tauMin : 2; t < tauMax - 1; t++) {
		if (cmnd[t] < kThreshold) {
			while (t + 1 < tauMax - 1 && cmnd[t + 1] < cmnd[t]) t++;
			tau = t;
			break;
		}
	}
	if (tau == 0) return 0.0f;

	// parabolic interpolation around the minimum
	float t = (float) tau;
	float a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
	float denom = a - 2.0f * b + c;
	if (denom > 1e-9f || denom < -1e-9f) t += 0.5f * (a - c) / denom;

	return fSampleRate / t;
}

static float rms(const float* x, unsigned n) {
	float s = 0.0f;
	for (unsigned i = 0; i < n; i++) s += x[i] * x[i];
	return sqrtf(s / (float) n);
}

class TunerMaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	// 24-bit: many UAC2 interfaces (e.g. UA Volt) expose capture at 24-bit only
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::Ethernet mNet{"192.168.10.1"};
	InstOS::Barebone::Maker<TunerState> mMaker{"192.168.10.2"};

	TunerEngine mEngine;
	TunerState mState;

	// analysis working set (2x decimated: 24 kHz, ~85 ms window)
	static constexpr unsigned kWindow = 4096;    // at 48 kHz
	static constexpr unsigned kDecWindow = 2048; // after decimation
	float mWindow[kWindow];
	float mDecimated[kDecWindow];

	unsigned mLastAnalysisTicks = 0;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mAudio.setEngine(&mEngine);
		mMaker.attach(&mState);
		return TRUE;
	}

	void loop() override {
		// ---- analysis at ~14 Hz (the Maker casts snapshots on its own) ----
		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastAnalysisTicks >= 7) { // 70 ms at HZ = 100
			mLastAnalysisTicks = nTicks;

			mEngine.latest(mWindow, kWindow);
			// decimate 2x with a 2-sample average (cheap LPF; a tuner
			// only needs content below 1.6 kHz to be clean)
			for (unsigned i = 0; i < kDecWindow; i++) {
				mDecimated[i] = 0.5f * (mWindow[2*i] + mWindow[2*i + 1]);
			}

			float level = rms(mDecimated, kDecWindow);
			float freq = 0.0f;
			if (level > 0.003f) {
				freq = yinPitch(mDecimated, kDecWindow, SAMPLERATE / 2.0f);
			}

			mState.freq = freq;
			mState.level = level;
			mState.changes++;
		}

		// non-blocking heartbeat; the Maker casts from its module update()
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
	static TunerMaker mTunerMaker;
	mTunerMaker.start(true); // full clock: YIN next to the audio pump
}
