#ifndef FAN_HPP
#define FAN_HPP

#include <gtk/gtk.h>
#include <atomic>
#include <string>
#include "socket.hpp"

class VictusFanControl
{
public:
	GtkWidget *fan_page;

	VictusFanControl(std::shared_ptr<VictusSocketClient> client);

	GtkWidget *get_page();

private:
    // New UI Widgets
    GtkWidget *mode_selector;
    GtkWidget *speed_slider;
    GtkWidget *slider_label;

    // Labels for displaying current state
	GtkWidget *state_label;

	// Analog dials, drawn in gauges.cpp, with the digital value under each.
	GtkWidget *fan1_gauge;
	GtkWidget *fan2_gauge;
	GtkWidget *cpu_gauge;
	GtkWidget *gpu_gauge;
	GtkWidget *manual_speed_box;   // hidden outright on boards without support

	// Numeric state the dials draw from, kept alongside the label strings.
	double fan1_rpm = 0.0;
	double fan2_rpm = 0.0;
	double cpu_celsius = 0.0;
	double gpu_celsius = 0.0;
	bool cpu_valid = false;
	bool gpu_valid = false;

	// Rotor angles advance from the measured RPM, so the blades visibly track
	// how hard the fans are actually working.
	double fan1_angle = 0.0;
	double fan2_angle = 0.0;
	guint gauge_tick_id = 0;
	gint64 gauge_last_frame_us = 0;
	GtkWidget *fan1_speed_label;
	GtkWidget *fan2_speed_label;
	GtkWidget *cpu_temp_label;
	GtkWidget *gpu_temp_label;

	void update_fan_speeds();
	void update_ui_from_system_state();
    void set_fan_rpm(int level);

    // Signal handlers
	static void on_mode_changed(GtkComboBox *widget, gpointer data);
	static void on_speed_slider_changed(GtkRange *range, gpointer data);
	static gboolean on_gauge_tick(gpointer data);
	static void draw_fan1(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data);
	static void draw_fan2(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data);
	static void draw_cpu(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data);
	static void draw_gpu(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data);

	// False when the driver exposes no fan*_target for this board, which
	// means MANUAL speed cannot work no matter what the slider is set to.
	bool fan_targets_supported = true;

	std::shared_ptr<VictusSocketClient> socket_client;
    std::atomic<unsigned long long> manual_request_generation{0};
    // Set while a periodic refresh worker is running so a slow backend call
    // (GET_GPU_TEMP -> `timeout 3 nvidia-smi`) can't pile up overlapping workers.
    std::atomic<bool> refresh_in_flight{false};
};

#endif // FAN_HPP
