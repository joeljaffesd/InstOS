#include <InstOS.hpp>

// Minimal example: blink the ACT LED and tick over UART. No modules needed.
class Blink : public InstOS::Firmware {
	CActLED mActLED;

	void loop() override {
		mActLED.On();
		CTimer::SimpleMsDelay(500);
		mActLED.Off();
		CTimer::SimpleMsDelay(500);

		static unsigned nCount = 0;
		CString Message;
		Message.Format("InstOS alive, tick %u\r\n", nCount++);
		this->m_Serial.Write(Message, Message.GetLength());
	}

};

int main() {
	Blink mBlink;
	mBlink.start();
}
