#include <gtk/gtk.h>
#include <iostream>
#include "about.hpp"

VictusAbout::VictusAbout()
{
	about_dialog = gtk_about_dialog_new();

	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about_dialog), "victus-control");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about_dialog), "1.0");
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about_dialog), "betelqeyza");
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about_dialog), "nothing :P");

	// Try to load icon from standard locations
	const char* icon_paths[] = {
		"/usr/share/icons/hicolor/48x48/apps/victus-icon.svg",
		"/usr/local/share/icons/hicolor/48x48/apps/victus-icon.svg",
		"./victus-icon.svg",
		nullptr
	};
	
	GdkTexture *texture = nullptr;
	for (const char** path = icon_paths; *path != nullptr; path++) {
		GError *error = nullptr;
		texture = gdk_texture_new_from_filename(*path, &error);
		if (texture != nullptr) {
			gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about_dialog), GDK_PAINTABLE(texture));
			g_object_unref(texture);
			break;
		}
		if (error) {
			g_error_free(error);
		}
	}

	g_signal_connect(about_dialog, "close-request", G_CALLBACK(+[](GtkWidget *dialog, gpointer)
	{
		gtk_widget_set_visible(dialog, FALSE);
		return GDK_EVENT_STOP;
	}), nullptr);
}

void VictusAbout::show_about_window(GtkWindow *parent)
{
	gtk_window_set_transient_for(GTK_WINDOW(about_dialog), parent);
	gtk_window_present(GTK_WINDOW(about_dialog));
}