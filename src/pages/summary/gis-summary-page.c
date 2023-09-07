/*
 * Copyright (C) 2015-2020 Gooroom <gooroom@gooroom.kr>
 * Copyright (C) 2012 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pwd.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <errno.h>

#include "summary-resources.h"
#include "gis-summary-page.h"
#include "gis-keyring.h"
#include "run-passwd.h"
#include "run-su.h"
#include "splash-window.h"
#include "gis-message-dialog.h"


#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>

enum {
	ACCOUNT_CREATING_ERROR,
	PASSWORD_SETTING_ERROR,
	ENV_CONFIGURATION_ERROR
};

struct _GisSummaryPagePrivate {
	GtkWidget *setup_done_label;
	GtkWidget *os_label;
	GtkWidget *os_text_label;
	GtkWidget *hostname_label;
	GtkWidget *hostname_text_label;
	GtkWidget *lang_label;
	GtkWidget *lang_text_label;
	GtkWidget *realname_label;
	GtkWidget *realname_text_label;
	GtkWidget *username_label;
	GtkWidget *username_text_label;
	GtkWidget *online_accounts_label;
	GtkWidget *online_accounts_text_label;

	SplashWindow *splash;
};

G_DEFINE_TYPE_WITH_PRIVATE (GisSummaryPage, gis_summary_page, GIS_TYPE_PAGE);



static void
hide_splash_window (GisSummaryPage *page)
{
	GisSummaryPagePrivate *priv = page->priv;

	if (priv->splash) {
		splash_window_destroy (priv->splash);
		priv->splash = NULL;
	}
}

static void
show_splash_window (GisSummaryPage *page, const gchar *message)
{
	GisSummaryPagePrivate *priv = page->priv;

	if (!priv->splash) {
		GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (page));
		priv->splash = splash_window_new (GTK_WINDOW (toplevel));
	}

	splash_window_set_message_label (SPLASH_WINDOW (priv->splash), message);
	splash_window_show (priv->splash);
}

static gboolean
is_valid_username (const char *user)
{
	struct passwd pw, *pwp;
	char buf[4096] = {0,};

	getpwnam_r (user, &pw, buf, sizeof (buf), &pwp);

	return (pwp != NULL);
}

static void
delete_lightdm_config (void)
{
	gchar *cmd = NULL;

	cmd = g_strdup_printf ("/usr/bin/pkexec %s", GIS_DELETE_LIGHTDM_CONFIG_HELPER);

	if (!g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL)) {
		g_warning ("Couldn't delete 95_gooroom-initial-setup.conf");
	}

	g_free (cmd);
}

//static void
//remove_after_checking_file_exists (GisSummaryPage *page, const gchar *username)
//{
//	gchar *cmd = NULL;
//	gchar **argv;
//
//	cmd = g_strdup_printf("/home/.ecryptfs/%s", username);
//	if (g_file_test (cmd, G_FILE_TEST_EXISTS)) {
//		cmd = g_strdup_printf ("sudo /usr/bin/rm -rf %s",cmd);
//		g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);
//	}
//
//	cmd = g_strdup_printf("/home/%s", username);
//	if (g_file_test (cmd, G_FILE_TEST_EXISTS)) {
//		cmd = g_strdup_printf ("sudo /usr/bin/rm -rf %s",cmd);
//		g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);
//	}
//
//	g_free(argv);
//	g_free(cmd);
//}

static void
delete_account (const char *user)
{
	gchar *cmd = NULL;

	if (is_valid_username (user)) {
		cmd = g_strdup_printf ("/usr/bin/pkexec /usr/sbin/userdel -rf %s", user);
		if (!g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL)) {
			g_warning ("Couldn't delete account: %s", user);
		}
	}

//	remove_after_checking_file_exists (NULL, user);

	g_free (cmd);
}

static gboolean
system_restart_cb (gpointer user_data)
{
	const gchar *cmd;
	gchar **argv = NULL;
	GisSummaryPage *self = GIS_SUMMARY_PAGE (user_data);

	hide_splash_window (self);

	cmd = "/usr/bin/gooroom-logout-command --reboot --delay=100";

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);

	gtk_main_quit ();

	return FALSE;
}

static gboolean
system_logout_cb (gpointer user_data)
{
	const gchar *cmd;
	gchar **argv = NULL;
	GisSummaryPage *self = GIS_SUMMARY_PAGE (user_data);
	int res;
	GtkWidget *dialog, *toplevel;
	const char *message, *title;

	hide_splash_window (self);

	title = _("Done setting");
	message = _("All settings have been completed. Do you want to reboot?");
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));

	dialog = gis_message_dialog_new (GTK_WINDOW (toplevel),
                                     "dialog-warning-symbolic.symbolic",
                                     title, message);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				_("_Ok"), GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	res = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (res == GTK_RESPONSE_OK) {
		cmd = "/usr/bin/gooroom-logout-command --reboot --delay=100";
		g_shell_parse_argv (cmd, NULL, &argv, NULL);
		g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	}
	g_strfreev (argv);
	gtk_main_quit ();

	return FALSE;
}

static void
show_error_dialog (GisSummaryPage *page,
                   int             error_code,
                   const gchar    *title,
                   const gchar    *message)
{
	int res;
	gchar *username = NULL;
    GtkWidget *dialog, *toplevel;
	GisSummaryPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	gis_page_manager_get_user_info (manager, NULL, &username, NULL);

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (page));

	dialog = gis_message_dialog_new (GTK_WINDOW (toplevel),
                                     "dialog-warning-symbolic.symbolic",
                                     title,
                                     message);

	if (error_code == ACCOUNT_CREATING_ERROR ||
        error_code == PASSWORD_SETTING_ERROR ||
        error_code == ENV_CONFIGURATION_ERROR)
	{
		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                _("_Ok"), GTK_RESPONSE_OK,
                                _("_Cancel"), GTK_RESPONSE_CANCEL,
                                NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

		res = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		if (res == GTK_RESPONSE_OK) {
			delete_account (username);
			g_idle_add ((GSourceFunc)system_restart_cb, page);
		}
	}

	g_free (username);
}

static char *
get_item (const char *buffer, const char *name)
{
	char end_char;
	char *label, *start, *end, *result;

	result = NULL;
	start = NULL;
	end = NULL;
	label = g_strconcat (name, "=", NULL);
	if ((start = strstr (buffer, label)) != NULL) {
		start += strlen (label);
		end_char = '\n';
		if (*start == '"') {
			start++;
			end_char = '"';
		}

		end = strchr (start, end_char);
	}

	if (start != NULL && end != NULL)
		result = g_strndup (start, end - start);

	g_free (label);

	return result;
}

static void
update_online_accounts_info (GisSummaryPage *page)
{
	guint length;
	gchar *text = NULL;
	GList *online_accounts = NULL;
	GisSummaryPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	online_accounts = gis_page_manager_get_online_accounts (manager);

	length = g_list_length (online_accounts);

	if (length == 0) {
		text = NULL;
	} else if (length == 1) {
		text = g_strdup_printf ("%s", (gchar *)g_list_nth_data (online_accounts, 0));
	} else {
		guint i;
		GString *string = g_string_new ("");
		for (i = 0; i < length -1; i++)
			g_string_append_printf (string, "%s, ", (gchar *)g_list_nth_data (online_accounts, i));
		g_string_append_printf (string, "%s", (gchar *)g_list_nth_data (online_accounts, i));

		text = g_strdup (string->str);

		g_string_free (string, TRUE);
	}
	if (text)
		gtk_label_set_text (GTK_LABEL (priv->online_accounts_label), text);
	else
		gtk_label_set_text (GTK_LABEL (priv->online_accounts_label), _("No Use"));
}

static void
update_user_info (GisSummaryPage *page)
{
	gchar *realname = NULL, *username = NULL;
	GisSummaryPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	gis_page_manager_get_user_info (manager, &realname, &username, NULL);

	if (realname)
		gtk_label_set_text (GTK_LABEL (priv->realname_label), realname);

	if (username)
		gtk_label_set_text (GTK_LABEL (priv->username_label), username);

	g_free (realname);
	g_free (username);
}

static void
update_lang_info (GisSummaryPage *page)
{
	GisSummaryPagePrivate *priv = page->priv;

	const gchar *locale;
	gchar *locale_name = NULL;

	locale = g_getenv ("LANG");
	if (!locale) {
		gsize len, i;
		gchar *buffer = NULL;
		if (g_file_get_contents ("/etc/default/locale", &buffer, &len, NULL)) {
			for (i = 0; i < len; i++) {
				if (buffer[i] == '\n')
					buffer[i] = '\0';
			}
			gchar **items = g_strsplit (buffer, "=", -1);
			if (items && g_strv_length (items) > 1)
				locale = items[1];

			g_strfreev (items);
		}
		g_free (buffer);
	}

	locale_name = locale ? gnome_get_language_from_locale (locale, locale) : g_strdup (_("Unknown"));

	gtk_label_set_text (GTK_LABEL (priv->lang_label), locale_name);

	g_free (locale_name);
}

static void
update_distro_info (GisSummaryPage *page)
{
	char *buffer;
	char *name;
	char *text;
	GisSummaryPagePrivate *priv = page->priv;

	name = NULL;

	if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL)) {
		name = get_item (buffer, "NAME");
		g_free (buffer);
	}

	if (!name)
		name = g_strdup ("Debian");

	gtk_label_set_text (GTK_LABEL (priv->os_label), name);

	g_free (name);
}

static void
update_hostname_info (GisSummaryPage *page)
{
	gsize len, i;
	gchar *buffer = NULL;
	const gchar *hostname = NULL;
	GisSummaryPagePrivate *priv = page->priv;

	if (g_file_get_contents ("/etc/hostname", &buffer, &len, NULL)) {
		for (i = 0; i < len; i++) {
			if (buffer[i] == '\n')
				buffer[i] = '\0';
		}
	}

	hostname = (buffer != NULL) ? buffer : "";

	gtk_label_set_text (GTK_LABEL (priv->hostname_label), hostname);

	g_free (buffer);
}

static void
copy_worker_done_cb (GPid pid, gint status, gpointer user_data)
{
	const gchar *message;
	GisSummaryPage *self = GIS_SUMMARY_PAGE (user_data);

	g_spawn_close_pid (pid);

	/* delete /etc/lightdm/lightdm.conf.d/95_gooroom-initial-setup.conf */
	delete_lightdm_config ();

	show_splash_window (self,
                             _("User's environment configuration is completed.\n"
                               "Restart the system after a while..."));

	g_timeout_add (3000, (GSourceFunc)system_logout_cb, self);
}

static gboolean
do_copy_work (gpointer user_data)
{
	GPid pid;
	gchar **argv;
	gchar *cmd = NULL, *username = NULL;
	GisSummaryPage *self = GIS_SUMMARY_PAGE (user_data);
	GisSummaryPagePrivate *priv = self->priv;
	GisPageManager *manager = GIS_PAGE (user_data)->manager;

	gis_page_manager_get_user_info (manager, NULL, &username, NULL);

	cmd = g_strdup_printf ("/usr/bin/pkexec %s -u '%s'", GIS_COPY_WORKER, username);

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	if (g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL)) {
		g_child_watch_add (pid, (GChildWatchFunc)copy_worker_done_cb, self);
	}

	g_free (cmd);
	g_free (username);
	g_strfreev (argv);

	return FALSE;
}

static void
su_auth_cb (SuHandler *handler,
            GError    *error,
            gpointer   user_data)
{
	GisSummaryPage *page = GIS_SUMMARY_PAGE (user_data);

	if (!error) {
		g_idle_add ((GSourceFunc)do_copy_work, page);
	} else {
		g_warning ("failed to switch user: %s", error->message);
		g_error_free (error);

		hide_splash_window (page);

		const gchar *message, *title;

		title = _("User Environment Configuration Error");
		message = _("Failed to configure user's environment. Do you want to try again after rebooting the system?");

		show_error_dialog (page, ENV_CONFIGURATION_ERROR, title, message);
	}
}

static gboolean
run_su_cb (gpointer user_data)
{
	SuHandler *su_handler = NULL;
	gchar *username = NULL, *password = NULL;

	GisSummaryPage *page = GIS_SUMMARY_PAGE (user_data);
	GisPageManager *manager = GIS_PAGE (page)->manager;

	gis_page_manager_get_user_info (manager, NULL, &username, &password);

	su_handler = su_init ();
	su_authenticate (su_handler, username, password, (SuCallback)su_auth_cb, page);

	g_free (username);
	g_free (password);

	return FALSE;
}

static void
password_changed_done_cb (PasswdHandler *handler,
                          GError        *error,
                          gpointer       user_data)
{
	GisSummaryPage *page = GIS_SUMMARY_PAGE (user_data);
	GisPageManager *manager = GIS_PAGE (page)->manager;

	if (!error) {
		gchar *password = NULL;
		gis_page_manager_get_user_info (manager, NULL, NULL, &password);
		if (password)
			gis_update_login_keyring_password (password);

		g_timeout_add (1000, (GSourceFunc)run_su_cb, page);

		g_free (password);
	} else {
		g_warning ("failed to run su: %s", error->message);
		g_error_free (error);

		hide_splash_window (page);

		const gchar *message, *title;

		title = _("Password Setting Error");
		message = _("Failed to set password. Do you want to try again after rebooting the system?");

		show_error_dialog (page, PASSWORD_SETTING_ERROR, title, message);
	}
}

static gboolean
add_user_group (GisSummaryPage *page)
{
	guint i = 0;
	gchar *cmd = NULL;
	gchar *username = NULL, *realname = NULL;
	GisSummaryPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	static char *modes[] = { "adm", "audio", "bluetooth", "cdrom", "dialout",
				 "dip", "fax", "floppy", "lpadmin", "netdev", "plugdev",
				 "scanner", "sudo", "tape", "users",  "video", NULL };

	gis_page_manager_get_user_info (manager, &realname, &username, NULL);

	for (i = 0; modes[i] != NULL; i++) {
		cmd = g_strdup_printf ("/usr/bin/pkexec /usr/sbin/usermod -aG %s %s",modes[i], username);

		if (!g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL)){
			g_warning ("Faild to register %s with group [ #%d: %s ]\n", username, i, modes[i]);
		}
		g_free (cmd);

		gtk_main_iteration ();
	}

	return FALSE;
}

static void
adduser_done_cb (GPid pid, gint status, gpointer user_data)
{
	PasswdHandler  *passwd_handler = NULL;
	gchar *username = NULL, *password = NULL;
	GisSummaryPage *page = GIS_SUMMARY_PAGE (user_data);
	GisSummaryPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	g_spawn_close_pid (pid);

	gis_page_manager_get_user_info (manager, NULL, &username, &password);

	if (is_valid_username (username)) {
		g_usleep (G_USEC_PER_SEC); // waiting for 1 secs

		add_user_group (page);

		passwd_handler = passwd_init ();
		passwd_change_password (passwd_handler, username, password,
				(PasswdCallback) password_changed_done_cb, page);
	} else {
		const gchar *message, *title;

		title = _("Account Creating Error");
		message = _("Failed to create an account. Do you want to try again after rebooting the system?");

		hide_splash_window (page);

		show_error_dialog (page, ACCOUNT_CREATING_ERROR, title, message);
	}

	g_free (username);
	g_free (password);

#if 0
	gint exit_status = WEXITSTATUS (status);

	switch (exit_status)
	{
		case 1:
		break;

		case 2:
		break;

		case 3:
		break;

		default:
		break;
	}
#endif
}

static void
gis_summary_page_save_data (GisPage *page)
{
	GPid pid;
	gchar **argv;
	const char *cmd_prefix;
	gchar *cmd = NULL, *realname = NULL, *username = NULL;
	GisSummaryPage *self = GIS_SUMMARY_PAGE (page);
	GisSummaryPagePrivate *priv = self->priv;
	GisPageManager *manager = page->manager;

	show_splash_window (self, _("Configuring user's environment\nPlease wait..."));

	gis_page_manager_get_user_info (manager, &realname, &username, NULL);

	cmd_prefix = "/usr/bin/pkexec /usr/sbin/adduser --allow-bad-names --shell /bin/bash --disabled-login --encrypt-home --gecos";

	if (!realname)
		realname = g_strdup (username);

	if (realname) {
		char *utf8_realname = g_locale_to_utf8 (realname, -1, NULL, NULL, NULL);
		cmd = g_strdup_printf ("%s \"%s\" %s", cmd_prefix, utf8_realname, username);
		g_free (utf8_realname);
	} else {
		cmd = g_strdup_printf ("%s \"%s\" %s", cmd_prefix, username, username);
	}

//	remove_after_checking_file_exists (self, username);
	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	if (g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL)) {
		g_child_watch_add (pid, (GChildWatchFunc) adduser_done_cb, self);
	}

	g_free (cmd);
	g_free (realname);
	g_free (username);
	g_strfreev (argv);
}

static void
gis_summary_page_shown (GisPage *page)
{
	GisSummaryPage *summary = GIS_SUMMARY_PAGE (page);

	update_distro_info (summary);
	update_hostname_info (summary);
	update_user_info (summary);
	update_lang_info (summary);
	update_online_accounts_info (summary);
}

static void
gis_summary_page_finalize (GObject *object)
{
	G_OBJECT_CLASS (gis_summary_page_parent_class)->finalize (object);
}

static void
gis_summary_page_constructed (GObject *object)
{
	GisSummaryPage *page = GIS_SUMMARY_PAGE (object);
	GisSummaryPagePrivate *priv = page->priv;

	G_OBJECT_CLASS (gis_summary_page_parent_class)->constructed (object);

	gis_page_set_complete (GIS_PAGE (page), TRUE);

	gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_summary_page_locale_changed (GisPage *page)
{
	GisSummaryPagePrivate *priv = GIS_SUMMARY_PAGE (page)->priv;

	gis_page_set_title (GIS_PAGE (page), _("Setup Complete"));

	gtk_label_set_text (GTK_LABEL (priv->setup_done_label), _("Setup is complete"));
	gtk_label_set_text (GTK_LABEL (priv->os_text_label), _("OS"));
	gtk_label_set_text (GTK_LABEL (priv->hostname_text_label), _("Hostname"));
	gtk_label_set_text (GTK_LABEL (priv->lang_text_label), _("Language"));
	gtk_label_set_text (GTK_LABEL (priv->username_text_label), _("User Name"));
	gtk_label_set_text (GTK_LABEL (priv->realname_text_label), _("Real Name"));
	gtk_label_set_text (GTK_LABEL (priv->online_accounts_text_label), _("Online Accounts"));
}

static void
gis_summary_page_init (GisSummaryPage *page)
{
	GisSummaryPagePrivate *priv;

	priv = page->priv = gis_summary_page_get_instance_private (page);

	priv->splash = NULL;

	g_resources_register (summary_get_resource ());

	gtk_widget_init_template (GTK_WIDGET (page));

	gis_page_set_title (GIS_PAGE (page), _("Setup Complete"));
}

static void
gis_summary_page_class_init (GisSummaryPageClass *klass)
{
	GisPageClass *page_class = GIS_PAGE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
			"/kr/gooroom/initial-setup/pages/summary/gis-summary-page.ui");

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, setup_done_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, os_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, os_text_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, hostname_text_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, hostname_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, lang_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, lang_text_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, realname_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, realname_text_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, username_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, username_text_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, online_accounts_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, online_accounts_text_label);

	page_class->shown = gis_summary_page_shown;
	page_class->save_data = gis_summary_page_save_data;
	page_class->locale_changed = gis_summary_page_locale_changed;

	object_class->constructed = gis_summary_page_constructed;
	object_class->finalize = gis_summary_page_finalize;
}

GisPage *
gis_prepare_summary_page (GisPageManager *manager)
{
	return g_object_new (GIS_TYPE_SUMMARY_PAGE,
                         "manager", manager,
                         NULL);
}
