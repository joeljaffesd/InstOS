#ifndef JAFFRACK_STATE_HPP
#define JAFFRACK_STATE_HPP

// The two wire structs of the Jaffrack pair (InstOS's remake of upstream
// Jaffrack's audioUI + cuttleSend combo). Two Barebone links run over the one cable:
//
//   audio/ (.1)  --ScopeState (port 63059)-->  graphics/ (.2)
//   audio/ (.1)  <--ControlState (port 63060)-- graphics/ (.2)
//
// POD only: these structs' bytes ARE the wire format.

#include "../barebone/state.hpp"  // SharedState: the 48000-sample scope ring
                                  // (bit-compatible with Jaffrack laptops)
#include "../monotron/state.hpp"  // MonotronState + knob mapping curves

// everything the panel commands, cast whenever anything changes
struct ControlState {
  int globalMute = 0;         // 1 = silence the UAC output (scope stays live)
  int bassMute = 0;           // 1 = mute the bass input chain only
  unsigned huckTrigger = 0;   // bump to (re)start the HuckFinn sample, one-shot
  MonotronState monotron = {};
  unsigned changes = 0;       // bumped on any change
};

constexpr unsigned short kControlPort = 63060; // scope rides the default port

#endif
