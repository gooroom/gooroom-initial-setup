/*
 * Copyright (C) 2015-2020 Gooroom <gooroom@gooroom.kr>
 * Copyright (C) 2012 Red Hat
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gis-assistant.h"
#include "pages/language/gis-language-page.h"
#include "pages/eulas/gis-eulas-page.h"
#include "pages/account/gis-account-page.h"
#include "pages/network/gis-network-page.h"
#include "pages/goa/gis-goa-page.h"
#include "pages/summary/gis-summary-page.h"



struct _GisAssistantPrivate {
	GtkWidget *stack;
	GtkWidget *done;
	GtkWidget *forward;
	GtkWidget *backward;
	GtkWidget *skip;
	GtkWidget *title;
	GtkWidget *logo_image;

	GList *pages;
	GisPage *current_page;

	GisPageManager *manager;
};

typedef GisPage *(*PreparePage) (GisPageManager *manager);

typedef struct {
  const gchar *page_id;
  PreparePage  prepare_page_func;
} PageData;


static PageData page_table[] = {
	//{ "language", gis_prepare_language_page },
	{ "eula",     gis_prepare_eulas_page    },
	{ "network",  gis_prepare_network_page  },
	{ "account",  gis_prepare_account_page  },
	{ "goa",      gis_prepare_goa_page      },
	{ "summary",  gis_prepare_summary_page  },
	{ NULL, NULL }
};

G_DEFINE_TYPE_WITH_PRIVATE (GisAssistant, gis_assistant, GTK_TYPE_BOX)



static void
switch_to (GisAssistant *assistant, GisPage *page)
{
	if (!page)
		return;

	gtk_stack_set_visible_child (GTK_STACK (assistant->priv->stack), GTK_WIDGET (page));
}

static GisPage *
find_first_page (GisAssistant *assistant)
{
	GList *l = NULL;
	GisAssistantPrivate *priv = assistant->priv;

	l = g_list_first (priv->pages);
	if (l) {
		GisPage *page = GIS_PAGE (l->data);
		if (gis_page_should_show (page))
			return page;
	}

	return NULL;
}

static GisPage *
find_next_page (GisAssistant *assistant)
{
	GList *l = NULL;
	GisAssistantPrivate *priv = assistant->priv;

	l = g_list_find (priv->pages, priv->current_page);
	if (l) l = l->next;

	for (; l != NULL; l = l->next) {
		GisPage *page = GIS_PAGE (l->data);
		if (gis_page_should_show (page))
			return page;
	}

	return NULL;
}

static GisPage *
find_prev_page (GisAssistant *assistant)
{
	GList *l = NULL;
	GisAssistantPrivate *priv = assistant->priv;

	l = g_list_find (priv->pages, priv->current_page);
	if (l) l = l->prev;

	for (; l != NULL; l = l->prev) {
		GisPage *page = GIS_PAGE (l->data);
		if (gis_page_should_show (page))
			return page;
	}

	return NULL;
}

static void
set_suggested_action_sensitive (GtkWidget *widget, gboolean sensitive)
{
  gtk_widget_set_sensitive (widget, sensitive);
  if (sensitive)
    gtk_style_context_add_class (gtk_widget_get_style_context (widget), "suggested-action");
  else
    gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "suggested-action");
}

static void
set_navigation_button (GisAssistant *assistant, GtkWidget *widget)
{
	GisAssistantPrivate *priv = assistant->priv;

	gtk_widget_set_visible (priv->forward, (widget == priv->forward));
	gtk_widget_set_visible (priv->skip, (widget == priv->skip));
}

static void
update_titlebar (GisAssistant *assistant)
{
	const gchar *title;
	GisAssistantPrivate *priv = assistant->priv;

	title = gis_assistant_get_title (assistant);

	if (title)
		gtk_label_set_text (GTK_LABEL (priv->title), title);
}

static void
update_navigation_buttons (GisAssistant *assistant)
{
	gboolean is_last_page, is_first_page;
	GisAssistantPrivate *priv = assistant->priv;

	if (priv->current_page == NULL)
		return;

	is_first_page = (find_prev_page (assistant) == NULL);
	is_last_page = (find_next_page (assistant) == NULL);

	gtk_widget_set_visible (priv->backward, !is_first_page);
	gtk_widget_set_visible (priv->forward, !is_last_page);
	gtk_widget_set_visible (priv->skip, !is_last_page);
	gtk_widget_set_visible (priv->done, is_last_page);

	if (!is_last_page) {
		if (gis_page_get_complete (priv->current_page)) {
			set_suggested_action_sensitive (priv->forward, TRUE);
			set_navigation_button (assistant, priv->forward);
		} else if (gis_page_get_skippable (priv->current_page)) {
			set_suggested_action_sensitive (priv->skip, TRUE);
			set_navigation_button (assistant, priv->skip);
		} else {
			set_suggested_action_sensitive (priv->forward, FALSE);
			set_suggested_action_sensitive (priv->skip, FALSE);
			set_navigation_button (assistant, priv->forward);
		}
	}
}

static void
update_current_page (GisAssistant *assistant,
                     GisPage      *page)
{
	GisAssistantPrivate *priv = assistant->priv;

	if (priv->current_page == page)
		return;

	priv->current_page = page;

	update_titlebar (assistant);
	update_navigation_buttons (assistant);
	gtk_widget_grab_focus (priv->forward);

	if (page)
		gis_page_shown (page);
}

static void
go_forward_button_cb (GtkWidget *button,
                      gpointer   user_data)
{
	gis_assistant_next_page (GIS_ASSISTANT (user_data));
}

static void
go_backward_button_cb (GtkWidget *button,
                       gpointer   user_data)
{
	gis_assistant_prev_page (GIS_ASSISTANT (user_data));
}

static void
done_button_clicked_cb (GtkWidget *button,
                        gpointer   user_data)
{
	GisAssistant *assistant = GIS_ASSISTANT (user_data);
	GisAssistantPrivate *priv = assistant->priv;

	gtk_widget_set_sensitive (priv->backward, FALSE);
	gtk_widget_set_sensitive (priv->done, FALSE);

	gis_assistant_save_data (assistant);

	gtk_widget_set_sensitive (priv->backward, TRUE);
	gtk_widget_set_sensitive (priv->done, TRUE);
}

static void
page_notify_cb (GisPage      *page,
                GParamSpec       *pspec,
                GisAssistant *assistant)
{
	GisAssistantPrivate *priv = assistant->priv;

	if (page != priv->current_page)
		return;

	if (strcmp (pspec->name, "title") == 0) {
		update_titlebar (assistant);
	} else {
		update_navigation_buttons (assistant);
	}
}

static void
current_page_changed_cb (GObject    *gobject,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
	GisAssistant *assistant = GIS_ASSISTANT (user_data);
	GtkWidget *new_page = gtk_stack_get_visible_child (GTK_STACK (gobject));

	update_current_page (assistant, GIS_PAGE (new_page));
}

static gboolean
change_current_page_idle (gpointer user_data)
{
	GisAssistant *assistant = GIS_ASSISTANT (user_data);

	current_page_changed_cb (G_OBJECT (assistant->priv->stack), NULL, assistant);

	return FALSE;
}

static void
go_next_page_cb (GisPageManager *manager,
                 gpointer        user_data)
{
	gis_assistant_next_page (GIS_ASSISTANT (user_data));
}

static void
locale_changed_cb (GisPageManager *manager,
                   gpointer        user_data)
{
	gis_assistant_locale_changed (GIS_ASSISTANT (user_data));
}

static void
gis_assistant_ui_setup (GisAssistant *assistant)
{
	GdkPixbuf *pixbuf = NULL;

	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/kr/gooroom/initial-setup/logo",
                                                    120, 14, FALSE, NULL);
                                                    //80, 13, FALSE, NULL);
	if (pixbuf) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (assistant->priv->logo_image), pixbuf);
		g_object_unref (pixbuf);
	}

	gtk_widget_hide (assistant->priv->done);
}

static void
gis_assistant_finalize (GObject *object)
{
	GisAssistant *assistant = GIS_ASSISTANT (object);
	GisAssistantPrivate *priv = assistant->priv;

	g_clear_object (&priv->manager);

	if (priv->pages) {
		g_list_free (priv->pages);
		priv->pages = NULL;
	}

	G_OBJECT_CLASS (gis_assistant_parent_class)->finalize (object);
}

static void
gis_assistant_init (GisAssistant *assistant)
{
	PageData *page_data;
	GisAssistantPrivate *priv;

	priv = assistant->priv = gis_assistant_get_instance_private (assistant);

	gtk_widget_init_template (GTK_WIDGET (assistant));

	gis_assistant_ui_setup (assistant);

	priv->manager = gis_page_manager_new ();

	page_data = page_table;
	for (; page_data->page_id != NULL; ++page_data) {
		GisPage *page = page_data->prepare_page_func (priv->manager);
		if (!page)
			continue;

		gis_assistant_add_page (assistant, page);
	}

	g_signal_connect (priv->manager, "go-next", G_CALLBACK (go_next_page_cb), assistant);
	g_signal_connect (priv->manager, "locale-changed", G_CALLBACK (locale_changed_cb), assistant);

	g_signal_connect (priv->stack, "notify::visible-child",
                      G_CALLBACK (current_page_changed_cb), assistant);

	g_signal_connect (priv->forward, "clicked", G_CALLBACK (go_forward_button_cb), assistant);
	g_signal_connect (priv->backward, "clicked", G_CALLBACK (go_backward_button_cb), assistant);
	g_signal_connect (priv->skip, "clicked", G_CALLBACK (go_forward_button_cb), assistant);
	g_signal_connect (priv->done, "clicked", G_CALLBACK (done_button_clicked_cb), assistant);

	g_idle_add ((GSourceFunc) change_current_page_idle, assistant);
}

static void
gis_assistant_class_init (GisAssistantClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->finalize = gis_assistant_finalize;

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                                 "/kr/gooroom/initial-setup/gis-assistant.ui");

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, stack);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, done);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, forward);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, backward);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, skip);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, title);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAssistant, logo_image);
}

GtkWidget *
gis_assistant_new (void)
{
	return g_object_new (GIS_TYPE_ASSISTANT, NULL);
}

void
gis_assistant_add_page (GisAssistant *assistant,
                        GisPage      *page)
{
	GisAssistantPrivate *priv = assistant->priv;

	priv->pages = g_list_append (priv->pages, page);

	g_signal_connect (page, "notify", G_CALLBACK (page_notify_cb), assistant);

	gtk_container_add (GTK_CONTAINER (priv->stack), GTK_WIDGET (page));

	gtk_widget_set_halign (GTK_WIDGET (page), GTK_ALIGN_FILL);
	gtk_widget_set_valign (GTK_WIDGET (page), GTK_ALIGN_FILL);
}

GisPage *
gis_assistant_get_current_page (GisAssistant *assistant)
{
	return assistant->priv->current_page;
}

const gchar *
gis_assistant_get_title (GisAssistant *assistant)
{
	GisAssistantPrivate *priv = assistant->priv;

	if (priv->current_page != NULL)
		return gis_page_get_title (priv->current_page);
	else
		return "";
}

void
gis_assistant_locale_changed (GisAssistant *assistant)
{
	GList *l = NULL;
	GisAssistantPrivate *priv = assistant->priv;

	gtk_button_set_label (GTK_BUTTON (priv->backward), _("Prev"));
	gtk_button_set_label (GTK_BUTTON (priv->forward), _("Next"));
	gtk_button_set_label (GTK_BUTTON (priv->skip), _("Skip"));
	gtk_button_set_label (GTK_BUTTON (priv->done), _("Done"));

	update_titlebar (assistant);

	priv->pages = g_list_first (priv->pages);
	for (l = priv->pages; l; l = l->next)
		gis_page_locale_changed (l->data);
}

void
gis_assistant_next_page (GisAssistant *assistant)
{
	GisPage *next_page;
	GisAssistantPrivate *priv = assistant->priv;

	next_page = find_next_page (assistant);

	if (next_page && priv->current_page && (priv->current_page != next_page)) {
		if (gis_page_pre_next (priv->current_page))
			switch_to (assistant, next_page);
	}
}

void
gis_assistant_prev_page (GisAssistant *assistant)
{
	GisPage *prev_page;
	GisAssistantPrivate *priv = assistant->priv;

	prev_page = find_prev_page (assistant);

	switch_to (assistant, prev_page);
}

void
gis_assistant_save_data (GisAssistant *assistant)
{
	GList *l = NULL;
	GisAssistantPrivate *priv = assistant->priv;

	priv->pages = g_list_first (priv->pages);
	for (l = priv->pages; l; l = l->next) {
		gis_page_save_data (l->data);
	}
}
