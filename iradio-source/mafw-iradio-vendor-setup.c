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
#include <libmafw/mafw-db.h>
#include <libmafw/mafw-metadata-serializer.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>

#include "mafw-iradio-source.h"
#include "mafw-iradio-vendor-setup.h"

/*---------------------------------------------------------------------------
  Macro definitions
  ---------------------------------------------------------------------------*/

/** The CONFML file that is parsed by this parser */
const gchar *vendor_setup_path="/usr/share/pre-installed/mafw-iradio-source-bookmarks/";

/* XML nodes that the parser recognizes from the CONFML file format */
#define NODE_CONFIGURATION "configuration"
#define NODE_DATA "data"
#define NODE_IRADIO_BOOKMARKS "mafw-iradio-source-bookmarks"
#define NODE_CHANNEL "IRadioChannel"
#define NODE_VIDEO "VideoBookmark"
#define NODE_NAME "Name"
#define NODE_URI "URI"
#define NODE_DURATION "Duration"
#define NODE_ICON "Icon"
#define NODE_LOCALPATH "localPath"
#define NODE_TARGETPATH "targetPath"

/* Dummy MIME types for audio & video */
#define MIME_AUDIO "audio/unknown"
#define MIME_VIDEO "video/unknown"

/*---------------------------------------------------------------------------
  Bookmark insertion
  ---------------------------------------------------------------------------*/

static void mafw_iradio_vendor_bookmark_created(MafwSource *self,
						 const gchar *object_id,
						 gpointer user_data,
						 const GError *error)
{
	if (error != NULL)
	{
		g_warning("Unable to create object from vendor bookmarks: %s",
			  error->message);
	}
	else
	{
		g_warning("Object created: %s", object_id);
	}
}

/**
 * mafw_iradio_create_bookmark_object:
 *
 * @self: An iradio source that the object is created to
 * @metadata: Metadata for the created item
 *
 * Creates a new object into the iradio source's database to be available for
 * browsing & metadata fetching.
 */
static void mafw_iradio_create_bookmark_object(MafwSource* self,
						GHashTable* metadata,
						gboolean check_dups)
{
	time_t curtime = time(NULL);

	g_assert(self != NULL);
	g_assert(metadata != NULL);

	mafw_metadata_add_long(metadata, MAFW_METADATA_KEY_ADDED,
					       curtime);
	if (check_dups)
	{
		gpointer value;
		
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		if (value)
		{
			gchar *serialized_data;
			gsize str_size = 0;
			sqlite3_stmt *stmt_dupfind = mafw_db_prepare("SELECT "
					"id FROM "
					IRADIO_TABLE " WHERE key = '" 
					MAFW_METADATA_KEY_URI "' AND value = :value");
		
			serialized_data = mafw_metadata_val_freeze(value, &str_size);
			mafw_db_bind_blob(stmt_dupfind, 0, serialized_data,
					str_size);
			g_assert(stmt_dupfind);
			if (mafw_db_select(stmt_dupfind, FALSE) == SQLITE_ROW)
			{
				sqlite3_finalize(stmt_dupfind);
				g_free(serialized_data);
				return;
			}
			sqlite3_finalize(stmt_dupfind);
			g_free(serialized_data);

		}
	}
	
	mafw_source_create_object(self,
				   MAFW_IRADIO_SOURCE_UUID "::",
				   metadata,
				   NULL,
				   mafw_iradio_vendor_bookmark_created);
}

/*---------------------------------------------------------------------------
  Bookmark entry parsing
  ---------------------------------------------------------------------------*/

/**
 * mafw_iradio_parse_bookmark_icon:
 *
 * @self: An iradio source that receives the parsed bookmark
 * @metadata: A GHashTable containing the bookmark item's metadata
 *
 * Parses a single bookmark's icon node, makes up a URI for the icon and
 * inserts the data into @metadata.
 */
static void mafw_iradio_parse_bookmark_icon(MafwSource* self,
					     GHashTable* metadata,
					     xmlNode* root)
{
	xmlNode* current;
	gchar* icon = NULL;

	current = root->children;
	while (current != NULL)
	{
		if (g_ascii_strcasecmp((const gchar*) current->name,
					NODE_LOCALPATH) == 0)
		{
			xmlChar* localpath;

			localpath = xmlNodeGetContent(current);
			if (localpath != NULL &&
			    strlen((const gchar*) localpath) != 0)
			{
				gchar** array;
				int last;

				/* Split the icon path to directories */
				array = g_strsplit((const gchar*) localpath,
						   "/", -1);

				/* Get the last entry (i.e. the file name) from
				   the array. */
				last = g_strv_length(array) - 1;
				icon = g_strdup(array[last]);

				g_strfreev(array);
			}
			else
			{
				/* Don't do anything if the tag is empty */
				icon = NULL;
			}

			xmlFree(localpath);
		}

		current = current->next;
	}

	/* If the icon tag was empty, don't create thumbnail_uri metadata */
	if (icon != NULL)
	{
		gchar* thumbnail_uri;

		/* Construct a valid URI for the thumbnail icon file */
		thumbnail_uri = g_strdup_printf("file://%s/%s",
						vendor_setup_path, icon);
		g_debug("THUMBNAIL_URI: %s", thumbnail_uri);
		mafw_metadata_add_str(metadata,
				       MAFW_METADATA_KEY_THUMBNAIL_URI,
				       thumbnail_uri);
		g_free(icon);
		g_free(thumbnail_uri);
	}
}

/**
 * mafw_iradio_parse_bookmark:
 *
 * @self: An iradio source that receives the parsed bookmark
 * @root: Either a <IRadioChannel> or a <VideoBookmark> node
 * 
 *
 * Parses a single bookmark node and inserts it into the iradio source (@self)
 * along with some metadata values (Name, URI & Icon).
 */
static void mafw_iradio_parse_bookmark(MafwSource* self, xmlNode* root,
					gboolean check_dups)
{
	GHashTable* metadata;
	xmlNode* current;

	g_assert(self != NULL);
	g_assert(root != NULL);

	metadata = mafw_metadata_new();

	current = root->children;
	while (current != NULL)
	{
		if (g_ascii_strcasecmp((const gchar*) current->name,
				       NODE_NAME) == 0)
		{
			xmlChar* title;

			title = xmlNodeGetContent(current);
			g_debug("TITLE: %s", title);
			mafw_metadata_add_str(metadata,
					       MAFW_METADATA_KEY_TITLE,
					       (const gchar *)title);
			xmlFree(title);
		}
		else if (g_ascii_strcasecmp((const gchar*) current->name,
				       NODE_DURATION) == 0)
		{
			xmlChar* durationstr;
			gint duration;
			gchar *strtail = NULL;

			durationstr = xmlNodeGetContent(current);
			duration = strtol((const char*)durationstr, &strtail, 10);
			g_debug("Duration: %d", duration);
			if (!strtail || strtail[0] == 0)
				mafw_metadata_add_int(metadata,
					       MAFW_METADATA_KEY_DURATION,
					       duration);
			xmlFree(durationstr);
		}
		else if (g_ascii_strcasecmp((const gchar *)current->name,
					NODE_URI) == 0)
		{
			const gchar* mime;
			xmlChar* uri;

			uri = xmlNodeGetContent(current);
			g_debug("URI: %s", uri);

			/* Dumbest way for putting a mime type here, but this
			   is enough for FMP. Besides, the customization tool
			   (that produces .confml files) doesn't support mime
			   type setting. */
			if (strcmp((const gchar*) root->name,
				   NODE_CHANNEL) == 0)
			{
				mime = "audio/unknown";
			}
			else
			{
				mime = "video/unknown";
			}
			g_debug("MIME: %s", mime);

			mafw_metadata_add_str(metadata,
					       MAFW_METADATA_KEY_URI,
					       (const gchar *)uri);
			mafw_metadata_add_str(metadata,
					       MAFW_METADATA_KEY_MIME,
					       (const gchar *)mime);
			xmlFree(uri);
		}
		else if (g_ascii_strcasecmp((const gchar *)current->name,
					NODE_ICON) == 0)
		{
			/* Parse an icon entry */
			mafw_iradio_parse_bookmark_icon(self, metadata,
							 current);
		}

		current = current->next;
	}

	/* Create an object to the IRadio database */
	mafw_iradio_create_bookmark_object(self, metadata, check_dups);

	/* Get rid of the metadata now that the stuff has been uploaded */
	g_hash_table_unref(metadata);
}

/*---------------------------------------------------------------------------
  CONFML file parsing
  ---------------------------------------------------------------------------*/

/**
 * mafw_iradio_parse_xml_customization:
 *
 * @self: An iradio source that should receive the bookmarks from the XML tree
 * @root: The root node of an XML tree that has been read from a .confml file
 *
 * Parses an XML node tree that should contain vendor-specific custom bookmarks
 * (in .confml format) that are then inserted to the iradio source's database.
 *
 * The format is roughly like this:
 * <configuration ...>
 *  <data>
 *   <mafw-iradio-source-bookmarks>
 *    [<IRadioChannel>|<VideoBookmark>]
 *     <Name>...</Name>
 *     <URI>...</URI>
 *     <Icon>
 *      <targetPath>...</targetPath>
 *      <localPath>...</localPath>
 *     </Icon>
 *    [</IRadioChannel>|</VideoBookmark>]
 *    ...
 *   </mafw-iradio-source-bookmarks>
 *  </data>
 * </configuration ...>
 */
static gboolean mafw_iradio_parse_confml(MafwSource* self, xmlNode* root,
						gboolean check_dups)
{
	xmlNode* current;
	gboolean result = FALSE;

	g_assert(root != NULL);

	/* Skip inside until we find the actual channel nodes */
	current = root;
	while (current != NULL)
	{
		if (g_ascii_strcasecmp((const gchar *)current->name,
					NODE_CONFIGURATION) == 0
		    || g_ascii_strcasecmp((const gchar *)current->name,
			    NODE_DATA) == 0)
		{
			/* Skip to node's children and continue the search */
			current = current->children;
			continue;
		}
		else if (g_ascii_strcasecmp((const gchar *)current->name,
					     NODE_IRADIO_BOOKMARKS) == 0)
		{
			/* Skip to node's children and stop searching */
			current = current->children;
			result = TRUE;
			break;
		}

		current = current->next;
	}

	/* Now we should be inside a node that contains IRadio channels and
	   video bookmarks */
	while (current != NULL)
	{
		if (g_ascii_strcasecmp((const gchar *)current->name,
					NODE_CHANNEL) == 0 ||
		    g_ascii_strcasecmp((const gchar *)current->name,
			    NODE_VIDEO) == 0)
		{
			/* Parse the node as audio */
			mafw_iradio_parse_bookmark(self, current, check_dups);
		}

		current = current->next;
	}

	return result;
}

/**
 * mafw_iradio_parse_bookmark_file:
 *
 * @self: An iradio source that receives the bookmarks from the parsed file
 *
 * Reads a .confml file (essentially XML) and parses the file's contents into
 * an XML node tree. That node tree is then fed to mafw_iradio_parse_confml().
 *
 * Returns: TRUE if successful, otherwise FALSE.
 */
gboolean mafw_iradio_parse_confml_file(MafwIradioSource* self,
					const gchar* path, gboolean check_dups)
{
	gboolean result;
	xmlNode* root;
	xmlDoc* doc;

	g_assert(self != NULL);
	g_assert(path != NULL);

	/* This initializes the library and checks for potential ABI mismatches
	   between the version it was compiled for and the actual shared lib */
	LIBXML_TEST_VERSION

	/* Parse the file into an xmlNode tree. */
	doc = xmlReadFile(path, NULL, 0);
	if (doc == NULL)
	{
		g_debug("Unable to parse confml file %s", path);
		result = FALSE;
	}
	else
	{
		root = xmlDocGetRootElement(doc);
		result = mafw_iradio_parse_confml(MAFW_SOURCE(self), root,
							check_dups);
		xmlFreeDoc(doc);
	}

	xmlCleanupParser();

	return result;
}

/**
 * mafw_iradio_vendor_setup:
 *
 * @self: An iradio source that gets its default bookmarks from a custom file
 *
 * Performs first-boot customization for the iradio source. Basically, this
 * reads a file that contains bookmarks, inserts them to the source's database
 * and sets a gconf key so that customization is done exactly once during the
 * plugin's lifetime.
 */
void mafw_iradio_vendor_setup(MafwIradioSource* self, gboolean check_dups)
{
	gchar *fname = g_strdup_printf("%s/%s", vendor_setup_path,
					VENDOR_FILENAME);

	g_assert(self != NULL);
	mafw_iradio_parse_confml_file(self, fname, check_dups);
	g_free(fname);
}
