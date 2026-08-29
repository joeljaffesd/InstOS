#include <InstOS.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include <modules/HDMIDisplay.hpp>
#include "../state.hpp"

// The receiving half of the split oscilloscope: this box has the screen.
// It takes the shared audio ring cast by the audio/ box over the Ethernet
// cable and draws it — one second of history, min/max envelope per column,
// newest at the right edge. No USB audio here at all.
// Direct cable, static IPs: audio/ (maker) .1 -> graphics/ (taker) .2.
class ScopeTaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::Ethernet mNet{"192.168.10.2"};
	InstOS::Barebone::Taker<SharedState> mTaker;
	InstOS::HDMIDisplay mDisplay;

	SharedState mState;
	boolean mHaveFrame = FALSE;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	void loop() override {
		C2DGraphics* pGraphics = mDisplay.graphics();

		if (mTaker.get(mState)) {
			mHaveFrame = TRUE;
			if (pGraphics != nullptr) {
				drawScope(pGraphics);
				pGraphics->UpdateDisplay(); // vsync-paced
			}
		}

		// non-blocking heartbeat
		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

	void drawScope(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();
		unsigned nMidY = nHeight / 2;
		float fGain = static_cast<float>(nHeight) * 0.5f; // full scale spans the screen

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));
		pGraphics->DrawLine(0, nMidY, nWidth - 1, nMidY, COLOR2D(48, 48, 48));

		// one second of history across the screen, newest at the right edge;
		// each column shows the min/max envelope of its span of samples
		unsigned nSamplesPerColumn = SharedState::bufferSize / nWidth;
		if (nSamplesPerColumn == 0) nSamplesPerColumn = 1;

		for (unsigned nX = 0; nX < nWidth; nX++) {
			float fMin = 1.0f, fMax = -1.0f;
			// column nWidth-1 holds the newest samples (order 0)
			int nBaseOrder = (int) (nWidth - 1 - nX) * nSamplesPerColumn;
			for (unsigned i = 0; i < nSamplesPerColumn; i++) {
				float fSample = mState.readSample(nBaseOrder + (int) i);
				if (fSample < fMin) fMin = fSample;
				if (fSample > fMax) fMax = fSample;
			}

			unsigned nYTop = sampleToY(fMax, nMidY, fGain, nHeight);
			unsigned nYBottom = sampleToY(fMin, nMidY, fGain, nHeight);
			pGraphics->DrawLine(nX, nYTop, nX, nYBottom, COLOR2D(255, 255, 255));
		}
	}

	static unsigned sampleToY(float fSample, unsigned nMidY, float fGain, unsigned nHeight) {
		int nY = static_cast<int>(nMidY) - static_cast<int>(fSample * fGain);
		if (nY < 0) nY = 0;
		if (nY >= static_cast<int>(nHeight)) nY = nHeight - 1;
		return static_cast<unsigned>(nY);
	}

};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static ScopeTaker mScopeTaker;
	mScopeTaker.start();
}
