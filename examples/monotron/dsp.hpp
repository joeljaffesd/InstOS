#ifndef MONOTRON_DSP_HPP
#define MONOTRON_DSP_HPP

#include <math.h>

// The monotron's audio engine, ported from Jaffrack's MonotronProcessor
// (reference/Jaffrack/include/monotron.hpp). Upstream leaned on Gimmel for
// the buffer and filters; those are reimplemented here so the engine is
// freestanding -- no allocation, no STL, no exceptions.

namespace monotron {

// M_PI is a POSIX extension, not ISO C++, and is not visible under the
// strict settings Circle builds with -- a host build will happily compile
// without this and the cross build will not.
static constexpr float kPi = 3.14159265358979323846f;

// Power-of-two so the wrap is a mask rather than a division per sample.
// 65536 samples is ~1365 ms at 48 kHz, comfortably over the 1090 ms the
// delay-time knob can ask for.
template <unsigned SIZE>
class CircularBuffer {
	float mBuffer[SIZE] = {};
	unsigned mWrite = 0;

public:
	void write(float fSample) {
		mBuffer[mWrite] = fSample;
		mWrite = (mWrite + 1) & (SIZE - 1);
	}

	// read fDelay samples back, linearly interpolated so delay-time sweeps
	// glide instead of stepping
	float read(float fDelay) const {
		if (fDelay < 1.f) fDelay = 1.f;
		if (fDelay > (float) (SIZE - 2)) fDelay = (float) (SIZE - 2);

		float fPos = (float) mWrite - fDelay;
		while (fPos < 0.f) {
			fPos += (float) SIZE;
		}

		unsigned nIndex = (unsigned) fPos;
		float fFrac = fPos - (float) nIndex;
		unsigned nNext = (nIndex + 1) & (SIZE - 1);

		return mBuffer[nIndex & (SIZE - 1)] * (1.f - fFrac) + mBuffer[nNext] * fFrac;
	}

	static unsigned size() { return SIZE; }
};

class OnePoleLP {
	float mCoeff = 1.f;
	float mY = 0.f;

public:
	void setCutoff(float fHz, float fSampleRate) {
		float fCoeff = 1.f - expf(-2.f * kPi * fHz / fSampleRate);
		mCoeff = fCoeff < 0.f ? 0.f : (fCoeff > 1.f ? 1.f : fCoeff);
	}

	float process(float fSample) {
		mY += mCoeff * (fSample - mY);
		return mY;
	}
};

// Keeps DC out of the feedback path; without it any offset accumulates
// around the loop until the delay line saturates.
class DCBlocker {
	float mR = 0.9996f;
	float mX1 = 0.f;
	float mY1 = 0.f;

public:
	void setCutoff(float fHz, float fSampleRate) {
		mR = 1.f - 2.f * kPi * fHz / fSampleRate;
	}

	float process(float fSample) {
		float fY = fSample - mX1 + mR * mY1;
		mX1 = fSample;
		mY1 = fY;
		return fY;
	}
};

// Jaffrack's CustomDelay: a damped feedback line with a DC blocker and a
// hard limit inside the loop. The limit is what makes a feedback knob that
// goes past unity (up to 1.2) safe -- the loop saturates instead of running
// away.
//
// NOTE, because it surprises: what gets written into the line is
// `wet * feedback`, so FEEDBACK ALSO ACTS AS THE SEND LEVEL. At feedback 0
// nothing enters the buffer and there is no delay at all, however long the
// delay time is. That is upstream's behaviour, verified against it, and not
// a bug to "fix".
class Delay {
	static constexpr unsigned kBufferSize = 65536;

	CircularBuffer<kBufferSize> mBuffer;
	OnePoleLP mDamping;
	DCBlocker mDCBlock;

	float mSampleRate = 48000.f;
	float mDelayTimeMs = 398.f;
	float mFeedback = 0.3f;
	bool mEnabled = true;

	static float limit(float fSample, float fCeiling) {
		if (fSample > fCeiling) return fCeiling;
		if (fSample < -fCeiling) return -fCeiling;
		return fSample;
	}

public:
	void init(float fSampleRate) {
		mSampleRate = fSampleRate;
		mDamping.setCutoff(12000.f, fSampleRate);
		mDCBlock.setCutoff(3.f, fSampleRate);
	}

	void setDelayTime(float fMillis) {
		float fMax = (float) (kBufferSize - 2) * 1000.f / mSampleRate;
		if (fMillis < 0.f) fMillis = 0.f;
		if (fMillis > fMax) fMillis = fMax;
		mDelayTimeMs = fMillis;
	}

	void setFeedback(float fGain) { mFeedback = fGain; }
	void setCutoff(float fHz) { mDamping.setCutoff(fHz, mSampleRate); }
	void setEnabled(bool bEnabled) { mEnabled = bEnabled; }

	float process(float fInput) {
		float fRead = mDelayTimeMs * mSampleRate / 1000.f;
		float fWet = mDamping.process(mBuffer.read(fRead) + fInput);

		mBuffer.write(mDCBlock.process(limit(fWet * mFeedback, 0.75f)));

		return mEnabled ? fWet : fInput;
	}
};

class LFO {
	float mPhase = 0.f;
	float mIncrement = 0.f;
	int mMode = 0; // 0 = triangle, 1 = square

public:
	void setFrequency(float fHz, float fSampleRate) { mIncrement = fHz / fSampleRate; }
	void setMode(int nMode) { mMode = nMode; }

	float process() {
		mPhase += mIncrement;
		if (mPhase >= 1.f) {
			mPhase -= 1.f;
		}

		if (mMode == 0) {
			float fTriangle = mPhase * 2.f - 1.f;
			if (fTriangle < 0.f) {
				fTriangle = -fTriangle;
			}
			return fTriangle * 2.f - 1.f;
		}

		return mPhase < 0.5f ? 1.f : -1.f;
	}
};

// Jaffrack's quasibandlimitedFMSaw, made an object rather than a function
// with static locals -- upstream's version shares one phase across every
// caller, which is invisible while there is exactly one voice and a bug the
// moment there is a second.
//
// The trick: a sine whose phase is modulated by its own previous output.
// The modulation index falls off as frequency rises ((0.5 - w)^4), which is
// what keeps the harmonics from folding back as aliasing up top.
class FMSaw {
	float mPhase = 0.f;
	float mHistory = 0.f;

public:
	float process(float fHz, float fSampleRate, float fNumHarms = 0.433f) {
		float fW = fHz / fSampleRate;

		mPhase += fW;
		if (mPhase > 1.f) {
			mPhase = 0.f; // upstream resets rather than wrapping
		}

		float fFilter = powf(fNumHarms, 1.3f);
		fFilter = 10.f * powf(0.5f - fW, 4.f) * fFilter;

		mHistory = sinf((mPhase + mHistory * fFilter) * 2.f * kPi);
		return mHistory;
	}
};

} // namespace monotron

#endif
