#pragma once

#include <InstOS.hpp>
#include <circle/sound/soundbasedevice.h>

namespace InstOS {

class AudioIO; // fwd

// Owns the app's DSP. Subclass, override init() + processAudio(), declare it
// as a member of your Firmware subclass (composition — do NOT also inherit
// it: its `void init()` collides with Firmware's `boolean init()`), and bind
// it to an AudioIO transport (UAC today, I2S later) with setEngine(). Both
// overrides run on the audio path — today the main loop, later a dedicated
// core — so keep them allocation-free and inside the block period.
class AudioEngine {
	friend class AudioIO;

public:
	// one-time DSP setup (load weights, build filters); called on the audio
	// path before the first block, after the transport is up — sampleRate(),
	// bufferFrames() and channels() are valid here
	virtual void init() {}

	// Standard non-interleaved layout: inputBuffers[channel][frame] and
	// outputBuffers[channel][frame], samples in [-1, 1]
	virtual void processAudio(const float** inputBuffers, float** outputBuffers,
				  unsigned nFrames) = 0;

	unsigned sampleRate() const;   // forwarded from the bound AudioIO
	unsigned bufferFrames() const;
	unsigned channels() const;

private:
	AudioIO* mpIO = nullptr; // set by AudioIO::setEngine()
};

// Transport-agnostic half of audio streaming: queues, priming, the block
// pump, float conversion/clamping, and rate diagnostics — everything that is
// the same whether samples move over USB, I2S, PWM, or HDMI. A subclass owns
// the concrete CSoundBaseDevice and everything transport-specific (device
// discovery, codec bring-up, clocking): it exposes the device via device()
// and can gate Start() attempts with readyToStart().
//
// Processing runs from update() on the main loop, NOT from Circle's need-data
// callback. That callback fires inside the transport's completion interrupt,
// where Circle re-arms one transfer at a time: for USB, any work longer than
// a microframe (125 us) drops frames on both directions (measured: a 360 us
// callback lost 8.5% of all frames). The queues hold kQueueBlocks blocks to
// ride out main-loop jitter.
class AudioIO : public Module {
public:
	static constexpr unsigned kMaxChannels = 2;     // static buffer bound
	static constexpr unsigned kMaxBufferFrames = 256;
	static constexpr unsigned kQueueBlocks = 4;     // per-direction queue depth, in blocks

	void setEngine(AudioEngine* pEngine) {
		mpEngine = pEngine;
		if (pEngine != nullptr) {
			pEngine->mpIO = this;
		}
	}

	unsigned sampleRate() const { return mSampleRate; }
	unsigned bufferFrames() const { return mBufferFrames; }
	unsigned channels() const { return mChannels; }
	boolean isStreaming() { return device()->IsActive(); }

protected:
	AudioIO(unsigned nSampleRate, unsigned nBufferFrames, unsigned nChannels,
		boolean bRXEnabled)
	: mSampleRate(nSampleRate),
	  mBufferFrames(nBufferFrames <= kMaxBufferFrames ? nBufferFrames : kMaxBufferFrames),
	  mChannels(nChannels <= kMaxChannels ? nChannels : kMaxChannels),
	  mRXEnabled(bRXEnabled) {}

	// the concrete transport's sound device; a virtual (rather than a ctor
	// parameter) so the base never touches a not-yet-constructed member
	virtual CSoundBaseDevice* device() = 0;

	// transport hook, called before each Start() attempt (e.g. UAC waits for
	// enumeration); returning FALSE skips this retry quietly
	virtual boolean readyToStart() { return TRUE; }

	// a subclass overriding init() must call AudioIO::init() first
	boolean init() override {
		if (!device()->AllocateQueueFrames(mBufferFrames * kQueueBlocks)) {
			CLogger::Get()->Write(name(), LogError, "Cannot allocate sound queue");
			return FALSE;
		}

		// we exchange clamped floats; Circle converts to the wire format
		device()->SetWriteFormat(SoundFormatFloat32, mChannels);

		if (mRXEnabled) {
			if (!device()->AllocateReadQueueFrames(mBufferFrames * kQueueBlocks)) {
				CLogger::Get()->Write(name(), LogError, "Cannot allocate read queue");
				return FALSE;
			}
			device()->SetReadFormat(SoundFormatFloat32, mChannels);
		}

		return TRUE;
	}

	void update() override {
		if (device()->IsActive()) {
			if (!mActiveLogged) {
				mActiveLogged = TRUE;
				CLogger::Get()->Write(name(), LogNotice, "audio streaming is active");
			}

			// the engine initializes on the audio path, once, before
			// its first block (setEngine may run any time before that)
			if (mpEngine != nullptr && !mEngineInited) {
				mEngineInited = TRUE;
				mpEngine->init();
			}

			pumpOnce();

			// DIAG: stream rates once a second
			unsigned nNow = CTimer::Get()->GetTicks();
			if (nNow - mDiagTicks >= HZ) {
				mDiagTicks = nNow;
				CLogger::Get()->Write(name(), LogNotice,
					"tx %u frames/s, rx %u frames/s, short reads %u (min %u frames)",
					mDiagTXFrames, mDiagRXFrames, mDiagShort, mDiagMinRead);
				mDiagTXFrames = mDiagRXFrames = mDiagShort = 0;
				mDiagMinRead = ~0u;
			}
			return;
		}

		// stream down (device gone, or not yet there): retry once a second
		unsigned nTicks = CTimer::Get()->GetTicks();
		if (nTicks - mLastStartTicks >= 1 * HZ) {
			mLastStartTicks = nTicks;
			mActiveLogged = FALSE;
			if (readyToStart()) {
				// prime with SILENCE, not engine output: Start() may still
				// re-negotiate the wire format (e.g. 24-in-32 subframes),
				// and zero bytes are valid audio in every format
				primeSilence();
				device()->Start();
			}
		}
	}

private:
	friend class AudioEngine;

	// fill the write queue with silence ahead of Start()
	void primeSilence() {
		for (unsigned i = 0; i < mBufferFrames * mChannels; i++) {
			mWriteBuffer[i] = 0.0f;
		}
		unsigned nFree = device()->GetQueueSizeFrames() - device()->GetQueueFramesAvail();
		while (nFree >= mBufferFrames) {
			device()->Write(mWriteBuffer, mBufferFrames * mChannels * sizeof(float));
			nFree -= mBufferFrames;
		}
	}

	// pull whatever input has arrived into mInputBuffers, zero-padding any
	// shortfall (input underruns must not stall the output stream)
	void readInput() {
		unsigned nFrames = 0;

		if (mRXEnabled) {
			int nBytes = device()->Read(mReadBuffer,
						    mBufferFrames * mChannels * sizeof(float));
			if (nBytes > 0) {
				nFrames = static_cast<unsigned>(nBytes) / (mChannels * sizeof(float));
			}
		}

		mDiagRXFrames += nFrames;
		if (nFrames < mBufferFrames) mDiagShort++;
		if (nFrames < mDiagMinRead) mDiagMinRead = nFrames;

		for (unsigned i = 0; i < mBufferFrames; i++) {
			for (unsigned ch = 0; ch < mChannels; ch++) {
				mInputBuffers[ch][i] =
					i < nFrames ? mReadBuffer[mChannels * i + ch] : 0.0f;
			}
		}
	}

	// process blocks while there is room for them in the write queue. Once
	// input is flowing, stay in lockstep with it (wait for a full input
	// block) unless the write queue is about to run dry.
	void pumpOnce() {
		const float* InputPtrs[kMaxChannels];
		float* OutputPtrs[kMaxChannels];
		for (unsigned ch = 0; ch < mChannels; ch++) {
			InputPtrs[ch] = mInputBuffers[ch];
			OutputPtrs[ch] = mOutputBuffers[ch];
		}

		for (;;) {
			unsigned nQueued = device()->GetQueueFramesAvail();
			unsigned nFree = device()->GetQueueSizeFrames() - nQueued;
			if (nFree < mBufferFrames) {
				break;
			}
			if (mRXEnabled
			    && device()->GetReadQueueFramesAvail() < mBufferFrames
			    && nQueued >= mBufferFrames) {
				break; // input not there yet, and output can wait for it
			}

			readInput();
			if (mpEngine != nullptr) {
				mpEngine->processAudio(InputPtrs, OutputPtrs, mBufferFrames);

				// interleave + clamp; Circle's float conversion wraps
				// instead of clamping, so overshoot must be caught here
				for (unsigned i = 0; i < mBufferFrames; i++) {
					for (unsigned ch = 0; ch < mChannels; ch++) {
						float fSample = mOutputBuffers[ch][i];
						if (fSample > 1.0f) fSample = 1.0f;
						if (fSample < -1.0f) fSample = -1.0f;
						mWriteBuffer[mChannels * i + ch] = fSample;
					}
				}
			} else {
				for (unsigned i = 0; i < mBufferFrames * mChannels; i++) {
					mWriteBuffer[i] = 0.0f; // no engine: keep the stream fed with silence
				}
			}

			device()->Write(mWriteBuffer, mBufferFrames * mChannels * sizeof(float));
			mDiagTXFrames += mBufferFrames;
		}
	}

	AudioEngine* mpEngine = nullptr;
	unsigned mSampleRate;
	unsigned mBufferFrames;
	unsigned mChannels;
	boolean mRXEnabled;
	unsigned mLastStartTicks = 0;
	boolean mActiveLogged = FALSE;
	boolean mEngineInited = FALSE;

	// DIAG counters
	unsigned mDiagTXFrames = 0, mDiagRXFrames = 0, mDiagShort = 0, mDiagMinRead = ~0u;
	unsigned mDiagTicks = 0;

	// block buffers
	float mInputBuffers[kMaxChannels][kMaxBufferFrames] = {}; // zero-padded on input underrun
	float mOutputBuffers[kMaxChannels][kMaxBufferFrames];
	float mReadBuffer[kMaxChannels * kMaxBufferFrames];  // interleaved, from Circle
	float mWriteBuffer[kMaxChannels * kMaxBufferFrames]; // interleaved, to Circle
};

inline unsigned AudioEngine::sampleRate() const { return mpIO->sampleRate(); }
inline unsigned AudioEngine::bufferFrames() const { return mpIO->bufferFrames(); }
inline unsigned AudioEngine::channels() const { return mpIO->channels(); }

} // namespace InstOS
