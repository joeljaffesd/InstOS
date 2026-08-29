#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>
#include <modules/HDMIDisplay.hpp>

#define SAMPLERATE 48000

// Oscilloscope: full-duplex audio passthrough with the input waveform drawn
// live on the HDMI display. Free-running (no trigger), one sample per pixel
// column, newest samples at the right edge — modeled on the Jaffrack/Allolib
// Oscilloscope (circular sample buffer feeding a line-strip mesh).
class Oscilloscope : public InstOS::Firmware, public InstOS::AudioProcessor {
	// modules: declaration order = init order; UAC requires USBHost before it
	InstOS::USBHost mUSB;
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};
	InstOS::HDMIDisplay mDisplay;

	// single-writer (audio path) / single-reader (render loop) sample ring;
	// races only ever smear pixels for one frame, so no locking needed.
	// Sized to hold the full second of history the display shows.
	static constexpr unsigned kHistorySamples = SAMPLERATE; // 1 s at the stream rate
	static constexpr unsigned kRingSize = 65536; // power of two >= kHistorySamples
	float mRing[kRingSize] = {};
	volatile unsigned mWriteIndex = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mAudio.setProcessor(this);
		return TRUE;
	}

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

	void loop() override {
		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics == nullptr) {
			return; // no display: still a working audio passthrough
		}

		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();
		unsigned nMidY = nHeight / 2;
		float fGain = static_cast<float>(nHeight) * 0.5f; // full scale spans the screen

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));
		pGraphics->DrawLine(0, nMidY, nWidth - 1, nMidY, COLOR2D(48, 48, 48));

		// one second of history across the screen, newest at the right edge;
		// each column shows the min/max envelope of its span of samples
		unsigned nSamplesPerColumn = kHistorySamples / nWidth;
		if (nSamplesPerColumn == 0) nSamplesPerColumn = 1;
		unsigned nStart = (mWriteIndex - kHistorySamples) & (kRingSize - 1);

		for (unsigned nX = 0; nX < nWidth; nX++) {
			float fMin = 1.0f, fMax = -1.0f;
			unsigned nBase = nStart + nX * nSamplesPerColumn;
			for (unsigned i = 0; i < nSamplesPerColumn; i++) {
				float fSample = mRing[(nBase + i) & (kRingSize - 1)];
				if (fSample < fMin) fMin = fSample;
				if (fSample > fMax) fMax = fSample;
			}

			unsigned nYTop = sampleToY(fMax, nMidY, fGain, nHeight);
			unsigned nYBottom = sampleToY(fMin, nMidY, fGain, nHeight);
			pGraphics->DrawLine(nX, nYTop, nX, nYBottom, COLOR2D(255, 255, 255));
		}

		pGraphics->UpdateDisplay(); // vsync-paced: throttles this loop to ~60 fps
	}

	static unsigned sampleToY(float fSample, unsigned nMidY, float fGain, unsigned nHeight) {
		int nY = static_cast<int>(nMidY) - static_cast<int>(fSample * fGain);
		if (nY < 0) nY = 0;
		if (nY >= static_cast<int>(nHeight)) nY = nHeight - 1;
		return static_cast<unsigned>(nY);
	}

};

int main() {
	// static: the sample ring makes this object far larger than the 128 KB
	// kernel stack — stack allocation would overflow into DMA territory
	static Oscilloscope mOscilloscope;
	mOscilloscope.start();
}
