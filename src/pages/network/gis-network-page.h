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

#ifndef __GIS_NETWORK_PAGE_H__
#define __GIS_NETWORK_PAGE_H__

#include "gis-page.h"

G_BEGIN_DECLS

#define GIS_TYPE_NETWORK_PAGE            (gis_network_page_get_type ())
#define GIS_NETWORK_PAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIS_TYPE_NETWORK_PAGE, GisNetworkPage))
#define GIS_NETWORK_PAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GIS_TYPE_NETWORK_PAGE, GisNetworkPageClass))
#define GIS_IS_NETWORK_PAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GIS_TYPE_NETWORK_PAGE))
#define GIS_IS_NETWORK_PAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GIS_TYPE_NETWORK_PAGE))
#define GIS_NETWORK_PAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GIS_TYPE_NETWORK_PAGE, GisNetworkPageClass))

typedef struct _GisNetworkPage        GisNetworkPage;
typedef struct _GisNetworkPageClass   GisNetworkPageClass;
typedef struct _GisNetworkPagePrivate GisNetworkPagePrivate;

struct _GisNetworkPage
{
	GisPage __parent__;

	GisNetworkPagePrivate *priv;
};

struct _GisNetworkPageClass
{
	GisPageClass __parent_class__;
};

GType gis_network_page_get_type (void);

GisPage *gis_prepare_network_page (GisPageManager *manager);

G_END_DECLS

#endif /* __GIS_NETWORK_PAGE_H__ */
