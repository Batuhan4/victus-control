#include <gtk/gtk.h>
#include <memory>

#include "about.hpp"
#include "fan.hpp"
#include "keyboard.hpp"
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

	VictusControl();
	~VictusControl();

	void add_tabs();
	void add_menu();
	void run();

private:
	static void on_about_clicked(GtkButton *button, gpointer user_data);
};
