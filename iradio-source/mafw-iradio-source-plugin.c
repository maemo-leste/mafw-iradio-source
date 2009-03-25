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
 
#include <libmafw/mafw.h>
#include <gmodule.h>

#include "mafw-iradio-source.h"
#include "mafw-iradio-vendor-setup.h"

/*----------------------------------------------------------------------------
  MAFW Plugin construction
  ----------------------------------------------------------------------------*/
static gboolean iradio_plugin_initialize(MafwRegistry *registry,
					    GError **error)
{
	MafwIradioSource *self = MAFW_IRADIO_SOURCE(
					mafw_iradio_source_new());

	g_debug("Mafw Iradio-Source initialization");
	mafw_registry_add_extension(registry, MAFW_EXTENSION(self));

	return TRUE;
}

static void iradio_plugin_deinitialize(GError **error)
{
}


G_MODULE_EXPORT MafwPluginDescriptor mafw_iradio_source_plugin_description = {
	{ .name = MAFW_IRADIO_SOURCE_PLUGIN_NAME },
	.initialize = iradio_plugin_initialize,
	.deinitialize = iradio_plugin_deinitialize,
};
