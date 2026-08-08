#pragma once

#include <circle/startup.h>
#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/serial.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/types.h>

// reboot flag; inline so every TU shares the one flag the handler sets
inline volatile boolean s_bRebootRequested = FALSE;

inline void RebootMagicReceived(void) {
  s_bRebootRequested = TRUE;
}

// Send over RX to trigger reboot
inline const char RebootMagic[] = "reboot-instos";

namespace InstOS {
class Firmware {
private:
  // private members
	CDeviceNameService m_DeviceNameService;	// singleton, required by CSerialDevice
	CExceptionHandler m_ExceptionHandler; // needed for ?
	CInterruptSystem m_Interrupt; // Handles how many interrupts?

protected:
  // members subclasses may use directly
	// UART0: TxD GPIO14, RxD GPIO15; must get the interrupt system, else it
	// falls back to polling mode and RegisterMagicReceivedHandler asserts
	CSerialDevice m_Serial{&m_Interrupt};

public:
	inline static Firmware* instance = nullptr; // Static pointer to the current instance of Program

	// overridable init function
	inline virtual boolean init() {
    if (!m_Interrupt.Initialize()) {
      return FALSE;
    }

    if (!m_Serial.Initialize(115200)) {
		  return FALSE;
	  }

    m_Serial.RegisterMagicReceivedHandler(RebootMagic, RebootMagicReceived);

    return TRUE;
  }

	// overridable loop operation
	inline virtual void loop() {}

	// Never returns: halts on init failure, reboots when the magic string arrives
	void start() {
		// initialize
    instance = this;
	  if (!this->init()) {
		  halt();
	  }

		// loop
		while (!s_bRebootRequested) {
			this->loop();
		}

		reboot();
	}

};

} // namespace InstOS
