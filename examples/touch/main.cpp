#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/Touch.hpp>

// Multitouch paint: a large filled circle follows every finger held on the
// panel, one colour per contact slot. Redraws only when the contact state
// has changed -- UpdateDisplay() blocks for vsync, so a free-running redraw
// would spend the entire loop blitting the full panel for nothing.
class TouchDemo : public InstOS::Firmware {
	// modules: declaration order = init order. USBHost first (the panel is a
	// USB HID digitizer), then the display whose pixels Touch reports in.
	InstOS::USBHost mUSB;
	InstOS::HDMIDisplay mDisplay;
	InstOS::Touch mTouch{mDisplay};

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	// one colour per contact slot, so overlapping fingers stay tellable apart
	static constexpr unsigned kColorCount = InstOS::Touch::kMaxFingers;
	const T2DColor mColors[kColorCount] = {
		COLOR2D(255,  64,  64), COLOR2D( 64, 255,  64), COLOR2D( 96, 160, 255),
		COLOR2D(255, 208,  64), COLOR2D(224,  96, 255), COLOR2D( 64, 240, 224),
		COLOR2D(255, 144,  48), COLOR2D(160, 255, 128), COLOR2D(255, 112, 176),
		COLOR2D(176, 176, 255)
	};

	void loop() override {
		C2DGraphics* pGraphics = mDisplay.graphics();

		if (pGraphics != nullptr && mTouch.isDirty()) {
			mTouch.clearDirty(); // clear first: an event during the redraw
						 // must leave the frame marked dirty
			draw(pGraphics);
			pGraphics->UpdateDisplay(); // vsync-paced
		}

		// non-blocking heartbeat: proves the loop is alive even when the
		// screen is idle, which otherwise looks identical to a hang
		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

	void draw(C2DGraphics* pGraphics) {
		unsigned nHeight = pGraphics->GetHeight();
		unsigned nRadius = nHeight / 8; // 75 px on the 1600x600 panel

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));

		const InstOS::Touch::Finger* pFingers = mTouch.fingers();
		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			if (pFingers[i].down) {
				drawDisc(pGraphics, (int) pFingers[i].x, (int) pFingers[i].y,
					 (int) nRadius, mColors[i % kColorCount]);
			}
		}

		// status line, so a quiet screen still says the firmware is running
		pGraphics->DrawText(8, 8, COLOR2D(128, 128, 128),
					mTouch.isConnected() ? "touch ready -- hold to paint"
							 : "waiting for touchscreen...");
	}

	// Circle's DrawCircle() REJECTS any circle that would cross an edge
	// rather than clipping it, so a finger near the border would paint
	// nothing at all. Draw the disc here instead, clipped: walk only the
	// on-screen part of the bounding box and radius-test each pixel.
	// DrawPixel() is the public entry point that bounds-checks (SetPixel is
	// private), and it keeps the whole thing safely inside the buffer.
	static void drawDisc(C2DGraphics* pGraphics, int nCentreX, int nCentreY,
				 int nRadius, T2DColor Color) {
		int nWidth = (int) pGraphics->GetWidth();
		int nHeight = (int) pGraphics->GetHeight();

		int nLeft   = nCentreX - nRadius < 0 ? 0 : nCentreX - nRadius;
		int nTop	= nCentreY - nRadius < 0 ? 0 : nCentreY - nRadius;
		int nRight  = nCentreX + nRadius > nWidth - 1 ? nWidth - 1 : nCentreX + nRadius;
		int nBottom = nCentreY + nRadius > nHeight - 1 ? nHeight - 1 : nCentreY + nRadius;

		int nRadiusSquared = nRadius * nRadius;

		for (int nY = nTop; nY <= nBottom; nY++) {
			int nDeltaY = nY - nCentreY;
			for (int nX = nLeft; nX <= nRight; nX++) {
				int nDeltaX = nX - nCentreX;
				if (nDeltaX * nDeltaX + nDeltaY * nDeltaY <= nRadiusSquared) {
					pGraphics->DrawPixel((unsigned) nX, (unsigned) nY, Color);
				}
			}
		}
	}
};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static TouchDemo mTouchDemo;
	mTouchDemo.start();
}
