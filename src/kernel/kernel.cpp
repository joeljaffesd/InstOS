#include "kernel.h"
#include <circle/gpiopin.h>
#include <circle/timer.h>

CKernel::CKernel (void)
{
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	return TRUE;
}

TShutdownMode CKernel::Run (void)
{	
	// Blink the act LED at 1Hz forever
	while (true)
	{
		m_ActLED.On ();
		CTimer::SimpleMsDelay (500);
		m_ActLED.Off ();
		CTimer::SimpleMsDelay (500);
	}

	return ShutdownReboot;
}
