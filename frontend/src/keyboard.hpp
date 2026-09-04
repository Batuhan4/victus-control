#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include <gtk/gtk.h>
#include <string>
#include "socket.hpp"
#include <array>
#include <map>

class VictusKeyboardControl
{
public:
  GtkWidget *keyboard_page;

  VictusKeyboardControl(std::shared_ptr<VictusSocketClient> client);

  GtkWidget *get_page();

private:
  // Material-style on/off for the backlight, top right of the card.
  GtkWidget *power_switch;
  GtkWidget *color_button;
  GtkWidget *colour_row;   // only shown while the SOLID style is selected
  GdkRGBA current_single_color;
  GtkWidget *zone_selector;
  GtkLabel *current_color_label;
  GtkLabel *current_state_label;

  // New members for keyboard type detection and visualization
  std::string keyboard_type;  // "SINGLE_ZONE" or "FOUR_ZONE"
  GtkWidget *keyboard_visual; // DrawingArea for visual representation
  GtkWidget *zone_container;  // Container for zone controls
  GdkRGBA zone_colors[4];     // Current colors for each zone
  GtkColorDialog *zone_choosers[4];

  // Lighting effects (animated colour)
  // One radio per style. FLOW is only created on four-zone boards, where
  // there is geometry for the colour to travel across.
  GtkWidget *style_radios[4];
  int style_count;
  GtkWidget *effect_dropdown;
  GtkWidget *effect_speed_scale;
  GtkWidget *effect_speed_row;
  std::string current_effect;   // STATIC / RAINBOW / BREATHE / FLOW
  int current_effect_speed;
  // Drives the on-screen preview so the drawn keyboard animates along with the
  // real one. Runs locally rather than polling the backend every frame.
  guint preview_tick_id;
  double preview_phase;
  gint64 preview_last_frame_us;

  // Preset system
  GtkWidget *preset_dropdown;
  std::map<std::string, std::array<std::string, 4>> presets;

  // Interactive keyboard
  int hovered_zone;
  GtkGesture *click_gesture;

  // Saved colors for toggle restore
  GdkRGBA saved_zone_colors[4];

  bool keyboard_enabled;

  void update_keyboard_state(bool enabled);
  void update_keyboard_state_from_device();

  void update_keyboard_color(const GdkRGBA &color);

  // New methods
  void detect_keyboard_type();
  void build_ui_for_keyboard_type();
  void update_keyboard_visual();
  void load_presets();
  void build_effect_controls();
  void apply_current_effect();
  void refresh_effect_from_device();
  void set_effect_controls_sensitive(bool sensitive);
  // Shows the colour picker only for SOLID and the speed slider only for the
  // animated styles, so the card never offers a control that does nothing.
  void update_control_visibility();
  void start_preview_animation();
  void stop_preview_animation();
  void apply_preset(const std::string &preset_name);
  void apply_zone_color_immediately(int zone);
  void save_current_preset(const std::string &preset_name);
  void remove_preset(const std::string &preset_name);

  static void draw_keyboard_visual(GtkDrawingArea *area, cairo_t *cr, int width,
                                   int height, gpointer data);
  static gboolean on_keyboard_motion(GtkEventControllerMotion *controller,
                                     double x, double y, gpointer data);
  static void on_keyboard_click(GtkGestureClick *gesture, int n_press, double x,
                                double y, gpointer data);
  static void on_toggle_clicked(GtkWidget *widget, gpointer data);
  static void on_choose_color_clicked(GtkWidget *widget, gpointer data);
  static void on_apply_color_clicked(GtkWidget *widget, gpointer data);
  static void on_zone_color_changed(GtkColorButton *widget, gpointer data);
  static void on_preset_changed(GtkComboBoxText *widget, gpointer data);
  static void on_effect_changed(GObject *dropdown, GParamSpec *pspec,
                                gpointer data);
  static void on_style_toggled(GtkCheckButton *button, gpointer data);
  static void on_power_switched(GObject *sw, GParamSpec *pspec, gpointer data);
  static void on_effect_speed_changed(GtkRange *range, gpointer data);
  static gboolean on_preview_tick(gpointer data);
  static void on_save_preset_clicked(GtkWidget *widget, gpointer data);
  static void on_remove_preset_clicked(GtkWidget *widget, gpointer data);
  static void update_current_color_label(gpointer data);

  std::shared_ptr<VictusSocketClient> socket_client;
};

#endif
