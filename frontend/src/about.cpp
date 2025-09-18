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

    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(about_dialog), "victus-icon");

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