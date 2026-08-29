#ifndef MONOTRON_STATE_HPP
#define MONOTRON_STATE_HPP

#include <math.h>

// Full monotron control state, cast from the PANEL box to the AUDIO box.
// This is the wire form of Jaffrack's MonotronParameters (see
// reference/Jaffrack/include/monotron.hpp), which used allolib's
// ParameterServer over OSC for the same job. POD only: these bytes ARE the
// wire format.
//
// 44 bytes: one barebone packet, so a frame is never split and every packet
// that lands is a complete, usable picture of the panel. Resolved units --
// Hz, ms, linear gain -- rather than knob positions, so the audio box never
// has to know the mapping curve.
struct MonotronState {
  int keyboardMute;   // 1 = ribbon released (carrier silent)
  int bypass;         // 1 = delay bypassed
  int lfoMode;        // 0 = triangle, 1 = square
  float frequency;    // ribbon pitch, Hz
  float intensity;    // LFO depth, 0..1
  float rate;         // LFO rate, Hz
  float cutoff;       // delay damping cutoff, Hz
  float delayTime;    // ms
  float feedback;     // linear gain
  unsigned changes;   // bumped whenever anything above changes
};

// Parameter ranges, verbatim from Jaffrack's MonotronParameters. Each triple
// is (min, max, mid): mid is the value that sits at half knob travel.
static const float kFreqMin = 59.f,    kFreqMax = 4806.f,   kFreqMid = 392.f;
static const float kRateMin = 1.f / 46.03f, kRateMax = 426.95f, kRateMid = 3.13f;
static const float kCutoffMin = 43.f,  kCutoffMax = 11600.f, kCutoffMid = 1204.f;
static const float kDelayMin = 35.7f,  kDelayMax = 1090.f,  kDelayMid = 576.f;
static const float kFeedbackMax = 1.2f;   // linear knob
// intensity is a linear 0..1 knob

// Jaffrack's mapParam(): a power curve placing `mid` at half travel, so the
// useful part of each range sits under the middle of the knob instead of
// being crushed into one end.
static inline float mapParam(float x, float outMin, float outMax, float mid) {
  if (x < 0.f) x = 0.f;
  if (x > 1.f) x = 1.f;
  float m = (mid - outMin) / (outMax - outMin);
  float k = logf(m) / logf(0.5f);
  return outMin + powf(x, k) * (outMax - outMin);
}

#endif
