#include <gtk/gtk.h>
#include <iostream>
#include "about.hpp"

VictusAbout::VictusAbout()
{
	static const char *authors[] = {"betelqeyza", "Batuhan4", nullptr};

	about_dialog = gtk_about_dialog_new();

	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about_dialog), "victus-control");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about_dialog), "1.0");
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about_dialog), "betelqeyza, Batuhan4");
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about_dialog), authors);
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about_dialog), "nothing :P");

	GdkTexture *texture = gdk_texture_new_from_filename(
		DATADIR "/icons/hicolor/48x48/apps/victus-icon.svg", NULL);

	if (texture != NULL)
	{
		gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about_dialog), GDK_PAINTABLE(texture));
		g_object_unref(texture);
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
