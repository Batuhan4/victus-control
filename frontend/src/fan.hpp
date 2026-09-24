#ifndef FAN_HPP
#define FAN_HPP

#include <gtk/gtk.h>
#include <atomic>
#include <string>
#include <vector>

#include "charts.hpp"
#include "socket.hpp"

// Four minutes of history at the two-second refresh. Chosen so the four axis
// ticks land on whole minutes (-4m/-3m/-2m/-1m) instead of 3.75/2.5/1.25.
inline constexpr size_t kHistorySamples = 120;

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
	// Previous /proc/stat totals, so CPU load is a delta between refreshes
	// rather than the meaningless since-boot average.
	unsigned long long prev_cpu_total = 0;
	unsigned long long prev_cpu_idle = 0;

	double fan1_angle = 0.0;
	double fan2_angle = 0.0;
	guint gauge_tick_id = 0;
	gint64 gauge_last_frame_us = 0;
	GtkWidget *fan1_speed_label;
	GtkWidget *fan2_speed_label;
	GtkWidget *cpu_temp_label;
	GtkWidget *gpu_temp_label;

	// Rolling telemetry history, drawn as two separate charts. Temperature and
	// RPM never share an axis: one grid for two unrelated scales would imply a
	// relationship that is not there.
	GtkWidget *history_box;        // the whole section, hidden when switched off
	GtkWidget *history_switch;
	GtkWidget *history_chart;
	SeriesHistory cpu_history{kHistorySamples};
	SeriesHistory gpu_history{kHistorySamples};
	SeriesHistory fan1_history{kHistorySamples};
	SeriesHistory fan2_history{kHistorySamples};
	SeriesHistory cpu_load_history{kHistorySamples};
	SeriesHistory gpu_load_history{kHistorySamples};
	SeriesHistory ram_history{kHistorySamples};
	SeriesHistory vram_history{kHistorySamples};
	bool history_enabled = false;

	// Series live here rather than in the draw callbacks so their visibility
	// survives redraws and the legend can toggle them.
	std::vector<ChartSeries> all_series;

	void load_history_preference();
	void save_history_preference();
	void apply_history_visibility();
	void build_history_series();

	void update_fan_speeds();
	void update_ui_from_system_state();
    void set_fan_rpm(int level);

    // Signal handlers
	static void on_mode_changed(GtkComboBox *widget, gpointer data);
	static void on_speed_slider_changed(GtkRange *range, gpointer data);
	static gboolean on_gauge_tick(gpointer data);
	static void on_history_toggled(GObject *sw, GParamSpec *pspec, gpointer data);
	static void draw_history_overlay(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data);
	static void on_chart_legend_clicked(GtkGestureClick *g, int n, double x, double y, gpointer data);
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
