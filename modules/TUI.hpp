#pragma once

#include <InstOS.hpp>
#include <modules/HDMIDisplay.hpp>
#include <circle/chargenerator.h>
#include <circle/font.h>
#include <circle/util.h>
#include <circle/string.h>
#include <stdarg.h>

namespace InstOS {

// TUI: a character-cell display rasterized onto an HDMIDisplay at an integer
// scale — the terminal aesthetic with pixel-level control. Fixed geometry:
// scale (and thus rows/cols) is chosen at construction; rebuild to resize.
//
// Two ways to draw, freely mixed:
//   1. Direct calls: at()/fg()/bg()/print()/printf()
//   2. write(): a small ANSI escape subset (cursor addressing, SGR with
//      256-color, clear) — terminal-style renderers (e.g. TUITUNE's) port
//      byte-for-byte, and the same bytes can go to a real terminal over
//      serial instead.
//
// Glyphs: ASCII via Circle's Font8x16, plus a supplement of hand-drawn
// box-drawing/geometric glyphs (UTF-8 in, see kAltGlyphs). Colors: the
// standard xterm-256 palette. present() rasterizes the full grid and flips;
// C2DGraphics::UpdateDisplay() is vsync-paced, so present() throttles the
// caller to the display rate (like the oscilloscope example's redraw).
//
// Not audio-path code: construct and draw from init()/loop() on the main
// loop only.
class TUI {
public:
	static constexpr unsigned kGlyphW = 8, kGlyphH = 16;

	// display must have graphics() ready (i.e. after Firmware::init());
	// nScale is the integer pixel multiplier per glyph pixel
	TUI(HDMIDisplay& display, unsigned nScale = 2)
	: mpGraphics(display.graphics()), mScale(nScale < 1 ? 1 : nScale),
	  mCharGen(Font8x16, CCharGenerator::FontFlagsNone) {
		if (mpGraphics == nullptr) {
			mCols = mRows = 0;
			mpCells = nullptr;
			return;
		}
		mCellW = kGlyphW * mScale;
		mCellH = kGlyphH * mScale;
		mCols = mpGraphics->GetWidth() / mCellW;
		mRows = mpGraphics->GetHeight() / mCellH;
		// heap is fine here: one-time, main-loop-side allocation
		mpCells = new Cell[mRows * mCols];
		buildPalette();
		clear();

		// clear BOTH framebuffers once: cells repaint their own area every
		// present(), but the remainder strips outside the grid (height and
		// width rarely divide evenly by the cell size) would otherwise show
		// uninitialized framebuffer memory forever
		mpGraphics->ClearScreen(mPalette[kDefaultBg]);
		mpGraphics->UpdateDisplay(); // flip, then clear the other buffer
		mpGraphics->ClearScreen(mPalette[kDefaultBg]);
	}

	~TUI() { delete[] mpCells; }

	boolean ready() const { return mpCells != nullptr; }
	unsigned rows() const { return mRows; }
	unsigned cols() const { return mCols; }

	// ---- direct drawing -------------------------------------------------

	void clear() {
		if (!ready()) return;
		for (unsigned i = 0; i < mRows * mCols; i++) {
			mpCells[i] = Cell{' ', mFg, mBg, 0};
		}
		mCursorRow = mCursorCol = 0;
	}

	void at(unsigned nRow, unsigned nCol) { // 0-based
		mCursorRow = nRow;
		mCursorCol = nCol;
	}

	void fg(u8 nColor256) { mFg = nColor256; }
	void bg(u8 nColor256) { mBg = nColor256; }
	void bold(boolean bOn) { mBold = bOn; }
	void resetAttrs() { mFg = kDefaultFg; mBg = kDefaultBg; mBold = FALSE; }

	// UTF-8; ASCII plus the supplement glyphs render, anything else shows '?'
	void print(const char* pText) {
		while (*pText != '\0') {
			unsigned nCode;
			pText = decodeUTF8(pText, nCode);
			putGlyph(nCode);
		}
	}

	// CString::Format subset of printf(3) (no float %f wider than Circle's)
	void printf(const char* pFormat, ...) {
		CString Text;
		va_list args;
		va_start(args, pFormat);
		Text.FormatV(pFormat, args);
		va_end(args);
		print(Text);
	}

	// ---- ANSI escape subset ---------------------------------------------
	// Supports: \E[H  \E[<r>;<c>H  \E[J \E[2J \E[K  \E[m with 0,1,7,27,
	// 30-37,90-97,40-47,100-107,38;5;N,48;5;N  \E[?25h/l (ignored).
	// Rows/cols in escapes are 1-based, as in a real terminal.

	void write(const char* pBytes) { write(pBytes, strlen(pBytes)); }

	void write(const char* pBytes, unsigned nCount) {
		for (unsigned i = 0; i < nCount; ) {
			if (pBytes[i] == '\x1b' && i + 1 < nCount && pBytes[i + 1] == '[') {
				i += 2;
				// parse parameter list
				unsigned params[8] = {};
				unsigned nParams = 0;
				boolean bPrivate = FALSE;
				if (i < nCount && pBytes[i] == '?') { bPrivate = TRUE; i++; }
				unsigned nCur = 0; boolean bAny = FALSE;
				while (i < nCount) {
					char c = pBytes[i];
					if (c >= '0' && c <= '9') {
						nCur = nCur * 10 + (unsigned) (c - '0');
						bAny = TRUE; i++;
					} else if (c == ';') {
						if (nParams < 8) params[nParams++] = nCur;
						nCur = 0; bAny = FALSE; i++;
					} else {
						break;
					}
				}
				if (bAny && nParams < 8) params[nParams++] = nCur;
				if (i >= nCount) break;
				char chFinal = pBytes[i++];
				if (!bPrivate) {
					escape(chFinal, params, nParams);
				} // ?25h/l cursor visibility: no cursor drawn, ignore
			} else {
				unsigned nCode;
				const char* pNext = decodeUTF8(pBytes + i, nCode);
				i += (unsigned) (pNext - (pBytes + i));
				if (nCode == '\r') {
					mCursorCol = 0;
				} else if (nCode == '\n') {
					mCursorCol = 0;
					if (mCursorRow + 1 < mRows) mCursorRow++;
				} else {
					putGlyph(nCode);
				}
			}
		}
	}

	// ---- rasterize ------------------------------------------------------

	// full-grid redraw + flip; UpdateDisplay() paces this to vsync
	void present() {
		if (!ready()) return;
		for (unsigned nRow = 0; nRow < mRows; nRow++) {
			for (unsigned nCol = 0; nCol < mCols; nCol++) {
				drawCell(nRow, nCol, mpCells[nRow * mCols + nCol]);
			}
		}
		mpGraphics->UpdateDisplay();
	}

private:
	struct Cell {
		unsigned glyph; // Unicode codepoint
		u8 fg, bg;      // xterm-256 indices
		u8 attrs;       // bit 0: bold
	};

	static constexpr u8 kDefaultFg = 7; // light gray on black, like a terminal
	static constexpr u8 kDefaultBg = 0;

	void escape(char chFinal, const unsigned* params, unsigned nParams) {
		switch (chFinal) {
		case 'H': // cursor home / position (1-based)
			mCursorRow = nParams >= 1 && params[0] > 0 ? params[0] - 1 : 0;
			mCursorCol = nParams >= 2 && params[1] > 0 ? params[1] - 1 : 0;
			break;
		case 'J': // clear screen (2J and plain J from home both land here)
			for (unsigned i = 0; i < mRows * mCols; i++) {
				mpCells[i] = Cell{' ', mFg, mBg, 0};
			}
			break;
		case 'K': { // clear to end of line
			if (mCursorRow >= mRows) break;
			for (unsigned c = mCursorCol; c < mCols; c++) {
				mpCells[mCursorRow * mCols + c] = Cell{' ', mFg, mBg, 0};
			}
			break;
		}
		case 'm': // SGR
			if (nParams == 0) { resetAttrs(); break; }
			for (unsigned i = 0; i < nParams; i++) {
				unsigned p = params[i];
				if (p == 0) resetAttrs();
				else if (p == 1) mBold = TRUE;
				else if (p == 7) { u8 t = mFg; mFg = mBg; mBg = t; }
				else if (p == 27) { /* un-reverse: not tracked, ignore */ }
				else if (p >= 30 && p <= 37) mFg = (u8) (p - 30);
				else if (p >= 90 && p <= 97) mFg = (u8) (p - 90 + 8);
				else if (p >= 40 && p <= 47) mBg = (u8) (p - 40);
				else if (p >= 100 && p <= 107) mBg = (u8) (p - 100 + 8);
				else if (p == 38 && i + 2 < nParams && params[i + 1] == 5) {
					mFg = (u8) params[i + 2]; i += 2;
				}
				else if (p == 48 && i + 2 < nParams && params[i + 1] == 5) {
					mBg = (u8) params[i + 2]; i += 2;
				}
			}
			break;
		default: // unsupported final byte: ignore
			break;
		}
	}

	void putGlyph(unsigned nCode) {
		if (mCursorRow >= mRows || mCursorCol >= mCols) return;
		mpCells[mCursorRow * mCols + mCursorCol] =
			Cell{nCode, mFg, mBg, (u8) (mBold ? 1 : 0)};
		mCursorCol++; // no wrap: terminal-style clipping at the edge
	}

	// ---- glyph raster ----

	// 8x16 bitmap rows (bit 7 = leftmost pixel) for non-ASCII glyphs
	struct AltGlyph { unsigned code; u8 rows[kGlyphH]; };

	static const AltGlyph* findAlt(unsigned nCode) {
		static const AltGlyph kAltGlyphs[] = {
			{0x2500, {0,0,0,0,0,0,0,0xFF,0xFF,0,0,0,0,0,0,0}},                 // ─
			{0x2502, {24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24}},       // │
			{0x250C, {0,0,0,0,0,0,0,0x1F,0x1F,24,24,24,24,24,24,24}},          // ┌
			{0x2510, {0,0,0,0,0,0,0,0xF8,0xF8,24,24,24,24,24,24,24}},          // ┐
			{0x2514, {24,24,24,24,24,24,24,0x1F,0x1F,0,0,0,0,0,0,0}},          // └
			{0x2518, {24,24,24,24,24,24,24,0xF8,0xF8,0,0,0,0,0,0,0}},          // ┘
			{0x251C, {24,24,24,24,24,24,24,0x1F,0x1F,24,24,24,24,24,24,24}},   // ├
			{0x2524, {24,24,24,24,24,24,24,0xF8,0xF8,24,24,24,24,24,24,24}},   // ┤
			{0x252C, {0,0,0,0,0,0,0,0xFF,0xFF,24,24,24,24,24,24,24}},          // ┬
			{0x2534, {24,24,24,24,24,24,24,0xFF,0xFF,0,0,0,0,0,0,0}},          // ┴
			{0x253C, {24,24,24,24,24,24,24,0xFF,0xFF,24,24,24,24,24,24,24}},   // ┼
			{0x2588, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
				  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},               // █
			{0x2580, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0}}, // ▀
			{0x2584, {0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}}, // ▄
			{0x2591, {0x88,0,0x22,0,0x88,0,0x22,0,0x88,0,0x22,0,0x88,0,0x22,0}}, // ░
			{0x2592, {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
				  0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}},               // ▒
			{0x25C6, {0,0,0x18,0x3C,0x7E,0xFF,0xFF,0xFF,
				  0xFF,0x7E,0x3C,0x18,0,0,0,0}},                           // ◆
			{0x25CF, {0,0,0,0x3C,0x7E,0x7E,0x7E,0x7E,0x7E,0x3C,0,0,0,0,0,0}},  // ●
		};
		for (unsigned i = 0; i < sizeof kAltGlyphs / sizeof kAltGlyphs[0]; i++) {
			if (kAltGlyphs[i].code == nCode) return &kAltGlyphs[i];
		}
		return nullptr;
	}

	void drawCell(unsigned nRow, unsigned nCol, const Cell& cell) {
		unsigned nX0 = nCol * mCellW;
		unsigned nY0 = nRow * mCellH;
		T2DColor Bg = mPalette[cell.bg];
		T2DColor Fg = mPalette[cell.fg];

		mpGraphics->DrawRect(nX0, nY0, mCellW, mCellH, Bg);

		unsigned nCode = cell.glyph;
		if (nCode == ' ') return;

		const AltGlyph* pAlt = nCode > 126 ? findAlt(nCode) : nullptr;
		if (nCode > 126 && pAlt == nullptr) nCode = '?';

		boolean bBold = (cell.attrs & 1) != 0; // double-strike: repeat at x+1
		for (unsigned gy = 0; gy < kGlyphH; gy++) {
			unsigned nBits = 0;
			if (pAlt != nullptr) {
				for (unsigned gx = 0; gx < kGlyphW; gx++) {
					if (pAlt->rows[gy] & (0x80 >> gx)) nBits |= 1u << gx;
				}
			} else {
				CCharGenerator::TPixelLine Line =
					mCharGen.GetPixelLine((char) nCode, gy);
				for (unsigned gx = 0; gx < kGlyphW; gx++) {
					if (mCharGen.GetPixel(gx, Line)) nBits |= 1u << gx;
				}
			}
			if (bBold) nBits |= nBits << 1;
			if (nBits == 0) continue;
			for (unsigned gx = 0; gx < kGlyphW; gx++) {
				if (!(nBits & (1u << gx))) continue;
				mpGraphics->DrawRect(nX0 + gx * mScale, nY0 + gy * mScale,
						     mScale, mScale, Fg);
			}
		}
	}

	// ---- xterm-256 palette ----

	void buildPalette() {
		// 0-15: standard + bright ANSI colors
		static const u8 kBase[16][3] = {
			{0,0,0},   {205,0,0},   {0,205,0},   {205,205,0},
			{0,0,238}, {205,0,205}, {0,205,205}, {229,229,229},
			{127,127,127}, {255,0,0},   {0,255,0},   {255,255,0},
			{92,92,255},   {255,0,255}, {0,255,255}, {255,255,255},
		};
		for (unsigned i = 0; i < 16; i++) {
			mPalette[i] = COLOR2D(kBase[i][0], kBase[i][1], kBase[i][2]);
		}
		// 16-231: 6x6x6 color cube
		static const u8 kCube[6] = {0, 95, 135, 175, 215, 255};
		for (unsigned r = 0; r < 6; r++)
			for (unsigned g = 0; g < 6; g++)
				for (unsigned b = 0; b < 6; b++)
					mPalette[16 + 36*r + 6*g + b] =
						COLOR2D(kCube[r], kCube[g], kCube[b]);
		// 232-255: grayscale ramp
		for (unsigned i = 0; i < 24; i++) {
			u8 v = (u8) (8 + i * 10);
			mPalette[232 + i] = COLOR2D(v, v, v);
		}
	}

	// ---- UTF-8 ----

	// decodes one codepoint, returns pointer past it; invalid bytes -> '?'
	static const char* decodeUTF8(const char* p, unsigned& nCode) {
		u8 c = (u8) p[0];
		if (c < 0x80) { nCode = c; return p + 1; }
		unsigned nLen, nMin;
		if ((c & 0xE0) == 0xC0) { nCode = c & 0x1F; nLen = 1; nMin = 0x80; }
		else if ((c & 0xF0) == 0xE0) { nCode = c & 0x0F; nLen = 2; nMin = 0x800; }
		else if ((c & 0xF8) == 0xF0) { nCode = c & 0x07; nLen = 3; nMin = 0x10000; }
		else { nCode = '?'; return p + 1; }
		for (unsigned i = 1; i <= nLen; i++) {
			if (((u8) p[i] & 0xC0) != 0x80) { nCode = '?'; return p + 1; }
			nCode = (nCode << 6) | ((u8) p[i] & 0x3F);
		}
		if (nCode < nMin) nCode = '?';
		return p + 1 + nLen;
	}

	C2DGraphics* mpGraphics;
	unsigned mScale, mCellW = 0, mCellH = 0;
	unsigned mRows, mCols;
	Cell* mpCells;
	CCharGenerator mCharGen;
	T2DColor mPalette[256];

	// cursor + attributes (shared by direct calls and write())
	unsigned mCursorRow = 0, mCursorCol = 0;
	u8 mFg = kDefaultFg, mBg = kDefaultBg;
	boolean mBold = FALSE;
};

} // namespace InstOS
