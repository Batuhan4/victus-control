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
  GtkWidget *toggle_button;
  GtkColorChooser *color_chooser;
  GtkWidget *zone_selector;
  GtkWidget *apply_button;
  GtkLabel *current_color_label;
  GtkLabel *current_state_label;

  // New members for keyboard type detection and visualization
  std::string keyboard_type;  // "SINGLE_ZONE" or "FOUR_ZONE"
  GtkWidget *keyboard_visual; // DrawingArea for visual representation
  GtkWidget *zone_container;  // Container for zone controls
  GdkRGBA zone_colors[4];     // Current colors for each zone
  GtkColorChooser
      *zone_choosers[4]; // Individual color choosers for 4-zone mode

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
  static void on_color_activated(GtkColorChooser *widget, gpointer data);
  static void on_zone_color_changed(GtkColorButton *widget, gpointer data);
  static void on_preset_changed(GtkComboBoxText *widget, gpointer data);
  static void on_save_preset_clicked(GtkWidget *widget, gpointer data);
  static void on_remove_preset_clicked(GtkWidget *widget, gpointer data);
  static void update_current_color_label(gpointer data);

  std::shared_ptr<VictusSocketClient> socket_client;
};

#endif