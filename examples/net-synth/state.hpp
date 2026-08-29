#ifndef NET_SYNTH_TWO_STATE_HPP
#define NET_SYNTH_TWO_STATE_HPP

#include <math.h>

// Polyphonic control state cast from the TOUCH box to the AUDIO box.
//
// Because the keyboard is fixed-pitch, the entire performance state is just
// "which keys are held" -- a bitmask. That is 8 bytes on the wire, one
// barebone packet, and it is naturally idempotent: latest-wins state casting
// is exactly right for it, and any single packet that lands is a complete,
// usable picture of the keyboard. Nothing needs to be reconstructed from a
// history of note-on/note-off events, which is what a dropped packet would
// otherwise ruin.
struct SynthState {
  unsigned keys;     // bit n set = semitone n above A1 is held (n = 0..12)
  unsigned changes;  // bumped whenever the held set changes
};

// One octave, A1 (55 Hz) up to A2 (110 Hz) inclusive -- 13 semitones, so 13
// keys: 8 white and 5 black.
static const float kKeyboardRootHz = 55.f;   // A1
static const int kKeyCount = 13;
static const int kWhiteCount = 8;
static const int kBlackCount = 5;

// semitone offset of each white key, left to right: A B C D E F G A
static const int kWhiteSemitone[kWhiteCount] = {0, 2, 3, 5, 7, 8, 10, 12};
// black keys sit on the boundary just right of these white keys ...
static const int kBlackAfterWhite[kBlackCount] = {0, 2, 3, 5, 6};
// ... and sound these semitones: A# C# D# F# G#
static const int kBlackSemitone[kBlackCount] = {1, 4, 6, 9, 11};

// What a bit MEANS is part of the wire contract, so it lives here with the
// struct rather than in either end: equal temperament above A1.
static inline float keyFrequency(int nSemitone) {
  return kKeyboardRootHz * powf(2.f, (float) nSemitone / 12.f);
}

#endif
