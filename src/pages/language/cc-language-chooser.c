/*
 * Copyright (C) 2013 Red Hat
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
 *     Matthias Clasen <mclasen@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <gtk/gtk.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>

#include "language-resources.h"
#include "cc-language-chooser.h"
#include "cc-common-language.h"
#include "cc-util.h"

#include <glib-object.h>

struct _CcLanguageChooserPrivate {
	GtkWidget *language_list;

	gchar *language;
};

G_DEFINE_TYPE_WITH_PRIVATE (CcLanguageChooser, cc_language_chooser, GTK_TYPE_BOX);

enum {
	PROP_0,
	PROP_LANGUAGE,
	PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum {
	CONFIRM,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	GtkWidget *box;
	GtkWidget *checkmark;

	gchar *locale_id;
	gchar *locale_name;
	gchar *locale_current_name;
	gchar *locale_untranslated_name;
	gchar *sort_key;
	gboolean is_extra;
} LanguageWidget;



static LanguageWidget *
get_language_widget (GtkWidget *widget)
{
	return g_object_get_data (G_OBJECT (widget), "language-widget");
}

static GtkWidget *
padded_label_new (char *text)
{
	GtkWidget *widget;
	widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign (widget, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top (widget, 10);
	gtk_widget_set_margin_bottom (widget, 10);
	gtk_box_pack_start (GTK_BOX (widget), gtk_label_new (text), FALSE, FALSE, 0);
	return widget;
}

static void
language_widget_free (gpointer data)
{
	LanguageWidget *widget = data;

	/* This is called when the box is destroyed,
	 * so don't bother destroying the widget and
	 * children again. */
	g_free (widget->locale_id);
	g_free (widget->locale_name);
	g_free (widget->locale_current_name);
	g_free (widget->locale_untranslated_name);
	g_free (widget->sort_key);
	g_free (widget);
}

static GtkWidget *
language_widget_new (const char *locale_id)
{
	GtkWidget *label;
	gchar *locale_name, *locale_current_name, *locale_untranslated_name;
	gchar *language = NULL;
	gchar *language_name;
	gchar *country = NULL;
	gchar *country_name = NULL;
	LanguageWidget *widget = g_new0 (LanguageWidget, 1);

	if (!gnome_parse_locale (locale_id, &language, &country, NULL, NULL))
		return NULL;

	language_name = gnome_get_language_from_code (language, locale_id);

	if (country)
		country_name = gnome_get_country_from_code (country, locale_id);

	locale_name = gnome_get_language_from_locale (locale_id, locale_id);
	locale_current_name = gnome_get_language_from_locale (locale_id, NULL);
	locale_untranslated_name = gnome_get_language_from_locale (locale_id, "C");

	widget->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_margin_top (widget->box, 10);
	gtk_widget_set_margin_bottom (widget->box, 10);
	gtk_widget_set_margin_start (widget->box, 10);
	gtk_widget_set_margin_end (widget->box, 10);
	gtk_widget_set_halign (widget->box, GTK_ALIGN_FILL);

	label = gtk_label_new (language_name);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 30);
	gtk_label_set_xalign (GTK_LABEL (label), 0);
	gtk_box_pack_start (GTK_BOX (widget->box), label, FALSE, FALSE, 0);

	widget->checkmark = gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
	gtk_box_pack_start (GTK_BOX (widget->box), widget->checkmark, FALSE, FALSE, 0);
	gtk_widget_show (widget->checkmark);

	if (country_name) {
		label = gtk_label_new (country_name);
		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (label), 30);
		gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_widget_set_halign (label, GTK_ALIGN_END);
		gtk_box_pack_end (GTK_BOX (widget->box), label, FALSE, FALSE, 0);
	}

	widget->locale_id = g_strdup (locale_id);
	widget->locale_name = locale_name;
	widget->locale_current_name = locale_current_name;
	widget->locale_untranslated_name = locale_untranslated_name;
	widget->sort_key = cc_util_normalize_casefold_and_unaccent (locale_name);

	g_object_set_data_full (G_OBJECT (widget->box), "language-widget", widget,
			language_widget_free);

	g_free (language);
	g_free (language_name);
	g_free (country);
	g_free (country_name);

	return widget->box;
}

static void
sync_checkmark (GtkWidget *row,
                gpointer   user_data)
{
	GtkWidget *child;
	LanguageWidget *widget;
	gchar *locale_id;
	gboolean should_be_visible;

	child = gtk_bin_get_child (GTK_BIN (row));
	widget = get_language_widget (child);

	if (widget == NULL)
		return;

	locale_id = user_data;
	should_be_visible = g_str_equal (widget->locale_id, locale_id);
	gtk_widget_set_opacity (widget->checkmark, should_be_visible ? 1.0 : 0.0);
}

static void
sync_all_checkmarks (CcLanguageChooser *chooser)
{
	CcLanguageChooserPrivate *priv = chooser->priv;

	gtk_container_foreach (GTK_CONTAINER (priv->language_list), sync_checkmark, priv->language);
}

static void
add_one_language (CcLanguageChooser *chooser,
                  const char        *locale_id)
{
	CcLanguageChooserPrivate *priv = chooser->priv;
	GtkWidget *widget;

	if (!cc_common_language_has_font (locale_id)) {
		return;
	}

	widget = language_widget_new (locale_id);
	if (widget)
		gtk_container_add (GTK_CONTAINER (priv->language_list), widget);
}

static void
add_languages (CcLanguageChooser  *chooser,
               char               **locale_ids)
{
	CcLanguageChooserPrivate *priv = chooser->priv;

	while (*locale_ids) {
		const gchar *locale_id;

		locale_id = *locale_ids;
		locale_ids ++;

		add_one_language (chooser, locale_id);
	}

	gtk_widget_show_all (priv->language_list);
}

static void
add_all_languages (CcLanguageChooser *chooser)
{
	char **locale_ids;

	locale_ids = gnome_get_all_locales ();

	add_languages (chooser, locale_ids);

	g_strfreev (locale_ids);
}

static gint
sort_languages (GtkListBoxRow *a,
                GtkListBoxRow *b,
                gpointer       data)
{
	LanguageWidget *la, *lb;

	la = get_language_widget (gtk_bin_get_child (GTK_BIN (a)));
	lb = get_language_widget (gtk_bin_get_child (GTK_BIN (b)));

	if (la == NULL)
		return 1;

	if (lb == NULL)
		return -1;

	return strcmp (la->sort_key, lb->sort_key);
}

static void
set_locale_id (CcLanguageChooser *chooser,
               const gchar       *new_locale_id)
{
	CcLanguageChooserPrivate *priv = chooser->priv;

	if (g_strcmp0 (priv->language, new_locale_id) == 0)
		return;

	g_free (priv->language);
	priv->language = g_strdup (new_locale_id);

	sync_all_checkmarks (chooser);

	g_object_notify_by_pspec (G_OBJECT (chooser), obj_props[PROP_LANGUAGE]);
}

static gboolean
confirm_choice (gpointer data)
{
	GtkWidget *widget = data;

	g_signal_emit (widget, signals[CONFIRM], 0);

	return G_SOURCE_REMOVE;
}

static void
row_activated (GtkListBox        *box,
               GtkListBoxRow     *row,
               CcLanguageChooser *chooser)
{
	CcLanguageChooserPrivate *priv = chooser->priv;
	GtkWidget *child;
	LanguageWidget *widget;

	if (row == NULL)
		return;

	child = gtk_bin_get_child (GTK_BIN (row));
	widget = get_language_widget (child);
	if (widget == NULL)
		return;

	if (g_strcmp0 (priv->language, widget->locale_id) == 0)
		g_idle_add (confirm_choice, chooser);
	else
		set_locale_id (chooser, widget->locale_id);
}

static void
update_header_func (GtkListBoxRow *child,
                    GtkListBoxRow *before,
                    gpointer       user_data)
{
	GtkWidget *header;

	if (before == NULL)
		return;

	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (child, header);
	gtk_widget_show (header);
}

static void
cc_language_chooser_finalize (GObject *object)
{
	CcLanguageChooser *chooser = CC_LANGUAGE_CHOOSER (object);

	g_free (chooser->priv->language);

	G_OBJECT_CLASS (cc_language_chooser_parent_class)->finalize (object);
}

static void
cc_language_chooser_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
	CcLanguageChooser *chooser = CC_LANGUAGE_CHOOSER (object);

	switch (prop_id) {
		case PROP_LANGUAGE:
			g_value_set_string (value, cc_language_chooser_get_language (chooser));
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
cc_language_chooser_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	CcLanguageChooser *chooser = CC_LANGUAGE_CHOOSER (object);

	switch (prop_id) {
		case PROP_LANGUAGE:
			cc_language_chooser_set_language (chooser, g_value_get_string (value));
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
cc_language_chooser_constructed (GObject *object)
{
	CcLanguageChooser *chooser = CC_LANGUAGE_CHOOSER (object);
	CcLanguageChooserPrivate *priv = chooser->priv;

	G_OBJECT_CLASS (cc_language_chooser_parent_class)->constructed (object);

	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->language_list),
                                sort_languages, chooser, NULL);
	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->language_list),
                                  update_header_func, chooser, NULL);

	add_all_languages (chooser);

	g_signal_connect (priv->language_list, "row-activated", G_CALLBACK (row_activated), chooser);

	if (priv->language == NULL)
		priv->language = cc_common_language_get_current_language ();

	sync_all_checkmarks (chooser);
}

static void
cc_language_chooser_init (CcLanguageChooser *chooser)
{
	chooser->priv = cc_language_chooser_get_instance_private (chooser);

	g_resources_register (language_get_resource ());

	gtk_widget_init_template (GTK_WIDGET (chooser));
}

static void
cc_language_chooser_class_init (CcLanguageChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
			"/kr/gooroom/initial-setup/pages/language/cc-language-chooser.ui");

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), CcLanguageChooser, language_list);

	object_class->finalize = cc_language_chooser_finalize;
	object_class->get_property = cc_language_chooser_get_property;
	object_class->set_property = cc_language_chooser_set_property;
	object_class->constructed = cc_language_chooser_constructed;

	signals[CONFIRM] = g_signal_new ("confirm",
                                     G_TYPE_FROM_CLASS (object_class),
                                     G_SIGNAL_RUN_FIRST,
                                     G_STRUCT_OFFSET (CcLanguageChooserClass, confirm),
                                     NULL, NULL,
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE, 0);

	obj_props[PROP_LANGUAGE] = g_param_spec_string ("language", "", "", "",
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

const gchar *
cc_language_chooser_get_language (CcLanguageChooser *chooser)
{
	return chooser->priv->language;
}

void
cc_language_chooser_set_language (CcLanguageChooser *chooser,
                                  const gchar        *language)
{
	set_locale_id (chooser, language);
}