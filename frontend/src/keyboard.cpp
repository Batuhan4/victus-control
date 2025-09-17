#include "keyboard.hpp"
#include <fstream>
#include <iostream>
#include <gtk/gtk.h>

VictusKeyboardControl::VictusKeyboardControl(std::shared_ptr<VictusSocketClient> client) : socket_client(client)
{
	keyboard_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_top(keyboard_page, 20);
	gtk_widget_set_margin_bottom(keyboard_page, 20);
	gtk_widget_set_margin_start(keyboard_page, 20);
	gtk_widget_set_margin_end(keyboard_page, 20);

	toggle_button = gtk_button_new_with_label("Keyboard: OFF");
	color_button = gtk_color_button_new();
	apply_button = gtk_button_new_with_label("Apply");
	current_color_label = GTK_LABEL(gtk_label_new("Current Color: #000000"));
	current_state_label = GTK_LABEL(gtk_label_new("Current State: OFF"));

	gtk_box_append(GTK_BOX(keyboard_page), toggle_button);
	gtk_box_append(GTK_BOX(keyboard_page), color_button);
	gtk_box_append(GTK_BOX(keyboard_page), apply_button);
	gtk_box_append(GTK_BOX(keyboard_page), GTK_WIDGET(current_color_label));
	gtk_box_append(GTK_BOX(keyboard_page), GTK_WIDGET(current_state_label));

	g_signal_connect(toggle_button, "clicked", G_CALLBACK(on_toggle_clicked), this);
	g_signal_connect(color_button, "color-set", G_CALLBACK(on_color_set), this);
	g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_clicked), this);

	update_keyboard_state_from_device();
	update_current_color_label(this);
}

GtkWidget *VictusKeyboardControl::get_page()
{
	return keyboard_page;
}

void VictusKeyboardControl::update_keyboard_state(bool enabled)
{
	std::string command = enabled ? "255" : "0";
	auto result = socket_client->send_command_async(SET_KBD_BRIGHTNESS, command);

	if (result.get() != "OK")
	{
		std::cerr << "Failed to update keyboard state!" << std::endl;
		return;
	}

	update_keyboard_state_from_device();
}

void VictusKeyboardControl::update_keyboard_state_from_device()
{
	auto keyboard_state = socket_client->send_command_async(GET_KBD_BRIGHTNESS);
	std::string szkeyboard_state = keyboard_state.get();

	if (szkeyboard_state.find("ERROR") == std::string::npos)
	{
		keyboard_enabled = (szkeyboard_state == "255");
		gtk_button_set_label(GTK_BUTTON(toggle_button), keyboard_enabled ? "Keyboard: ON" : "Keyboard: OFF");

		if (current_state_label)
			gtk_label_set_text(current_state_label, ("Current State: " + (keyboard_enabled ? std::string("ON") : std::string("OFF"))).c_str());
	}
	else
		std::cerr << "Failed to get current keyboard state!" << std::endl;
}

void VictusKeyboardControl::update_keyboard_color(const GdkRGBA &color)
{
	// auto color_string = gdk_rgba_to_string(&color);
	auto value = std::to_string((int)(color.red * 255)) + " " +
				  std::to_string((int)(color.green * 255)) + " " +
				  std::to_string((int)(color.blue * 255));
	auto color_state = socket_client->send_command_async(SET_KEYBOARD_COLOR, value);
	std::string result = color_state.get();

	if (result != "OK")
		std::cerr << "Failed to update keyboard color!: " << result << std::endl;
}

void VictusKeyboardControl::on_toggle_clicked(GtkWidget *widget, gpointer data)
{
	VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);
	self->keyboard_enabled = !self->keyboard_enabled;
	self->update_keyboard_state(self->keyboard_enabled);
}

void VictusKeyboardControl::on_color_set(GtkColorButton *widget, gpointer data)
{
	VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

	GdkRGBA color;
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &color);

	self->update_keyboard_color(color);
}

void VictusKeyboardControl::on_apply_clicked(GtkWidget *widget, gpointer data)
{
	VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

	GdkRGBA color;
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(self->color_button), &color);

	self->update_keyboard_color(color);

	self->update_keyboard_state(self->keyboard_enabled);

	self->update_keyboard_state_from_device();

	self->update_current_color_label(self);
}

void VictusKeyboardControl::update_current_color_label(gpointer data)
{
	VictusKeyboardControl *self = static_cast<VictusKeyboardControl *>(data);

	auto current_color = self->socket_client->send_command_async(GET_KEYBOARD_COLOR);
	std::string szcurrent_color = current_color.get();

	gtk_label_set_text(self->current_color_label, ("Current Color: " + szcurrent_color).c_str());
}