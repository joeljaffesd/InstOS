#ifndef TUNER_STATE_HPP
#define TUNER_STATE_HPP

// The shared state cast from audio/ (maker) to graphics/ (taker) at the
// analysis rate. POD only: this struct's bytes ARE the wire format.
struct TunerState {
  float freq = 0.0f;    // latest YIN estimate in Hz; 0 = no confident pitch
  float level = 0.0f;   // window RMS (the graphics box shows signal presence)
  unsigned changes = 0; // bumped per analysis, so the taker can spot staleness
};

#endif
