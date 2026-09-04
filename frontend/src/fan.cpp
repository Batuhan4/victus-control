#include "fan.hpp"
#include "gauges.hpp"
#include "socket.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <memory>

namespace {
// Carries the strings produced by the off-thread refresh back to the GTK main
// thread, where the label writes must happen.
struct FanLabelUpdate {
    VictusFanControl *self;
    std::string fan1;
    std::string fan2;
    std::string cpu;
    std::string gpu;
};
} // namespace

// Constants for manual fan control
const int MIN_RPM = 2000;
const int FAN1_MAX_RPM = 5800;
const int FAN2_MAX_RPM = 6100;
const int RPM_STEPS = 8;

namespace {

// Temperature readouts change colour as they climb, so a hot machine is
// obvious without reading the number.
void apply_temperature_class(GtkWidget *label, const std::string &value)
{
    gtk_widget_remove_css_class(label, "warn");
    gtk_widget_remove_css_class(label, "hot");

    try {
        int degrees = std::stoi(value);
        if (degrees >= 85)
            gtk_widget_add_css_class(label, "hot");
        else if (degrees >= 70)
            gtk_widget_add_css_class(label, "warn");
    } catch (...) {
        // "idle" or "N/A": leave it in the default accent colour.
    }
}

// One telemetry dial: caption, the analog gauge, then the digital value under
// it, so the shape gives the impression and the number gives the detail.
GtkWidget *make_gauge_tile(const char *caption, const char *unit,
                           const char *icon_name, int gauge_width,
                           int gauge_height, GtkDrawingAreaDrawFunc draw_func,
                           gpointer draw_data, GtkWidget **gauge_out,
                           GtkWidget **value_out)
{
    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(tile, TRUE);
    gtk_widget_set_halign(tile, GTK_ALIGN_CENTER);

    GtkWidget *caption_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *caption_icon = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(caption_icon, "tile-icon");
    GtkWidget *caption_label = gtk_label_new(caption);
    gtk_widget_add_css_class(caption_label, "field-label");
    gtk_box_append(GTK_BOX(caption_row), caption_icon);
    gtk_box_append(GTK_BOX(caption_row), caption_label);
    gtk_widget_set_halign(caption_row, GTK_ALIGN_CENTER);

    GtkWidget *gauge = gtk_drawing_area_new();
    gtk_widget_set_size_request(gauge, gauge_width, gauge_height);
    gtk_widget_set_halign(gauge, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(gauge), draw_func,
                                   draw_data, nullptr);

    GtkWidget *value_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(value_row, GTK_ALIGN_CENTER);
    GtkWidget *value = gtk_label_new("--");
    gtk_widget_add_css_class(value, "readout");
    GtkWidget *unit_label = gtk_label_new(unit);
    gtk_widget_add_css_class(unit_label, "readout-unit");
    gtk_widget_set_valign(unit_label, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(unit_label, 4);
    gtk_box_append(GTK_BOX(value_row), value);
    gtk_box_append(GTK_BOX(value_row), unit_label);

    gtk_box_append(GTK_BOX(tile), caption_row);
    gtk_box_append(GTK_BOX(tile), gauge);
    gtk_box_append(GTK_BOX(tile), value_row);

    *gauge_out = gauge;
    *value_out = value;
    return tile;
}

} // namespace

VictusFanControl::VictusFanControl(std::shared_ptr<VictusSocketClient> client) : socket_client(client)
{
    fan_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(fan_page, 20);
    gtk_widget_set_margin_bottom(fan_page, 20);
    gtk_widget_set_margin_start(fan_page, 20);
    gtk_widget_set_margin_end(fan_page, 20);

    // --- Header: title left, cooling profile right ---
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *header_icon = gtk_image_new_from_icon_name("weather-windy-symbolic");
    gtk_widget_add_css_class(header_icon, "section-icon");
    GtkWidget *header_label = gtk_label_new("COOLING");
    gtk_widget_add_css_class(header_label, "section-title");
    GtkWidget *header_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(header_spacer, TRUE);

    mode_selector = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "AUTO", "AUTO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "BETTER_AUTO", "Better Auto");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MANUAL", "MANUAL");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MAX", "MAX");
    g_signal_connect(mode_selector, "changed", G_CALLBACK(on_mode_changed), this);

    gtk_box_append(GTK_BOX(header), header_icon);
    gtk_box_append(GTK_BOX(header), header_label);
    gtk_box_append(GTK_BOX(header), header_spacer);
    gtk_box_append(GTK_BOX(header), mode_selector);
    gtk_box_append(GTK_BOX(fan_page), header);

    // --- Analog dials ---
    GtkWidget *dial_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 26);
    gtk_widget_set_halign(dial_row, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(dial_row),
        make_gauge_tile("FAN 1", "RPM", "weather-windy-symbolic", 104, 104,
                        draw_fan1, this, &fan1_gauge, &fan1_speed_label));
    gtk_box_append(GTK_BOX(dial_row),
        make_gauge_tile("FAN 2", "RPM", "weather-windy-symbolic", 104, 104,
                        draw_fan2, this, &fan2_gauge, &fan2_speed_label));
    gtk_box_append(GTK_BOX(dial_row),
        make_gauge_tile("CPU", "\u00b0C", "computer-symbolic", 46, 104,
                        draw_cpu, this, &cpu_gauge, &cpu_temp_label));
    gtk_box_append(GTK_BOX(dial_row),
        make_gauge_tile("GPU", "\u00b0C", "video-display-symbolic", 46, 104,
                        draw_gpu, this, &gpu_gauge, &gpu_temp_label));

    gtk_box_append(GTK_BOX(fan_page), dial_row);

    // --- Manual speed (only meaningful where the firmware accepts targets) ---
    manual_speed_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    slider_label = gtk_label_new("MANUAL SPEED");
    gtk_widget_add_css_class(slider_label, "field-label");
    gtk_box_append(GTK_BOX(manual_speed_box), slider_label);

    speed_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, RPM_STEPS, 1);
    gtk_scale_set_draw_value(GTK_SCALE(speed_slider), TRUE);
    gtk_widget_set_hexpand(speed_slider, TRUE);
    g_signal_connect(speed_slider, "value-changed", G_CALLBACK(on_speed_slider_changed), this);
    gtk_box_append(GTK_BOX(manual_speed_box), speed_slider);
    gtk_box_append(GTK_BOX(fan_page), manual_speed_box);

    state_label = gtk_label_new("Current State: N/A");
    gtk_widget_add_css_class(state_label, "status-line");
    gtk_widget_set_halign(state_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(fan_page), state_label);

    // Boards whose firmware refuses software fan control never expose
    // fan*_target, so neither manual speed nor Better Auto (which steers the
    // fans through the same targets) can work there: both are removed rather
    // than shown greyed out. With VICTUS_NO_FAN_CONTROL=1 the backend leaves
    // the fans to the firmware altogether, so the profile selector is locked
    // and the card is telemetry only.
    std::string support;
    {
        auto reply = socket_client->send_command_async(GET_FAN_TARGET_SUPPORT);
        support = reply.get();
    }
    fan_targets_supported = (support == "SUPPORTED");
    const bool fan_control_disabled = (support == "DISABLED");

    if (!fan_targets_supported) {
        gtk_widget_set_visible(manual_speed_box, FALSE);
        // Remove the higher index first so the lower one keeps its position.
        gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(mode_selector), 2);  // MANUAL
        gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(mode_selector), 1);  // Better Auto
        if (fan_control_disabled)
            gtk_widget_set_sensitive(mode_selector, FALSE);

        const char *text = fan_control_disabled
            ? "Fan control is switched off for this machine "
              "(VICTUS_NO_FAN_CONTROL=1), so the firmware runs the fans. "
              "Speeds and temperatures are still shown."
            : "This board's firmware does not accept fan speed targets, so "
              "manual speed and Better Auto are unavailable. AUTO and MAX "
              "still work.";
        GtkWidget *notice = gtk_label_new(text);
        gtk_label_set_wrap(GTK_LABEL(notice), TRUE);
        gtk_widget_add_css_class(notice, "notice");
        gtk_box_append(GTK_BOX(fan_page), notice);
    }

    // Rotors turn from the measured RPM, so the dials track the real fans.
    gauge_last_frame_us = g_get_monotonic_time();
    gauge_tick_id = g_timeout_add(33, on_gauge_tick, this);

    // Block "changed" signal during init so set_active_id doesn't fire
    // on_mode_changed and reset fan speeds with the slider's default value.
    g_signal_handlers_block_by_func(mode_selector, (gpointer)on_mode_changed, this);
    update_ui_from_system_state();
    g_signal_handlers_unblock_by_func(mode_selector, (gpointer)on_mode_changed, this);

    update_fan_speeds();

    // Set up a timer to periodically update fan speeds and temps
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
    // The socket client serialises every call on one connection, and
    // GET_GPU_TEMP makes the backend run `timeout 3 nvidia-smi` when the dGPU is
    // awake. Doing the blocking .get()s on the GTK main thread (this is called
    // from a 2 s g_timeout) freezes the UI for up to ~3 s per tick. So run the
    // round-trips on a worker thread and marshal the label writes back to the
    // main thread with g_idle_add. Skip if a prior refresh is still running so
    // slow ticks don't pile up overlapping workers.
    bool expected = false;
    if (!refresh_in_flight.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread([this]() {
        auto response1 = socket_client->send_command_async(GET_FAN_SPEED, "1");
        auto response2 = socket_client->send_command_async(GET_FAN_SPEED, "2");
        auto response_temp = socket_client->send_command_async(GET_CPU_TEMP);
        auto response_gpu_temp = socket_client->send_command_async(GET_GPU_TEMP);

        std::string fan1_speed = response1.get();
        if (fan1_speed.find("ERROR") != std::string::npos) fan1_speed = "N/A";

        std::string fan2_speed = response2.get();
        if (fan2_speed.find("ERROR") != std::string::npos) fan2_speed = "N/A";

        std::string cpu_temp = response_temp.get();
        if (cpu_temp.find("ERROR") != std::string::npos) cpu_temp = "N/A";

        // GPU: "IDLE" means the dGPU is runtime-suspended (no reading, not an error).
        std::string gpu_temp = response_gpu_temp.get();
        std::string gpu_temp_text;
        if (gpu_temp == "IDLE") {
            gpu_temp_text = "idle";
        } else if (gpu_temp.find("ERROR") != std::string::npos) {
            gpu_temp_text = "N/A";
        } else {
            gpu_temp_text = gpu_temp;
        }

        // The tiles carry their own captions and units, so only the value goes here.
        auto *payload = new FanLabelUpdate{
            this, fan1_speed, fan2_speed, cpu_temp, gpu_temp_text};

        g_idle_add(
            +[](gpointer data) -> gboolean {
                std::unique_ptr<FanLabelUpdate> u(static_cast<FanLabelUpdate *>(data));
                VictusFanControl *self = u->self;
                gtk_label_set_text(GTK_LABEL(self->fan1_speed_label), u->fan1.c_str());
                gtk_label_set_text(GTK_LABEL(self->fan2_speed_label), u->fan2.c_str());
                gtk_label_set_text(GTK_LABEL(self->cpu_temp_label), u->cpu.c_str());
                gtk_label_set_text(GTK_LABEL(self->gpu_temp_label), u->gpu.c_str());
                apply_temperature_class(self->cpu_temp_label, u->cpu);
                apply_temperature_class(self->gpu_temp_label, u->gpu);

                // Keep the dials' numeric state in step with the labels.
                auto to_number = [](const std::string &text, double *out) {
                    try { *out = std::stod(text); return true; }
                    catch (...) { *out = 0.0; return false; }
                };
                to_number(u->fan1, &self->fan1_rpm);
                to_number(u->fan2, &self->fan2_rpm);
                self->cpu_valid = to_number(u->cpu, &self->cpu_celsius);
                self->gpu_valid = to_number(u->gpu, &self->gpu_celsius);

                gtk_widget_queue_draw(self->cpu_gauge);
                gtk_widget_queue_draw(self->gpu_gauge);
                gtk_widget_queue_draw(self->fan1_gauge);
                gtk_widget_queue_draw(self->fan2_gauge);
                self->refresh_in_flight.store(false);
                return G_SOURCE_REMOVE;
            },
            payload);
    }).detach();
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
    // get_active_id returns a const pointer owned by GTK — copy immediately, never free
    const gchar *active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget));
    if (!active_id) return;
    std::string mode_str(active_id);

    // Send the mode command and wait for it to complete.
    auto result = self->socket_client->send_command_async(SET_FAN_MODE, mode_str).get();

    if (result == "OK") {
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

    // After all commands are sent, update the UI to reflect the final state.
    self->update_ui_from_system_state();
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

// --- Analog dials -----------------------------------------------------------

gboolean VictusFanControl::on_gauge_tick(gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);

    gint64 now_us = g_get_monotonic_time();
    double elapsed = (now_us - self->gauge_last_frame_us) / 1000000.0;
    self->gauge_last_frame_us = now_us;

    // Real fans turn far too fast to render honestly (3000 RPM is 50 rev/s), so
    // the drawn rotation is scaled down while staying proportional to the
    // measured speed.
    const double kVisualRevPerRpmSecond = 1.0 / 1200.0;
    self->fan1_angle += elapsed * self->fan1_rpm * kVisualRevPerRpmSecond * 2.0 * M_PI;
    self->fan2_angle += elapsed * self->fan2_rpm * kVisualRevPerRpmSecond * 2.0 * M_PI;
    self->fan1_angle = std::fmod(self->fan1_angle, 2.0 * M_PI);
    self->fan2_angle = std::fmod(self->fan2_angle, 2.0 * M_PI);

    // Only the rotors animate; the thermometers redraw when a reading lands.
    if (self->fan1_rpm > 0.0)
        gtk_widget_queue_draw(self->fan1_gauge);
    if (self->fan2_rpm > 0.0)
        gtk_widget_queue_draw(self->fan2_gauge);

    return G_SOURCE_CONTINUE;
}

void VictusFanControl::draw_fan1(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    draw_fan_rotor(cr, w, h, self->fan1_angle, self->fan1_rpm / FAN1_MAX_RPM,
                   self->fan1_rpm > 0.0);
}

void VictusFanControl::draw_fan2(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    draw_fan_rotor(cr, w, h, self->fan2_angle, self->fan2_rpm / FAN2_MAX_RPM,
                   self->fan2_rpm > 0.0);
}

void VictusFanControl::draw_cpu(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    draw_thermometer(cr, w, h, self->cpu_celsius, 100.0, self->cpu_valid);
}

void VictusFanControl::draw_gpu(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    draw_thermometer(cr, w, h, self->gpu_celsius, 100.0, self->gpu_valid);
}
