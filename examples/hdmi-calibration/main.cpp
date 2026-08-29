#include <InstOS.hpp>
#include <modules/HDMIDisplay.hpp>

// Static display calibration pattern, drawn once at the panel's native mode:
//
//   - double border on the outermost pixels: if it doesn't touch all four
//     glass edges, the video mode is larger than the framebuffer
//     (letterboxing — see the [all]/config-extra.txt lesson) or overscan
//     is eating pixels
//   - 100px grid + center crosshair: geometry and linearity
//   - color bars and a 16-step grayscale ramp: channel order and depth
//   - single-pixel alternating stripes: blur or moiré here means the panel
//     is NOT being driven 1:1 at native resolution
//   - resolution readout: what the framebuffer believes it is
//
// Pixel geometry note: the WaveShare 9.3" panel has SQUARE pixels (display
// area 221x83mm at 1600x600 = 0.138mm pitch both ways; the datasheet's
// 0.046mm "H pitch" is the RGB subpixel). The circle should look round.
class DisplayCalibration : public InstOS::Firmware {
	InstOS::HDMIDisplay mDisplay;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;
	boolean mDrawn = FALSE;

	void loop() override {
		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics != nullptr && !mDrawn) {
			mDrawn = TRUE;
			drawPattern(pGraphics);
			pGraphics->UpdateDisplay();
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

	void drawPattern(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));

		// 100px grid (dim)
		for (unsigned nX = 100; nX < nWidth; nX += 100) {
			pGraphics->DrawLine(nX, 0, nX, nHeight - 1, COLOR2D(32, 32, 32));
		}
		for (unsigned nY = 100; nY < nHeight; nY += 100) {
			pGraphics->DrawLine(0, nY, nWidth - 1, nY, COLOR2D(32, 32, 32));
		}

		// center crosshair + circle (should appear round: square pixels)
		pGraphics->DrawLine(nWidth / 2, 0, nWidth / 2, nHeight - 1, COLOR2D(96, 96, 96));
		pGraphics->DrawLine(0, nHeight / 2, nWidth - 1, nHeight / 2, COLOR2D(96, 96, 96));
		pGraphics->DrawCircleOutline(nWidth / 2, nHeight / 2, nHeight / 4, COLOR2D(96, 96, 96));

		// edge markers: double border on the outermost pixels
		pGraphics->DrawRectOutline(0, 0, nWidth, nHeight, COLOR2D(255, 255, 255));
		pGraphics->DrawRectOutline(2, 2, nWidth - 4, nHeight - 4, COLOR2D(255, 255, 255));

		// color bars: W R G B C M Y, centered band
		static const T2DColor Bars[] = {
			COLOR2D(255, 255, 255), COLOR2D(255, 0, 0), COLOR2D(0, 255, 0),
			COLOR2D(0, 0, 255), COLOR2D(0, 255, 255), COLOR2D(255, 0, 255),
			COLOR2D(255, 255, 0),
		};
		constexpr unsigned kNumBars = sizeof Bars / sizeof Bars[0];
		unsigned nBarWidth = (nWidth - 200) / kNumBars;
		for (unsigned i = 0; i < kNumBars; i++) {
			pGraphics->DrawRect(100 + i * nBarWidth, 60, nBarWidth, 80, Bars[i]);
		}

		// 16-step grayscale ramp
		unsigned nStepWidth = (nWidth - 200) / 16;
		for (unsigned i = 0; i < 16; i++) {
			u8 nLevel = static_cast<u8>(i * 17); // 0..255
			pGraphics->DrawRect(100 + i * nStepWidth, 160, nStepWidth, 60,
					    COLOR2D(nLevel, nLevel, nLevel));
		}

		// 1:1 pixel check: alternating single-pixel stripes,
		// vertical (left patch) and horizontal (right patch)
		for (unsigned nX = 100; nX < 100 + 200; nX += 2) {
			pGraphics->DrawLine(nX, 240, nX, 300, COLOR2D(255, 255, 255));
		}
		for (unsigned nY = 240; nY < 300; nY += 2) {
			pGraphics->DrawLine(nWidth - 300, nY, nWidth - 100, nY, COLOR2D(255, 255, 255));
		}

		// resolution readout
		CString Resolution;
		Resolution.Format("%u x %u", nWidth, nHeight);
		pGraphics->DrawText(nWidth / 2, nHeight / 2 + 20, COLOR2D(255, 255, 255),
				    Resolution, C2DGraphics::AlignCenter);
		pGraphics->DrawText(nWidth / 2, nHeight / 2 + 40, COLOR2D(128, 128, 128),
				    "InstOS display calibration", C2DGraphics::AlignCenter);
	}

};

int main() {
	static DisplayCalibration mDisplayCalibration;
	mDisplayCalibration.start();
}
