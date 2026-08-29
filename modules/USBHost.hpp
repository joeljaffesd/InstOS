#pragma once

#include <InstOS.hpp>
#include <circle/usb/usbhcidevice.h>

namespace InstOS {

// USB host controller module. Required by (and declared before) any module
// that talks to a USB device: UAC, USBTouch, USBMIDI, ...
//
// CM4 note: this drives the BCM2711's internal xHCI, which requires Circle
// built with USE_XHCI_INTERNAL (see init.sh) and otg_mode=1 in config.txt.
// On the CM4-NANO-B carrier the port is the Type-A socket (BOOT switch OFF).
// Mutually exclusive with USB gadget/device-mode modules — there is one bus.
class USBHost : public Module {
	// bPlugAndPlay=TRUE is mandatory: update() calls UpdatePlugAndPlay(),
	// which asserts (and panics) on a controller in non-PnP mode
	CUSBHCIDevice mHCI{CInterruptSystem::Get(), CTimer::Get(), TRUE};
	boolean mActive = FALSE;

public:
	inline static USBHost* instance = nullptr; // for dependents' init checks

	USBHost() { instance = this; }

	const char* name() const override { return "usbhost"; }

	boolean init() override {
		mActive = mHCI.Initialize();
		return mActive;
	}

	void update() override {
		if (mActive) {
			// process attach/detach; drivers log device names as they bind
			mHCI.UpdatePlugAndPlay();
		}
	}
};

} // namespace InstOS
