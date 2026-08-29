#include <InstOS.hpp>
#include <modules/USBHost.hpp>
#include <modules/UAC.hpp>

#define SAMPLERATE 48000

// Full-duplex passthrough on a USB audio device plugged into
// the Type-A port: input channel 0 is monitored on both outputs. 
// Blinks the ACT LED as a heartbeat.
class PassThru : public InstOS::Firmware, public InstOS::AudioProcessor {
	// modules: declaration order = init order; UAC requires USBHost before it
	InstOS::USBHost mUSB;
	// 24-bit: many UAC2 interfaces (e.g. UA Volt) expose capture at 24-bit only
	InstOS::UAC mAudio{SAMPLERATE, 128, InstOS::UAC::TDeviceMode::DeviceModeTXRX, 24};

	CActLED mActLED;
	unsigned mActLEDTickCounter = 0;
	bool mActLEDState = false;

	boolean init() override {
		if (!Firmware::init()) {
			return FALSE;
		}

		mAudio.setProcessor(this);
		return TRUE;
	}

	void processAudio(const float** inputBuffers, 
		                float** outputBuffers, 
										unsigned nFrames) override 
	{
		for (unsigned i = 0; i < nFrames; i++) {
			// write input to output
			float inSample = inputBuffers[0][i];
			outputBuffers[0][i] = inSample; // L
			outputBuffers[1][i] = inSample; // R

			// increment and wrap ActLED tick counter at 0.5Hz
			mActLEDTickCounter++;
			if (mActLEDTickCounter >= (SAMPLERATE/2)) {
				mActLEDState = !mActLEDState;
				mActLEDTickCounter = 0;
			}
		}
	}

	void loop() override {
		// non-blocking heartbeat so module updates stay responsive
		static bool lastLEDState = false;
		if (mActLEDState != lastLEDState) {
			lastLEDState = mActLEDState;
			mActLEDState ? mActLED.On() : mActLED.Off();
		}
	}

};

int main() {
	PassThru mPassThru;
	mPassThru.start();
}
