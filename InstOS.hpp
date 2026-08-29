#pragma once

#include <circle/startup.h>
#include <circle/actled.h>
#include <circle/cputhrottle.h>
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

// reboot flag; inline so every TU shares the one flag the handler sets
inline volatile boolean s_bRebootRequested = FALSE;

inline void RebootMagicReceived(void) {
  s_bRebootRequested = TRUE;
}

// Send over RX to trigger reboot
inline const char RebootMagic[] = "reboot-instos";

namespace InstOS {

// Base for drop-in hardware modules (see modules/). Declaring one as a member
// of your Firmware subclass is all it takes: the constructor registers it,
// Firmware::init() initializes modules in declaration order, and start()'s
// loop pumps update() on each pass. Order matters — a module that depends on
// another (e.g. UAC on USBHost) must be declared after it.
class Module {
	friend class Firmware;

	inline static Module* sHead = nullptr;
	Module* mNext = nullptr;

protected:
	Module() {
		// append, preserving declaration order
		Module** pp = &sHead;
		while (*pp != nullptr) {
			pp = &(*pp)->mNext;
		}
		*pp = this;
	}

	virtual const char* name() const { return "module"; }

	// one-time setup; core singletons (interrupt, timer, serial, logger,
	// scheduler) are already up when this runs
	virtual boolean init() = 0;

	// called every pass of the main loop, before the app's loop()
	virtual void update() {}
};

class Firmware {
private:
  // private members (declaration order = construction order)
	CKernelOptions m_Options; // singleton; sound/USB drivers read it via Get()
	CDeviceNameService m_DeviceNameService;	// singleton, required by CSerialDevice
	CExceptionHandler m_ExceptionHandler;
	CInterruptSystem m_Interrupt;
	CScheduler m_Scheduler; // singleton; USB enumeration sleeps on it

protected:
  // members subclasses may use directly
	// UART0: TxD GPIO14, RxD GPIO15; must get the interrupt system, else it
	// falls back to polling mode and RegisterMagicReceivedHandler asserts
	CSerialDevice m_Serial{&m_Interrupt};
	CTimer m_Timer{&m_Interrupt}; // system clock; USB and sound need CTimer::Get()
	// Verbosity comes from "loglevel=" in cmdline.txt (0=panic .. 4=debug);
	// Circle's own default when the option is absent is LogDebug, which is
	// what you want while bringing up a driver. Safe to read m_Options here
	// ONLY because it is declared first above and so is already constructed
	// -- the same ordering trap that silently forced DHCP in Ethernet.hpp.
	CLogger m_Logger{CKernelOptions::Get()->GetLogLevel(), &m_Timer};
	CCPUThrottle m_CPUThrottle; // the firmware boots the ARM at its idle clock (600 MHz on CM4)

public:
	inline static Firmware* instance = nullptr; // Static pointer to the current instance of Program

	// overridable init function; overrides must call Firmware::init() first
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

    // run at full speed: DSP costs 2.5x more at the boot clock
    unsigned nBootClock = m_CPUThrottle.GetClockRate();
    m_CPUThrottle.SetSpeed(CPUSpeedMaximum);
    m_Logger.Write("InstOS", LogNotice, "ARM clock %u MHz -> %u MHz",
                   nBootClock / 1000000, m_CPUThrottle.GetClockRate() / 1000000);

    // bring up registered modules in declaration order; a failed module is
    // logged but not fatal — the rest of the firmware keeps running
    for (Module* pModule = Module::sHead; pModule != nullptr; pModule = pModule->mNext) {
      if (!pModule->init()) {
        m_Logger.Write("InstOS", LogError, "Module '%s' failed to initialize", pModule->name());
      }
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
			for (Module* pModule = Module::sHead; pModule != nullptr; pModule = pModule->mNext) {
				pModule->update();
			}
			this->loop();
		}

		reboot();
	}

};

} // namespace InstOS
