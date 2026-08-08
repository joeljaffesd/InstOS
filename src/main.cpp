#include <InstOS.hpp>
#include <circle/sound/usbsoundbasedevice.h>

class VoltSine : public InstOS::Firmware {
	static constexpr unsigned kSampleRate = 48000;
	static constexpr unsigned kBufferFrames = 128; // frames per callback write
	static constexpr float kAmplitude = 0.5f;

	// 110 Hz sine as a rotation oscillator: no libm needed.
	// Constants are sin/cos of 2*pi*110/48000.
	static constexpr float kSinDelta = 0.01439847f;
	static constexpr float kCosDelta = 0.99989634f;

	CActLED mActLED;
	CUSBSoundBaseDevice mSound{kSampleRate, CUSBSoundBaseDevice::DeviceModeTXOnly};
	float mSin = 0.0f;
	float mCos = 1.0f;
	unsigned mLastBlinkTicks = 0;
	unsigned mLastStartTicks = 0;
	boolean mStartLogged = FALSE;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		m_Logger.Write("voltsine", LogNotice,
			       "USB audio: %u Hz, %u-frame buffers, 110 Hz sine to L+R",
			       kSampleRate, kBufferFrames);

		if (!mSound.AllocateQueueFrames(kBufferFrames * 2)) {
			m_Logger.Write("voltsine", LogError, "Cannot allocate sound queue");
			return FALSE;
		}

		mSound.SetWriteFormat(SoundFormatSigned16, 2);
		mSound.RegisterNeedDataCallback(NeedDataStub, this);

		return TRUE;
	}

	// audio callback: keep the queue topped up, one 128-frame buffer at a time
	void fillQueue() {
		s16 Buffer[kBufferFrames * 2];

		unsigned nFree = mSound.GetQueueSizeFrames() - mSound.GetQueueFramesAvail();
		while (nFree >= kBufferFrames) {
			for (unsigned i = 0; i < kBufferFrames; i++) {
				s16 nSample = static_cast<s16>(mSin * kAmplitude * 32767.0f);
				Buffer[2 * i]     = nSample; // L
				Buffer[2 * i + 1] = nSample; // R

				float fSin = mSin * kCosDelta + mCos * kSinDelta;
				float fCos = mCos * kCosDelta - mSin * kSinDelta;
				mSin = fSin;
				mCos = fCos;
			}

			// re-normalize the oscillator so it never drifts in amplitude
			float fScale = (3.0f - (mSin * mSin + mCos * mCos)) * 0.5f;
			mSin *= fScale;
			mCos *= fScale;

			mSound.Write(Buffer, sizeof Buffer);
			nFree -= kBufferFrames;
		}
	}

	static void NeedDataStub(void* pParam) {
		static_cast<VoltSine*>(pParam)->fillQueue();
	}

	void loop() override {
		unsigned nTicks = m_Timer.GetTicks();

		// Streaming not running: retry once a second (device may still be
		// enumerating, or get hot-plugged later). Start() logs its own errors.
		if (!mSound.IsActive() && nTicks - mLastStartTicks >= 1 * HZ) {
			mLastStartTicks = nTicks;
			mStartLogged = FALSE;
			m_Logger.Write("voltsine", LogNotice, "Starting USB audio streaming...");
			fillQueue(); // prime the queue before the first transfer
			mSound.Start();
		}

		if (mSound.IsActive() && !mStartLogged) {
			mStartLogged = TRUE;
			m_Logger.Write("voltsine", LogNotice, "USB audio streaming is active");
		}

		// non-blocking heartbeat so USB plug-and-play stays responsive
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

};

int main() {
	VoltSine mVoltSine;
	mVoltSine.start();
}
