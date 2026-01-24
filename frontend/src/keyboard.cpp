#include "keyboard.hpp"
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
  for (int i = 0; i < 4; i++) {
    zone_colors[i] = {1.0, 1.0, 1.0, 1.0};
    zone_choosers[i] = nullptr;
    saved_zone_colors[i] = {1.0, 1.0, 1.0, 1.0};
  }

  keyboard_enabled = true; // Explicit initialization
  hovered_zone = -1;       // No zone hovered initially

  // Detect keyboard type first
  detect_keyboard_type();

  // Load presets
  load_presets();

  // Read initial zone colors from device
  if (keyboard_type == "FOUR_ZONE") {
    for (int i = 0; i < 4; i++) {
      auto color_future = socket_client->send_command_async(
          GET_KEYBOARD_ZONE_COLOR, std::to_string(i));
      std::string color_str = color_future.get();

      // Parse "R G B" format
      if (color_str.find("ERROR") == std::string::npos) {
        std::stringstream ss(color_str);
        int r, g, b;
        ss >> r >> g >> b;
        zone_colors[i] = {r / 255.0, g / 255.0, b / 255.0, 1.0};
      }
    }
    // Initialize saved colors with initial colors for toggle restore
    for (int i = 0; i < 4; i++) {
      saved_zone_colors[i] = zone_colors[i];
    }
  }

  // Build UI based on keyboard type
  build_ui_for_keyboard_type();

  // Update state from device
  update_keyboard_state_from_device();
  update_current_color_label(this);
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

void VictusKeyboardControl::load_presets() {
  // Create config directory if it doesn't exist
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw->pw_dir;
  }

  std::string config_dir = std::string(home) + "/.config/victus-control";
  mkdir(config_dir.c_str(), 0755);

  std::string preset_file = config_dir + "/presets.conf";

  // Create default presets file if it doesn't exist
  std::ifstream test(preset_file);
  if (!test.good()) {
    std::ofstream preset_out(preset_file);
    preset_out << "[Aqua]\n";
    preset_out << "zone00=00FFFF\n";
    preset_out << "zone01=00FFFF\n";
    preset_out << "zone02=00FFFF\n";
    preset_out << "zone03=00FFFF\n\n";

    preset_out << "[Fire]\n";
    preset_out << "zone00=FF0000\n";
    preset_out << "zone01=FF4400\n";
    preset_out << "zone02=FF8800\n";
    preset_out << "zone03=FFAA00\n\n";

    preset_out << "[Matrix]\n";
    preset_out << "zone00=00FF00\n";
    preset_out << "zone01=00FF00\n";
    preset_out << "zone02=00FF00\n";
    preset_out << "zone03=00FF00\n\n";

    preset_out << "[Ocean]\n";
    preset_out << "zone00=0066FF\n";
    preset_out << "zone01=0088FF\n";
    preset_out << "zone02=00AAFF\n";
    preset_out << "zone03=00CCFF\n\n";

    preset_out << "[Sunset]\n";
    preset_out << "zone00=FF1493\n";
    preset_out << "zone01=FF6347\n";
    preset_out << "zone02=FFD700\n";
    preset_out << "zone03=FFA500\n\n";

    preset_out << "[Purple Haze]\n";
    preset_out << "zone00=9370DB\n";
    preset_out << "zone01=8A2BE2\n";
    preset_out << "zone02=9932CC\n";
    preset_out << "zone03=BA55D3\n";

    preset_out.close();
  }

  // Load presets
  std::ifstream preset_in(preset_file);
  std::string line, current_preset;

  while (std::getline(preset_in, line)) {
    // Skip empty lines
    if (line.empty())
      continue;

    // Check for preset name [PresetName]
    if (line[0] == '[' && line[line.length() - 1] == ']') {
      current_preset = line.substr(1, line.length() - 2);
      presets[current_preset] = {"FFFFFF", "FFFFFF", "FFFFFF", "FFFFFF"};
    }
    // Parse zone values
    else if (!current_preset.empty() && line.find('=') != std::string::npos) {
      size_t eq_pos = line.find('=');
      std::string key = line.substr(0, eq_pos);
      std::string value = line.substr(eq_pos + 1);

      if (key == "zone00")
        presets[current_preset][0] = value;
      else if (key == "zone01")
        presets[current_preset][1] = value;
      else if (key == "zone02")
        presets[current_preset][2] = value;
      else if (key == "zone03")
        presets[current_preset][3] = value;
    }
  }
}

void VictusKeyboardControl::apply_preset(const std::string &preset_name) {
  if (presets.find(preset_name) == presets.end()) {
    std::cerr << "Preset '" << preset_name << "' not found!" << std::endl;
    return;
  }

  auto &preset = presets[preset_name];

  // Convert hex to GdkRGBA and apply to all zones
  for (int i = 0; i < 4; i++) {
    std::string hex = preset[i];
    int r, g, b;
    std::stringstream ss;
    ss << std::hex << hex.substr(0, 2);
    ss >> r;
    ss.clear();
    ss << std::hex << hex.substr(2, 2);
    ss >> g;
    ss.clear();
    ss << std::hex << hex.substr(4, 2);
    ss >> b;

    zone_colors[i] = {r / 255.0, g / 255.0, b / 255.0, 1.0};

    // Apply immediately
    apply_zone_color_immediately(i);
  }

  update_keyboard_visual();
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

void VictusKeyboardControl::build_ui_for_keyboard_type() {
  // Toggle button (common for both types)
  toggle_button = gtk_button_new_with_label("Keyboard: OFF");
  gtk_box_append(GTK_BOX(keyboard_page), toggle_button);
  g_signal_connect(toggle_button, "clicked", G_CALLBACK(on_toggle_clicked),
                   this);

  // Build different UI based on keyboard type
  if (keyboard_type == "FOUR_ZONE") {
    // Preset dropdown
    preset_dropdown = gtk_combo_box_text_new();
    for (const auto &preset : presets) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(preset_dropdown),
                                     preset.first.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(preset_dropdown), 0);

    GtkWidget *preset_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *preset_label = gtk_label_new("Preset:");
    gtk_box_append(GTK_BOX(preset_box), preset_label);
    gtk_box_append(GTK_BOX(preset_box), preset_dropdown);
    gtk_box_append(GTK_BOX(keyboard_page), preset_box);

    g_signal_connect(preset_dropdown, "changed", G_CALLBACK(on_preset_changed),
                     this);

    // Save and Remove preset buttons
    GtkWidget *save_button = gtk_button_new_with_label("Save Preset");
    GtkWidget *remove_button = gtk_button_new_with_label("Remove Preset");
    gtk_box_append(GTK_BOX(preset_box), save_button);
    gtk_box_append(GTK_BOX(preset_box), remove_button);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_preset_clicked),
                     this);
    g_signal_connect(remove_button, "clicked",
                     G_CALLBACK(on_remove_preset_clicked), this);

    // Interactive virtual keyboard visualization
    keyboard_visual = gtk_drawing_area_new();
    gtk_widget_set_size_request(keyboard_visual, 400, 120);
    gtk_widget_set_halign(keyboard_visual,
                          GTK_ALIGN_CENTER); // Center horizontally
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(keyboard_visual),
                                   (GtkDrawingAreaDrawFunc)draw_keyboard_visual,
                                   this, nullptr);
    gtk_box_append(GTK_BOX(keyboard_page), keyboard_visual);

    // Add motion controller for hover effects
    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion",
                     G_CALLBACK(on_keyboard_motion), this);
    gtk_widget_add_controller(keyboard_visual, motion_controller);

    // Add click gesture for zone selection
    click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_keyboard_click),
                     this);
    gtk_widget_add_controller(keyboard_visual,
                              GTK_EVENT_CONTROLLER(click_gesture));

    // Info label
    GtkWidget *info_label =
        gtk_label_new("Hover over zones to highlight, click to change color");
    gtk_box_append(GTK_BOX(keyboard_page), info_label);

  } else {
    // SINGLE_ZONE mode - simple color chooser
    keyboard_visual = gtk_drawing_area_new();
    gtk_widget_set_size_request(keyboard_visual, 400, 120);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(keyboard_visual),
                                   (GtkDrawingAreaDrawFunc)draw_keyboard_visual,
                                   this, nullptr);
    gtk_box_append(GTK_BOX(keyboard_page), keyboard_visual);

    color_chooser = GTK_COLOR_CHOOSER(gtk_color_chooser_widget_new());
    gtk_box_append(GTK_BOX(keyboard_page), GTK_WIDGET(color_chooser));

    g_signal_connect(color_chooser, "color-activated",
                     G_CALLBACK(on_color_activated), this);
  }

  // Status labels (common for both types)
  current_color_label = GTK_LABEL(gtk_label_new("Current Color: #000000"));
  current_state_label = GTK_LABEL(gtk_label_new("Current State: OFF"));
  gtk_box_append(GTK_BOX(keyboard_page), GTK_WIDGET(current_state_label));
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

void VictusKeyboardControl::draw_keyboard_visual(GtkDrawingArea *area,
                                                 cairo_t *cr, int width,
                                                 int height, gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  // No background - let the window theme show through

  // Define keyboard layout - simplified representation
  int key_width = 20;
  int key_height = 20;
  int spacing = 3;

  // Center the keyboard within the drawing area
  int total_width = 16 * (key_width + spacing) - spacing;
  int total_height = 4 * (key_height + spacing) - spacing;
  int start_x = (width - total_width) / 2;
  int start_y = (height - total_height) / 2;

  if (self->keyboard_type == "FOUR_ZONE") {
    // Draw keyboard row by row with hover highlights
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 16; col++) {
        int x = start_x + col * (key_width + spacing);
        int y = start_y + row * (key_height + spacing);

        // Determine zone for this key
        int zone = get_zone_at_position(row, col);

        // Draw key with zone color
        cairo_set_source_rgb(cr, self->zone_colors[zone].red,
                             self->zone_colors[zone].green,
                             self->zone_colors[zone].blue);
        cairo_rectangle(cr, x, y, key_width, key_height);
        cairo_fill(cr);

        // Draw smart contrast border if this zone is hovered
        if (zone == self->hovered_zone) {
          // Calculate luminance to determine border color
          double luminance = 0.299 * self->zone_colors[zone].red +
                             0.587 * self->zone_colors[zone].green +
                             0.114 * self->zone_colors[zone].blue;
          // Use dark border if color is light, white border otherwise
          if (luminance > 0.7) {
            cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Dark gray
          } else {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // White
          }
          cairo_set_line_width(cr, 2);
          cairo_rectangle(cr, x, y, key_width, key_height);
          cairo_stroke(cr);
        }
      }
    }

  } else {
    // SINGLE_ZONE - all keys same color
    GdkRGBA color;
    if (self->color_chooser) {
      gtk_color_chooser_get_rgba(self->color_chooser, &color);
    } else {
      color = {1.0, 1.0, 1.0, 1.0};
    }

    cairo_set_source_rgb(cr, color.red, color.green, color.blue);
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 16; col++) {
        int x = start_x + col * (key_width + spacing);
        int y = start_y + row * (key_height + spacing);
        cairo_rectangle(cr, x, y, key_width, key_height);
        cairo_fill(cr);
      }
    }
  }
}

gboolean
VictusKeyboardControl::on_keyboard_motion(GtkEventControllerMotion *controller,
                                          double x, double y, gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  // Calculate which zone the mouse is over
  int key_width = 20;
  int key_height = 20;
  int spacing = 3;
  int start_x = 10;
  int start_y = 10;

  // Check if within keyboard bounds
  if (x < start_x || y < start_y) {
    if (self->hovered_zone != -1) {
      self->hovered_zone = -1;
      self->update_keyboard_visual();
    }
    return FALSE;
  }

  // Calculate row and column
  int col = (int)((x - start_x) / (key_width + spacing));
  int row = (int)((y - start_y) / (key_height + spacing));

  if (row >= 0 && row < 4 && col >= 0 && col < 16) {
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
  int key_width = 20;
  int key_height = 20;
  int spacing = 3;
  int start_x = 10;
  int start_y = 10;

  int col = (int)((x - start_x) / (key_width + spacing));
  int row = (int)((y - start_y) / (key_height + spacing));

  if (row >= 0 && row < 4 && col >= 0 && col < 16) {
    int zone = get_zone_at_position(row, col);

    // Open color chooser dialog for this zone
    GtkWidget *toplevel =
        GTK_WIDGET(gtk_widget_get_root(self->keyboard_visual));
    GtkWidget *dialog = gtk_color_chooser_dialog_new("Choose Color for Zone",
                                                     GTK_WINDOW(toplevel));

    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog),
                               &self->zone_colors[zone]);

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    // Use async dialog response
    g_object_set_data(G_OBJECT(dialog), "zone-index", GINT_TO_POINTER(zone));
    g_object_set_data(G_OBJECT(dialog), "self-ptr", self);

    g_signal_connect(
        dialog, "response",
        G_CALLBACK(+[](GtkDialog *dialog, int response, gpointer user_data) {
          if (response == GTK_RESPONSE_OK) {
            VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(
                g_object_get_data(G_OBJECT(dialog), "self-ptr"));
            int zone = GPOINTER_TO_INT(
                g_object_get_data(G_OBJECT(dialog), "zone-index"));

            GdkRGBA color;
            gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);

            self->zone_colors[zone] = color;
            self->apply_zone_color_immediately(zone);
            self->update_keyboard_visual();
          }
          gtk_window_destroy(GTK_WINDOW(dialog));
        }),
        nullptr);

    gtk_widget_show(dialog);
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
    // For single-zone keyboards, use brightness to determine state
    // For 4-zone keyboards, we control on/off via zone colors, so don't
    // override
    if (keyboard_type != "FOUR_ZONE") {
      keyboard_enabled = (szkeyboard_state == "255");
    }
    gtk_button_set_label(GTK_BUTTON(toggle_button),
                         keyboard_enabled ? "Keyboard: ON" : "Keyboard: OFF");

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
}

void VictusKeyboardControl::on_toggle_clicked(GtkWidget *widget,
                                              gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  std::cout << "Toggle clicked. Current state: "
            << (self->keyboard_enabled ? "ON" : "OFF") << std::endl;
  self->keyboard_enabled = !self->keyboard_enabled;
  std::cout << "New state: " << (self->keyboard_enabled ? "ON" : "OFF")
            << std::endl;

  if (self->keyboard_type == "FOUR_ZONE") {
    if (!self->keyboard_enabled) {
      std::cout << "Turning OFF - saving colors and setting to black"
                << std::endl;
      // Save current colors before turning off (only if not already black)
      bool has_color = false;
      for (int i = 0; i < 4; i++) {
        if (self->zone_colors[i].red > 0.01 ||
            self->zone_colors[i].green > 0.01 ||
            self->zone_colors[i].blue > 0.01) {
          has_color = true;
          break;
        }
      }
      if (has_color) {
        for (int i = 0; i < 4; i++) {
          self->saved_zone_colors[i] = self->zone_colors[i];
        }
      }
      // Set all zones to black
      for (int i = 0; i < 4; i++) {
        self->zone_colors[i] = {0.0f, 0.0f, 0.0f, 1.0f};
        self->apply_zone_color_immediately(i);
      }
    } else {
      // Restore saved colors
      std::cout << "Restoring saved colors..." << std::endl;
      for (int i = 0; i < 4; i++) {
        std::cout << "Zone " << i << ": R=" << self->saved_zone_colors[i].red
                  << " G=" << self->saved_zone_colors[i].green
                  << " B=" << self->saved_zone_colors[i].blue << std::endl;
        self->zone_colors[i] = self->saved_zone_colors[i];
        self->apply_zone_color_immediately(i);
      }
    }
    self->update_keyboard_visual();
  }

  self->update_keyboard_state(self->keyboard_enabled);
}

void VictusKeyboardControl::on_color_activated(GtkColorChooser *widget,
                                               gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  GdkRGBA color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(self->color_chooser), &color);

  self->update_keyboard_color(color);
  self->update_keyboard_visual();
}

void VictusKeyboardControl::on_zone_color_changed(GtkColorButton *widget,
                                                  gpointer data) {
  // This callback is no longer needed since we use the interactive keyboard
  // Left for compatibility
}

void VictusKeyboardControl::on_preset_changed(GtkComboBoxText *widget,
                                              gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  gchar *preset_name = gtk_combo_box_text_get_active_text(widget);
  if (preset_name) {
    self->apply_preset(std::string(preset_name));
    g_free(preset_name);
  }
}

void VictusKeyboardControl::update_current_color_label(gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  auto current_color =
      self->socket_client->send_command_async(GET_KEYBOARD_COLOR);
  std::string szcurrent_color = current_color.get();

  gtk_label_set_text(self->current_color_label,
                     ("Current Color: " + szcurrent_color).c_str());
}

void VictusKeyboardControl::save_current_preset(
    const std::string &preset_name) {
  // Add preset to map
  presets[preset_name] = {"", "", "", ""};

  // Convert zone colors to hex
  for (int i = 0; i < 4; i++) {
    int r = (int)(zone_colors[i].red * 255);
    int g = (int)(zone_colors[i].green * 255);
    int b = (int)(zone_colors[i].blue * 255);

    char hex[7];
    snprintf(hex, sizeof(hex), "%02X%02X%02X", r, g, b);
    presets[preset_name][i] = hex;
  }

  // Save to file
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw->pw_dir;
  }

  std::string config_dir = std::string(home) + "/.config/victus-control";
  std::string preset_file = config_dir + "/presets.conf";

  std::ofstream out(preset_file);
  for (const auto &preset : presets) {
    out << "[" << preset.first << "]\n";
    for (int i = 0; i < 4; i++) {
      out << "zone0" << i << "=" << preset.second[i] << "\n";
    }
    out << "\n";
  }
  out.close();

  // Add to dropdown
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(preset_dropdown),
                                 preset_name.c_str());
}

void VictusKeyboardControl::remove_preset(const std::string &preset_name) {
  // Remove from map
  presets.erase(preset_name);

  // Save to file
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw->pw_dir;
  }

  std::string config_dir = std::string(home) + "/.config/victus-control";
  std::string preset_file = config_dir + "/presets.conf";

  std::ofstream out(preset_file);
  for (const auto &preset : presets) {
    out << "[" << preset.first << "]\n";
    for (int i = 0; i < 4; i++) {
      out << "zone0" << i << "=" << preset.second[i] << "\n";
    }
    out << "\n";
  }
  out.close();

  // Rebuild dropdown
  gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(preset_dropdown));
  for (const auto &preset : presets) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(preset_dropdown),
                                   preset.first.c_str());
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(preset_dropdown), 0);
}

void VictusKeyboardControl::on_save_preset_clicked(GtkWidget *widget,
                                                   gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(widget));
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Save Preset", GTK_WINDOW(toplevel), GTK_DIALOG_MODAL, "Save",
      GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);

  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter preset name");
  gtk_box_append(GTK_BOX(content_area), entry);

  g_object_set_data(G_OBJECT(dialog), "entry-ptr", entry);
  g_object_set_data(G_OBJECT(dialog), "self-ptr", self);

  g_signal_connect(
      dialog, "response",
      G_CALLBACK(+[](GtkDialog *dialog, int response, gpointer) {
        if (response == GTK_RESPONSE_OK) {
          VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(
              g_object_get_data(G_OBJECT(dialog), "self-ptr"));
          GtkEntry *entry =
              GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "entry-ptr"));
          const char *name = gtk_editable_get_text(GTK_EDITABLE(entry));
          if (name && strlen(name) > 0) {
            self->save_current_preset(std::string(name));
          }
        }
        gtk_window_destroy(GTK_WINDOW(dialog));
      }),
      nullptr);

  gtk_window_present(GTK_WINDOW(dialog));
}

void VictusKeyboardControl::on_remove_preset_clicked(GtkWidget *widget,
                                                     gpointer data) {
  VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

  gchar *preset_name = gtk_combo_box_text_get_active_text(
      GTK_COMBO_BOX_TEXT(self->preset_dropdown));
  if (preset_name) {
    self->remove_preset(std::string(preset_name));
    g_free(preset_name);
  }
}