#include "fan.hpp"
#include "socket.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

// Constants for manual fan control
const int MIN_RPM = 2000;
const int FAN1_MAX_RPM = 5800;
const int FAN2_MAX_RPM = 6100;
const int RPM_STEPS = 8;

VictusFanControl::VictusFanControl(std::shared_ptr<VictusSocketClient> client) : socket_client(client)
{
    fan_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(fan_page, 20);
    gtk_widget_set_margin_bottom(fan_page, 20);
    gtk_widget_set_margin_start(fan_page, 20);
    gtk_widget_set_margin_end(fan_page, 20);

    // --- Mode Selector ---
    mode_selector = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "AUTO", "AUTO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "BETTER_AUTO", "Better Auto");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MANUAL", "MANUAL");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MAX", "MAX");
    g_signal_connect(mode_selector, "changed", G_CALLBACK(on_mode_changed), this);
    gtk_box_append(GTK_BOX(fan_page), mode_selector);

    // --- Speed Slider ---
    slider_label = gtk_label_new("Manual Speed Control (1-8)");
    gtk_widget_set_halign(slider_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan_page), slider_label);

    speed_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, RPM_STEPS, 1);
    gtk_scale_set_draw_value(GTK_SCALE(speed_slider), TRUE);
    g_signal_connect(speed_slider, "value-changed", G_CALLBACK(on_speed_slider_changed), this);
    gtk_box_append(GTK_BOX(fan_page), speed_slider);

    // --- Status Labels ---
    state_label = gtk_label_new("Current State: N/A");
    gtk_widget_set_halign(state_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan_page), state_label);

    fan1_speed_label = gtk_label_new("Fan 1 Speed: N/A RPM");
    gtk_widget_set_halign(fan1_speed_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan_page), fan1_speed_label);

    fan2_speed_label = gtk_label_new("Fan 2 Speed: N/A RPM");
    gtk_widget_set_halign(fan2_speed_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan_page), fan2_speed_label);

    // Initial UI state update
    update_ui_from_system_state();
    update_fan_speeds();

    // Set up a timer to periodically update fan speeds
    g_timeout_add_seconds(2, [](gpointer data) -> gboolean {
        static_cast<VictusFanControl*>(data)->update_fan_speeds();
        return G_SOURCE_CONTINUE;
    }, this);
}

GtkWidget* VictusFanControl::get_page()
{
    return fan_page;
}

void VictusFanControl::update_ui_from_system_state()
{
    auto response = socket_client->send_command_async(GET_FAN_MODE);
    std::string fan_mode = response.get();

    if (fan_mode.find("ERROR") != std::string::npos) {
        fan_mode = "AUTO"; // Default to AUTO on error
        std::cerr << "Failed to get fan mode, defaulting to AUTO." << std::endl;
    }

    gtk_label_set_text(GTK_LABEL(state_label), ("Current State: " + fan_mode).c_str());

    if (fan_mode == "MANUAL") {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode_selector), "MANUAL");
        gtk_widget_set_sensitive(speed_slider, TRUE);
        gtk_widget_set_sensitive(slider_label, TRUE);
    } else if (fan_mode == "BETTER_AUTO") {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode_selector), "BETTER_AUTO");
        gtk_widget_set_sensitive(speed_slider, FALSE);
        gtk_widget_set_sensitive(slider_label, FALSE);
    } else if (fan_mode == "MAX") {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode_selector), "MAX");
        gtk_widget_set_sensitive(speed_slider, FALSE);
        gtk_widget_set_sensitive(slider_label, FALSE);
    } else { // AUTO
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode_selector), "AUTO");
        gtk_widget_set_sensitive(speed_slider, FALSE);
        gtk_widget_set_sensitive(slider_label, FALSE);
    }
}

void VictusFanControl::update_fan_speeds()
{
    auto response1 = socket_client->send_command_async(GET_FAN_SPEED, "1");
    std::string fan1_speed = response1.get();
    if (fan1_speed.find("ERROR") != std::string::npos) fan1_speed = "N/A";

    auto response2 = socket_client->send_command_async(GET_FAN_SPEED, "2");
    std::string fan2_speed = response2.get();
    if (fan2_speed.find("ERROR") != std::string::npos) fan2_speed = "N/A";

    gtk_label_set_text(GTK_LABEL(fan1_speed_label), ("Fan 1 Speed: " + fan1_speed + " RPM").c_str());
    gtk_label_set_text(GTK_LABEL(fan2_speed_label), ("Fan 2 Speed: " + fan2_speed + " RPM").c_str());
}

void VictusFanControl::set_fan_rpm(int level)
{
    if (level < 1 || level > RPM_STEPS) return;

    auto compute_rpm = [](int lvl, int max_rpm) {
        if (RPM_STEPS <= 1) {
            return max_rpm;
        }
        double step = static_cast<double>(max_rpm - MIN_RPM) / static_cast<double>(RPM_STEPS - 1);
        double value = static_cast<double>(MIN_RPM) + static_cast<double>(lvl - 1) * step;
        int rpm = static_cast<int>(std::round(value));
        rpm = std::clamp(rpm, MIN_RPM, max_rpm);
        return rpm;
    };

    int fan1_rpm = compute_rpm(level, FAN1_MAX_RPM);
    int fan2_rpm = compute_rpm(level, FAN2_MAX_RPM);

    std::string fan1_rpm_str = std::to_string(fan1_rpm);
    std::string fan2_rpm_str = std::to_string(fan2_rpm);
    unsigned long long generation =
        manual_request_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Apply fan 1 immediately, but only let the newest request schedule fan 2
    // after the firmware-required delay.
    std::thread([this, fan1_rpm_str, fan2_rpm_str, generation]() {
        auto fan1_result =
            socket_client->send_command_async(SET_FAN_SPEED,
                                              "1 " + fan1_rpm_str)
                .get();
        if (fan1_result != "OK") {
            std::cerr << "Failed to set fan 1 speed: " << fan1_result
                      << std::endl;
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));

        if (manual_request_generation.load(std::memory_order_acquire) !=
            generation) {
            return;
        }

        auto fan2_result =
            socket_client->send_command_async(SET_FAN_SPEED,
                                              "2 " + fan2_rpm_str)
                .get();
        if (fan2_result != "OK") {
            std::cerr << "Failed to set fan 2 speed: " << fan2_result
                      << std::endl;
        }
    }).detach();
}

void VictusFanControl::on_mode_changed(GtkComboBox *widget, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl*>(data);
    gchar *mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

    if (mode) {
        std::string mode_str(mode);
        
        // Send the mode command and wait for it to complete.
        auto result = self->socket_client->send_command_async(SET_FAN_MODE, mode_str).get();

        if (result == "OK") {
            // If we are entering manual mode, now we can safely set the fan speed.
            if (mode_str == "MANUAL") {
                int level = static_cast<int>(gtk_range_get_value(GTK_RANGE(self->speed_slider)));
                self->set_fan_rpm(level);
            } else if (mode_str == "BETTER_AUTO") {
                gtk_widget_set_sensitive(self->speed_slider, FALSE);
                gtk_widget_set_sensitive(self->slider_label, FALSE);
            }
        } else {
            std::cerr << "Failed to set fan mode: " << result << std::endl;
        }
        
        g_free(mode);
        
        // After all commands are sent, update the UI to reflect the final state.
        self->update_ui_from_system_state();
    }
}

void VictusFanControl::on_speed_slider_changed(GtkRange *range, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl*>(data);
    const char *active_id =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(self->mode_selector));
    if (!active_id || std::string(active_id) != "MANUAL") {
        return;
    }

    int level = static_cast<int>(gtk_range_get_value(range));
    self->set_fan_rpm(level);
}
