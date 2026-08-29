#include <InstOS.hpp>
#include <modules/HDMIDisplay.hpp>

// Prints information about the display on the mini-HDMI port (firmware
// dimensions + EDID identity) to the serial monitor, then blinks.
class HDMIInfo : public InstOS::Firmware {
	InstOS::HDMIDisplay mHDMI{TRUE}; // info-only: no framebuffer brought up

	CActLED mActLED;
	unsigned mLastBlinkTicks = 0;

	void loop() override {
		unsigned nTicks = m_Timer.GetTicks();
		if (nTicks - mLastBlinkTicks >= HZ / 2) {
			mLastBlinkTicks = nTicks;
			static boolean bOn = FALSE;
			bOn = !bOn;
			if (bOn) mActLED.On(); else mActLED.Off();
		}
	}

};

int main() {
	HDMIInfo mHDMIInfo;
	mHDMIInfo.start();
}
