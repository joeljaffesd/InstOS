#ifndef JAFFRACK_TUI_TUNER_HPP
#define JAFFRACK_TUI_TUNER_HPP

// The TUITUNE guts (YIN, note math, and the terminal-styled screen), shared
// by the tuner mode of Jaffrack. Same code as examples/tuner, gathered into
// one header: analysis in float on a 2x-decimated window, rendering through
// the TUI engine's ANSI path with the original's exact escape codes.

#include <modules/TUI.hpp>
#include <math.h>

namespace TuiTuner {

// ------------------------------------------------------- pitch detection ----

// YIN (de Cheveigne & Kawahara 2002) with parabolic interpolation, in float.
// Returns frequency in Hz, or 0 if no confident pitch.
static float yinPitch(const float* x, unsigned n, float fSampleRate) {
	const unsigned tauMax = n / 2;                          // ~23 Hz floor
	const unsigned tauMin = (unsigned) (fSampleRate / 1600.0f); // 1.6 kHz cap
	static float d[2048], cmnd[2048];                       // sized for n = 4096

	for (unsigned tau = 1; tau < tauMax; tau++) {
		float sum = 0.0f;
		for (unsigned i = 0; i < tauMax; i++) {
			float diff = x[i] - x[i + tau];
			sum += diff * diff;
		}
		d[tau] = sum;
	}
	float running = 0.0f;
	cmnd[0] = 1.0f;
	for (unsigned tau = 1; tau < tauMax; tau++) {
		running += d[tau];
		cmnd[tau] = running > 0.0f ? d[tau] * (float) tau / running : 1.0f;
	}

	constexpr float kThreshold = 0.12f;
	unsigned tau = 0;
	for (unsigned t = tauMin > 2 ? tauMin : 2; t < tauMax - 1; t++) {
		if (cmnd[t] < kThreshold) {
			while (t + 1 < tauMax - 1 && cmnd[t + 1] < cmnd[t]) t++;
			tau = t;
			break;
		}
	}
	if (tau == 0) return 0.0f;

	float t = (float) tau;
	float a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
	float denom = a - 2.0f * b + c;
	if (denom > 1e-9f || denom < -1e-9f) t += 0.5f * (a - c) / denom;

	return fSampleRate / t;
}

static float rms(const float* x, unsigned n) {
	float s = 0.0f;
	for (unsigned i = 0; i < n; i++) s += x[i] * x[i];
	return sqrtf(s / (float) n);
}

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

// ------------------------------------------------------------- the mode ----

// The whole tuner screen as a drop-in mode: feed it a window of samples now
// and then via analyze(), call render() every frame.
struct Tuner {
	float mA4 = 440.0f;
	float mFreq = 0.0f;
	float mSmoothedFreq = 0.0f;
	float mDisplayCents = 0.0f;
	unsigned mHoldFrames = 0;

	// window: kWindow samples at fSampleRate; decimated 2x internally
	static constexpr unsigned kWindow = 4096;
	static constexpr unsigned kDecWindow = 2048;

	void analyze(const float* pWindow, float fSampleRate) {
		static float Decimated[kDecWindow];
		for (unsigned i = 0; i < kDecWindow; i++) {
			Decimated[i] = 0.5f * (pWindow[2*i] + pWindow[2*i + 1]);
		}
		float level = rms(Decimated, kDecWindow);
		mFreq = 0.0f;
		if (level > 0.003f) {
			mFreq = yinPitch(Decimated, kDecWindow, fSampleRate / 2.0f);
		}
	}

	void render(InstOS::TUI& tui) {
		// smoothing / hold (every frame, TUITUNE's regimes)
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

		unsigned cols = tui.cols();
		unsigned width = cols > 28 ? (cols - 8 > 100 ? 100 : cols - 8) : 20;
		unsigned left = cols > width ? (cols - width) / 2 : 1;
		if (left < 1) left = 1;

		static Frame f; // 8 KB buffer: static, not stack
		f.clear();

		f.at(1, left);
		f.addf("\x1b[1;38;5;81mTUITUNE\x1b[0m\x1b[38;5;244m  chromatic tuner"
		       "  A4=%.0f Hz  [+/-] ref (serial)  tap to exit\x1b[0m",
		       (double) mA4);

		const char* color = active ? centsColor(mDisplayCents) : "\x1b[38;5;238m";

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

} // namespace TuiTuner

#endif
