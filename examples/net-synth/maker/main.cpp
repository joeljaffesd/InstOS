#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/Touch.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include "../state.hpp"

// Net-synth two, sending half: a one-octave piano keyboard (A1..A2) on the
// glass instead of net-synth's continuous ribbon, and polyphonic rather than
// monophonic. Every contact is hit-tested to a key and folded into a single
// bitmask, so two fingers on the same key collapse to one held note for
// free, and the state size does not grow with the number of fingers.
// Direct cable, static IPs: maker (screen) .1 -> taker (audio) .2.
class KeyboardMaker : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::USBHost mUSB;
	InstOS::HDMIDisplay mDisplay;
	InstOS::Touch mTouch{mDisplay};
	InstOS::Ethernet mNet{"192.168.10.1"};
	InstOS::Barebone::Maker<SynthState> mMaker{"192.168.10.2",
						   InstOS::Barebone::kDefaultPort, 120};

	SynthState mState = {};

	// top strip reserved for the readout, so the keys own the rest
	static constexpr unsigned kStatusHeight = 48;

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mMaker.attach(&mState);
		return TRUE;
	}

	void loop() override {
		pollKeyboard();

		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics != nullptr && mTouch.isDirty()) {
			mTouch.clearDirty();
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

	void pollKeyboard() {
		C2DGraphics* pGraphics = mDisplay.graphics();
		if (pGraphics == nullptr) {
			return;
		}

		const InstOS::Touch::Finger* pFingers = mTouch.fingers();
		unsigned nKeys = 0;

		for (unsigned i = 0; i < InstOS::Touch::kMaxFingers; i++) {
			if (!pFingers[i].down) {
				continue;
			}

			int nKey = keyAt(pFingers[i].x, pFingers[i].y,
					 pGraphics->GetWidth(), pGraphics->GetHeight());
			if (nKey >= 0) {
				nKeys |= 1u << nKey; // duplicates collapse
			}
		}

		if (nKeys != mState.keys) {
			mState.keys = nKeys;
			mState.changes++;
		}
	}

	// Hit test in screen pixels. Black keys overlap the white ones and are
	// drawn on top, so they must be tested first or they are unreachable.
	static int keyAt(unsigned nX, unsigned nY, unsigned nWidth, unsigned nHeight) {
		if (nY < kStatusHeight || nHeight <= kStatusHeight) {
			return -1; // readout strip is not playable
		}

		unsigned nBoardTop = kStatusHeight;
		unsigned nBoardHeight = nHeight - kStatusHeight;
		unsigned nWhiteWidth = nWidth / kWhiteCount;
		if (nWhiteWidth == 0) {
			return -1;
		}

		unsigned nBlackWidth = (nWhiteWidth * 3) / 5;
		unsigned nBlackHeight = (nBoardHeight * 3) / 5;

		if (nY - nBoardTop < nBlackHeight) {
			for (unsigned i = 0; i < kBlackCount; i++) {
				unsigned nCentre = (kBlackAfterWhite[i] + 1) * nWhiteWidth;
				unsigned nLeft = nCentre - nBlackWidth / 2;
				if (nX >= nLeft && nX < nLeft + nBlackWidth) {
					return kBlackSemitone[i];
				}
			}
		}

		unsigned nWhite = nX / nWhiteWidth;
		if (nWhite >= (unsigned) kWhiteCount) {
			nWhite = kWhiteCount - 1; // rightmost column rounding
		}
		return kWhiteSemitone[nWhite];
	}

	void draw(C2DGraphics* pGraphics) {
		unsigned nWidth = pGraphics->GetWidth();
		unsigned nHeight = pGraphics->GetHeight();
		if (nHeight <= kStatusHeight) {
			return;
		}

		unsigned nBoardTop = kStatusHeight;
		unsigned nBoardHeight = nHeight - kStatusHeight;
		unsigned nWhiteWidth = nWidth / kWhiteCount;
		unsigned nBlackWidth = (nWhiteWidth * 3) / 5;
		unsigned nBlackHeight = (nBoardHeight * 3) / 5;

		pGraphics->ClearScreen(COLOR2D(0, 0, 0));

		static const char* WhiteNames[kWhiteCount] = {"A", "B", "C", "D",
							      "E", "F", "G", "A"};

		for (unsigned i = 0; i < (unsigned) kWhiteCount; i++) {
			boolean bHeld = !!(mState.keys & (1u << kWhiteSemitone[i]));
			pGraphics->DrawRect(i * nWhiteWidth, nBoardTop, nWhiteWidth - 2,
					    nBoardHeight,
					    bHeld ? COLOR2D(96, 200, 255)
						  : COLOR2D(232, 232, 232));
			pGraphics->DrawText(i * nWhiteWidth + nWhiteWidth / 2 - 4,
					    nBoardTop + nBoardHeight - 24,
					    COLOR2D(40, 40, 40), WhiteNames[i]);
		}

		for (unsigned i = 0; i < (unsigned) kBlackCount; i++) {
			boolean bHeld = !!(mState.keys & (1u << kBlackSemitone[i]));
			unsigned nCentre = (kBlackAfterWhite[i] + 1) * nWhiteWidth;
			pGraphics->DrawRect(nCentre - nBlackWidth / 2, nBoardTop,
					    nBlackWidth, nBlackHeight,
					    bHeld ? COLOR2D(64, 150, 200)
						  : COLOR2D(24, 24, 28));
		}

		CString Status;
		Status.Format("%u held    %u updates", countKeys(mState.keys),
			      mState.changes);
		pGraphics->DrawText(8, 12, COLOR2D(200, 200, 200), (const char*) Status);
	}

	static unsigned countKeys(unsigned nKeys) {
		unsigned nCount = 0;
		for (unsigned i = 0; i < (unsigned) kKeyCount; i++) {
			if (nKeys & (1u << i)) {
				nCount++;
			}
		}
		return nCount;
	}
};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static KeyboardMaker mKeyboardMaker;
	mKeyboardMaker.start();
}
