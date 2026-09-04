#ifndef VICTUS_GAUGES_HPP
#define VICTUS_GAUGES_HPP

#include <cairo.h>

// Analog telemetry dials drawn with cairo.

// A fan rotor. `angle` is the current rotation in radians and `duty` is the
// speed as a fraction of the fan's maximum (0-1), which sets how brightly the
// blades are lit. `spinning` is false when the fan has stopped, so a stalled
// fan reads differently from one turning slowly.
void draw_fan_rotor(cairo_t *cr, int width, int height, double angle,
                    double duty, bool spinning);

// A thermometer. `celsius` fills the column against `max_celsius`, and the
// mercury shifts from cool to amber to red as it climbs, matching the colour
// thresholds used by the digital readouts.
void draw_thermometer(cairo_t *cr, int width, int height, double celsius,
                      double max_celsius, bool valid);

#endif // VICTUS_GAUGES_HPP
