/*
 * Copyright (C) 2015-2020 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <signal.h>

#include "gis-keyring.h"
#include "gis-assistant.h"

static void
ensure_nm_applet (void)
{
	gchar **argv;
	const gchar *cmd;

	cmd = "nm-applet --no-indicator";
	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
}


static void
sigterm_cb (gpointer user_data)
{
	gboolean is_callback = GPOINTER_TO_INT (user_data);

	if (is_callback)
		g_debug ("SIGTERM received");

	if (is_callback)
	{
		gtk_main_quit ();
#ifdef KILL_ON_SIGTERM
		/* LP: #1445461 */
		g_debug ("Killing gooroom-initial-setup with exit()...");
		exit (EXIT_SUCCESS);
#endif
	}
}

static void
init_config_files (void)
{
	guint i = 0;
	GDir *dir = NULL;
    GError *error = NULL;
	const char *homedir, *filename;
	char *path = NULL, *dirname = NULL;

	static char *remove_paths[] = {
		".xsessionrc",
		".config/user_agreements",
		".config/goa-1.0/accounts.conf",
		".local/share/keyrings",
		NULL
	};

	homedir = g_get_home_dir ();

	for (i = 0; i < 4; i++) {
		path = g_build_filename (homedir, remove_paths[i], NULL);
		g_remove (path);
		g_free (path);
	}

	dirname = g_build_filename (homedir, remove_paths[i], NULL);
	if (!(dir = g_dir_open (dirname, 0, &error))) {
		g_warning ("Failed to open directory '%s': %s", dirname, error->message);
		g_error_free (error);
		g_free (dirname);
		return; 
	}

	while ((filename = g_dir_read_name (dir))) {
		path = g_build_filename (dirname, filename, NULL);
		g_remove (path);
		g_free (path);
	}

	g_free (dirname);
	g_dir_close (dir);
}
static void
on_activate (GtkApplication *app, gpointer user_data)
{
	GtkCssProvider *provider;
	GtkWidget *window, *assistant;

	window = gtk_application_window_new (app);
	gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_DESKTOP);
	gtk_window_set_keep_below (GTK_WINDOW (window), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
	gtk_widget_set_app_paintable (window, TRUE);
	gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width (GTK_CONTAINER (window), 60);

	GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (window));
	if (gdk_screen_is_composited (screen)) {
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		if (visual == NULL)
			visual = gdk_screen_get_system_visual (screen);

		gtk_widget_set_visual (window, visual);
	}

	assistant = gis_assistant_new ();
	gtk_widget_set_halign (assistant, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (assistant, GTK_ALIGN_CENTER);
	gtk_widget_show (assistant);

	gtk_container_add (GTK_CONTAINER (window), assistant);

	gtk_widget_show (window);

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_resource (provider, "/kr/gooroom/initial-setup/theme.css");
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);

}

int
main (int argc, char **argv)
{
	GtkApplication *app;

	int ret = EXIT_SUCCESS;

	/* Initialize i18n */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	init_config_files ();
	ensure_nm_applet ();
	gis_ensure_login_keyring ();

	app = gtk_application_new ("kr.gooroom.initial-setup", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

	ret = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);
//	sigterm_cb (GINT_TO_POINTER (FALSE));

    return ret;
}
