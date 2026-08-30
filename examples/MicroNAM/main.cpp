#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>

#include <MicroNAM.h>
#include "models/MarshallModel.h"

#define SAMPLERATE 48000
#define BLOCKSIZE 128

// Neural amp modeling (NAM) on a USB audio interface plugged into the Type-A
// port: input channel 0 runs through a WaveNet and is written to both
// outputs. Blinks the ACT LED as a heartbeat.
//
// Models: python3 external/MicroNAM/nam_to_header.py <model.nam> models/<Model>.h
// The net class must match the .nam architecture (weights count):
//   StandardNet 13802 | LiteNet 6554 | FeatherNet 3026 | NanoNet 842
#define NET     MicroNAM::FeatherNet
#define MODEL   MarshallModel
#define WEIGHTS 3026

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define MODEL_WEIGHTS CAT(MODEL, Weights)
#define MODEL_COUNT   CAT(MODEL, WeightsCount)

static_assert(MODEL_COUNT == WEIGHTS, "model does not match NET architecture");

// Diagnostic modes (see NamEngine::selfTest() too):
//   0  NAM on USB input (normal)
//   1  bypass: USB input straight to output, net never runs (input chain check)
//   2  NAM on an internal sawtooth, USB input ignored (net-only check)
#define MODE 0

// BENCH 1: no USB audio at all. The main loop runs forward() on blocks of
// random [-1, 1] floats and reports timing over serial once a second.
#define BENCH 0

#if BENCH
class NamBench : public InstOS::Firmware {
	NET<BLOCKSIZE> mNet;
	float mIn[BLOCKSIZE], mOut[BLOCKSIZE];
	unsigned mRng = 0x12345678;
	unsigned mMinUs = ~0u, mMaxUs = 0, mSumUs = 0, mBlocks = 0;
	unsigned mLastReport = 0;
	float mSink = 0.f; // keeps the output live

	float rnd() { // LCG, uniform in [-1, 1]
		mRng = mRng * 1664525u + 1013904223u;
		return (float)(int)(mRng >> 1) / 1073741824.0f;
	}

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}
		mNet.reset();
		mNet.load_weights(MODEL_WEIGHTS);
		m_Logger.Write("nam", LogNotice, "bench: %u-frame blocks, %u weights; budget %u us/block",
			BLOCKSIZE, WEIGHTS, 1000000u * BLOCKSIZE / SAMPLERATE);
		return TRUE;
	}

	void loop() override {
		for (int i = 0; i < BLOCKSIZE; i++) mIn[i] = rnd();

		unsigned t0 = CTimer::Get()->GetClockTicks();
		mNet.forward(mIn, mOut);
		unsigned dt = CTimer::Get()->GetClockTicks() - t0;

		mSink += mOut[BLOCKSIZE - 1];
		if (dt < mMinUs) mMinUs = dt;
		if (dt > mMaxUs) mMaxUs = dt;
		mSumUs += dt; mBlocks++;

		unsigned now = CTimer::Get()->GetTicks();
		if (now - mLastReport >= HZ) {
			mLastReport = now;
			m_Logger.Write("nam", LogNotice,
				"forward: min %u  avg %u  max %u us  (%u blocks/s, %u%% of realtime)",
				mMinUs, mSumUs / mBlocks, mMaxUs, mBlocks,
				(mSumUs / mBlocks) * 100u / (1000000u * BLOCKSIZE / SAMPLERATE));
			mMinUs = ~0u; mMaxUs = 0; mSumUs = 0; mBlocks = 0;
		}
	}
};
#else
// The DSP half: a member of the Firmware subclass below. init() runs on the
// audio path once the stream is up; loop() reads the public counters.
class NamEngine : public InstOS::AudioEngine {
public:
	// written on the audio path, read by the app's loop()
	volatile bool mLEDState = false;
	unsigned mMaxUs = 0, mSumUs = 0, mBlocks = 0; // forward() timing

	void init() override {
		mNet.reset();
		mNet.load_weights(MODEL_WEIGHTS);
		selfTest();
	}

private:
	// block-templated: forward() processes exactly BLOCKSIZE samples per call
	NET<BLOCKSIZE> mNet;

	unsigned mTickCounter = 0;
	float mSaw = 0.f;   // MODE 2 test signal

	// Runs the net on a fixed deterministic block (3 passes) and logs the
	// first outputs in micro-units. Reference from the same code natively on
	// the host (FeatherNet + MarshallModel):
	//   45298 29626 -28183 -71223 -78215 -85912
	void selfTest() {
		float in[BLOCKSIZE], out[BLOCKSIZE];
		for (int i = 0; i < BLOCKSIZE; i++) in[i] = ((i * 37) % 97 - 48) / 64.0f;
		for (int b = 0; b < 3; b++) mNet.forward(in, out);
		CLogger::Get()->Write("nam", LogNotice, "selftest: %d %d %d %d %d %d",
			(int)(out[0]*1000000), (int)(out[1]*1000000), (int)(out[2]*1000000),
			(int)(out[3]*1000000), (int)(out[4]*1000000), (int)(out[5]*1000000));
		mNet.reset();
	}

	void processAudio(const float** inputBuffers,
	                  float** outputBuffers,
	                  unsigned nFrames) override
	{
		// UAC delivers fixed BLOCKSIZE blocks; the net is sized for exactly that
		if (nFrames != BLOCKSIZE) {
			for (unsigned i = 0; i < nFrames; i++) {
				outputBuffers[0][i] = 0.f;
				outputBuffers[1][i] = 0.f;
			}
			return;
		}

#if MODE == 1
		for (unsigned i = 0; i < nFrames; i++) outputBuffers[0][i] = inputBuffers[0][i];
#elif MODE == 2
		float saw[BLOCKSIZE]; // ~110 Hz sawtooth at -12 dBFS
		for (unsigned i = 0; i < nFrames; i++) {
			mSaw += 2.0f * 110.0f / SAMPLERATE;
			if (mSaw > 1.0f) mSaw -= 2.0f;
			saw[i] = 0.25f * mSaw;
		}
		mNet.forward(saw, outputBuffers[0]);
#else
		unsigned t0 = CTimer::Get()->GetClockTicks();
		mNet.forward(inputBuffers[0], outputBuffers[0]);
		unsigned dt = CTimer::Get()->GetClockTicks() - t0;
		if (dt > mMaxUs) mMaxUs = dt;
		mSumUs += dt; mBlocks++;
#endif

		for (unsigned i = 0; i < nFrames; i++) {
			outputBuffers[1][i] = outputBuffers[0][i]; // R = L

			// increment and wrap ActLED tick counter at 0.5Hz
			mTickCounter++;
			if (mTickCounter >= (SAMPLERATE/2)) {
				mLEDState = !mLEDState;
				mTickCounter = 0;
			}
		}
	}
};

class NamAmp : public InstOS::Firmware {
	// modules: declaration order = init order; UAC requires USBHost before it
	InstOS::USBHost mUSB;
	// 24-bit: many UAC2 interfaces (e.g. UA Volt) expose capture at 24-bit only
	InstOS::UAC mAudio{SAMPLERATE, BLOCKSIZE, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};

	NamEngine mEngine;

	CActLED mActLED;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mAudio.setEngine(&mEngine);
		return TRUE;
	}

	void loop() override {
		// once a second: forward() cost (budget: 2667 us per 128-frame block)
		static unsigned lastReport = 0;
		unsigned now = CTimer::Get()->GetTicks();
		if (now - lastReport >= HZ) {
			lastReport = now;
			if (mEngine.mBlocks > 0) {
				m_Logger.Write("nam", LogNotice, "forward: max %u us, avg %u us over %u blocks",
					mEngine.mMaxUs, mEngine.mSumUs / mEngine.mBlocks, mEngine.mBlocks);
				mEngine.mMaxUs = 0; mEngine.mSumUs = 0; mEngine.mBlocks = 0;
			}
		}

		// non-blocking heartbeat so module updates stay responsive
		static bool lastLEDState = false;
		if (mEngine.mLEDState != lastLEDState) {
			lastLEDState = mEngine.mLEDState;
			lastLEDState ? mActLED.On() : mActLED.Off();
		}
	}
};

#endif // BENCH

int main() {
	// static: a net's conv state can be hundreds of KB, more than the 128 KB stack
#if BENCH
	static NamBench mFirmware;
#else
	static NamAmp mFirmware;
#endif
	mFirmware.start(true);	// true arg starts app with boosted clock speed
}
