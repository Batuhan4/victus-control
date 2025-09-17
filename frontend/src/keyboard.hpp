#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include <gtk/gtk.h>
#include <string>
#include "socket.hpp"

class VictusKeyboardControl
{
public:
	GtkWidget *keyboard_page;

	VictusKeyboardControl(std::shared_ptr<VictusSocketClient> client);

	GtkWidget *get_page();

private:
	GtkWidget *toggle_button;
	GtkWidget *color_button;
	GtkWidget *apply_button;
	GtkLabel *current_color_label;
	GtkLabel *current_state_label;

	bool keyboard_enabled;

	void update_keyboard_state(bool enabled);
	void update_keyboard_state_from_device();

	void update_keyboard_color(const GdkRGBA &color);

	static void on_toggle_clicked(GtkWidget *widget, gpointer data);
	static void on_color_set(GtkColorButton *widget, gpointer data);
	static void on_apply_clicked(GtkWidget *widget, gpointer data);
	static void update_current_color_label(gpointer data);

	std::shared_ptr<VictusSocketClient> socket_client;
};

#endif