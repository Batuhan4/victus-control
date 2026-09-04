#include "fan.hpp"
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

GtkWidget *make_card(const char *title, const char *icon_name)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "victus-card");

    if (title != nullptr) {
        // Symbolic icons come from the icon theme rather than a bundled icon
        // font, so they inherit the accent colour and need no extra assets.
        GtkWidget *heading_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
        gtk_widget_add_css_class(icon, "section-icon");

        GtkWidget *heading = gtk_label_new(title);
        gtk_widget_add_css_class(heading, "section-title");

        gtk_box_append(GTK_BOX(heading_row), icon);
        gtk_box_append(GTK_BOX(heading_row), heading);
        gtk_widget_set_halign(heading_row, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(card), heading_row);
    }
    return card;
}

// One telemetry tile: a small caption over a large monospace value.
GtkWidget *make_stat_tile(const char *caption, const char *unit,
                          const char *icon_name, GtkWidget **value_out)
{
    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(tile, TRUE);

    GtkWidget *caption_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *caption_icon = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(caption_icon, "tile-icon");
    GtkWidget *caption_label = gtk_label_new(caption);
    gtk_widget_add_css_class(caption_label, "field-label");
    gtk_box_append(GTK_BOX(caption_row), caption_icon);
    gtk_box_append(GTK_BOX(caption_row), caption_label);
    gtk_widget_set_halign(caption_row, GTK_ALIGN_START);

    GtkWidget *value_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *value = gtk_label_new("--");
    gtk_widget_add_css_class(value, "readout");
    gtk_widget_set_halign(value, GTK_ALIGN_START);

    GtkWidget *unit_label = gtk_label_new(unit);
    gtk_widget_add_css_class(unit_label, "readout-unit");
    gtk_widget_set_valign(unit_label, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(unit_label, 4);

    gtk_box_append(GTK_BOX(value_row), value);
    gtk_box_append(GTK_BOX(value_row), unit_label);
    gtk_box_append(GTK_BOX(tile), caption_row);
    gtk_box_append(GTK_BOX(tile), value_row);

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

    // --- Cooling mode ---
    GtkWidget *mode_card = make_card("COOLING MODE", "power-profile-performance-symbolic");

    GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *mode_caption = gtk_label_new("PROFILE");
    gtk_widget_add_css_class(mode_caption, "field-label");
    gtk_box_append(GTK_BOX(mode_row), mode_caption);

    mode_selector = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "AUTO", "AUTO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "BETTER_AUTO", "Better Auto");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MANUAL", "MANUAL");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_selector), "MAX", "MAX");
    gtk_widget_set_hexpand(mode_selector, TRUE);
    g_signal_connect(mode_selector, "changed", G_CALLBACK(on_mode_changed), this);
    gtk_box_append(GTK_BOX(mode_row), mode_selector);
    gtk_box_append(GTK_BOX(mode_card), mode_row);

    // --- Manual speed ---
    slider_label = gtk_label_new("MANUAL SPEED (1-8)");
    gtk_widget_add_css_class(slider_label, "field-label");
    gtk_widget_set_halign(slider_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(mode_card), slider_label);

    speed_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, RPM_STEPS, 1);
    gtk_scale_set_draw_value(GTK_SCALE(speed_slider), TRUE);
    g_signal_connect(speed_slider, "value-changed", G_CALLBACK(on_speed_slider_changed), this);
    gtk_box_append(GTK_BOX(mode_card), speed_slider);
    gtk_box_append(GTK_BOX(fan_page), mode_card);

    // --- Telemetry tiles ---
    GtkWidget *telemetry_card = make_card("TELEMETRY", "speedometer-symbolic");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    gtk_grid_attach(GTK_GRID(grid), make_stat_tile("FAN 1", "RPM", "weather-windy-symbolic", &fan1_speed_label), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_stat_tile("FAN 2", "RPM", "weather-windy-symbolic", &fan2_speed_label), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_stat_tile("CPU", "\u00b0C", "computer-symbolic", &cpu_temp_label), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_stat_tile("GPU", "\u00b0C", "video-display-symbolic", &gpu_temp_label), 1, 1, 1, 1);

    gtk_box_append(GTK_BOX(telemetry_card), grid);
    gtk_box_append(GTK_BOX(fan_page), telemetry_card);

    state_label = gtk_label_new("Current State: N/A");
    gtk_widget_add_css_class(state_label, "status-line");
    gtk_widget_set_halign(state_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(fan_page), state_label);

    // Boards whose BIOS refuses software fan control never expose fan*_target.
    // Offering MANUAL there produces a slider that silently fails, so say what
    // is going on instead.
    {
        auto support = socket_client->send_command_async(GET_FAN_TARGET_SUPPORT);
        fan_targets_supported = (support.get() == "SUPPORTED");
    }

    if (!fan_targets_supported) {
        GtkWidget *notice = gtk_label_new(
            "This board's firmware does not accept fan speed targets, so "
            "MANUAL speed is unavailable. AUTO and MAX still work.");
        gtk_label_set_wrap(GTK_LABEL(notice), TRUE);
        gtk_widget_add_css_class(notice, "notice");
        gtk_widget_set_halign(notice, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(mode_card), notice);

        gtk_widget_set_sensitive(speed_slider, FALSE);
        gtk_widget_set_sensitive(slider_label, FALSE);
    }

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
