#ifndef VICTUS_EFFECTS_HPP
#define VICTUS_EFFECTS_HPP

#include <string>

// Animated keyboard lighting.
//
// A single background thread walks a phase value and repaints the keyboard each
// frame, so the colour keeps changing instead of sitting on one static value.
// On four-zone boards the zones are driven with a spatial offset, which makes
// the colour travel across the keyboard like a river rather than all four zones
// blinking in unison.

// Effect names accepted by set_keyboard_effect():
//   STATIC   stop animating and leave the current colour alone
//   RAINBOW  whole keyboard cycles through the hue wheel together
//   BREATHE  current colour fades in and out
//   FLOW     hue travels left-to-right across the zones (four-zone boards);
//            degrades to RAINBOW on single-zone hardware, which has no
//            geometry for a wave to travel across
//
// speed is 1-100 (slow to fast). Returns "OK" or "ERROR: ...".
std::string set_keyboard_effect(const std::string &name,
                                const std::string &speed);

// Returns "<NAME> <SPEED>", e.g. "FLOW 60".
std::string get_keyboard_effect();

// Stops the animation thread. Safe to call when nothing is running.
// Called when a static colour is set, and on shutdown.
void stop_keyboard_effect();

// Restarts whatever effect was running when the service last stopped, so the
// lighting survives a reboot. Does nothing if no state was saved.
void restore_keyboard_effect();

#endif // VICTUS_EFFECTS_HPP
