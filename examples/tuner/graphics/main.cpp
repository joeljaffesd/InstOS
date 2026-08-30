#include <InstOS.hpp>
#include <modules/Ethernet.hpp>
#include <modules/Barebone.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/TUI.hpp>
#include "../state.hpp"

#include <math.h>

// Tuner, showing half: this box has the screen. Pitch estimates arrive over
// the Ethernet cable from the audio/ box; this side does the note mapping,
// smoothing and hold, and renders the TUITUNE screen — big note readout,
// cents meter, A4 readout — through the TUI engine's ANSI path, using the
// same escape codes the terminal original writes.
// Direct cable, static IPs: audio/ (maker) .1 -> graphics/ (taker) .2.
//
// Keys (over the serial monitor): '+'/'-' adjust A4 (415-466 Hz). A4 lives
// here because it only affects note mapping, not detection.

// ----------------------------------------------------------------- music ----

static const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
				     "F#", "G",  "G#", "A",  "A#", "B"};

struct NoteInfo {
	char name[4];   // e.g. "A4"
	float freq;     // target frequency of nearest note
	float cents;    // detected offset from it, -50..+50
};

static NoteInfo nearestNote(float freq, float a4) {
	float semis = 12.0f * log2f(freq / a4);
	int n = (int) lroundf(semis);
	float target = a4 * powf(2.0f, (float) n / 12.0f);
	int midi = n + 69;
	NoteInfo info;
	const char* pName = kNoteNames[((midi % 12) + 12) % 12];
	int octave = midi / 12 - 1;
	CString Name;
	Name.Format("%s%d", pName, octave);
	strncpy(info.name, Name, sizeof info.name - 1);
	info.name[sizeof info.name - 1] = '\0';
	info.freq = target;
	info.cents = 1200.0f * log2f(freq / target);
	return info;
}

// -------------------------------------------------------------- rendering ----

// TUITUNE's Frame, retargeted: build one escape string, hand it to the TUI
struct Frame {
	char buf[8192];
	unsigned len = 0;
	void clear() { len = 0; add("\x1b[H\x1b[2J"); }
	void at(unsigned row, unsigned col) { // 1-based, as in the original
		addf("\x1b[%u;%uH", row, col);
	}
	void add(const char* s) {
		while (*s != '\0' && len < sizeof buf - 1) buf[len++] = *s++;
		buf[len] = '\0';
	}
	void addf(const char* fmt, ...) {
		CString Text;
		va_list args;
		va_start(args, fmt);
		Text.FormatV(fmt, args);
		va_end(args);
		add(Text);
	}
	void flush(InstOS::TUI& tui) { tui.write(buf, len); }
};

static const char* centsColor(float cents) {
	float a = cents < 0 ? -cents : cents;
	if (a < 3.0f)  return "\x1b[38;5;46m";   // green
	if (a < 10.0f) return "\x1b[38;5;226m";  // yellow
	return "\x1b[38;5;196m";                 // red
}

static void drawCentsMeter(Frame& f, unsigned row, unsigned col, unsigned width,
			   float cents, bool active) {
	int center = (int) width / 2;
	float c = cents < -50.0f ? -50.0f : (cents > 50.0f ? 50.0f : cents);
	int pos = center + (int) lroundf(c / 50.0f * (float) center);
	if (pos < 0) pos = 0;
	if (pos > (int) width - 1) pos = (int) width - 1;
	f.at(row, col);
	f.add("\x1b[38;5;240m");
	for (int i = 0; i < (int) width; i++) {
		if (active && i == pos) {
			f.add(centsColor(cents));
			f.add("\xE2\x97\x86"); // ◆
			f.add("\x1b[0m\x1b[38;5;240m");
		} else if (i == center) {
			f.add("\xE2\x94\xBC"); // ┼
		} else {
			f.add("\xE2\x94\x80"); // ─
		}
	}
	f.add("\x1b[0m");
	// scale labels
	f.at(row + 1, col);
	f.add("\x1b[38;5;244m-50\x1b[0m");
	f.at(row + 1, col + (unsigned) center - 1);
	f.add("\x1b[38;5;244m 0\x1b[0m");
	f.at(row + 1, col + width - 3);
	f.add("\x1b[38;5;244m+50\x1b[0m");
}

// TUITUNE's 5-row banner font (A-G, # and digits), '@' meaning a full block
// (U+2588), expanded at draw time
static const char* const* glyph(char c) {
	static const char* const font[20][5] = {
		{" @@  ", "@  @ ", "@@@@ ", "@  @ ", "@  @ "},  // A
		{"@@@  ", "@  @ ", "@@@  ", "@  @ ", "@@@  "},  // B
		{" @@@ ", "@    ", "@    ", "@    ", " @@@ "},  // C
		{"@@@  ", "@  @ ", "@  @ ", "@  @ ", "@@@  "},  // D
		{"@@@@ ", "@    ", "@@@  ", "@    ", "@@@@ "},  // E
		{"@@@@ ", "@    ", "@@@  ", "@    ", "@    "},  // F
		{" @@@ ", "@    ", "@ @@ ", "@  @ ", " @@@ "},  // G
		{" @ @ ", "@@@@@", " @ @ ", "@@@@@", " @ @ "},  // #
		{" @@  ", "@  @ ", "@  @ ", "@  @ ", " @@  "},  // 0
		{" @   ", "@@   ", " @   ", " @   ", "@@@  "},  // 1
		{"@@@  ", "   @ ", " @@  ", "@    ", "@@@@ "},  // 2
		{"@@@  ", "   @ ", " @@  ", "   @ ", "@@@  "},  // 3
		{"@  @ ", "@  @ ", "@@@@ ", "   @ ", "   @ "},  // 4
		{"@@@@ ", "@    ", "@@@  ", "   @ ", "@@@  "},  // 5
		{" @@  ", "@    ", "@@@  ", "@  @ ", " @@  "},  // 6
		{"@@@@ ", "   @ ", "  @  ", " @   ", " @   "},  // 7
		{" @@  ", "@  @ ", " @@  ", "@  @ ", " @@  "},  // 8
		{" @@  ", "@  @ ", " @@@ ", "   @ ", " @@  "},  // 9
		{"     ", "     ", "     ", "     ", "     "},  // space
		{"     ", "     ", "     ", "     ", "     "},  // (spare)
	};
	if (c >= 'A' && c <= 'G') return font[c - 'A'];
	if (c == '#') return font[7];
	if (c >= '0' && c <= '9') return font[8 + (c - '0')];
	return font[19];
}

static void drawBigNote(Frame& f, unsigned row, unsigned col, const char* note,
			const char* color) {
	f.add(color);
	for (unsigned line = 0; line < 5; line++) {
		f.at(row + line, col);
		for (const char* p = note; *p != '\0'; p++) {
			for (const char* q = glyph(*p)[line]; *q != '\0'; q++) {
				f.add(*q == '@' ? "\xE2\x96\x88" : " "); // block or space
			}
		}
	}
	f.add("\x1b[0m");
}

// ------------------------------------------------------------------ app ----

class TunerDisplay : public InstOS::Firmware {
	// modules: declaration order = init order
	InstOS::Ethernet mNet{"192.168.10.2"};
	InstOS::Barebone::Taker<TunerState> mTaker;
	InstOS::HDMIDisplay mDisplay;

	InstOS::TUI* mpTUI = nullptr; // needs graphics(); built on first loop()

	TunerState mState = {};

	// tuner display state (TUITUNE's, same constants)
	float mA4 = 440.0f;
	float mFreq = 0.0f;       // latest cast estimate (0 = none)
	float mSmoothedFreq = 0.0f;
	float mDisplayCents = 0.0f;
	unsigned mHoldFrames = 0; // frames since last confident pitch

	void loop() override {
		if (mDisplay.graphics() == nullptr) {
			return;
		}
		if (mpTUI == nullptr) {
			mpTUI = new InstOS::TUI(mDisplay, 2); // fixed scale; rebuild to resize
			m_Logger.Write("tuner", LogNotice, "%u x %u cells",
				       mpTUI->cols(), mpTUI->rows());
		}

		// ---- input: A4 reference over the serial monitor ----
		char keys[8];
		int nk = m_Serial.Read(keys, sizeof keys);
		for (int i = 0; i < nk; i++) {
			if (keys[i] == '+' || keys[i] == '=') {
				mA4 = mA4 < 466.0f ? mA4 + 1.0f : 466.0f;
			}
			if (keys[i] == '-' || keys[i] == '_') {
				mA4 = mA4 > 415.0f ? mA4 - 1.0f : 415.0f;
			}
		}

		// ---- pitch from the wire ----
		if (mTaker.get(mState)) {
			mFreq = mState.freq;
		}

		// ---- smoothing / hold (every frame, TUITUNE's regimes) ----
		bool active;
		if (mFreq > 20.0f && mFreq < 2000.0f) {
			float delta = mFreq - mSmoothedFreq;
			float rel = mSmoothedFreq > 0.0f
				? (delta < 0 ? -delta : delta) / mSmoothedFreq : 1.0f;
			if (mSmoothedFreq <= 0.0f || rel > 0.06f) {
				mSmoothedFreq = mFreq; // note jump: snap
			} else {
				mSmoothedFreq += 0.35f * delta;
			}
			mHoldFrames = 0;
			active = true;
		} else {
			active = ++mHoldFrames < 30 && mSmoothedFreq > 0.0f; // 0.5 s hold
		}

		NoteInfo note = {"--", 0.0f, 0.0f};
		if (mSmoothedFreq > 0.0f) {
			note = nearestNote(mSmoothedFreq, mA4);
			mDisplayCents += 0.4f * (note.cents - mDisplayCents);
		}

		// ---- render (TUITUNE's screen, via the ANSI path) ----
		InstOS::TUI& tui = *mpTUI;
		unsigned cols = tui.cols();
		unsigned width = cols > 28 ? (cols - 8 > 100 ? 100 : cols - 8) : 20;
		unsigned left = cols > width ? (cols - width) / 2 : 1;
		if (left < 1) left = 1;

		static Frame f; // 8 KB buffer: static, not stack
		f.clear();

		f.at(1, left);
		f.addf("\x1b[1;38;5;81mTUITUNE\x1b[0m\x1b[38;5;244m  chromatic tuner"
		       "  A4=%.0f Hz  [+/-] ref (serial)\x1b[0m", (double) mA4);

		const char* color = active ? centsColor(mDisplayCents) : "\x1b[38;5;238m";

		// big note name + detected frequency
		const char* noteStr = active || mHoldFrames < 30 ? note.name : "--";
		drawBigNote(f, 3, left, active ? noteStr : "--", color);
		f.at(5, left + (unsigned) strlen(noteStr) * 5 + 4);
		if (active) {
			f.addf("%s%+.1f\x1b[0m\x1b[38;5;244m cents   "
			       "%.2f Hz \x1b[38;5;244m-> %.2f Hz\x1b[0m",
			       color, (double) mDisplayCents,
			       (double) mSmoothedFreq, (double) note.freq);
		} else {
			f.add("\x1b[38;5;244mlistening...\x1b[0m");
		}

		drawCentsMeter(f, 9, left, width, mDisplayCents, active);

		f.flush(tui);
		tui.present(); // vsync-paced
	}
};

int main() {
	// static: Firmware app objects are larger than the 128 KB kernel stack allows
	static TunerDisplay mTunerDisplay;
	mTunerDisplay.start();
}
