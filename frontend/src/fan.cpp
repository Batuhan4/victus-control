#include "fan.hpp"
#include "gauges.hpp"
#include "socket.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>
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
    // Percentages for the history charts; -1 marks a reading that was not
    // available, which draws as a gap rather than a zero.
    double cpu_load = -1.0;
    double gpu_load = -1.0;
    double ram_pct = -1.0;
    double vram_pct = -1.0;
};

// Total and idle jiffies from /proc/stat. CPU load is the delta between two
// reads; the absolute counters are a since-boot average and say nothing about
// now.
bool read_proc_cpu(unsigned long long *total, unsigned long long *idle)
{
    std::ifstream stat("/proc/stat");
    if (!stat)
        return false;

    std::string cpu_label;
    stat >> cpu_label;
    if (cpu_label != "cpu")
        return false;

    unsigned long long value = 0, sum = 0, idle_sum = 0;
    for (int field = 0; field < 10 && (stat >> value); field++) {
        sum += value;
        if (field == 3 || field == 4)   // idle + iowait
            idle_sum += value;
    }

    *total = sum;
    *idle = idle_sum;
    return sum > 0;
}

double read_ram_percent()
{
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo)
        return -1.0;

    double total = 0.0, available = 0.0;
    std::string key;
    double value = 0.0;
    std::string unit;
    while (meminfo >> key >> value >> unit) {
        if (key == "MemTotal:") total = value;
        else if (key == "MemAvailable:") available = value;
        if (total > 0.0 && available > 0.0) break;
    }

    if (total <= 0.0)
        return -1.0;
    return ((total - available) / total) * 100.0;
}
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

    // --- Telemetry history -------------------------------------------------
    history_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *history_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *history_icon = gtk_image_new_from_icon_name("utilities-system-monitor-symbolic");
    gtk_widget_add_css_class(history_icon, "section-icon");
    GtkWidget *history_label = gtk_label_new("HISTORY");
    gtk_widget_add_css_class(history_label, "section-title");
    GtkWidget *history_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(history_spacer, TRUE);

    history_switch = gtk_switch_new();
    gtk_widget_set_valign(history_switch, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(history_switch, "power-switch");
    g_signal_connect(history_switch, "notify::active", G_CALLBACK(on_history_toggled), this);

    gtk_box_append(GTK_BOX(history_header), history_icon);
    gtk_box_append(GTK_BOX(history_header), history_label);
    gtk_box_append(GTK_BOX(history_header), history_spacer);
    gtk_box_append(GTK_BOX(history_header), history_switch);
    gtk_box_append(GTK_BOX(fan_page), history_header);

    // One plot: every series overlaid, each against its own axis in the
    // gutter, sharing a single time axis. Toggle traces from the legend.
    history_chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(history_chart, 420, 210);
    gtk_widget_set_hexpand(history_chart, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(history_chart),
                                   draw_history_overlay, this, nullptr);
    gtk_box_append(GTK_BOX(history_box), history_chart);

    gtk_box_append(GTK_BOX(fan_page), history_box);

    build_history_series();

    // Legend entries are the on/off control for each trace, so the charts need
    // to receive clicks.
    GtkGesture *chart_click = gtk_gesture_click_new();
    g_signal_connect(chart_click, "pressed", G_CALLBACK(on_chart_legend_clicked), this);
    gtk_widget_add_controller(history_chart, GTK_EVENT_CONTROLLER(chart_click));

    load_history_preference();
    gtk_switch_set_active(GTK_SWITCH(history_switch), history_enabled);
    apply_history_visibility();

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
        auto response_gpu_load = socket_client->send_command_async(GET_GPU_USAGE);
        auto response_gpu_vram = socket_client->send_command_async(GET_GPU_VRAM);

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
        // CPU and RAM come straight from /proc: essentially free, and cheaper
        // than asking the backend to shell out for them.
        double cpu_load = -1.0;
        unsigned long long total = 0, idle = 0;
        if (read_proc_cpu(&total, &idle) && prev_cpu_total > 0 && total > prev_cpu_total) {
            double total_delta = static_cast<double>(total - prev_cpu_total);
            double idle_delta = static_cast<double>(idle - prev_cpu_idle);
            cpu_load = std::clamp((1.0 - idle_delta / total_delta) * 100.0, 0.0, 100.0);
        }
        prev_cpu_total = total;
        prev_cpu_idle = idle;

        double ram_pct = read_ram_percent();

        double gpu_load = -1.0;
        try {
            gpu_load = std::stod(response_gpu_load.get());
        } catch (...) {
            gpu_load = -1.0;   // IDLE or no NVIDIA GPU
        }

        double vram_pct = -1.0;
        {
            std::istringstream vram(response_gpu_vram.get());
            double used = 0.0, vram_total = 0.0;
            if ((vram >> used >> vram_total) && vram_total > 0.0)
                vram_pct = (used / vram_total) * 100.0;
        }

        auto *payload = new FanLabelUpdate{
            this, fan1_speed, fan2_speed, cpu_temp, gpu_temp_text,
            cpu_load, gpu_load, ram_pct, vram_pct};

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

                // Record history even while the charts are hidden, so turning
                // them on shows the period just gone rather than an empty grid
                // that has to fill from scratch.
                self->cpu_history.push(self->cpu_celsius, self->cpu_valid);
                self->gpu_history.push(self->gpu_celsius, self->gpu_valid);
                self->fan1_history.push(self->fan1_rpm, self->fan1_rpm > 0.0);
                self->fan2_history.push(self->fan2_rpm, self->fan2_rpm > 0.0);
                self->cpu_load_history.push(u->cpu_load, u->cpu_load >= 0.0);
                self->gpu_load_history.push(u->gpu_load, u->gpu_load >= 0.0);
                self->ram_history.push(u->ram_pct, u->ram_pct >= 0.0);
                self->vram_history.push(u->vram_pct, u->vram_pct >= 0.0);

                if (self->history_enabled)
                    gtk_widget_queue_draw(self->history_chart);
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

// --- Telemetry history ------------------------------------------------------

namespace {

// Deliberately not the amber/red/green used for temperature status, which are
// reserved for state and must not double as series identity.
// The documented default categorical order, dark steps. Validated on the card
// surface: worst adjacent CVD dE 8.4 (protan), normal-vision 19.3, all eight
// inside the dark lightness band and over 3:1 contrast. Hues are assigned in
// this fixed order and never cycled - reusing four across eight series would
// give CPU temperature and CPU load the same colour.
constexpr double kSeries[8][3] = {
    {0x39 / 255.0, 0x87 / 255.0, 0xe5 / 255.0},   // blue
    {0xd9 / 255.0, 0x59 / 255.0, 0x26 / 255.0},   // orange
    {0x19 / 255.0, 0x9e / 255.0, 0x70 / 255.0},   // aqua
    {0xc9 / 255.0, 0x85 / 255.0, 0x00 / 255.0},   // yellow
    {0xd5 / 255.0, 0x51 / 255.0, 0x81 / 255.0},   // magenta
    {0x00 / 255.0, 0x83 / 255.0, 0x00 / 255.0},   // green
    {0x90 / 255.0, 0x85 / 255.0, 0xe9 / 255.0},   // violet
    {0xe6 / 255.0, 0x67 / 255.0, 0x67 / 255.0},   // red
};

std::string history_config_path()
{
    const char *home = std::getenv("HOME");
    if (home == nullptr)
        return "";
    return std::string(home) + "/.config/victus-control/history.conf";
}

} // namespace

void VictusFanControl::load_history_preference()
{
    std::string path = history_config_path();
    if (path.empty())
        return;

    std::ifstream file(path);
    if (!file)
        return;

    std::string value;
    std::getline(file, value);
    history_enabled = (value == "1");
}

void VictusFanControl::save_history_preference()
{
    std::string path = history_config_path();
    if (path.empty())
        return;

    // The directory already holds presets.conf, but the app may never have
    // written one on a fresh profile.
    std::string dir = path.substr(0, path.find_last_of('/'));
    mkdir(dir.c_str(), 0755);

    std::ofstream file(path, std::ios::trunc);
    if (file)
        file << (history_enabled ? "1" : "0") << "\n";
}

void VictusFanControl::apply_history_visibility()
{
    if (history_box != nullptr)
        gtk_widget_set_visible(history_box, history_enabled);
}

void VictusFanControl::on_history_toggled(GObject *sw, GParamSpec *, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);

    self->history_enabled = gtk_switch_get_active(GTK_SWITCH(sw));
    self->apply_history_visibility();
    self->save_history_preference();
}

void VictusFanControl::build_history_series()
{
    struct Spec {
        const SeriesHistory *history;
        const char *label;
        const char *unit;
        double min_span, step;
        bool visible;
    };

    // Temperatures and fan speeds are shown by default; the load and memory
    // traces are a click away, because eight axes at once is a wall.
    const Spec specs[] = {
        {&cpu_history,      "CPU \u00b0C", "\u00b0",  15.0,   5.0, true},
        {&gpu_history,      "GPU \u00b0C", "\u00b0",  15.0,   5.0, true},
        {&fan1_history,     "Fan 1",   "",     400.0, 200.0, true},
        {&fan2_history,     "Fan 2",   "",     400.0, 200.0, true},
        {&cpu_load_history, "CPU %",   "%",     25.0,  10.0, false},
        {&gpu_load_history, "GPU %",   "%",     25.0,  10.0, false},
        {&ram_history,      "RAM %",   "%",     25.0,  10.0, false},
        {&vram_history,     "VRAM %",  "%",     25.0,  10.0, false},
    };

    all_series.clear();
    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        ChartSeries series;
        series.history = specs[i].history;
        series.label = specs[i].label;
        series.r = kSeries[i][0];
        series.g = kSeries[i][1];
        series.b = kSeries[i][2];
        series.visible = specs[i].visible;
        series.unit = specs[i].unit;
        series.min_span = specs[i].min_span;
        series.step = specs[i].step;
        series.floor_value = 0.0;
        all_series.push_back(series);
    }
}

namespace {

// Toggling the last visible trace would leave an empty grid with no way back,
// so the final one stays on.
void toggle_series(std::vector<ChartSeries> &series, int index)
{
    if (index < 0 || static_cast<size_t>(index) >= series.size())
        return;

    int visible = 0;
    for (const auto &entry : series)
        visible += entry.visible ? 1 : 0;

    if (series[index].visible && visible <= 1)
        return;

    series[index].visible = !series[index].visible;
}

} // namespace

void VictusFanControl::draw_history_overlay(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    draw_overlay_chart(cr, w, h, self->all_series, kHistorySamples * 2.0);
}

void VictusFanControl::on_chart_legend_clicked(GtkGestureClick *, int, double x, double y, gpointer data)
{
    VictusFanControl *self = static_cast<VictusFanControl *>(data);
    int index = chart_legend_index_at(self->all_series, x, y);
    if (index < 0)
        return;
    toggle_series(self->all_series, index);
    gtk_widget_queue_draw(self->history_chart);
}
