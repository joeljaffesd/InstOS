#include <InstOS.hpp>
#include <modules/HDMIDisplay.hpp>
#include <modules/TUI.hpp>

// TUI engine demo: exercises both drawing paths on a few panels —
// the xterm-256 palette, the box-drawing glyph supplement, a TUITUNE-style
// cents meter drawn through the ANSI write() path (byte-compatible with a
// real terminal), and a banner readout. The meter sweeps so redraw cost and
// motion smoothness are visible.
class TUIDemo : public InstOS::Firmware {
	InstOS::HDMIDisplay mDisplay;
	InstOS::TUI* mpTUI = nullptr; // needs graphics(); built on first loop()

	unsigned mFrame = 0;

	void loop() override {
		if (mDisplay.graphics() == nullptr) {
			return;
		}
		if (mpTUI == nullptr) {
			mpTUI = new InstOS::TUI(mDisplay, 2); // fixed scale; rebuild to resize
			m_Logger.Write("tui", LogNotice, "%u x %u cells",
				       mpTUI->cols(), mpTUI->rows());
		}
		InstOS::TUI& tui = *mpTUI;

		tui.clear();

		// ---- header (direct calls) ----
		tui.at(0, 2);
		tui.bold(TRUE); tui.fg(81);
		tui.print("InstOS TUI");
		tui.bold(FALSE); tui.fg(244);
		tui.printf("  %u x %u cells, 256 colors, box glyphs", tui.cols(), tui.rows());
		tui.resetAttrs();

		// ---- palette swatch: the 6x6x6 cube ----
		for (unsigned i = 0; i < 216 && 4 + i / 36 < tui.rows(); i++) {
			tui.at(2 + i / 36, 2 + i % 36);
			tui.bg((u8) (16 + i));
			tui.print(" ");
		}
		tui.resetAttrs();

		// ---- box-drawing panel ----
		unsigned nBoxTop = 9, nBoxLeft = 2, nBoxW = 40;
		tui.fg(240);
		tui.at(nBoxTop, nBoxLeft); tui.print("┌");
		for (unsigned i = 0; i < nBoxW - 2; i++) tui.print("─");
		tui.print("┐");
		tui.at(nBoxTop + 1, nBoxLeft); tui.print("│");
		tui.at(nBoxTop + 1, nBoxLeft + nBoxW - 1); tui.print("│");
		tui.at(nBoxTop + 2, nBoxLeft); tui.print("└");
		for (unsigned i = 0; i < nBoxW - 2; i++) tui.print("─");
		tui.print("┘");
		tui.at(nBoxTop + 1, nBoxLeft + 2);
		tui.fg(15);
		tui.print("boxes ─│┼  blocks █▀▄  shades ░▒  ◆ ●");
		tui.resetAttrs();

		// ---- TUITUNE-style cents meter via the ANSI path ----
		// (identical bytes would drive a real terminal over serial)
		float phase = (float) (mFrame % 240) / 240.0f;
		float sweep = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f; // -1..1
		int nWidth = 50;
		int nCenter = nWidth / 2;
		int nPos = nCenter + (int) (sweep * (float) nCenter);
		if (nPos < 0) nPos = 0;
		if (nPos > nWidth - 1) nPos = nWidth - 1;
		const char* pColor = (nPos > nCenter - 2 && nPos < nCenter + 2)
			? "\x1b[38;5;46m" : (nPos > nCenter - 8 && nPos < nCenter + 8)
			? "\x1b[38;5;226m" : "\x1b[38;5;196m";

		char line[512]; int n = 0;
		n += snprintfRow(line + n, "\x1b[14;3H\x1b[38;5;240m");
		for (int i = 0; i < nWidth; i++) {
			if (i == nPos) {
				n += snprintfRow(line + n, pColor);
				n += snprintfRow(line + n, "◆");
				n += snprintfRow(line + n, "\x1b[38;5;240m");
			} else {
				n += snprintfRow(line + n, i == nCenter ? "┼" : "─");
			}
		}
		n += snprintfRow(line + n, "\x1b[0m");
		tui.write(line, (unsigned) n);

		tui.at(15, 2); tui.fg(244);
		tui.print("-50");
		tui.at(15, 2 + (unsigned) nCenter - 1); tui.print(" 0");
		tui.at(15, 2 + (unsigned) nWidth - 3); tui.print("+50");
		tui.resetAttrs();

		tui.present(); // vsync-paced
		mFrame++;
	}

	// tiny strcpy-with-length helper (no libc snprintf on bare metal)
	static int snprintfRow(char* pOut, const char* pIn) {
		int n = 0;
		while (pIn[n] != '\0') { pOut[n] = pIn[n]; n++; }
		pOut[n] = '\0';
		return n;
	}
};

int main() {
	static TUIDemo mDemo;
	mDemo.start();
}
