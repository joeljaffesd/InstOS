#pragma once

#include <circle/startup.h>
#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/serial.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/usb/usbhcidevice.h>

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
  // private members (declaration order = construction order)
	CKernelOptions m_Options; // singleton; sound/USB drivers read it via Get()
	CDeviceNameService m_DeviceNameService;	// singleton, required by CSerialDevice
	CExceptionHandler m_ExceptionHandler; // needed for ?
	CInterruptSystem m_Interrupt; // Handles how many interrupts?
	CScheduler m_Scheduler; // singleton; USB enumeration sleeps on it

protected:
  // members subclasses may use directly
	// UART0: TxD GPIO14, RxD GPIO15; must get the interrupt system, else it
	// falls back to polling mode and RegisterMagicReceivedHandler asserts
	CSerialDevice m_Serial{&m_Interrupt};
	CTimer m_Timer{&m_Interrupt}; // system clock; USB and sound need CTimer::Get()
	CLogger m_Logger{LogNotice, &m_Timer}; // Circle drivers log through CLogger::Get()
	// internal xHCI host controller (needs otg_mode=1 in config.txt on CM4);
	// bPlugAndPlay=TRUE is mandatory here: start()'s loop calls
	// UpdatePlugAndPlay(), which asserts (and panics) in non-PnP mode
	CUSBHCIDevice m_USBHCI{&m_Interrupt, &m_Timer, TRUE};

public:
	inline static Firmware* instance = nullptr; // Static pointer to the current instance of Program

	// overridable init function
	inline virtual boolean init() {
    if (!m_Interrupt.Initialize()) {
      return FALSE;
    }

    if (!m_Timer.Initialize()) {
      return FALSE;
    }

    if (!m_Serial.Initialize(115200)) {
		  return FALSE;
	  }

    m_Logger.Initialize(&m_Serial); // driver + app logging on UART

    m_Serial.RegisterMagicReceivedHandler(RebootMagic, RebootMagicReceived);

    // Non-fatal: no USB just means no USB devices; UART devloop keeps working
    if (!m_USBHCI.Initialize()) {
      m_Logger.Write("instos", LogError, "USB host controller init failed");
    }

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
			// process USB attach/detach; drivers log device names as they bind
			m_USBHCI.UpdatePlugAndPlay();
			this->loop();
		}

		reboot();
	}

};

} // namespace InstOS
