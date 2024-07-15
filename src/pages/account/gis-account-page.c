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


#define VALIDATION_TIMEOUT 100

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "account-resources.h"
#include "gis-account-page.h"
#include "um-utils.h"
#include "pw-utils.h"

struct _GisAccountPagePrivate {
	GtkWidget *subtitle_label;
	GtkWidget *username_label;
	GtkWidget *password_label;
	GtkWidget *realname_label;
	GtkWidget *password_confirm_label;
	GtkWidget *username_entry;
	GtkWidget *password_entry;
	GtkWidget *realname_entry;
	GtkWidget *password_confirm_entry;
	GtkWidget *error_label;

	gboolean is_valid_username;
	gboolean is_valid_realname;
	gboolean is_valid_password;
	gboolean is_valid_confirm_password;

	guint validation_timeout_id;

	gchar *realname_entry_text;
};

G_DEFINE_TYPE_WITH_PRIVATE (GisAccountPage, gis_account_page, GIS_TYPE_PAGE);



static void
gis_account_page_shown (GisPage *page)
{
	GisAccountPage *self = GIS_ACCOUNT_PAGE (page);

	gtk_widget_grab_focus (GTK_WIDGET (self->priv->username_entry));
}

static gboolean
update_page_validation (GisAccountPage *page)
{
	gboolean valid = FALSE;
	GisAccountPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	valid = (priv->is_valid_username &&
             priv->is_valid_realname &&
             priv->is_valid_password &&
             priv->is_valid_confirm_password);

	if (valid) {
		const gchar *realname, *username, *password;
		realname = (priv->realname_entry_text != NULL) ? priv->realname_entry_text : NULL;
		username = gtk_entry_get_text (GTK_ENTRY (priv->username_entry));
		password = gtk_entry_get_text (GTK_ENTRY (priv->password_entry));

		gis_page_manager_set_user_info (manager, realname, username, password);
	}

	gis_page_set_complete (GIS_PAGE (page), valid);
}

static gboolean
validate_cb (gpointer data)
{
	gint strength_level;
	gchar *tip = NULL;
	const gchar *realname, *username;
	const gchar *password, *verify, *hint;

	GisAccountPage *page = GIS_ACCOUNT_PAGE (data);
	GisAccountPagePrivate *priv = page->priv;

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = 0;
	priv->is_valid_username = FALSE;
	priv->is_valid_realname = FALSE;
	priv->is_valid_password = FALSE;
	priv->is_valid_confirm_password = FALSE;

	/* check username */
	realname = (priv->realname_entry_text != NULL) ? priv->realname_entry_text : "";
	username = gtk_entry_get_text (GTK_ENTRY (priv->username_entry));
	password = gtk_entry_get_text (GTK_ENTRY (priv->password_entry));
	verify = gtk_entry_get_text (GTK_ENTRY (priv->password_confirm_entry));

	priv->is_valid_username = is_valid_username (username, &tip);
	gtk_widget_set_sensitive (priv->password_entry, priv->is_valid_username);
	gtk_widget_set_sensitive (priv->password_confirm_entry, priv->is_valid_username);
	if (priv->is_valid_username) {
		set_entry_validation_checkmark (GTK_ENTRY (priv->username_entry));
	} else {
		if (tip) {
			gtk_label_set_text (GTK_LABEL (priv->error_label), tip);
		}
		goto done;
	}

	/* check realname */
	priv->is_valid_realname = is_valid_realname (realname);
	if (priv->is_valid_realname) {
		set_entry_validation_checkmark (GTK_ENTRY (priv->realname_entry));
	} else {
		goto done;
	}

	/* check password */
	if (strlen (password) > 0) {
		pw_strength (password, NULL, username, &hint, &strength_level);

		priv->is_valid_password = (strlen (password) && strength_level > 1);
		if (priv->is_valid_password) {
			set_entry_validation_checkmark (GTK_ENTRY (priv->password_entry));
		} else {
			if (hint) {
				gtk_label_set_text (GTK_LABEL (priv->error_label), hint);
			}
			goto done;
		}
	}

	/* check confirm password */
	if (strlen (verify) > 0) {
		priv->is_valid_confirm_password = g_str_equal (password, verify);
		if (priv->is_valid_confirm_password) {
			set_entry_validation_checkmark (GTK_ENTRY (priv->password_confirm_entry));
		} else {
			gtk_label_set_label (GTK_LABEL (priv->error_label), _("The passwords do not match."));
			goto done;
		}
	}

done:
	update_page_validation (page);

	return FALSE;
}

static void
username_entry_changed_cb (GisAccountPage *page)
{
	const gchar *username;
	GisAccountPagePrivate *priv = page->priv;

	username = gtk_entry_get_text (GTK_ENTRY (priv->username_entry));

	gtk_label_set_text (GTK_LABEL (priv->error_label), "");

	clear_entry_validation_error (GTK_ENTRY (priv->username_entry));

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = g_timeout_add (VALIDATION_TIMEOUT, (GSourceFunc)validate_cb, page);
}

static void
realname_entry_changed_cb (GisAccountPage *page)
{
	GisAccountPagePrivate *priv = page->priv;

	gtk_label_set_text (GTK_LABEL (priv->error_label), "");

	clear_entry_validation_error (GTK_ENTRY (priv->realname_entry));

	g_clear_pointer (&priv->realname_entry_text, (GDestroyNotify) g_free);
	priv->realname_entry_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->realname_entry)));

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = g_timeout_add (VALIDATION_TIMEOUT, (GSourceFunc)validate_cb, page);
}

static void
realname_entry_preedit_changed_cb (GtkEntry *entry,
                                   gchar    *preedit,
                                   gpointer  user_data)
{
	const gchar *entry_text;
	GisAccountPage *page = GIS_ACCOUNT_PAGE (user_data);
	GisAccountPagePrivate *priv = page->priv;

	gtk_label_set_text (GTK_LABEL (priv->error_label), "");

	clear_entry_validation_error (GTK_ENTRY (priv->realname_entry));

	entry_text = gtk_entry_get_text (GTK_ENTRY (priv->realname_entry));

	g_clear_pointer (&priv->realname_entry_text, (GDestroyNotify) g_free);
	priv->realname_entry_text = g_strdup_printf ("%s%s", entry_text, preedit);

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = g_timeout_add (VALIDATION_TIMEOUT, (GSourceFunc)validate_cb, page);
}

static void
password_entry_changed_cb (GisAccountPage *page)
{
	GisAccountPagePrivate *priv = page->priv;

	gtk_label_set_text (GTK_LABEL (priv->error_label), "");

	clear_entry_validation_error (GTK_ENTRY (priv->password_entry));
	clear_entry_validation_error (GTK_ENTRY (priv->password_confirm_entry));

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = g_timeout_add (VALIDATION_TIMEOUT, (GSourceFunc)validate_cb, page);
}

static void
password_confirm_entry_changed_cb (GisAccountPage *page)
{
	GisAccountPagePrivate *priv = page->priv;

	gtk_label_set_text (GTK_LABEL (priv->error_label), "");

	clear_entry_validation_error (GTK_ENTRY (priv->password_confirm_entry));

	g_clear_handle_id (&priv->validation_timeout_id, g_source_remove);
	priv->validation_timeout_id = g_timeout_add (VALIDATION_TIMEOUT, (GSourceFunc)validate_cb, page);
}

static void
entry_activated_cb (GisAccountPage *page)
{
	gboolean valid;
	GisAccountPagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	valid = (priv->is_valid_username &&
             priv->is_valid_realname &&
             priv->is_valid_password &&
             priv->is_valid_confirm_password);

	if (valid)
		gis_page_manager_go_next (manager);
}

static void
gis_account_page_finalize (GObject *object)
{
	GisAccountPage *self = GIS_ACCOUNT_PAGE (object);
	GisAccountPagePrivate *priv = self->priv;

	g_clear_pointer (&priv->realname_entry_text, (GDestroyNotify) g_free);

	G_OBJECT_CLASS (gis_account_page_parent_class)->finalize (object);
}

static void
gis_account_page_constructed (GObject *object)
{
	GisAccountPage *self = GIS_ACCOUNT_PAGE (object);
	GisAccountPagePrivate *priv = self->priv;

	G_OBJECT_CLASS (gis_account_page_parent_class)->constructed (object);

	gtk_widget_set_sensitive (priv->password_entry, FALSE);
	gtk_widget_set_sensitive (priv->password_confirm_entry, FALSE);

	g_signal_connect_swapped (priv->username_entry, "changed",
                              G_CALLBACK (username_entry_changed_cb), self);
	g_signal_connect_swapped (priv->realname_entry, "changed",
                              G_CALLBACK (realname_entry_changed_cb), self);
    g_signal_connect (priv->realname_entry, "preedit-changed",
                      G_CALLBACK (realname_entry_preedit_changed_cb), self);
	g_signal_connect_swapped (priv->password_entry, "changed",
                              G_CALLBACK (password_entry_changed_cb), self);
	g_signal_connect_swapped (priv->password_confirm_entry, "changed",
                              G_CALLBACK (password_confirm_entry_changed_cb), self);

	g_signal_connect_swapped (priv->username_entry, "activate",
                              G_CALLBACK (entry_activated_cb), self);
	g_signal_connect_swapped (priv->realname_entry, "activate",
                              G_CALLBACK (entry_activated_cb), self);
	g_signal_connect_swapped (priv->password_entry, "activate",
                              G_CALLBACK (entry_activated_cb), self);
	g_signal_connect_swapped (priv->password_confirm_entry, "activate",
                              G_CALLBACK (entry_activated_cb), self);

	update_page_validation (self);

	gtk_widget_show (GTK_WIDGET (self));
}

static void
gis_account_page_locale_changed (GisPage *page)
{
	GisAccountPagePrivate *priv = GIS_ACCOUNT_PAGE (page)->priv;

	gis_page_set_title (GIS_PAGE (page), _("Creating Accounts"));

	gtk_label_set_text (GTK_LABEL (priv->subtitle_label),
                        _("Enter the user's information for login. After logging in, "
                          "you can change your information in Settings."));

	gtk_label_set_text (GTK_LABEL (priv->username_label), _("User Name"));
	gtk_label_set_text (GTK_LABEL (priv->realname_label), _("Real Name"));
	gtk_label_set_text (GTK_LABEL (priv->password_label), _("Password"));
	gtk_label_set_text (GTK_LABEL (priv->password_confirm_label), _("Retype Password"));
}

static gboolean
gis_account_page_pre_next (GisPage *page)
{
	const gchar *realname, *username, *password;
	GisAccountPagePrivate *priv = GIS_ACCOUNT_PAGE (page)->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	realname = (priv->realname_entry_text != NULL) ? priv->realname_entry_text : NULL;
	username = gtk_entry_get_text (GTK_ENTRY (priv->username_entry));
	password = gtk_entry_get_text (GTK_ENTRY (priv->password_entry));

	gis_page_manager_set_user_info (manager, realname, username, password);

	return TRUE;
}

static void
gis_account_page_init (GisAccountPage *page)
{
	GisAccountPagePrivate *priv;
	priv = page->priv = gis_account_page_get_instance_private (page);

	priv->is_valid_username = FALSE;
	priv->is_valid_realname = FALSE;
	priv->is_valid_password = FALSE;
	priv->is_valid_confirm_password = FALSE;
	priv->realname_entry_text = NULL;
	priv->validation_timeout_id = 0;

	g_resources_register (account_get_resource ());

	gtk_widget_init_template (GTK_WIDGET (page));

	gis_page_set_title (GIS_PAGE (page), _("Creating Accounts"));
}

static void
gis_account_page_class_init (GisAccountPageClass *klass)
{
	GisPageClass *page_class = GIS_PAGE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
			"/kr/gooroom/initial-setup/pages/account/gis-account-page.ui");

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, subtitle_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, username_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, realname_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, password_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, password_confirm_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, username_entry);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, realname_entry);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, password_entry);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, password_confirm_entry);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAccountPage, error_label);

	page_class->pre_next = gis_account_page_pre_next;
	page_class->locale_changed = gis_account_page_locale_changed;
	page_class->shown = gis_account_page_shown;

	object_class->constructed = gis_account_page_constructed;
	object_class->finalize = gis_account_page_finalize;
}

GisPage *
gis_prepare_account_page (GisPageManager *manager)
{
	return g_object_new (GIS_TYPE_ACCOUNT_PAGE,
                         "manager", manager,
                         NULL);
}
