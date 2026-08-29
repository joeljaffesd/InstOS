#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/Touch.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include <math.h>
#include "../state.hpp"

// M_PI is a POSIX extension and is not visible under the strict-ISO
// settings Circle builds with.
static constexpr float kPi = 3.14159265358979323846f;

// Monotron, panel half -- Jaffrack's MonotronEditor on a touchscreen.
// The five knobs (rate, intensity, cutoff, delay time, feedback), the
// three-way mode selector and the pitch ribbon all live here; the resolved
// parameter set is cast to the audio box, which owns no controls at all.
// Upstream did the same split with allolib's ParameterServer over OSC.
// Direct cable, static IPs: maker (panel) .1 -> taker (audio) .2.
class MonotronMaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	InstOS::HDMIDisplay mDisplay;
	InstOS::Touch mTouch{mDisplay};
	InstOS::Ethernet mNet{"192.168.10.1"};
	InstOS::Barebone::Maker<MonotronState> mMaker{"192.168.10.2",
						      InstOS::Barebone::kDefaultPort, 120};

	static constexpr unsigned kPanelHeight = 220; // knobs above, ribbon below
	static constexpr unsigned kKnobCount = 5;
	static constexpr unsigned kModeCount = 3;
	// pixels of vertical drag for a knob's full travel
	static constexpr unsigned kDragRange = 300;

	enum { kKnobRate, kKnobIntensity, kKnobCutoff, kKnobDelay, kKnobFeedback };
	enum { kModeBypass, kModeTriangle, kModeSquare };

	// Knob positions are normalized; the curve is applied when the state is
	// assembled, so the audio box never needs to know the mapping. Defaults
	// chosen so each knob starts at its Jaffrack default -- which, for the
	// curved ones, is exactly half travel.
	float mKnob[kKnobCount] = {0.5f, 0.f, 0.5f, 0.5f, 0.f};
	int mMode = kModeBypass;

	// per-contact grab state, so a knob keeps tracking the finger that took
	// it even as other fingers come and go
	int mGrab[InstOS::Touch::kMaxFingers];
	float mGrabValue[InstOS::Touch::kMaxFingers];
	unsigned mGrabY[InstOS::Touch::kMaxFingers];
	boolean mWasDown[InstOS::Touch::kMaxFingers];

	MonotronState mState = {};
	unsigned mRibbonX = 0;
	boolean mDirty = TRUE;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			mGrab[i] = -1;
			mWasDown[i] = FALSE;
		}

		mState.keyboardMute = 1;
		mState.frequency = kFreqMid;
		mMaker.attach(&mState);
		return TRUE;
	}

	void loop() override {
		pollPanel();

		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics != nullptr && (mDirty || mTouch.isDirty())) {
			mTouch.clearDirty();
			mDirty = FALSE;
			draw(pGraphics);
			pGraphics->UpdateDisplay(); // vsync-paced
		}

		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

	void pollPanel() {
		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics == nullptr) {
			return;
		}

		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();

		const InstOS::Touch::Finger* pFingers = mTouch.fingers();
		boolean bRibbonHeld = FALSE;

		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			boolean bDown = pFingers[i].down;
			unsigned nX = pFingers[i].x;
			unsigned nY = pFingers[i].y;

			if (bDown && !mWasDown[i]) {
				beginGrab(i, nX, nY, nWidth);
			} else if (!bDown && mWasDown[i]) {
				mGrab[i] = -1;
			}
			mWasDown[i] = bDown;

			if (!bDown) {
				continue;
			}

			if (mGrab[i] >= 0) {
				// relative drag: absolute tracking would make the
				// knob jump to the finger the instant it lands
				float fDelta = ((float) mGrabY[i] - (float) nY)
					       / (float) kDragRange;
				float fValue = mGrabValue[i] + fDelta;
				if (fValue < 0.f) fValue = 0.f;
				if (fValue > 1.f) fValue = 1.f;

				if (fValue != mKnob[mGrab[i]]) {
					mKnob[mGrab[i]] = fValue;
					mDirty = TRUE;
				}
			} else if (nY >= kPanelHeight && nHeight > kPanelHeight) {
				bRibbonHeld = TRUE;
				mRibbonX = nX;
			}
		}

		buildState(bRibbonHeld, nWidth);
	}

	// Decide what a new contact has taken hold of: a knob, a mode button,
	// or the ribbon.
	void beginGrab(unsigned nSlot, unsigned nX, unsigned nY, unsigned nWidth) {
		mGrab[nSlot] = -1;

		if (nY >= kPanelHeight) {
			return; // ribbon; handled by the caller each pass
		}

		for (unsigned k = 0; k < kKnobCount; k++) {
			unsigned nCentreX, nCentreY, nRadius;
			knobGeometry(k, nWidth, &nCentreX, &nCentreY, &nRadius);

			int nDX = (int) nX - (int) nCentreX;
			int nDY = (int) nY - (int) nCentreY;
			if ((unsigned) (nDX * nDX + nDY * nDY) <= nRadius * nRadius) {
				mGrab[nSlot] = (int) k;
				mGrabValue[nSlot] = mKnob[k];
				mGrabY[nSlot] = nY;
				return;
			}
		}

		for (unsigned m = 0; m < kModeCount; m++) {
			unsigned nLeft, nTop, nW, nH;
			modeGeometry(m, nWidth, &nLeft, &nTop, &nW, &nH);
			if (nX >= nLeft && nX < nLeft + nW && nY >= nTop && nY < nTop + nH) {
				if (mMode != (int) m) {
					mMode = (int) m;
					mDirty = TRUE;
				}
				return;
			}
		}
	}

	void buildState(boolean bRibbonHeld, unsigned nWidth) {
		MonotronState New = mState;

		New.keyboardMute = bRibbonHeld ? 0 : 1;
		if (bRibbonHeld) {
			New.frequency = mapParam((float) mRibbonX / (float) nWidth,
						 kFreqMin, kFreqMax, kFreqMid);
		}

		// Upstream's radio button: choice 0 bypasses the instrument
		// entirely, 1 and 2 run it with a triangle or square LFO.
		New.bypass = (mMode == kModeBypass) ? 1 : 0;
		New.lfoMode = (mMode == kModeSquare) ? 1 : 0;

		New.rate = mapParam(mKnob[kKnobRate], kRateMin, kRateMax, kRateMid);
		New.intensity = mKnob[kKnobIntensity];
		New.cutoff = mapParam(mKnob[kKnobCutoff], kCutoffMin, kCutoffMax, kCutoffMid);
		New.delayTime = mapParam(mKnob[kKnobDelay], kDelayMin, kDelayMax, kDelayMid);
		New.feedback = mKnob[kKnobFeedback] * kFeedbackMax;

		if (   New.keyboardMute != mState.keyboardMute
		    || New.bypass != mState.bypass
		    || New.lfoMode != mState.lfoMode
		    || New.frequency != mState.frequency
		    || New.intensity != mState.intensity
		    || New.rate != mState.rate
		    || New.cutoff != mState.cutoff
		    || New.delayTime != mState.delayTime
		    || New.feedback != mState.feedback) {
			New.changes = mState.changes + 1;
			mState = New;
			mDirty = TRUE;
		}
	}

	// ---- layout ------------------------------------------------------
	// Computed from the live surface rather than hardcoded for 1600x600,
	// so the panel still lays out sensibly on another display.

	static void knobGeometry(unsigned nKnob, unsigned nWidth,
				 unsigned* pCentreX, unsigned* pCentreY, unsigned* pRadius) {
		unsigned nZone = (nWidth * 3) / 5;      // knobs own the left 60%
		unsigned nSpacing = nZone / kKnobCount;
		unsigned nRadius = nSpacing / 2 > 10 ? nSpacing / 2 - 10 : 4;
		if (nRadius > kPanelHeight / 2 - 40) {
			nRadius = kPanelHeight / 2 - 40;
		}

		*pCentreX = nSpacing / 2 + nKnob * nSpacing;
		*pCentreY = kPanelHeight / 2;
		*pRadius = nRadius;
	}

	static void modeGeometry(unsigned nMode, unsigned nWidth, unsigned* pLeft,
				 unsigned* pTop, unsigned* pW, unsigned* pH) {
		unsigned nZone = (nWidth * 3) / 5;
		unsigned nAvail = nWidth - nZone - 40;
		unsigned nW = nAvail / kModeCount;

		*pLeft = nZone + 20 + nMode * nW;
		*pTop = kPanelHeight / 2 - 40;
		*pW = nW > 8 ? nW - 8 : nW;
		*pH = 80;
	}

	// ---- drawing -----------------------------------------------------

	void draw(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();

		pGraphics->ClearScreen(COLOR2D(8, 8, 10));

		static const char* KnobNames[kKnobCount] = {"RATE", "INT", "CUT", "DLY", "FB"};
		float Values[kKnobCount] = {mState.rate, mState.intensity, mState.cutoff,
					    mState.delayTime, mState.feedback};

		for (unsigned k = 0; k < kKnobCount; k++) {
			drawKnob(pGraphics, k, nWidth, KnobNames[k], Values[k]);
		}

		static const char* ModeNames[kModeCount] = {"BYPASS", "TRI", "SQR"};
		for (unsigned m = 0; m < kModeCount; m++) {
			unsigned nLeft, nTop, nW, nH;
			modeGeometry(m, nWidth, &nLeft, &nTop, &nW, &nH);
			boolean bOn = (mMode == (int) m);
			pGraphics->DrawRect(nLeft, nTop, nW, nH,
					    bOn ? COLOR2D(96, 200, 255) : COLOR2D(32, 32, 38));
			pGraphics->DrawText(nLeft + 12, nTop + nH / 2 - 8,
					    bOn ? COLOR2D(0, 0, 0) : COLOR2D(180, 180, 180),
					    ModeNames[m]);
		}

		// ribbon
		if (nHeight > kPanelHeight) {
			pGraphics->DrawRect(0, kPanelHeight, nWidth, 2, COLOR2D(60, 60, 70));

			if (!mState.keyboardMute) {
				unsigned nBarWidth = 12;
				unsigned nLeft = mRibbonX > nBarWidth / 2
						 ? mRibbonX - nBarWidth / 2 : 0;
				if (nLeft + nBarWidth > nWidth) {
					nLeft = nWidth - nBarWidth;
				}
				// DrawRect rejects (does not clip) an out-of-bounds
				// rect, hence the clamp above
				pGraphics->DrawRect(nLeft, kPanelHeight + 2, nBarWidth,
						    nHeight - kPanelHeight - 2,
						    COLOR2D(96, 200, 255));
			}

			CString Ribbon;
			if (mState.keyboardMute) {
				Ribbon.Format("ribbon idle    %u updates", mState.changes);
			} else {
				Ribbon.Format("%u Hz    %u updates",
					      (unsigned) mState.frequency, mState.changes);
			}
			pGraphics->DrawText(8, kPanelHeight + 12, COLOR2D(200, 200, 200),
					    (const char*) Ribbon);
		}
	}

	void drawKnob(C2DGraphics* pGraphics, unsigned nKnob, unsigned nWidth,
		      const char* pName, float fValue) {
		unsigned nCentreX, nCentreY, nRadius;
		knobGeometry(nKnob, nWidth, &nCentreX, &nCentreY, &nRadius);

		pGraphics->DrawCircle(nCentreX, nCentreY, nRadius, COLOR2D(40, 40, 48));
		pGraphics->DrawCircle(nCentreX, nCentreY, nRadius > 6 ? nRadius - 6 : 1,
				      COLOR2D(24, 24, 28));

		// pointer sweeps 270 degrees, straight up at half travel
		float fAngle = (-135.f + mKnob[nKnob] * 270.f) * kPi / 180.f;
		int nTipX = (int) nCentreX + (int) (sinf(fAngle) * (float) (nRadius - 8));
		int nTipY = (int) nCentreY - (int) (cosf(fAngle) * (float) (nRadius - 8));
		pGraphics->DrawLine(nCentreX, nCentreY, (unsigned) nTipX, (unsigned) nTipY,
				    COLOR2D(96, 200, 255));

		CString Label;
		if (fValue >= 100.f) {
			Label.Format("%s %u", pName, (unsigned) fValue);
		} else {
			Label.Format("%s %.2f", pName, (double) fValue);
		}
		pGraphics->DrawText(nCentreX - nRadius, nCentreY + nRadius + 10,
				    COLOR2D(170, 170, 170), (const char*) Label);
	}
};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static MonotronMaker mMonotronMaker;
	mMonotronMaker.start();
}
