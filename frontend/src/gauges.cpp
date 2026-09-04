#include <algorithm>
#include <cmath>

#include "gauges.hpp"

namespace {

// Shared with the stylesheet: cool cyan below 70C, amber to 85C, red above.
void temperature_colour(double celsius, double *r, double *g, double *b) {
  if (celsius >= 85.0) {
    *r = 1.0; *g = 0.28; *b = 0.34;
  } else if (celsius >= 70.0) {
    *r = 1.0; *g = 0.65; *b = 0.15;
  } else {
    *r = 0.0; *g = 0.85; *b = 1.0;
  }
}

void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                  double radius) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius,     radius, -M_PI / 2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
  cairo_arc(cr, x + radius,     y + h - radius, radius, M_PI / 2, M_PI);
  cairo_arc(cr, x + radius,     y + radius,     radius, M_PI, 1.5 * M_PI);
  cairo_close_path(cr);
}

} // namespace

void draw_fan_rotor(cairo_t *cr, int width, int height, double angle,
                    double duty, bool spinning) {
  const double cx = width / 2.0;
  const double cy = height / 2.0;
  const double outer = std::min(width, height) / 2.0 - 4.0;
  const double hub = outer * 0.20;

  duty = std::clamp(duty, 0.0, 1.0);

  // Housing.
  cairo_set_source_rgb(cr, 0.035, 0.047, 0.063);
  cairo_arc(cr, cx, cy, outer, 0, 2 * M_PI);
  cairo_fill_preserve(cr);
  cairo_set_source_rgb(cr, 0.14, 0.19, 0.25);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);

  // A faint ring that brightens with speed, so the dial reads even at a glance.
  if (spinning) {
    cairo_set_source_rgba(cr, 0.0, 0.85, 1.0, 0.15 + 0.5 * duty);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, cx, cy, outer - 1.0, 0, 2 * M_PI);
    cairo_stroke(cr);
  }

  const int blade_count = 7;
  for (int i = 0; i < blade_count; i++) {
    double a = angle + i * (2 * M_PI / blade_count);

    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, hub, a, a + 0.62);
    cairo_arc_negative(cr, cx, cy, outer * 0.92, a + 1.02, a + 0.30);
    cairo_close_path(cr);

    if (spinning) {
      // Leading edge slightly brighter than the body of the blade.
      cairo_set_source_rgba(cr, 0.0, 0.72 + 0.28 * duty, 1.0, 0.30 + 0.45 * duty);
    } else {
      cairo_set_source_rgba(cr, 0.35, 0.42, 0.52, 0.55);
    }
    cairo_fill(cr);
  }

  // Hub.
  cairo_set_source_rgb(cr, 0.07, 0.10, 0.14);
  cairo_arc(cr, cx, cy, hub, 0, 2 * M_PI);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.0, 0.85, 1.0, spinning ? 0.8 : 0.3);
  cairo_set_line_width(cr, 1.2);
  cairo_stroke(cr);
}

void draw_thermometer(cairo_t *cr, int width, int height, double celsius,
                      double max_celsius, bool valid) {
  if (max_celsius <= 0.0)
    max_celsius = 100.0;

  const double bulb_radius = std::min(width * 0.34, height * 0.14);
  const double tube_width = bulb_radius * 1.15;
  const double cx = width / 2.0;
  const double bulb_cy = height - bulb_radius - 3.0;
  const double tube_top = 6.0;
  const double tube_bottom = bulb_cy;
  const double tube_height = tube_bottom - tube_top;

  // Glass.
  cairo_set_source_rgb(cr, 0.035, 0.047, 0.063);
  rounded_rect(cr, cx - tube_width / 2.0, tube_top, tube_width, tube_height,
               tube_width / 2.0);
  cairo_fill(cr);
  cairo_arc(cr, cx, bulb_cy, bulb_radius, 0, 2 * M_PI);
  cairo_fill(cr);

  double r = 0.0, g = 0.0, b = 0.0;
  temperature_colour(celsius, &r, &g, &b);

  if (valid) {
    double fraction = std::clamp(celsius / max_celsius, 0.0, 1.0);
    double column = tube_height * fraction;

    // Mercury: bulb always filled, column rises with temperature.
    cairo_set_source_rgba(cr, r, g, b, 0.95);
    cairo_arc(cr, cx, bulb_cy, bulb_radius - 2.0, 0, 2 * M_PI);
    cairo_fill(cr);

    if (column > 1.0) {
      rounded_rect(cr, cx - (tube_width - 4.0) / 2.0, tube_bottom - column,
                   tube_width - 4.0, column, (tube_width - 4.0) / 2.0);
      cairo_fill(cr);
    }

    // Glow scales with heat, so a hot sensor draws the eye.
    cairo_set_source_rgba(cr, r, g, b, 0.10 + 0.25 * fraction);
    cairo_arc(cr, cx, bulb_cy, bulb_radius + 4.0, 0, 2 * M_PI);
    cairo_fill(cr);
  }

  // Graduations down the side of the tube.
  cairo_set_source_rgba(cr, 0.49, 0.55, 0.64, 0.55);
  cairo_set_line_width(cr, 1.0);
  for (int i = 1; i < 5; i++) {
    double y = tube_top + tube_height * (i / 5.0);
    cairo_move_to(cr, cx + tube_width / 2.0 + 2.0, y);
    cairo_line_to(cr, cx + tube_width / 2.0 + 7.0, y);
  }
  cairo_stroke(cr);

  // Glass outline last so it sits over the mercury.
  cairo_set_source_rgb(cr, 0.14, 0.19, 0.25);
  cairo_set_line_width(cr, 1.4);
  rounded_rect(cr, cx - tube_width / 2.0, tube_top, tube_width, tube_height,
               tube_width / 2.0);
  cairo_stroke(cr);
  cairo_arc(cr, cx, bulb_cy, bulb_radius, 0, 2 * M_PI);
  cairo_stroke(cr);
}
