#include "kernel.h"
#include <circle/gpiopin.h>
#include <circle/string.h>
#include <circle/timer.h>

// Sent by devloop.sh (cflashy --reboot) to request a reboot into the serial
// bootloader; must never occur in data sent TO the board
static const char RebootMagic[] = "reboot-instos";

static volatile boolean s_bRebootRequested = FALSE;

void CKernel::RebootMagicReceived (void)
{
	s_bRebootRequested = TRUE;	// defer the reboot to the main loop
}

CKernel::CKernel (void)
:	m_Serial (&m_Interrupt)
{
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	if (!m_Interrupt.Initialize ())
	{
		return FALSE;
	}

	if (!m_Serial.Initialize (115200))
	{
		return FALSE;
	}

	m_Serial.RegisterMagicReceivedHandler (RebootMagic, RebootMagicReceived);

	return TRUE;
}

TShutdownMode CKernel::Run (void)
{
	CGPIOPin mGPIO (21, GPIOModeOutput);
	// Blink the act LED / pin 21 and write a serial message at 1Hz,
	// until the reboot magic arrives on serial
	unsigned nCount = 0;
	while (!s_bRebootRequested)
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

	return ShutdownReboot;	// main() calls reboot(), back into the bootloader
}
