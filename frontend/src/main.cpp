#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <gtk/gtk.h>
#include "keyboard.hpp"
#include "fan.hpp"
#include "about.hpp"
#include "socket.hpp"

class VictusControl
{
public:
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *menu_button;
	GtkWidget *menu;

	std::shared_ptr<VictusSocketClient> socket_client;
	std::unique_ptr<VictusFanControl> fan_control;
	std::unique_ptr<VictusKeyboardControl> keyboard_control;
	VictusAbout about;

	VictusControl()
	{
		socket_client = std::make_shared<VictusSocketClient>("/run/victus-control/victus_backend.sock");
		fan_control = std::make_unique<VictusFanControl>(socket_client);
		keyboard_control = std::make_unique<VictusKeyboardControl>(socket_client);

		window = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(window), "victus-control");
		gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

		notebook = gtk_notebook_new();
		gtk_widget_set_hexpand(notebook, TRUE);
		gtk_widget_set_vexpand(notebook, TRUE);
		gtk_window_set_child(GTK_WINDOW(window), notebook);

		add_tabs();
		add_menu();
	}

	~VictusControl()
	{
	}

	void add_tabs()
	{
		GtkWidget *keyboard_page = keyboard_control->get_page();
		GtkWidget *fan_page = fan_control->get_page();

		GtkWidget *label_keyboard = gtk_label_new("Keyboard");
		GtkWidget *label_fan = gtk_label_new("FAN");

		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), keyboard_page, label_keyboard);
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fan_page, label_fan);
	}

	void add_menu()
	{
		GtkWidget *header_bar = gtk_header_bar_new();
		gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

		GtkWidget *title_label = gtk_label_new("victus-control");
		gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar), title_label);

		menu_button = gtk_menu_button_new();
		gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), menu_button);

		menu = gtk_popover_new();
		GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		GtkWidget *about_button = gtk_button_new_with_label("About victus-control");
		g_signal_connect(about_button, "clicked", G_CALLBACK(on_about_clicked), this);

		gtk_box_append(GTK_BOX(menu_box), about_button);
		gtk_popover_set_child(GTK_POPOVER(menu), menu_box);
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), menu);

		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header_bar), TRUE);
	}

	void run()
	{
		GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

		g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer loop)
		{
			g_main_loop_quit(static_cast<GMainLoop *>(loop));
		}), loop);

		gtk_widget_set_visible(window, true);

		g_main_loop_run(loop);

		g_main_loop_unref(loop);
	}

private:
	static void on_about_clicked(GtkButton *button, gpointer user_data)
	{
		VictusControl *self = static_cast<VictusControl *>(user_data);

		self->about.show_about_window(GTK_WINDOW(self->window));
	}
};

int main(int argc, char *argv[])
{
	gtk_init();

	try {
		VictusControl app;
		app.run();
	} catch (const std::exception &e) {
		std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
		
		GtkWidget *error_dialog = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(error_dialog), "Error");
		gtk_window_set_default_size(GTK_WINDOW(error_dialog), 400, 200);
		gtk_window_set_modal(GTK_WINDOW(error_dialog), TRUE);
		
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		gtk_widget_set_margin_start(box, 20);
		gtk_widget_set_margin_end(box, 20);
		gtk_widget_set_margin_top(box, 20);
		gtk_widget_set_margin_bottom(box, 20);
		gtk_window_set_child(GTK_WINDOW(error_dialog), box);
		
		GtkWidget *label = gtk_label_new(("An error occurred: " + std::string(e.what())).c_str());
		gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(box), label);
		
		GtkWidget *close_button = gtk_button_new_with_label("Close");
		g_signal_connect_swapped(close_button, "clicked", G_CALLBACK(gtk_window_destroy), error_dialog);
		gtk_box_append(GTK_BOX(box), close_button);
		
		gtk_widget_set_visible(error_dialog, true);
		
		GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
		g_signal_connect(error_dialog, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer loop) {
			g_main_loop_quit(static_cast<GMainLoop *>(loop));
		}), loop);
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
		return 1;
	}


	return 0;
}