#pragma once

#include <modules/AudioIO.hpp>
#include <modules/USBHost.hpp>
#include <circle/sound/usbsoundbasedevice.h>

namespace InstOS {

// USB Audio Class (UAC) transport, full duplex by default. Requires a USBHost
// module declared before it. AudioIO retries Start() once a second until an
// audio device (e.g. UA Volt) is enumerated, so attach order and hot-plug
// both work. Pass DeviceModeTXOnly for output-only devices.
class UAC : public AudioIO {
public:
	static constexpr unsigned kChannels = 2;

	using TDeviceMode = CUSBSoundBaseDevice::TDeviceMode;

	// nBitDepth: 16 or 24 selects the USB stream resolution; 0 inherits
	// "soundopt=" from cmdline.txt (default 16). The choice is global — it
	// steers which altsettings bind at enumeration for every audio device —
	// so it must be made here, before the USB stack starts. Note some
	// interfaces (e.g. UA Volt) expose their capture path only at 24-bit.
	UAC(unsigned nSampleRate = 48000, unsigned nBufferFrames = 128,
	    TDeviceMode DeviceMode = CUSBSoundBaseDevice::DeviceModeTXRX,
	    unsigned nBitDepth = 0)
	: AudioIO(nSampleRate, nBufferFrames, kChannels,
		  DeviceMode != CUSBSoundBaseDevice::DeviceModeTXOnly),
	  mBitDepth(applyBitDepth(nBitDepth)), // must precede mSound: both it and
	                                       // enumeration read the option
	  mSound(nSampleRate, DeviceMode) {}

	const char* name() const override { return "uac"; }

	boolean init() override {
		if (USBHost::instance == nullptr) {
			CLogger::Get()->Write(name(), LogError,
					      "Requires a USBHost module declared before it");
			return FALSE;
		}

		if (!AudioIO::init()) {
			return FALSE;
		}

		CLogger::Get()->Write(name(), LogNotice, "%u Hz, %u-frame buffers, %u-bit stream",
				      sampleRate(), bufferFrames(), mBitDepth);
		return TRUE;
	}

protected:
	CSoundBaseDevice* device() override { return &mSound; }

private:
	// applies the app's bit-depth choice to the kernel options BEFORE the
	// sound device (and later the USB stack) reads it; returns the depth
	// actually in effect
	static unsigned applyBitDepth(unsigned nBitDepth) {
		if (nBitDepth == 16 || nBitDepth == 24) {
			CKernelOptions::Get()->SetSoundOption(nBitDepth);
		}
		return CKernelOptions::Get()->GetSoundOption() == 24 ? 24 : 16;
	}

	unsigned mBitDepth; // declared before mSound: initializer order matters
	CUSBSoundBaseDevice mSound;
};

} // namespace InstOS
