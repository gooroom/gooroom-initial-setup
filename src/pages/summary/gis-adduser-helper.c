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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <glib.h>
#include <glib/gi18n.h>

#include "run-passwd.h"


static gchar *username = NULL;
static gchar *realname = NULL;
static gchar *password = NULL;
static gboolean encrypt_home = FALSE;


static GOptionEntry option_entries[] =
{
	{ "username",     'u', 0, G_OPTION_ARG_STRING, &username,     NULL, NULL },
	{ "realname",     'r', 0, G_OPTION_ARG_STRING, &realname,     NULL, NULL },
	{ "password",     'p', 0, G_OPTION_ARG_STRING, &password,     NULL, NULL },
	{ "encrypt-home", 'e', 0, G_OPTION_ARG_NONE,   &encrypt_home, NULL, NULL },
    { NULL }
};


static GMainLoop *loop = NULL;


static void
password_changed_cb (PasswdHandler *handler,
                     GError        *error,
                     gpointer       user_data)
{
	g_main_loop_quit (loop);
}

static gboolean
is_valid_username (const char *user)
{
	struct passwd pw, *pwp;
	char buf[4096] = {0,};

	getpwnam_r (user, &pw, buf, sizeof (buf), &pwp);

	return (pwp != NULL);
}

int
main (int argc, char **argv)
{
	gboolean        retval;
	GError         *error = NULL;
	GOptionContext *context;
	gchar          *cmd;
	const gchar    *cmd_prefix;
	PasswdHandler  *passwd_handler = NULL;

	/* Initialize i18n */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, option_entries, NULL);
	retval = g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	/* parse options */
	if (!retval) {
		g_warning ("%s", error->message);
		g_error_free (error);
		return 1;
	}

	if (!username || !password) {
		g_warning ("No username or password was specified.");
		return 2;
	}

	if (is_valid_username (username)) {
		g_warning ("Already exising user.");
		return 3;
	}

	if (encrypt_home) {
		cmd_prefix = "/usr/sbin/adduser --allow-bad-names --shell /bin/bash --disabled-login --encrypt-home --gecos";
	} else {
		cmd_prefix = "/usr/sbin/adduser --allow-bad-names --shell /bin/bash --disabled-login --gecos";
	}

	if (realname) {
		char *utf8_realname = g_locale_to_utf8 (realname, -1, NULL, NULL, NULL);
		cmd = g_strdup_printf ("%s \"%s\" %s", cmd_prefix, utf8_realname, username);
		g_free (utf8_realname);
	} else {
		cmd = g_strdup_printf ("%s \"%s\" %s", cmd_prefix, username, username);
	}

	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	if (is_valid_username (username)) {
		g_usleep (G_USEC_PER_SEC); // waiting for 1 secs

		passwd_handler = passwd_init ();

		passwd_change_password (passwd_handler, username, password, TRUE,
                                (PasswdCallback) password_changed_cb, NULL);
	}

	g_free (cmd);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	if (passwd_handler)
		passwd_destroy (passwd_handler);

	return 0;
}
