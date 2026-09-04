#include "keyboard.hpp"
#include <algorithm>
#include <cmath>
#include <gtk/gtk.h>
#include <iostream>
#include <sstream>

namespace {

constexpr int kFourZoneCount = 4;
constexpr int kKeyboardRows = 4;
constexpr int kKeyboardColumns = 16;
constexpr int kKeyWidth = 24;
constexpr int kKeyHeight = 24;
constexpr int kKeySpacing = 4;

bool parse_rgb_triplet(const std::string &rgb_string, GdkRGBA *color) {
  std::stringstream ss(rgb_string);
  int red;
  int green;
  int blue;
  char extra;

  if (!(ss >> red >> green >> blue))
    return false;

  if (ss >> extra)
    return false;

  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
      blue > 255)
    return false;

  *color = {static_cast<float>(red / 255.0), static_cast<float>(green / 255.0), static_cast<float>(blue / 255.0), 1.0f};
  return true;
}

bool is_color_off(const GdkRGBA &color) {
  return color.red <= 0.01 && color.green <= 0.01 && color.blue <= 0.01;
}

bool any_zone_has_color(const GdkRGBA zone_colors[kFourZoneCount]) {
  for (int i = 0; i < kFourZoneCount; i++) {
    if (!is_color_off(zone_colors[i]))
      return true;
  }

  return false;
}

void get_keyboard_visual_origin(GtkWidget *widget, int *start_x, int *start_y) {
  const int total_width = kKeyboardColumns * (kKeyWidth + kKeySpacing) - kKeySpacing;
  const int total_height = kKeyboardRows * (kKeyHeight + kKeySpacing) - kKeySpacing;
  const int width = gtk_widget_get_width(widget);
  const int height = gtk_widget_get_height(widget);

  *start_x = (width - total_width) / 2;
  *start_y = (height - total_height) / 2;
}

} // namespace

VictusKeyboardControl::VictusKeyboardControl(
    std::shared_ptr<VictusSocketClient> client)
    : socket_client(client) {
  keyboard_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_top(keyboard_page, 20);
  gtk_widget_set_margin_bottom(keyboard_page, 20);
  gtk_widget_set_margin_start(keyboard_page, 20);
  gtk_widget_set_margin_end(keyboard_page, 20);

  // Initialize zone colors to white (will be overwritten after detecting
  // keyboard type)
  for (int i = 0; i < kFourZoneCount; i++) {
    zone_colors[i] = {1.0, 1.0, 1.0, 1.0};
    saved_zone_colors[i] = {1.0, 1.0, 1.0, 1.0};
  }

  keyboard_enabled = false;
  hovered_zone = -1;       // No zone hovered initially

  effect_speed_scale = nullptr;
  effect_speed_row = nullptr;
  current_effect = "STATIC";
  current_effect_speed = 50;
  preview_tick_id = 0;
  preview_phase = 0.0;
  preview_last_frame_us = 0;

  // Detect keyboard type first
  detect_keyboard_type();

  // Read initial zone colors from device
  if (keyboard_type == "FOUR_ZONE") {
    for (int i = 0; i < kFourZoneCount; i++) {
      auto color_future = socket_client->send_command_async(
          GET_KEYBOARD_ZONE_COLOR, std::to_string(i));
      std::string color_str = color_future.get();

      if (color_str.find("ERROR") == std::string::npos)
        parse_rgb_triplet(color_str, &zone_colors[i]);
    }

    keyboard_enabled = any_zone_has_color(zone_colors);
    if (keyboard_enabled) {
      for (int i = 0; i < kFourZoneCount; i++)
        saved_zone_colors[i] = zone_colors[i];
    }
  }

  // Build UI based on keyboard type
  build_ui_for_keyboard_type();

  // Update state from device
  update_keyboard_state_from_device();
  update_current_color_label(this);
  refresh_effect_from_device();
}

void VictusKeyboardControl::detect_keyboard_type() {
  auto type_future = socket_client->send_command_async(GET_KEYBOARD_TYPE);
  keyboard_type = type_future.get();

  if (keyboard_type.find("ERROR") != std::string::npos) {
    // Fallback to SINGLE_ZONE if error
    keyboard_type = "SINGLE_ZONE";
  }

  std::cout << "Detected keyboard type: " << keyboard_type << std::endl;
}

void VictusKeyboardControl::apply_zone_color_immediately(int zone) {
  if (zone < 0 || zone > 3)
    return;

  const GdkRGBA &color = zone_colors[zone];
  int r = (int)(color.red * 255);
  int g = (int)(color.green * 255);
  int b = (int)(color.blue * 255);

  auto value = std::to_string(zone) + " " + std::to_string(r) + " " +
               std::to_string(g) + " " + std::to_string(b);
  auto color_state =
      socket_client->send_command_async(SET_KEYBOARD_ZONE_COLOR, value);
  std::string result = color_state.get();
  if (result != "OK")
    std::cerr << "Failed to update keyboard zone " << zone
              << " color!: " << result << std::endl;
}

namespace {

GtkWidget *make_field_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_add_css_class(label, "field-label");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  return label;
}

} // namespace

void VictusKeyboardControl::build_ui_for_keyboard_type() {
  // Header: title on the left, backlight switch on the right.
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *header_icon =
      gtk_image_new_from_icon_name("input-keyboard-symbolic");
  gtk_widget_add_css_class(header_icon, "section-icon");
  GtkWidget *header_label = gtk_label_new("KEYBOARD LIGHTING");
  gtk_widget_add_css_class(header_label, "section-title");

  GtkWidget *header_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(header_spacer, TRUE);

  power_switch = gtk_switch_new();
  gtk_widget_set_valign(power_switch, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(power_switch, "power-switch");
  g_signal_connect(power_switch, "notify::active",
                   G_CALLBACK(on_power_switched), this);

  gtk_box_append(GTK_BOX(header), header_icon);
  gtk_box_append(GTK_BOX(header), header_label);
  gtk_box_append(GTK_BOX(header), header_spacer);
  gtk_box_append(GTK_BOX(header), power_switch);
  gtk_box_append(GTK_BOX(keyboard_page), header);

  // The preview is the centrepiece of the card, so it goes directly under the
  // header and gets the room to be read at a glance.
  keyboard_visual = gtk_drawing_area_new();
  gtk_widget_set_size_request(keyboard_visual, 500, 150);
  gtk_widget_set_halign(keyboard_visual, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(keyboard_visual),
                                 (GtkDrawingAreaDrawFunc)draw_keyboard_visual,
                                 this, nullptr);
  gtk_box_append(GTK_BOX(keyboard_page), keyboard_visual);

  current_single_color = {1.0f, 1.0f, 1.0f, 1.0f};

  if (keyboard_type == "FOUR_ZONE") {
    // Per-zone editing: hover to highlight, click to recolour that zone.
    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion",
                     G_CALLBACK(on_keyboard_motion), this);
    gtk_widget_add_controller(keyboard_visual, motion_controller);

    click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_keyboard_click),
                     this);
    gtk_widget_add_controller(keyboard_visual,
                              GTK_EVENT_CONTROLLER(click_gesture));

    GtkWidget *info_label =
        gtk_label_new("Click a zone on the preview to recolour it");
    gtk_widget_add_css_class(info_label, "status-line");
    gtk_widget_set_halign(info_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(keyboard_page), info_label);
  }

  // Style radios, speed slider and colour picker.
  build_effect_controls();

  // Status strip
  current_color_label = GTK_LABEL(gtk_label_new("Current Color: #000000"));
  current_state_label = GTK_LABEL(gtk_label_new(""));
  gtk_widget_add_css_class(GTK_WIDGET(current_color_label), "status-line");
  gtk_widget_add_css_class(GTK_WIDGET(current_state_label), "status-line");

  GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_halign(status_row, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(status_row), GTK_WIDGET(current_color_label));
  gtk_box_append(GTK_BOX(status_row), GTK_WIDGET(current_state_label));
  gtk_box_append(GTK_BOX(keyboard_page), status_row);
}

int get_zone_at_position(int row, int col) {
  // Zone 03 (WASD):  W=row0,col2  A=row1,col1  S=row1,col2  D=row1,col3
  if ((row == 1 && col == 2) || // W
      (row == 2 && col == 1) || // A
      (row == 2 && col == 2) || // S
      (row == 2 && col == 3)) { // D
    return 3;
  }
  // Zone 02 (Left side, excluding WASD): cols 0-4 except WASD
  else if (col <= 4) {
    return 2;
  }
  // Zone 01 (Middle): cols 5-11
  else if (col <= 11) {
    return 1;
  }
  // Zone 00 (Right side): cols 12+
  else {
    return 0;
  }
}

namespace {

void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                  double radius) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius,     radius, -G_PI / 2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, G_PI / 2);
  cairo_arc(cr, x + radius,     y + h - radius, radius, G_PI / 2, G_PI);
  cairo_arc(cr, x + radius,     y + radius,     radius, G_PI, 1.5 * G_PI);
  cairo_close_path(cr);
}

// Draws one key: an unlit dark cap, the backlight colour over it, and a few
// expanding translucent passes standing in for a glow (cairo has no blur).
// Glow strength follows the colour's brightness, so a dim colour does not
// bloom like a bright one.
void draw_key(cairo_t *cr, double x, double y, double w, double h,
              const GdkRGBA &color, bool lit) {
  const double radius = 4.0;
  double brightness = 0.299 * color.red + 0.587 * color.green + 0.114 * color.blue;

  if (lit && brightness > 0.02) {
    for (int pass = 3; pass >= 1; pass--) {
      double spread = pass * 2.5;
      double alpha = 0.055 * brightness * (4 - pass);
      cairo_set_source_rgba(cr, color.red, color.green, color.blue, alpha);
      rounded_rect(cr, x - spread, y - spread, w + spread * 2, h + spread * 2,
                   radius + spread);
      cairo_fill(cr);
    }
  }

  // Unlit key cap, so the keyboard still reads as a keyboard when it is off.
  cairo_set_source_rgb(cr, 0.10, 0.13, 0.18);
  rounded_rect(cr, x, y, w, h, radius);
  cairo_fill(cr);

  if (lit) {
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.92);
    rounded_rect(cr, x + 1, y + 1, w - 2, h - 2, radius - 1);
    cairo_fill(cr);
  }

  // Top bevel: a thin brighter edge that suggests a moulded key cap.
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, lit ? 0.22 : 0.06);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x + radius, y + 1.0);
  cairo_line_to(cr, x + w - radius, y + 1.0);
  cairo_stroke(cr);
}

} // namespace

void VictusKeyboardControl::draw_keyboard_visual(GtkDrawingArea *area,
                                                 cairo_t *cr, int width,
                                                 int height, gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  int total_width = kKeyboardColumns * (kKeyWidth + kKeySpacing) - kKeySpacing;
  int total_height = kKeyboardRows * (kKeyHeight + kKeySpacing) - kKeySpacing;
  int start_x = (width - total_width) / 2;
  int start_y = (height - total_height) / 2;

  // Chassis the keys sit in, so the panel reads as a laptop deck rather than
  // squares floating on the window background.
  const double pad = 12.0;
  cairo_set_source_rgb(cr, 0.043, 0.055, 0.075);
  rounded_rect(cr, start_x - pad, start_y - pad, total_width + pad * 2,
               total_height + pad * 2, 8.0);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.14, 0.19, 0.25, 1.0);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  for (int row = 0; row < kKeyboardRows; row++) {
    for (int col = 0; col < kKeyboardColumns; col++) {
      double x = start_x + col * (kKeyWidth + kKeySpacing);
      double y = start_y + row * (kKeyHeight + kKeySpacing);

      GdkRGBA color;
      int zone = -1;
      if (self->keyboard_type == "FOUR_ZONE") {
        zone = get_zone_at_position(row, col);
        color = self->zone_colors[zone];
      } else {
        color = self->current_single_color;
      }

      draw_key(cr, x, y, kKeyWidth, kKeyHeight, color, self->keyboard_enabled);

      // Outline the hovered zone so it is obvious which one a click will edit.
      if (zone >= 0 && zone == self->hovered_zone) {
        double luminance =
            0.299 * color.red + 0.587 * color.green + 0.114 * color.blue;
        if (luminance > 0.7)
          cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, 0.9);
        else
          cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
        cairo_set_line_width(cr, 2.0);
        rounded_rect(cr, x - 1, y - 1, kKeyWidth + 2, kKeyHeight + 2, 5.0);
        cairo_stroke(cr);
      }
    }
  }
}

gboolean
VictusKeyboardControl::on_keyboard_motion(GtkEventControllerMotion *controller,
                                          double x, double y, gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  // Calculate which zone the mouse is over
  int start_x;
  int start_y;
  get_keyboard_visual_origin(self->keyboard_visual, &start_x, &start_y);

  // Check if within keyboard bounds
  if (x < start_x || y < start_y) {
    if (self->hovered_zone != -1) {
      self->hovered_zone = -1;
      self->update_keyboard_visual();
    }
    return FALSE;
  }

  // Calculate row and column
  int col = (int)((x - start_x) / (kKeyWidth + kKeySpacing));
  int row = (int)((y - start_y) / (kKeyHeight + kKeySpacing));

  if (row >= 0 && row < kKeyboardRows && col >= 0 && col < kKeyboardColumns) {
    int zone = get_zone_at_position(row, col);
    if (zone != self->hovered_zone) {
      self->hovered_zone = zone;
      self->update_keyboard_visual();
    }
  } else {
    if (self->hovered_zone != -1) {
      self->hovered_zone = -1;
      self->update_keyboard_visual();
    }
  }

  return FALSE;
}

void VictusKeyboardControl::on_keyboard_click(GtkGestureClick *gesture,
                                              int n_press, double x, double y,
                                              gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  // Calculate which zone was clicked
  int start_x;
  int start_y;
  get_keyboard_visual_origin(self->keyboard_visual, &start_x, &start_y);

  int col = (int)((x - start_x) / (kKeyWidth + kKeySpacing));
  int row = (int)((y - start_y) / (kKeyHeight + kKeySpacing));

  if (row >= 0 && row < kKeyboardRows && col >= 0 && col < kKeyboardColumns) {
    int zone = get_zone_at_position(row, col);

    GtkWindow *toplevel = GTK_WINDOW(gtk_widget_get_root(self->keyboard_visual));

    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Choose Color for Zone");
    gtk_color_dialog_set_modal(dialog, TRUE);
    gtk_color_dialog_set_with_alpha(dialog, FALSE);

    const GdkRGBA &initial_color =
        self->keyboard_enabled ? self->zone_colors[zone]
                               : self->saved_zone_colors[zone];

    struct CallbackData {
      VictusKeyboardControl *self;
      int zone;
      GtkColorDialog *dialog;
    };
    auto *cb = new CallbackData{self, zone, dialog};

    gtk_color_dialog_choose_rgba(
        dialog, toplevel, &initial_color, nullptr,
        [](GObject *source, GAsyncResult *result, gpointer user_data) {
          auto *cb = static_cast<CallbackData *>(user_data);
          GError *error = nullptr;
          GdkRGBA *color = gtk_color_dialog_choose_rgba_finish(
              GTK_COLOR_DIALOG(source), result, &error);

          if (color) {
            if (!cb->self->keyboard_enabled) {
              cb->self->saved_zone_colors[cb->zone] = *color;
            } else {
              cb->self->zone_colors[cb->zone] = *color;
              cb->self->apply_zone_color_immediately(cb->zone);
              cb->self->update_keyboard_visual();
              VictusKeyboardControl::update_current_color_label(cb->self);
            }
            gdk_rgba_free(color);
          } else if (error && error->code != GTK_DIALOG_ERROR_DISMISSED) {
            std::cerr << "Color dialog error: " << error->message << std::endl;
          }

          if (error) g_error_free(error);
          g_object_unref(cb->dialog);
          delete cb;
        },
        cb);
  }
}

void VictusKeyboardControl::update_keyboard_visual() {
  gtk_widget_queue_draw(keyboard_visual);
}

GtkWidget *VictusKeyboardControl::get_page() { return keyboard_page; }

void VictusKeyboardControl::update_keyboard_state(bool enabled) {
  std::string command = enabled ? "255" : "0";
  auto result = socket_client->send_command_async(SET_KBD_BRIGHTNESS, command);

  if (result.get() != "OK") {
    std::cerr << "Failed to update keyboard state!" << std::endl;
    return;
  }

  update_keyboard_state_from_device();
}

void VictusKeyboardControl::update_keyboard_state_from_device() {
  auto keyboard_state = socket_client->send_command_async(GET_KBD_BRIGHTNESS);
  std::string szkeyboard_state = keyboard_state.get();

  if (szkeyboard_state.find("ERROR") == std::string::npos) {
    keyboard_enabled = (szkeyboard_state != "0");
    // The preview draws unlit key caps from keyboard_enabled, so it has to be
    // repainted when the backlight is switched on or off.
    if (keyboard_visual != nullptr)
      gtk_widget_queue_draw(keyboard_visual);

    // Reflect hardware state without the switch echoing it straight back.
    if (power_switch != nullptr) {
      g_signal_handlers_block_by_func(
          power_switch, (gpointer)G_CALLBACK(on_power_switched), this);
      gtk_switch_set_active(GTK_SWITCH(power_switch), keyboard_enabled);
      g_signal_handlers_unblock_by_func(
          power_switch, (gpointer)G_CALLBACK(on_power_switched), this);
    }

    if (current_state_label)
      gtk_label_set_text(
          current_state_label,
          ("Current State: " +
           (keyboard_enabled ? std::string("ON") : std::string("OFF")))
              .c_str());
  } else {
    std::cerr << "Failed to get current keyboard state!" << std::endl;
  }
}

void VictusKeyboardControl::update_keyboard_color(const GdkRGBA &color) {
  // auto color_string = gdk_rgba_to_string(&color);
  auto value = std::to_string((int)(color.red * 255)) + " " +
               std::to_string((int)(color.green * 255)) + " " +
               std::to_string((int)(color.blue * 255));
  auto color_state =
      socket_client->send_command_async(SET_KEYBOARD_COLOR, value);
  std::string result = color_state.get();

  if (result != "OK")
    std::cerr << "Failed to update keyboard color!: " << result << std::endl;
  else
    update_current_color_label(this);
}

void VictusKeyboardControl::on_choose_color_clicked(GtkWidget *widget,
                                                    gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);
  GtkWindow *toplevel = GTK_WINDOW(gtk_widget_get_root(widget));

  GtkColorDialog *dialog = gtk_color_dialog_new();
  gtk_color_dialog_set_title(dialog, "Choose Keyboard Color");
  gtk_color_dialog_set_modal(dialog, TRUE);
  gtk_color_dialog_set_with_alpha(dialog, FALSE);

  struct CB { VictusKeyboardControl *self; GtkColorDialog *dialog; GtkWidget *button; };
  auto *cb = new CB{self, dialog, widget};

  gtk_color_dialog_choose_rgba(
      dialog, toplevel, &self->current_single_color, nullptr,
      [](GObject *source, GAsyncResult *result, gpointer user_data) {
        auto *cb = static_cast<CB *>(user_data);
        GError *error = nullptr;
        GdkRGBA *color = gtk_color_dialog_choose_rgba_finish(
            GTK_COLOR_DIALOG(source), result, &error);

        if (color) {
          cb->self->current_single_color = *color;
          // Update button label to show chosen hex color
          char hex[8];
          g_snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                     (int)(color->red * 255),
                     (int)(color->green * 255),
                     (int)(color->blue * 255));
          gtk_button_set_label(GTK_BUTTON(cb->button), hex);
          // Applied on selection - the app has no Apply step.
          cb->self->update_keyboard_color(cb->self->current_single_color);
          cb->self->update_keyboard_visual();
          gdk_rgba_free(color);
        } else if (error && error->code != GTK_DIALOG_ERROR_DISMISSED) {
          std::cerr << "Color dialog error: " << error->message << std::endl;
        }

        if (error) g_error_free(error);
        g_object_unref(cb->dialog);
        delete cb;
      },
      cb);
}

void VictusKeyboardControl::update_current_color_label(gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  if (self->keyboard_type == "FOUR_ZONE") {
    std::string label = "Current Colors:";

    for (int i = 0; i < kFourZoneCount; i++) {
      auto zone_color = self->socket_client->send_command_async(
          GET_KEYBOARD_ZONE_COLOR, std::to_string(i));
      label += " Z" + std::to_string(i) + " " + zone_color.get();
    }

    gtk_label_set_text(self->current_color_label, label.c_str());
    return;
  }

  auto current_color =
      self->socket_client->send_command_async(GET_KEYBOARD_COLOR);
  std::string szcurrent_color = current_color.get();

  gtk_label_set_text(self->current_color_label,
                     ("Current Color: " + szcurrent_color).c_str());
}

// ---------------------------------------------------------------------------
// Animated lighting effects
//
// The backend owns the animation and drives the real keyboard. The controls
// here select the effect and its speed, and run a matching animation on the
// drawn keyboard so the preview shows what the hardware is doing without
// polling the backend every frame.
// ---------------------------------------------------------------------------

namespace {

// Cycle period at the slowest and fastest speed. Kept in step with the same
// constants in backend/src/effects.cpp so the preview runs at the rate the
// keyboard actually animates.
constexpr double kSlowestPeriodSec = 24.0;
constexpr double kFastestPeriodSec = 1.2;
constexpr int kPreviewFrameMs = 33;

// Zones left to right: far left, WASD, middle, right — the path the colour
// travels along in FLOW.
constexpr int kFlowOrder[kFourZoneCount] = {2, 3, 1, 0};

double effect_phase_step_per_second(int speed) {
  double clamped = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  double t = (clamped - 1.0) / 99.0;
  return 1.0 / (kSlowestPeriodSec + t * (kFastestPeriodSec - kSlowestPeriodSec));
}

GdkRGBA hsv_to_rgba(double hue, double saturation, double value) {
  hue -= std::floor(hue);

  double sector = hue * 6.0;
  int index = static_cast<int>(sector) % 6;
  double fraction = sector - std::floor(sector);

  double p = value * (1.0 - saturation);
  double q = value * (1.0 - saturation * fraction);
  double t = value * (1.0 - saturation * (1.0 - fraction));

  GdkRGBA rgba = {0.0f, 0.0f, 0.0f, 1.0f};
  switch (index) {
  case 0: rgba.red = value; rgba.green = t;     rgba.blue = p;     break;
  case 1: rgba.red = q;     rgba.green = value; rgba.blue = p;     break;
  case 2: rgba.red = p;     rgba.green = value; rgba.blue = t;     break;
  case 3: rgba.red = p;     rgba.green = q;     rgba.blue = value; break;
  case 4: rgba.red = t;     rgba.green = p;     rgba.blue = value; break;
  default: rgba.red = value; rgba.green = p;    rgba.blue = q;     break;
  }
  return rgba;
}

// Inverse of hsv_to_rgba for fully saturated colours: recovers the position in
// the hue cycle, so the preview can be started in step with the keyboard.
double rgba_to_hue(const GdkRGBA &color) {
  double r = color.red, g = color.green, b = color.blue;
  double max = std::max({r, g, b});
  double min = std::min({r, g, b});
  double delta = max - min;

  if (delta <= 0.0)
    return 0.0;

  double hue;
  if (max == r)
    hue = (g - b) / delta;
  else if (max == g)
    hue = 2.0 + (b - r) / delta;
  else
    hue = 4.0 + (r - g) / delta;

  hue /= 6.0;
  return hue - std::floor(hue);
}

} // namespace

void VictusKeyboardControl::build_effect_controls() {
  // FLOW needs zones for the colour to travel across, so it is only offered on
  // four-zone boards rather than shown as an option that degrades to RAINBOW.
  const bool has_zones = (keyboard_type == "FOUR_ZONE");
  style_count = has_zones ? 4 : 3;

  static const char *const kStyleLabels[4] = {"Solid", "Rainbow", "Breathe",
                                              "Flow"};

  GtkWidget *style_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_box_append(GTK_BOX(style_row), make_field_label("STYLE"));

  GtkWidget *first = nullptr;
  for (int i = 0; i < style_count; i++) {
    style_radios[i] = gtk_check_button_new_with_label(kStyleLabels[i]);
    gtk_widget_add_css_class(style_radios[i], "style-radio");

    // Grouping check buttons is what gives them radio behaviour in GTK4.
    if (first == nullptr) {
      first = style_radios[i];
      gtk_check_button_set_active(GTK_CHECK_BUTTON(style_radios[i]), TRUE);
    } else {
      gtk_check_button_set_group(GTK_CHECK_BUTTON(style_radios[i]),
                                 GTK_CHECK_BUTTON(first));
    }

    g_signal_connect(style_radios[i], "toggled",
                     G_CALLBACK(on_style_toggled), this);
    gtk_box_append(GTK_BOX(style_row), style_radios[i]);
  }
  for (int i = style_count; i < 4; i++)
    style_radios[i] = nullptr;

  gtk_box_append(GTK_BOX(keyboard_page), style_row);

  // Speed sits directly under the style choice, since it only qualifies the
  // animated styles.
  effect_speed_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(effect_speed_row), make_field_label("SPEED"));

  effect_speed_scale =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 100, 1);
  gtk_range_set_value(GTK_RANGE(effect_speed_scale), current_effect_speed);
  gtk_scale_set_draw_value(GTK_SCALE(effect_speed_scale), TRUE);
  gtk_widget_set_hexpand(effect_speed_scale, TRUE);
  g_signal_connect(effect_speed_scale, "value-changed",
                   G_CALLBACK(on_effect_speed_changed), this);
  gtk_box_append(GTK_BOX(effect_speed_row), effect_speed_scale);
  gtk_box_append(GTK_BOX(keyboard_page), effect_speed_row);

  // Colour only means anything for SOLID.
  colour_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(colour_row), make_field_label("COLOUR"));

  color_button = gtk_button_new_with_label("Choose Colour…");
  gtk_widget_set_hexpand(color_button, TRUE);
  g_signal_connect(color_button, "clicked",
                   G_CALLBACK(on_choose_color_clicked), this);
  gtk_box_append(GTK_BOX(colour_row), color_button);
  gtk_box_append(GTK_BOX(keyboard_page), colour_row);

  update_control_visibility();
}

void VictusKeyboardControl::update_control_visibility() {
  const bool solid = (current_effect == "STATIC");
  if (colour_row != nullptr)
    gtk_widget_set_visible(colour_row, solid);
  if (effect_speed_row != nullptr)
    gtk_widget_set_visible(effect_speed_row, !solid);
}

void VictusKeyboardControl::on_style_toggled(GtkCheckButton *button,
                                             gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  // "toggled" fires for the button being switched off as well; only act on the
  // one that is now active.
  if (!gtk_check_button_get_active(button))
    return;

  static const char *const kStyleCommands[4] = {"STATIC", "RAINBOW", "BREATHE",
                                                "FLOW"};
  for (int i = 0; i < self->style_count; i++) {
    if (self->style_radios[i] == GTK_WIDGET(button)) {
      self->current_effect = kStyleCommands[i];
      break;
    }
  }

  self->update_control_visibility();
  self->apply_current_effect();
}

void VictusKeyboardControl::on_power_switched(GObject *sw,
                                              GParamSpec * /*pspec*/,
                                              gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);
  self->update_keyboard_state(gtk_switch_get_active(GTK_SWITCH(sw)));
}

void VictusKeyboardControl::refresh_effect_from_device() {
  if (style_radios[0] == nullptr)
    return;

  auto effect_future = socket_client->send_command_async(GET_KBD_EFFECT);
  std::string reply = effect_future.get();
  if (reply.find("ERROR") != std::string::npos)
    return;

  std::istringstream stream(reply);
  std::string name;
  int speed = current_effect_speed;
  stream >> name >> speed;

  static const char *const kStyleCommands[4] = {"STATIC", "RAINBOW", "BREATHE",
                                                "FLOW"};
  int index = 0;
  for (int i = 0; i < style_count; i++) {
    if (name == kStyleCommands[i]) {
      index = i;
      break;
    }
  }

  current_effect = kStyleCommands[index];
  current_effect_speed = (speed < 1 || speed > 100) ? 50 : speed;

  // Reflect the backend's state without echoing it straight back at it.
  g_signal_handlers_block_by_func(
      style_radios[index], (gpointer)G_CALLBACK(on_style_toggled), this);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(style_radios[index]), TRUE);
  g_signal_handlers_unblock_by_func(
      style_radios[index], (gpointer)G_CALLBACK(on_style_toggled), this);

  g_signal_handlers_block_by_func(
      effect_speed_scale, (gpointer)G_CALLBACK(on_effect_speed_changed), this);
  gtk_range_set_value(GTK_RANGE(effect_speed_scale), current_effect_speed);
  g_signal_handlers_unblock_by_func(
      effect_speed_scale, (gpointer)G_CALLBACK(on_effect_speed_changed), this);

  update_control_visibility();
  if (current_effect != "STATIC")
    start_preview_animation();
}

void VictusKeyboardControl::apply_current_effect() {
  std::string argument =
      current_effect + " " + std::to_string(current_effect_speed);
  auto result_future =
      socket_client->send_command_async(SET_KBD_EFFECT, argument);
  std::string result = result_future.get();

  if (result.find("ERROR") != std::string::npos) {
    std::cerr << "Failed to set keyboard effect: " << result << std::endl;
    return;
  }

  bool animating = current_effect != "STATIC";
  update_control_visibility();

  if (animating) {
    start_preview_animation();
  } else {
    stop_preview_animation();
    // Fall back to whatever colours the keyboard settled on. Without re-reading
    // them the preview keeps the colour it was constructed with (white) while
    // the hardware sits on the last animated frame. On four-zone boards each
    // zone keeps its own last frame, so read them all rather than copying
    // zone 0 across the preview.
    if (keyboard_type == "FOUR_ZONE") {
      for (int zone = 0; zone < kFourZoneCount; zone++) {
        auto zone_future = socket_client->send_command_async(
            GET_KEYBOARD_ZONE_COLOR, std::to_string(zone));
        GdkRGBA settled;
        if (parse_rgb_triplet(zone_future.get(), &settled))
          zone_colors[zone] = settled;
      }
      current_single_color = zone_colors[0];
    } else {
      auto colour_future = socket_client->send_command_async(GET_KEYBOARD_COLOR);
      GdkRGBA settled;
      if (parse_rgb_triplet(colour_future.get(), &settled)) {
        current_single_color = settled;
        for (int zone = 0; zone < kFourZoneCount; zone++)
          zone_colors[zone] = settled;
      }
    }
    update_keyboard_state_from_device();
    update_current_color_label(this);
    gtk_widget_queue_draw(keyboard_visual);
  }
}

void VictusKeyboardControl::start_preview_animation() {
  if (preview_tick_id != 0)
    return;

  // The hue cycles map phase directly onto colour, so reading back what the
  // keyboard is showing puts the preview at the same point in the cycle rather
  // than starting from an arbitrary one. BREATHE's phase is a brightness ramp,
  // not a hue, so there is nothing to recover for it.
  if (current_effect == "RAINBOW" || current_effect == "FLOW") {
    auto color_future = socket_client->send_command_async(GET_KEYBOARD_COLOR);
    GdkRGBA shown;
    if (parse_rgb_triplet(color_future.get(), &shown)) {
      preview_phase = rgba_to_hue(shown);

      // GET_KEYBOARD_COLOR reports zone 0, which FLOW paints ahead of the
      // base phase by its position along the keyboard. Take that offset back
      // out, or the preview would apply it a second time and run half a
      // cycle away from the hardware.
      if (current_effect == "FLOW" && keyboard_type == "FOUR_ZONE") {
        for (int position = 0; position < kFourZoneCount; position++) {
          if (kFlowOrder[position] == 0) {
            preview_phase -= static_cast<double>(position) / kFourZoneCount;
            break;
          }
        }
        preview_phase -= std::floor(preview_phase);
      }
    }
  }

  preview_last_frame_us = g_get_monotonic_time();
  preview_tick_id = g_timeout_add(kPreviewFrameMs, on_preview_tick, this);
}

void VictusKeyboardControl::stop_preview_animation() {
  if (preview_tick_id == 0)
    return;

  g_source_remove(preview_tick_id);
  preview_tick_id = 0;
}

gboolean VictusKeyboardControl::on_preview_tick(gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  gint64 now_us = g_get_monotonic_time();
  double elapsed = (now_us - self->preview_last_frame_us) / 1000000.0;
  self->preview_last_frame_us = now_us;

  self->preview_phase +=
      elapsed * effect_phase_step_per_second(self->current_effect_speed);
  self->preview_phase -= std::floor(self->preview_phase);

  // While the backlight is off the drawn keyboard shows its off state; there is
  // nothing to preview until it is switched back on.
  if (!self->keyboard_enabled)
    return G_SOURCE_CONTINUE;

  double phase = self->preview_phase;

  if (self->current_effect == "BREATHE") {
    double level = 0.15 + 0.85 * (0.5 - 0.5 * std::cos(2.0 * M_PI * phase));
    GdkRGBA base = self->current_single_color;
    GdkRGBA faded = {static_cast<float>(base.red * level),
                     static_cast<float>(base.green * level),
                     static_cast<float>(base.blue * level), 1.0f};
    for (int zone = 0; zone < kFourZoneCount; zone++)
      self->zone_colors[zone] = faded;
    if (self->keyboard_type != "FOUR_ZONE")
      self->current_single_color = faded;
  } else if (self->current_effect == "FLOW" &&
             self->keyboard_type == "FOUR_ZONE") {
    for (int position = 0; position < kFourZoneCount; position++) {
      double zone_phase = phase + static_cast<double>(position) / kFourZoneCount;
      self->zone_colors[kFlowOrder[position]] = hsv_to_rgba(zone_phase, 1.0, 1.0);
    }
  } else {
    GdkRGBA color = hsv_to_rgba(phase, 1.0, 1.0);
    for (int zone = 0; zone < kFourZoneCount; zone++)
      self->zone_colors[zone] = color;
    self->current_single_color = color;
  }

  // The readout would otherwise show whatever colour was set when the effect
  // started, which reads as a bug next to an animating keyboard. Take it from
  // the frame just drawn rather than asking the backend 30 times a second.
  if (self->current_color_label != nullptr) {
    const GdkRGBA &shown = (self->keyboard_type == "FOUR_ZONE")
                               ? self->zone_colors[0]
                               : self->current_single_color;
    char buffer[64];
    g_snprintf(buffer, sizeof(buffer), "Current Color: %d %d %d",
               static_cast<int>(shown.red * 255.0 + 0.5),
               static_cast<int>(shown.green * 255.0 + 0.5),
               static_cast<int>(shown.blue * 255.0 + 0.5));
    gtk_label_set_text(self->current_color_label, buffer);
  }

  gtk_widget_queue_draw(self->keyboard_visual);
  return G_SOURCE_CONTINUE;
}

void VictusKeyboardControl::on_effect_speed_changed(GtkRange *range,
                                                    gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  self->current_effect_speed = static_cast<int>(gtk_range_get_value(range));
  if (self->current_effect == "STATIC")
    return;

  self->apply_current_effect();
}
