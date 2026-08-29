#pragma once

#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <circle/sound/usbsoundbasedevice.h>

namespace InstOS {

// Implement this and hand it to UAC to generate audio. Called from the main
// loop (UAC::update()), one block at a time — keep it allocation-free and
// well inside the block period so the queue never runs dry.
class AudioProcessor {
public:
	// Standard non-interleaved layout: inputBuffers[channel][frame] and
	// outputBuffers[channel][frame], samples in [-1, 1]. Until input
	// streaming lands, inputBuffers carries UAC::kChannels of silence.
	virtual void processAudio(const float** inputBuffers, float** outputBuffers,
				  unsigned nFrames) = 0;
};

// USB Audio Class (UAC) streaming module, full duplex by default. Requires a
// USBHost module declared before it. Retries Start() once a second until an
// audio device (e.g. UA Volt) is enumerated, so attach order and hot-plug
// both work. Pass DeviceModeTXOnly for output-only devices.
//
// Processing runs from update() on the main loop, NOT from Circle's need-data
// callback. That callback fires inside the USB completion interrupt, and
// Circle re-arms one isochronous transfer at a time from there: any work
// longer than a USB microframe (125 us) drops microframes on both directions
// (measured: a 360 us callback lost 8.5% of all frames). The queues hold
// kQueueBlocks blocks to ride out main-loop jitter.
class UAC : public Module {
public:
	static constexpr unsigned kChannels = 2;
	static constexpr unsigned kMaxBufferFrames = 256;
	static constexpr unsigned kQueueBlocks = 4; // per-direction queue depth, in blocks

	using TDeviceMode = CUSBSoundBaseDevice::TDeviceMode;

	// nBitDepth: 16 or 24 selects the USB stream resolution; 0 inherits
	// "soundopt=" from cmdline.txt (default 16). The choice is global — it
	// steers which altsettings bind at enumeration for every audio device —
	// so it must be made here, before the USB stack starts. Note some
	// interfaces (e.g. UA Volt) expose their capture path only at 24-bit.
	UAC(unsigned nSampleRate = 48000, unsigned nBufferFrames = 128,
	    TDeviceMode DeviceMode = CUSBSoundBaseDevice::DeviceModeTXRX,
	    unsigned nBitDepth = 0)
	: mBitDepth(applyBitDepth(nBitDepth)), // must precede mSound: both it and
	                                       // enumeration read the option
	  mSound(nSampleRate, DeviceMode),
	  mSampleRate(nSampleRate),
	  mBufferFrames(nBufferFrames <= kMaxBufferFrames ? nBufferFrames : kMaxBufferFrames),
	  mRXEnabled(DeviceMode != CUSBSoundBaseDevice::DeviceModeTXOnly) {}

	void setProcessor(AudioProcessor* pProcessor) { mpProcessor = pProcessor; }

	unsigned sampleRate() const { return mSampleRate; }
	unsigned bufferFrames() const { return mBufferFrames; }
	boolean isStreaming() { return mSound.IsActive(); }

	const char* name() const override { return "uac"; }

	boolean init() override {
		if (USBHost::instance == nullptr) {
			CLogger::Get()->Write(name(), LogError,
					      "Requires a USBHost module declared before it");
			return FALSE;
		}

		if (!mSound.AllocateQueueFrames(mBufferFrames * kQueueBlocks)) {
			CLogger::Get()->Write(name(), LogError, "Cannot allocate sound queue");
			return FALSE;
		}

		// We hand Circle clamped float frames; it converts to the USB
		// stream's negotiated resolution — whatever nBitDepth selected
		// at construction (see above).
		mSound.SetWriteFormat(SoundFormatFloat32, kChannels);

		if (mRXEnabled) {
			if (!mSound.AllocateReadQueueFrames(mBufferFrames * kQueueBlocks)) {
				CLogger::Get()->Write(name(), LogError, "Cannot allocate read queue");
				return FALSE;
			}
			mSound.SetReadFormat(SoundFormatFloat32, kChannels);
		}

		CLogger::Get()->Write(name(), LogNotice, "%u Hz, %u-frame buffers, %u-bit stream",
				      mSampleRate, mBufferFrames, mBitDepth);
		return TRUE;
	}

	void update() override {
		if (mSound.IsActive()) {
			if (!mActiveLogged) {
				mActiveLogged = TRUE;
				CLogger::Get()->Write(name(), LogNotice, "USB audio streaming is active");
			}
			pump();

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

		// device gone (or not yet there): retry once a second
		unsigned nTicks = CTimer::Get()->GetTicks();
		if (nTicks - mLastStartTicks >= 1 * HZ) {
			mLastStartTicks = nTicks;
			mActiveLogged = FALSE;
			// prime with SILENCE, not processor output: Start() may still
			// re-negotiate the wire format (e.g. 24-in-32 subframes), and
			// zero bytes are valid audio in every format
			primeSilence();
			mSound.Start();
		}
	}

private:
	// fill the write queue with silence ahead of Start()
	void primeSilence() {
		for (unsigned i = 0; i < mBufferFrames * kChannels; i++) {
			mWriteBuffer[i] = 0.0f;
		}
		unsigned nFree = mSound.GetQueueSizeFrames() - mSound.GetQueueFramesAvail();
		while (nFree >= mBufferFrames) {
			mSound.Write(mWriteBuffer, mBufferFrames * kChannels * sizeof(float));
			nFree -= mBufferFrames;
		}
	}

	// pull whatever input has arrived into mInputBuffers, zero-padding any
	// shortfall (input underruns must not stall the output stream)
	void readInput() {
		unsigned nFrames = 0;

		if (mRXEnabled) {
			int nBytes = mSound.Read(mReadBuffer,
						 mBufferFrames * kChannels * sizeof(float));
			if (nBytes > 0) {
				nFrames = static_cast<unsigned>(nBytes) / (kChannels * sizeof(float));
			}
		}

		mDiagRXFrames += nFrames;
		if (nFrames < mBufferFrames) mDiagShort++;
		if (nFrames < mDiagMinRead) mDiagMinRead = nFrames;

		for (unsigned i = 0; i < mBufferFrames; i++) {
			for (unsigned ch = 0; ch < kChannels; ch++) {
				mInputBuffers[ch][i] =
					i < nFrames ? mReadBuffer[kChannels * i + ch] : 0.0f;
			}
		}
	}

	// main-loop pump: process one block whenever there is room for it in the
	// write queue. Once input is flowing, stay in lockstep with it (wait for a
	// full input block) unless the write queue is about to run dry.
	void pump() {
		const float* InputPtrs[kChannels];
		float* OutputPtrs[kChannels];
		for (unsigned ch = 0; ch < kChannels; ch++) {
			InputPtrs[ch] = mInputBuffers[ch];
			OutputPtrs[ch] = mOutputBuffers[ch];
		}

		for (;;) {
			unsigned nQueued = mSound.GetQueueFramesAvail();
			unsigned nFree = mSound.GetQueueSizeFrames() - nQueued;
			if (nFree < mBufferFrames) {
				break;
			}
			if (mRXEnabled
			    && mSound.GetReadQueueFramesAvail() < mBufferFrames
			    && nQueued >= mBufferFrames) {
				break; // input not there yet, and output can wait for it
			}

			readInput();
			if (mpProcessor != nullptr) {
				mpProcessor->processAudio(InputPtrs, OutputPtrs, mBufferFrames);

				// interleave + clamp; Circle's float conversion wraps
				// instead of clamping, so overshoot must be caught here
				for (unsigned i = 0; i < mBufferFrames; i++) {
					for (unsigned ch = 0; ch < kChannels; ch++) {
						float fSample = mOutputBuffers[ch][i];
						if (fSample > 1.0f) fSample = 1.0f;
						if (fSample < -1.0f) fSample = -1.0f;
						mWriteBuffer[kChannels * i + ch] = fSample;
					}
				}
			} else {
				for (unsigned i = 0; i < mBufferFrames * kChannels; i++) {
					mWriteBuffer[i] = 0.0f; // no processor: keep the stream fed with silence
				}
			}

			mSound.Write(mWriteBuffer, mBufferFrames * kChannels * sizeof(float));
			mDiagTXFrames += mBufferFrames;
		}
	}

	// applies the app's bit-depth choice to the kernel options BEFORE the
	// sound device (and later the USB stack) reads it; returns the depth
	// actually in effect
	static unsigned applyBitDepth(unsigned nBitDepth) {
		if (nBitDepth == 16 || nBitDepth == 24) {
			CKernelOptions::Get()->SetSoundOption(nBitDepth);
		}
		return CKernelOptions::Get()->GetSoundOption() == 24 ? 24 : 16;
	}

	unsigned mBitDepth; // declared before mSound: initializer order matters
	CUSBSoundBaseDevice mSound;
	AudioProcessor* mpProcessor = nullptr;
	unsigned mSampleRate;
	unsigned mBufferFrames;
	unsigned mLastStartTicks = 0;
	boolean mRXEnabled;
	boolean mActiveLogged = FALSE;

	// DIAG counters
	unsigned mDiagTXFrames = 0, mDiagRXFrames = 0, mDiagShort = 0, mDiagMinRead = ~0u;
	unsigned mDiagTicks = 0;

	// block buffers
	float mInputBuffers[kChannels][kMaxBufferFrames] = {}; // zero-padded on input underrun
	float mOutputBuffers[kChannels][kMaxBufferFrames];
	float mReadBuffer[kChannels * kMaxBufferFrames];  // interleaved, from Circle
	float mWriteBuffer[kChannels * kMaxBufferFrames]; // interleaved, to Circle
};

} // namespace InstOS
