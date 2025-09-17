#ifndef FAN_HPP
#define FAN_HPP

#include <gtk/gtk.h>
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
	GtkWidget *fan1_speed_label;
	GtkWidget *fan2_speed_label;

	void update_fan_speeds();
	void update_ui_from_system_state();
    void set_fan_rpm(int level);

    // Signal handlers
	static void on_mode_changed(GtkDropDown *widget, gpointer data);
	static void on_speed_slider_changed(GtkRange *range, gpointer data);

	std::shared_ptr<VictusSocketClient> socket_client;
};

#endif // FAN_HPP
