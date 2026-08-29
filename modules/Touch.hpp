#pragma once

#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/HDMIDisplay.hpp>
#include <circle/input/touchscreen.h>

namespace InstOS {

// Multitouch input module for a USB HID touchscreen (the panel enumerates as
// a digitizer on the USB bus, so a USBHost module must be declared before
// this one). The HDMIDisplay it reports against must be declared before it
// too: CTouchScreenDevice::Setup() needs a live display to scale onto, and
// modules initialize in declaration order.
//
// Coordinates arrive already in display pixels: our Circle fork reads the
// panel's declared HID coordinate range and calibrates the device from it,
// as the Linux HID stack does. Stock Circle does not, and reports raw HID
// units that look wildly out of range.
//
// Attach order and hot-plug both work: the device is looked up once a second
// until it appears, and its disappearance is noticed the same way (Circle
// deregisters the device when the USB device goes away, so the pointer can
// never dangle).
class Touch : public Module {
public:
	// Circle's USB HID touchscreen driver tracks at most 10 contacts
	static constexpr unsigned kMaxFingers = 10;

	struct Finger {
		boolean down;
		unsigned x;
		unsigned y;
	};

	// Circle's touch event handler carries no user-data pointer (unlike the
	// sound callbacks), so a singleton is how an event finds its way back
	// to this module.
	inline static Touch* instance = nullptr;

	Touch(HDMIDisplay& rDisplay) : mrDisplay(rDisplay) { instance = this; }

	const char* name() const override { return "touch"; }

	boolean isConnected() const { return mpDevice != nullptr; }

	// Slot-indexed contact state. Written from Circle's USB completion path
	// and read from the app's loop(); a torn read can only ever misplace one
	// circle for one frame, so no locking.
	const Finger* fingers() const { return mFingers; }

	unsigned fingersDown() const {
		unsigned nCount = 0;
		for (unsigned i = 0; i < kMaxFingers; i++) {
			if (mFingers[i].down) {
				nCount++;
			}
		}
		return nCount;
	}

	// TRUE when the contact state changed since the last clearDirty().
	// Starts TRUE so the app paints one frame before anything is touched --
	// an unpainted screen is indistinguishable from a hung firmware.
	boolean isDirty() const { return mDirty; }
	void clearDirty() { mDirty = FALSE; }

	boolean init() override {
		if (USBHost::instance == nullptr) {
			CLogger::Get()->Write(name(), LogError,
				"Requires a USBHost module declared before it");
			return FALSE;
		}

		if (mrDisplay.graphics() == nullptr) {
			CLogger::Get()->Write(name(), LogError,
				"Requires a working HDMIDisplay module declared before it");
			return FALSE;
		}

		CLogger::Get()->Write(name(), LogNotice, "Reporting into a %u x %u surface",
			mrDisplay.graphics()->GetWidth(), mrDisplay.graphics()->GetHeight());

		acquire(); // may not be enumerated yet; update() keeps looking

		if (mpDevice == nullptr) {
			CLogger::Get()->Write(name(), LogNotice,
				"No touchscreen at init; devices present:");
			logDeviceList();
		}

		return TRUE;
	}

	void update() override {
		// poll for attach/detach; cheap enough at 1 Hz, and the only way to
		// notice either, since Circle has no touch hot-plug notification
		unsigned nTicks = CTimer::Get()->GetTicks();
		if (nTicks - mLastPollTicks >= 1 * HZ) {
			mLastPollTicks = nTicks;
			acquire();

			if (mpDevice == nullptr && ++mPollCount % 5 == 0) {
				CLogger::Get()->Write(name(), LogWarning,
					"Still no touchscreen after %u s", mPollCount);
			}
		}

		if (mpDevice != nullptr) {
			// a no-op for USB devices (their reports are interrupt-driven),
			// but required by the DSI and XPT2046 back ends
			mpDevice->Update();
		}
	}

private:
	void acquire() {
		CTouchScreenDevice* pDevice = findDevice();

		if (pDevice == mpDevice) {
			return; // no change
		}

		if (pDevice == nullptr) {
			CLogger::Get()->Write(name(), LogWarning, "Touchscreen removed");
			mpDevice = nullptr;
			releaseAll();
			return;
		}

		// Setup() may only be called once per device object, which is why
		// this runs only when the pointer actually changes
		pDevice->Setup(mrDisplay.graphics()->GetDisplay());
		pDevice->RegisterEventHandler(EventStub);
		mpDevice = pDevice;
		releaseAll();

		CLogger::Get()->Write(name(), LogNotice,
			"Touchscreen ready on %s (up to %u contacts)",
			(const char*) mDeviceName, kMaxFingers);
	}

	// Circle numbers touch devices from 1; a re-plug, or a panel exposing a
	// second digitizer (touch plus pen), can land on a higher index, so
	// probe a few rather than assuming "touch1".
	CTouchScreenDevice* findDevice() {
		for (unsigned i = 1; i <= 4; i++) {
			CDevice* pDevice = CDeviceNameService::Get()->GetDevice("touch", i, FALSE);
			if (pDevice != nullptr) {
				mDeviceName.Format("touch%u", i);
				return static_cast<CTouchScreenDevice*>(pDevice);
			}
		}
		return nullptr;
	}

	// Dumps every registered device with vendor/product strings straight to
	// the UART. This is the diagnostic that separates "panel never
	// enumerated" from "panel enumerated under another driver".
	void logDeviceList() {
		CDevice* pTarget = CDeviceNameService::Get()->GetDevice("ttyS1", FALSE);
		if (pTarget == nullptr) {
			CLogger::Get()->Write(name(), LogWarning, "No ttyS1 to list on");
			return;
		}
		CDeviceNameService::Get()->ListDevices(pTarget, TRUE);
	}

	static void EventStub(TTouchScreenEvent Event, unsigned nID,
			      unsigned nPosX, unsigned nPosY) {
		if (instance != nullptr) {
			instance->onEvent(Event, nID, nPosX, nPosY);
		}
	}

	void onEvent(TTouchScreenEvent Event, unsigned nID, unsigned nPosX, unsigned nPosY) {
		if (nID >= kMaxFingers) {
			return;
		}

		switch (Event) {
		case TouchScreenEventFingerDown:
		case TouchScreenEventFingerMove:
			mFingers[nID].x = nPosX;
			mFingers[nID].y = nPosY;
			mFingers[nID].down = TRUE;
			break;

		case TouchScreenEventFingerUp:
			// carries the slot only -- no position
			mFingers[nID].down = FALSE;
			break;

		default:
			return;
		}

		mDirty = TRUE;
	}

	void releaseAll() {
		for (unsigned i = 0; i < kMaxFingers; i++) {
			mFingers[i].down = FALSE;
		}
		mDirty = TRUE;
	}

	HDMIDisplay& mrDisplay;
	CTouchScreenDevice* mpDevice = nullptr;
	Finger mFingers[kMaxFingers] = {};
	unsigned mLastPollTicks = 0;
	unsigned mPollCount = 0;
	CString mDeviceName{"touch1"};
	volatile boolean mDirty = TRUE;
};

} // namespace InstOS
