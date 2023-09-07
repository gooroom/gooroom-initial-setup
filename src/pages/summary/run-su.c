/*
 * Copyright (C) 2015-2020 Gooroom <gooroom@gooroom.kr>
 * Copyright (C) 2002 Diego Gonzalez
 * Copyright (C) 2006 Johannes H. Jensen
 * Copyright (C) 2010 Milan Bouchet-Valat
 *
 * Written by: Diego Gonzalez <diego@pemas.net>
 * Modified by: Johannes H. Jensen <joh@deworks.net>,
 *              Milan Bouchet-Valat <nalimilan@club.fr>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Most of this code originally comes from gnome-about-me-password.c,
 * from gnome-control-center.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#if __sun
#include <sys/types.h>
#include <signal.h>
#endif

#include "run-su.h"

/* su states */
typedef enum {
	SU_STATE_NONE,              /* su is not asking for anything */
	SU_STATE_AUTH,              /* su is asking for our current password */
	SU_STATE_DONE,              /* su succeeded but has not yet exited */
} SuState;

struct SuHandler {
	const char *current_password;
	const char *user;

	/* Communication with the su program */
	GPid backend_pid;

	GIOChannel *backend_stdin;
	GIOChannel *backend_stdout;

	GQueue *backend_stdin_queue;            /* Write queue to backend_stdin */

	/* GMainLoop IDs */
	guint backend_child_watch_id;           /* g_child_watch_add (PID) */
	guint backend_stdout_watch_id;          /* g_io_add_watch (stdout) */
	guint su_check_timeout_id;

	/* State of the su program */
	SuState backend_state;

	SuCallback auth_cb;
	gpointer   auth_cb_data;
};

/* Buffer size for backend output */
#define BUFSIZE 64


static GQuark
su_error_quark (void)
{
	static GQuark q = 0;

	if (q == 0) {
		q = g_quark_from_static_string("su_error");
	}

	return q;
}

/* Error handling */
#define SU_ERROR (su_error_quark ())


static void stop_su (SuHandler *su_handler);
static void free_su_resources (SuHandler *su_handler);
static gboolean io_watch_stdout (GIOChannel *source, GIOCondition condition, SuHandler *su_handler);


/*
 * Spawning and closing of backend {{
 */

/* Child watcher */
static void
child_watch_cb (GPid pid, gint status, SuHandler *su_handler)
{
	if (WIFEXITED (status)) {
		if (WEXITSTATUS (status) >= 255) {
			g_warning ("Child exited unexpectedly");
		}
		if (WEXITSTATUS (status) == 0) {
			if (su_handler->backend_state == SU_STATE_AUTH) {
				su_handler->backend_state = SU_STATE_DONE;
				if (su_handler->auth_cb) {
					su_handler->auth_cb (su_handler, NULL, su_handler->auth_cb_data);
				}
			}
		}
	}

	free_su_resources (su_handler);
}

static void
ignore_sigpipe (gpointer data)
{
	signal (SIGPIPE, SIG_IGN);
}

/* Spawn su backend
 * Returns: TRUE on success, FALSE otherwise and sets error appropriately */
static gboolean
spawn_su (SuHandler *su_handler, GError **error)
{
	gchar   *argv[3] = {0,};
	gchar  **envp;
	gint    my_stdin, my_stdout, my_stderr;

	argv[0] = "/bin/su";
	argv[1] = g_strdup (su_handler->user);
	argv[2] = NULL;

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "LC_ALL", "C", TRUE);

	if (!g_spawn_async_with_pipes (NULL,                            /* Working directory */
                                   argv,                            /* Argument vector */
                                   envp,                            /* Environment */
                                   G_SPAWN_DO_NOT_REAP_CHILD,       /* Flags */
                                   ignore_sigpipe,                  /* Child setup */
                                   NULL,                            /* Data to child setup */
                                   &su_handler->backend_pid,    /* PID */
                                   &my_stdin,                       /* Stdin */
                                   &my_stdout,                      /* Stdout */
                                   &my_stderr,                      /* Stderr */
                                   error)) {                        /* GError */

		/* An error occured */
		free_su_resources (su_handler);

		g_strfreev (envp);

		return FALSE;
	}

	g_free (argv[1]);
	g_strfreev (envp);

	/* 2>&1 */
	if (dup2 (my_stderr, my_stdout) == -1) {
		/* Failed! */
		g_set_error_literal (error,
				SU_ERROR,
				SU_ERROR_BACKEND,
				strerror (errno));

		/* Clean up */
		stop_su (su_handler);

		return FALSE;
	}

	/* Open IO Channels */
	su_handler->backend_stdin = g_io_channel_unix_new (my_stdin);
	su_handler->backend_stdout = g_io_channel_unix_new (my_stdout);

	/* Set raw encoding */
	/* Set nonblocking mode */
	if (g_io_channel_set_encoding (su_handler->backend_stdin, NULL, error) != G_IO_STATUS_NORMAL ||
        g_io_channel_set_encoding (su_handler->backend_stdout, NULL, error) != G_IO_STATUS_NORMAL ||
        g_io_channel_set_flags (su_handler->backend_stdin, G_IO_FLAG_NONBLOCK, error) != G_IO_STATUS_NORMAL ||
        g_io_channel_set_flags (su_handler->backend_stdout, G_IO_FLAG_NONBLOCK, error) != G_IO_STATUS_NORMAL ) {

		/* Clean up */
		stop_su (su_handler);
		return FALSE;
	}

	/* Turn off buffering */
	g_io_channel_set_buffered (su_handler->backend_stdin, FALSE);
	g_io_channel_set_buffered (su_handler->backend_stdout, FALSE);

	/* Add IO Channel watcher */
	su_handler->backend_stdout_watch_id = g_io_add_watch (su_handler->backend_stdout,
                                                          G_IO_IN | G_IO_PRI,
                                                          (GIOFunc) io_watch_stdout, su_handler);

	/* Add child watcher */
	su_handler->backend_child_watch_id = g_child_watch_add (su_handler->backend_pid,
                                                            (GChildWatchFunc) child_watch_cb,
                                                            su_handler);

	/* Success! */
	return TRUE;
}

/* Stop su backend */
static void
stop_su (SuHandler *su_handler)
{
	if (su_handler->backend_pid != -1) {
		kill (su_handler->backend_pid, 9);
	}

	/* We must run free_su_resources here and not let our child
	 * watcher do it, since it will access invalid memory after the
	 * dialog has been closed and cleaned up.
	 *
	 * If we had more than a single thread we'd need to remove
	 * the child watch before trying to kill the child.
	 */
	free_su_resources (su_handler);
}

/* Clean up su resources */
static void
free_su_resources (SuHandler *su_handler)
{
	GError  *error = NULL;

	/* Remove the child watcher */
	if (su_handler->backend_child_watch_id != 0) {
		g_source_remove (su_handler->backend_child_watch_id);
		su_handler->backend_child_watch_id = 0;
	}


	/* Close IO channels (internal file descriptors are automatically closed) */
	if (su_handler->backend_stdin != NULL) {
		if (g_io_channel_shutdown (su_handler->backend_stdin, TRUE, &error) != G_IO_STATUS_NORMAL) {
			g_warning ("Could not shutdown backend_stdin IO channel: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
		g_io_channel_unref (su_handler->backend_stdin);
		su_handler->backend_stdin = NULL;
	}

	if (su_handler->backend_stdout != NULL) {
		if (g_io_channel_shutdown (su_handler->backend_stdout, TRUE, &error) != G_IO_STATUS_NORMAL) {
			g_warning ("Could not shutdown backend_stdout IO channel: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
		g_io_channel_unref (su_handler->backend_stdout);
		su_handler->backend_stdout = NULL;
	}

	/* Remove IO watcher */
	if (su_handler->backend_stdout_watch_id != 0) {
		g_source_remove (su_handler->backend_stdout_watch_id);
		su_handler->backend_stdout_watch_id = 0;
	}

	/* Close PID */
	if (su_handler->backend_pid != -1) {
		g_spawn_close_pid (su_handler->backend_pid);
		su_handler->backend_pid = -1;
	}

	/* Clear backend state */
	su_handler->backend_state = SU_STATE_NONE;
}

/*
 * }} Spawning and closing of backend
 */

/*
 * Backend communication code {{
 */

/* Write the first element of queue through channel */
static void
io_queue_pop (GQueue *queue, GIOChannel *channel)
{
	gchar   *buf;
	gsize   bytes_written;
	GError  *error = NULL;

	buf = g_queue_pop_head (queue);

	if (buf != NULL) {

		if (g_io_channel_write_chars (channel, buf, -1, &bytes_written, &error) != G_IO_STATUS_NORMAL) {
			g_warning ("Could not write queue element \"%s\" to channel: %s", buf, error->message);
			g_error_free (error);
		}

		/* Ensure passwords are cleared from memory */
		memset (buf, 0, strlen (buf));
		g_free (buf);
	}
}

/* Goes through the argument list, checking if one of them occurs in str
 * Returns: TRUE as soon as an element is found to match, FALSE otherwise */
static gboolean
is_string_complete (gchar *str, ...)
{
	va_list ap;
	gchar   *arg;

	if (strlen (str) == 0) {
		return FALSE;
	}

	va_start (ap, str);

	while ((arg = va_arg (ap, char *)) != NULL) {
		if (strstr (str, arg) != NULL) {
			va_end (ap);
			return TRUE;
		}
	}

	va_end (ap);

	return FALSE;
}

static gboolean
maybe_su_is_successful_idle_cb (gpointer user_data)
{
	SuHandler *su_handler = (SuHandler *)user_data;

	/* Authentication successful */
	if (su_handler->auth_cb)
		su_handler->auth_cb (su_handler, NULL, su_handler->auth_cb_data);

	return FALSE;
}

/*
 * IO watcher for stdout, called whenever there is data to read from the backend.
 * This is where most of the actual IO handling happens.
 */
static gboolean
io_watch_stdout (GIOChannel *source, GIOCondition condition, SuHandler *su_handler)
{
	static GString *str = NULL;     /* Persistent buffer */

	gchar           buf[BUFSIZE];           /* Temporary buffer */
	gsize           bytes_read;
	GError          *gio_error = NULL;      /* Error returned by functions */
	GError          *error = NULL;          /* Error sent to callbacks */

	gboolean        reinit = FALSE;

	/* Initialize buffer */
	if (str == NULL) {
		str = g_string_new ("");
	}

	if (g_io_channel_read_chars (source, buf, BUFSIZE, &bytes_read, &gio_error) != G_IO_STATUS_NORMAL) {
		g_warning ("IO Channel read error: %s", gio_error->message);
		g_error_free (gio_error);

		return TRUE;
	}

	str = g_string_append_len (str, buf, bytes_read);

	/* In which state is the backend? */
	switch (su_handler->backend_state) {
		case SU_STATE_AUTH:
		{
			g_source_remove (su_handler->su_check_timeout_id);

			if (strstr (str->str, "Authentication failure") != NULL) {
				/* Authentication failed */
				error = g_error_new_literal (SU_ERROR, SU_ERROR_AUTH_FAILED,
                                             _("Authentication failed"));

				if (su_handler->auth_cb)
					su_handler->auth_cb (su_handler, error, su_handler->auth_cb_data);

				g_error_free (error);
			} else {
				/* Authentication successful */
				if (su_handler->auth_cb)
					su_handler->auth_cb (su_handler, NULL, su_handler->auth_cb_data);
			}
			reinit = TRUE;
		break;
		}

		case SU_STATE_NONE:
		{
			/* su is not asking for anything yet */
			if (is_string_complete (str->str, "assword: ", NULL)) {
				su_handler->backend_state = SU_STATE_AUTH;

				/* Pop the IO queue, i.e. send current password */
				io_queue_pop (su_handler->backend_stdin_queue, su_handler->backend_stdin);

				su_handler->su_check_timeout_id = g_timeout_add (3000, (GSourceFunc)maybe_su_is_successful_idle_cb, su_handler);

				reinit = TRUE;
			}
		break;
		}

		default:
			/* su has returned an error */
			reinit = TRUE;
		break;
	}

	if (reinit) {
		g_string_free (str, TRUE);
		str = NULL;
	}

	/* Continue calling us */
	return TRUE;
}

/*
 * }} Backend communication code
 */

/* Adds the current password to the IO queue */
static void
authenticate (SuHandler *su_handler)
{
	gchar *s;

	s = g_strdup_printf ("%s\n", su_handler->current_password);

	g_queue_push_tail (su_handler->backend_stdin_queue, s);
}

SuHandler *
su_init (void)
{
	SuHandler *su_handler;

	su_handler = g_new0 (SuHandler, 1);

	/* Initialize backend_pid. -1 means the backend is not running */
	su_handler->backend_pid = -1;

	/* Initialize IO Channels */
	su_handler->backend_stdin = NULL;
	su_handler->backend_stdout = NULL;

	/* Initialize write queue */
	su_handler->backend_stdin_queue = g_queue_new ();

	/* Initialize watchers */
	su_handler->backend_child_watch_id = 0;
	su_handler->backend_stdout_watch_id = 0;

	/* Initialize backend state */
	su_handler->backend_state = SU_STATE_NONE;

	return su_handler;
}

void
su_destroy (SuHandler *su_handler)
{
	g_queue_free (su_handler->backend_stdin_queue);
	stop_su (su_handler);
	g_free (su_handler);
}

void
su_authenticate (SuHandler     *su_handler,
                 const char    *user,
                 const char    *current_password,
                 SuCallback     cb,
                 const gpointer user_data)
{
	GError *error = NULL;

	/* Clear data from possible previous attempts to change password */
	su_handler->user = user;
	su_handler->current_password = current_password;
	su_handler->auth_cb = cb;
	su_handler->auth_cb_data = user_data;

	g_queue_foreach (su_handler->backend_stdin_queue, (GFunc) g_free, NULL);
	g_queue_clear (su_handler->backend_stdin_queue);

	/* Spawn backend */
	stop_su (su_handler);

	if (!spawn_su (su_handler, &error)) {
		g_warning ("%s", error->message);
		g_error_free (error);

		return;
	}

	authenticate (su_handler);

	/* Our IO watcher should now handle the rest */
}
