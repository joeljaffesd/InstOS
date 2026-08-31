#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/Touch.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include <modules/TUI.hpp>
#include "../state.hpp"
#include "../tui-tuner.hpp"
#include <assets/graphics/yes.h>
#include <math.h>

#define SAMPLERATE 48000

// Jaffrack, showing half — InstOS's remake of upstream Jaffrack's audioUI. The home
// page is an oscilloscope of the output mix (cast from the audio/ box) under
// a top bar of six touch buttons, left to right:
//
//   1. global mute (toggle)     4. monotron panel (mode)
//   2. bass mute (toggle)       5. HuckFinn one-shot (fire/restart)
//   3. Yes Man image (mode)     6. TUITUNE tuner (mode)
//
// Toggles act immediately and are remembered across mode switches (not
// across boots). Modes take the whole screen. Image and tuner come home on
// a tap ANYWHERE; the monotron panel (whose surface is all controls) comes
// home on a top-left corner tap. All control flows to the audio box as one
// ControlState.
// Direct cable, static IPs: audio/ .1 <-> graphics/ .2.

static constexpr float kPi = 3.14159265358979323846f;

// ------------------------------------------------- monotron panel (mode) ----
// Adapted from examples/monotron/graphics: same knobs, modes, ribbon and
// drawing; owns normalized knob positions and produces a MonotronState.
struct MonotronPanel {
	static constexpr unsigned kPanelHeight = 220;
	static constexpr unsigned kKnobCount = 5;
	static constexpr unsigned kModeCount = 3;
	static constexpr unsigned kDragRange = 300;

	enum { kKnobRate, kKnobIntensity, kKnobCutoff, kKnobDelay, kKnobFeedback };
	enum { kModeBypass, kModeTriangle, kModeSquare };

	float mKnob[kKnobCount] = {0.5f, 0.f, 0.5f, 0.5f, 0.f};
	int mMode = kModeBypass;

	int mGrab[InstOS::Touch::kMaxFingers];
	float mGrabValue[InstOS::Touch::kMaxFingers];
	unsigned mGrabY[InstOS::Touch::kMaxFingers];
	boolean mWasDown[InstOS::Touch::kMaxFingers];

	MonotronState mState = {};
	unsigned mRibbonX = 0;
	boolean mDirty = TRUE;

	MonotronPanel() {
		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			mGrab[i] = -1;
			mWasDown[i] = FALSE;
		}
		mState.keyboardMute = 1;
		mState.frequency = kFreqMid;
	}

	void poll(const InstOS::Touch::Finger* pFingers, unsigned nWidth, unsigned nHeight) {
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

	static void knobGeometry(unsigned nKnob, unsigned nWidth,
				 unsigned* pCentreX, unsigned* pCentreY, unsigned* pRadius) {
		unsigned nZone = (nWidth * 3) / 5;
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

		// back hint (the shell owns the top-left corner)
		pGraphics->DrawText(nWidth - 170, 8, COLOR2D(120, 120, 130),
				    "< tap corner: home");

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

// ------------------------------------------------------------------ app ----

class GraphicsBox : public InstOS::Firmware {
	// modules: declaration order = init order (Touch needs USBHost + display)
	InstOS::USBHost mUSB;
	InstOS::HDMIDisplay mDisplay;
	InstOS::Touch mTouch{mDisplay};
	InstOS::Ethernet mNet{"192.168.10.2"};
	InstOS::Barebone::Taker<SharedState> mTaker; // scope ring, default port
	InstOS::Barebone::Maker<ControlState> mMaker{"192.168.10.1", kControlPort, 60};

	InstOS::TUI* mpTUI = nullptr; // built on first use (tuner mode)

	enum TMode { ModeHome, ModeImage, ModeMonotron, ModeTuner };
	static constexpr unsigned kButtons = 6;
	static constexpr unsigned kBarHeight = 120;
	static constexpr unsigned kCorner = 140; // back tap zone in modes

	TMode mMode = ModeHome;
	ControlState mControl = {};
	SharedState mScope; // large: this object MUST be static (see main)
	MonotronPanel mPanel;
	TuiTuner::Tuner mTuner;

	boolean mShellWasDown[InstOS::Touch::kMaxFingers] = {};
	unsigned mFrame = 0;
	float mWindow[TuiTuner::Tuner::kWindow];

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mControl.monotron = mPanel.mState;
		mMaker.attach(&mControl);
		return TRUE;
	}

	void loop() override {
		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics == nullptr) {
			return;
		}

		mTaker.get(mScope);

		// serial keys still adjust the tuner's A4, as in examples/tuner
		char keys[8];
		int nk = m_Serial.Read(keys, sizeof keys);
		for (int i = 0; i < nk; i++) {
			if (keys[i] == '+' || keys[i] == '=') {
				mTuner.mA4 = mTuner.mA4 < 466.0f ? mTuner.mA4 + 1.0f : 466.0f;
			}
			if (keys[i] == '-' || keys[i] == '_') {
				mTuner.mA4 = mTuner.mA4 > 415.0f ? mTuner.mA4 - 1.0f : 415.0f;
			}
		}

		handleTouch(pGraphics);

		switch (mMode) {
		case ModeHome:     drawHome(pGraphics); break;
		case ModeImage:    drawImage(pGraphics); break;
		case ModeMonotron: {
			mPanel.poll(mTouch.fingers(), pGraphics->GetWidth(),
				    pGraphics->GetHeight());
			mControl.monotron = mPanel.mState;
			bumpChanges();
			mPanel.draw(pGraphics);
			pGraphics->UpdateDisplay();
			break;
		}
		case ModeTuner: {
			if (mpTUI == nullptr) {
				mpTUI = new InstOS::TUI(mDisplay, 2);
			}
			// window of the latest samples, oldest first, from the
			// cast ring (~15 Hz is plenty; the ring lands at 30)
			if (mFrame % 4 == 0) {
				for (unsigned i = 0; i < TuiTuner::Tuner::kWindow; i++) {
					mWindow[i] = mScope.readSample(
						(int) (TuiTuner::Tuner::kWindow - i));
				}
				mTuner.analyze(mWindow, (float) SAMPLERATE);
			}
			mTuner.render(*mpTUI); // presents (vsync-paced)
			break;
		}
		}

		mFrame++;
	}

	// ---- input -----------------------------------------------------------

	void handleTouch(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();
		const InstOS::Touch::Finger* pFingers = mTouch.fingers();

		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			boolean bDown = pFingers[i].down;
			boolean bNewPress = bDown && !mShellWasDown[i];
			mShellWasDown[i] = bDown;
			if (!bNewPress) {
				continue;
			}

			unsigned nX = pFingers[i].x;
			unsigned nY = pFingers[i].y;

			if (mMode == ModeImage || mMode == ModeTuner) {
				mMode = ModeHome; // any tap leaves these screens
				continue;
			}
			if (mMode == ModeMonotron) {
				// only the top strip comes home: everything below
				// is knobs, mode buttons and the ribbon
				if (nX < kCorner && nY < 40) {
					mMode = ModeHome;
				}
				continue;
			}

			// home: the button bar along the top
			(void) nHeight;
			if (nY >= kBarHeight) {
				continue;
			}
			unsigned nButton = nX / (nWidth / kButtons);
			if (nButton >= kButtons) nButton = kButtons - 1;
			onButton(nButton);
		}
	}

	void onButton(unsigned nButton) {
		switch (nButton) {
		case 0: mControl.globalMute = !mControl.globalMute; bumpChanges(); break;
		case 1: mControl.bassMute = !mControl.bassMute; bumpChanges(); break;
		case 2: mMode = ModeImage; break;
		case 3: mMode = ModeMonotron; break;
		case 4: mControl.huckTrigger++; bumpChanges(); break; // fire/restart
		case 5: mMode = ModeTuner; break;
		}
	}

	void bumpChanges() { mControl.changes++; }

	// ---- home page -------------------------------------------------------

	void drawHome(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();
		unsigned nScopeTop = kBarHeight; // the bar owns the top strip
		unsigned nScopeH = nHeight - kBarHeight;

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));

		// oscilloscope of the cast ring: one second across the width,
		// min/max envelope per column (as examples/oscilloscope)
		unsigned nMidY = nScopeTop + nScopeH / 2;
		float fGain = (float) nScopeH * 0.5f;
		pGraphics->DrawLine(0, nMidY, nWidth - 1, nMidY, COLOR2D(48, 48, 48));

		unsigned nPerColumn = SharedState::bufferSize / nWidth;
		if (nPerColumn == 0) nPerColumn = 1;
		for (unsigned nX = 0; nX < nWidth; nX++) {
			float fMin = 1.0f, fMax = -1.0f;
			// newest at the right edge
			int nBase = (int) ((nWidth - 1 - nX) * nPerColumn);
			for (unsigned i = 0; i < nPerColumn; i++) {
				float fSample = mScope.readSample(nBase + (int) i + 1);
				if (fSample < fMin) fMin = fSample;
				if (fSample > fMax) fMax = fSample;
			}
			int nYTop = (int) nMidY - (int) (fMax * fGain);
			int nYBottom = (int) nMidY - (int) (fMin * fGain);
			if (nYTop < (int) nScopeTop) nYTop = (int) nScopeTop;
			if (nYBottom >= (int) nHeight) nYBottom = (int) nHeight - 1;
			if (nYTop > nYBottom) nYTop = nYBottom;
			pGraphics->DrawLine(nX, (unsigned) nYTop, nX, (unsigned) nYBottom,
					    COLOR2D(255, 255, 255));
		}

		drawButtonBar(pGraphics, nWidth);
		pGraphics->UpdateDisplay();
	}

	// ---- the six buttons -------------------------------------------------

	void drawButtonBar(C2DGraphics* pGraphics, unsigned nWidth) {
		unsigned nTop = 0;
		unsigned nTileW = nWidth / kButtons;

		pGraphics->DrawRect(0, nTop, nWidth, kBarHeight, COLOR2D(16, 16, 20));

		for (unsigned b = 0; b < kButtons; b++) {
			unsigned nX0 = b * nTileW;
			// tile frame; toggles glow when engaged
			boolean bLit = (b == 0 && mControl.globalMute)
				    || (b == 1 && mControl.bassMute);
			pGraphics->DrawRectOutline(nX0 + 6, nTop + 6, nTileW - 12,
						   kBarHeight - 12,
						   bLit ? COLOR2D(255, 80, 80)
							: COLOR2D(70, 70, 80));

			unsigned nCX = nX0 + nTileW / 2;
			unsigned nCY = nTop + kBarHeight / 2;
			switch (b) {
			case 0: drawSpeakerIcon(pGraphics, nCX, nCY, mControl.globalMute); break;
			case 1: drawBassIcon(pGraphics, nCX, nCY, mControl.bassMute); break;
			case 2: drawPhotoIcon(pGraphics, nCX, nCY); break;
			case 3: drawMonotronIcon(pGraphics, nCX, nCY); break;
			case 4: drawPlayIcon(pGraphics, nCX, nCY); break;
			case 5: drawForkIcon(pGraphics, nCX, nCY); break;
			}
		}
	}

	// icons are built from primitives around a center point; ~72 px tall
	static void drawSlash(C2DGraphics* g, unsigned cx, unsigned cy) {
		for (int t = -1; t <= 1; t++) {
			g->DrawLine(cx - 32, cy + 32 + t, cx + 32, cy - 32 + t,
				    COLOR2D(255, 64, 64));
		}
	}

	static void drawSpeakerIcon(C2DGraphics* g, unsigned cx, unsigned cy, int bMuted) {
		T2DColor C = COLOR2D(220, 220, 230);
		g->DrawRect(cx - 26, cy - 10, 12, 20, C);              // driver box
		for (int i = 0; i < 20; i++) {                         // cone
			g->DrawLine(cx - 14, cy - 10 + i, cx + 4 + i / 2, cy - 22 + i * 2, C);
		}
		if (!bMuted) {                                         // sound waves
			g->DrawCircleOutline(cx + 8, cy, 14, C);
			g->DrawCircleOutline(cx + 8, cy, 22, C);
		} else {
			drawSlash(g, cx, cy);
		}
	}

	static void drawBassIcon(C2DGraphics* g, unsigned cx, unsigned cy, int bMuted) {
		T2DColor C = COLOR2D(230, 180, 90);
		g->DrawCircle(cx - 8, cy + 14, 16, C);                 // body
		g->DrawCircle(cx - 2, cy + 4, 12, C);                  // upper bout
		g->DrawCircle(cx - 6, cy + 12, 5, COLOR2D(20, 20, 24)); // sound hole
		for (int t = 0; t < 4; t++) {                          // neck
			g->DrawLine(cx + 2 + t, cy + 2, cx + 24 + t, cy - 28, C);
		}
		g->DrawRect(cx + 24, cy - 34, 10, 8, C);               // headstock
		if (bMuted) drawSlash(g, cx, cy);
	}

	static void drawPhotoIcon(C2DGraphics* g, unsigned cx, unsigned cy) {
		T2DColor C = COLOR2D(140, 200, 140);
		g->DrawRectOutline(cx - 30, cy - 22, 60, 44, C);       // frame
		g->DrawCircle(cx + 14, cy - 8, 6, COLOR2D(255, 220, 120)); // sun
		g->DrawLine(cx - 26, cy + 18, cx - 6, cy - 6, C);      // mountains
		g->DrawLine(cx - 6, cy - 6, cx + 6, cy + 8, C);
		g->DrawLine(cx + 6, cy + 8, cx + 16, cy - 2, C);
		g->DrawLine(cx + 16, cy - 2, cx + 26, cy + 18, C);
	}

	static void drawMonotronIcon(C2DGraphics* g, unsigned cx, unsigned cy) {
		T2DColor C = COLOR2D(96, 200, 255);
		g->DrawRectOutline(cx - 32, cy - 20, 64, 40, C);       // panel
		g->DrawCircleOutline(cx - 18, cy - 8, 6, C);           // knobs
		g->DrawCircleOutline(cx, cy - 8, 6, C);
		g->DrawCircleOutline(cx + 18, cy - 8, 6, C);
		g->DrawRect(cx - 28, cy + 8, 56, 6, C);                // ribbon
	}

	static void drawPlayIcon(C2DGraphics* g, unsigned cx, unsigned cy) {
		T2DColor C = COLOR2D(120, 230, 120);            // play triangle
		for (int i = 0; i < 28; i++) {
			int h = 28 - i;
			g->DrawLine(cx - 14 + i, cy - h, cx - 14 + i, cy + h, C);
		}
		// a little note flag to say "song"
		g->DrawLine(cx + 20, cy - 24, cx + 20, cy + 2, COLOR2D(220, 220, 230));
		g->DrawCircle(cx + 16, cy + 4, 5, COLOR2D(220, 220, 230));
	}

	static void drawForkIcon(C2DGraphics* g, unsigned cx, unsigned cy) {
		T2DColor C = COLOR2D(200, 200, 220);
		for (int t = 0; t < 3; t++) {                          // prongs
			g->DrawLine(cx - 10 + t, cy - 30, cx - 10 + t, cy - 2, C);
			g->DrawLine(cx + 10 + t, cy - 30, cx + 10 + t, cy - 2, C);
		}
		g->DrawCircleOutline(cx + 1, cy - 2, 10, C);           // the U join
		g->DrawRect(cx - 25, cy - 4, 14, 8, COLOR2D(16, 16, 20)); // trim the U
		g->DrawRect(cx + 13, cy - 4, 14, 8, COLOR2D(16, 16, 20));
		g->DrawRect(cx - 25, cy - 14, 52, 8, COLOR2D(16, 16, 20));
		for (int t = 0; t < 3; t++) {                          // stem
			g->DrawLine(cx + t, cy + 6, cx + t, cy + 30, C);
		}
		g->DrawCircle(cx + 1, cy + 32, 4, C);                  // base
	}

	// ---- image mode --------------------------------------------------------

	void drawImage(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));
		unsigned nX = nWidth > YesWidth ? (nWidth - YesWidth) / 2 : 0;
		unsigned nY = nHeight > YesHeight ? (nHeight - YesHeight) / 2 : 0;
		pGraphics->DrawImage(nX, nY, YesWidth, YesHeight, YesPixels);
		pGraphics->DrawText(8, 8, COLOR2D(120, 120, 130), "tap anywhere: home");
		pGraphics->UpdateDisplay();
	}
};

int main() {
	// static: the scope ring alone is 192 KB, far over the 128 KB kernel stack
	static GraphicsBox mGraphicsBox;
	mGraphicsBox.start(true); // full clock: YIN + full-screen redraws
}
