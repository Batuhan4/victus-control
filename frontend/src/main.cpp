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
#include "style.hpp"

class VictusControl
{
public:
	GtkWidget *window;
	GtkWidget *dashboard;
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
		gtk_window_set_title(GTK_WINDOW(window), "VICTUS CONTROL");
		gtk_window_set_default_size(GTK_WINDOW(window), 900, 900);

		// One page with a card per subsystem, rather than tabs: both the
		// keyboard and the fans are visible at once, which is the point of a
		// control panel you glance at.
		dashboard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
		gtk_widget_set_margin_top(dashboard, 18);
		gtk_widget_set_margin_bottom(dashboard, 18);
		gtk_widget_set_margin_start(dashboard, 18);
		gtk_widget_set_margin_end(dashboard, 18);

		GtkWidget *scroller = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), dashboard);
		gtk_window_set_child(GTK_WINDOW(window), scroller);

		add_cards();
		add_menu();
	}

	~VictusControl()
	{
	}

	void add_cards()
	{
		GtkWidget *keyboard_page = keyboard_control->get_page();
		GtkWidget *fan_page = fan_control->get_page();

		gtk_widget_add_css_class(keyboard_page, "victus-card");
		gtk_widget_add_css_class(fan_page, "victus-card");

		gtk_box_append(GTK_BOX(dashboard), keyboard_page);
		gtk_box_append(GTK_BOX(dashboard), fan_page);
	}

	void add_menu()
	{
		GtkWidget *header_bar = gtk_header_bar_new();
		gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

		GtkWidget *title_label = gtk_label_new("VICTUS CONTROL");
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
	apply_victus_style();

	try {
		VictusControl app;
		app.run();
	} catch (const std::exception &e) {
		std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
		GtkWidget *error_dialog = gtk_message_dialog_new(
			nullptr,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"An error occurred: %s",
			e.what()
		);
		gtk_window_set_title(GTK_WINDOW(error_dialog), "Error");
		GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
		g_signal_connect(error_dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, int, gpointer user_data) {
			g_main_loop_quit(static_cast<GMainLoop *>(user_data));
			gtk_window_destroy(GTK_WINDOW(dialog));
		}), loop);
		g_signal_connect(error_dialog, "close-request", G_CALLBACK(+[](GtkWidget *, gpointer user_data) {
			g_main_loop_quit(static_cast<GMainLoop *>(user_data));
			return FALSE;
		}), loop);
		gtk_widget_set_visible(error_dialog, true);
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
		return 1;
	}


	return 0;
}
