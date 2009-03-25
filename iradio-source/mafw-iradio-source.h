/*
 * This file is a part of MAFW
 *
 * Copyright (C) 2007, 2008, 2009 Nokia Corporation, all rights reserved.
 *
 * Contact: Visa Smolander <visa.smolander@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <libmafw/mafw-source.h>

#ifndef MAFW_IRADIO_SOURCE_H
#define MAFW_IRADIO_SOURCE_H

G_BEGIN_DECLS

#define MAFW_IRADIO_SOURCE_NAME "Mafw-IRadio-Source"
#define MAFW_IRADIO_SOURCE_UUID "iradiosource"
#define MAFW_IRADIO_SOURCE_PLUGIN_NAME "MAFW-IRadio-Source"

/*----------------------------------------------------------------------------
  GObject type conversion macros
  ----------------------------------------------------------------------------*/

#define MAFW_TYPE_IRADIO_SOURCE			\
	(mafw_iradio_source_get_type ())

#define MAFW_IRADIO_SOURCE(obj)						\
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), MAFW_TYPE_IRADIO_SOURCE,	\
				     MafwIradioSource))
#define MAFW_IS_IRADIO_SOURCE(obj)					\
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAFW_TYPE_IRADIO_SOURCE))

#define MAFW_IRADIO_SOURCE_CLASS(klass)					\
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_IRADIO_SOURCE,	\
				 MafwIradioSourceClass))

#define MAFW_IS_IRADIO_SOURCE_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_IRADIO_SOURCE))

#define MAFW_IRADIO_SOURCE_GET_CLASS(obj)					\
	(G_TYPE_INSTANCE_GET_CLASS ((obj), MAFW_TYPE_IRADIO_SOURCE,	\
				    MafwIradioSourceClass))

/*----------------------------------------------------------------------------
  Object structures
  ----------------------------------------------------------------------------*/

typedef struct _MafwIradioSource MafwIradioSource;
typedef struct _MafwIradioSourceClass MafwIradioSourceClass;
typedef struct _MafwIradioSourcePrivate MafwIradioSourcePrivate;

struct _MafwIradioSource {
	MafwSource parent;
	MafwIradioSourcePrivate *priv;
};

struct _MafwIradioSourceClass {
	MafwSourceClass parent_class;
};

/*----------------------------------------------------------------------------
  Public API
  ----------------------------------------------------------------------------*/

GObject *mafw_iradio_source_new(void);
GType mafw_iradio_source_get_type(void);

G_END_DECLS

#endif /* MAFW_IRADIO_SOURCE_H */

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
