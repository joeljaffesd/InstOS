#include "kernel.h"
#include <circle/gpiopin.h>
#include <circle/string.h>
#include <circle/timer.h>

CKernel::CKernel (void)
{
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	return m_Serial.Initialize (115200);
}

TShutdownMode CKernel::Run (void)
{
	CGPIOPin mGPIO (21, GPIOModeOutput);
	// Blink the act LED / pin 21 and write a serial message at 1Hz forever
	unsigned nCount = 0;
	while (true)
	{
		m_ActLED.On ();
		mGPIO.Write (HIGH);
		CTimer::SimpleMsDelay (500);
		m_ActLED.Off ();
		mGPIO.Write (LOW);
		CTimer::SimpleMsDelay (500);

		CString Message;
		Message.Format ("InstOS alive, tick %u\r\n", nCount++);
		m_Serial.Write (Message, Message.GetLength ());
	}

	return ShutdownReboot;
}
