#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/serial.h>
#include <circle/types.h>

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	static void RebootMagicReceived (void);	// IRQ context

	CActLED m_ActLED;
	CDeviceNameService m_DeviceNameService;	// singleton, required by CSerialDevice
	CExceptionHandler m_ExceptionHandler;
	CInterruptSystem m_Interrupt;
	CSerialDevice m_Serial;	// UART0: TxD GPIO14, RxD GPIO15, interrupt-driven

};

#endif
