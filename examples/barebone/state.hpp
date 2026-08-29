#ifndef STATE_HPP
#define STATE_HPP

// The shared state cast between maker and taker — layout-identical to
// Jaffrack's include/state.hpp, so either end of this pair also interops
// with a stock Cuttlebone peer on a laptop running the Jaffrack apps.
// POD only: this struct's bytes ARE the wire format.
struct SharedState {
  static const int bufferSize = 48000;	// one second at 48 kHz
  int writeIndex = 0;
  float buffer[bufferSize];

  void writeSample(float input) {
    buffer[writeIndex] = input;
    writeIndex++;
    if (writeIndex >= bufferSize) {
      writeIndex = 0; // circular logic
    }
  }

  float readSample(int order) const {
    if (order >= bufferSize) {order = bufferSize - 1;}
    int readIndex = writeIndex - order;
    if (readIndex < 0) {readIndex += bufferSize;}
    return buffer[readIndex];
  }
};

#endif
