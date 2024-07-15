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
 *     Michael Wood <michael.g.wood@intel.com>
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "language-resources.h"
#include "cc-language-chooser.h"
#include "gis-language-page.h"

#include <locale.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

struct _GisLanguagePagePrivate
{
	GtkWidget *subtitle_label;
	GtkWidget *language_chooser;

	const gchar *new_locale_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GisLanguagePage, gis_language_page, GIS_TYPE_PAGE);


static void
language_changed_cb (CcLanguageChooser *chooser,
                     GParamSpec        *pspec,
                     gpointer           user_data)
{
	const char *language;
	GisLanguagePage *page = GIS_LANGUAGE_PAGE (user_data);
	GisLanguagePagePrivate *priv = page->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	language = cc_language_chooser_get_language (chooser);

	setenv("LANG", language, TRUE);
//	setenv("LC_ALL", language, TRUE);
//	setenv("LC_MESSAGES", language, TRUE);
	setlocale(LC_ALL, "");

	gis_page_manager_set_language (manager, language);

	gis_page_manager_locale_changed (manager);
}

static void
gis_language_page_shown (GisPage *page)
{
	const char *language;
	GisLanguagePage *self = GIS_LANGUAGE_PAGE (page);
	GisLanguagePagePrivate *priv = self->priv;
	GisPageManager *manager = GIS_PAGE (page)->manager;

	language = cc_language_chooser_get_language (CC_LANGUAGE_CHOOSER (priv->language_chooser));

	gis_page_manager_set_language (manager, language);
}

static void
gis_language_page_save_data (GisPage *page)
{
	GError *error = NULL;
	char *language = NULL;
	gchar *file = NULL, *data = NULL;

	language = gis_page_manager_get_language (page->manager);

	data = g_strdup_printf ("export LANG=%s", language);
	file = g_build_filename (g_get_home_dir (), ".xsessionrc", NULL);
	if (!g_file_set_contents (file, data, -1, &error)) {
		g_warning ("Unable to create %s: %s", file, error->message);
		g_clear_error (&error);
	}
	g_free (file);
	g_free (data);
	g_free (language);
}

static void
gis_language_page_constructed (GObject *object)
{
	GisLanguagePage *page = GIS_LANGUAGE_PAGE (object);
	GisLanguagePagePrivate *priv = page->priv;

	G_OBJECT_CLASS (gis_language_page_parent_class)->constructed (object);

	g_signal_connect (priv->language_chooser, "notify::language",
                      G_CALLBACK (language_changed_cb), page);

	gis_page_set_complete (GIS_PAGE (page), TRUE);

	gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_language_page_locale_changed (GisPage *page)
{
	GisLanguagePage *self = GIS_LANGUAGE_PAGE (page);

	gis_page_set_title (GIS_PAGE (page), _("Language Settings"));

	gtk_label_set_text (GTK_LABEL (self->priv->subtitle_label),
                        _("Set the user's language for login. "
                          "After logging in, you can change the user's language in the settings."));
}

static void
gis_language_page_dispose (GObject *object)
{
	G_OBJECT_CLASS (gis_language_page_parent_class)->dispose (object);
}

static void
gis_language_page_init (GisLanguagePage *page)
{
	GisLanguagePagePrivate *priv;

	page->priv = gis_language_page_get_instance_private (page);

	g_resources_register (language_get_resource ());

	g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);

	gtk_widget_init_template (GTK_WIDGET (page));

	gis_page_set_title (GIS_PAGE (page), _("Language Settings"));
}

static void
gis_language_page_class_init (GisLanguagePageClass *klass)
{
	GisPageClass *page_class = GIS_PAGE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = gis_language_page_constructed;
	object_class->dispose = gis_language_page_dispose;

	page_class->shown = gis_language_page_shown;
	page_class->save_data = gis_language_page_save_data;
	page_class->locale_changed = gis_language_page_locale_changed;

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
			"/kr/gooroom/initial-setup/pages/language/gis-language-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, subtitle_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, language_chooser);

}

GisPage *
gis_prepare_language_page (GisPageManager *manager)
{
	return g_object_new (GIS_TYPE_LANGUAGE_PAGE,
                         "manager", manager,
                         NULL);
}
