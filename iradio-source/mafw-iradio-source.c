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
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>



#include "config.h"
#include "mafw-iradio-source.h"
#include "mafw-iradio-vendor-setup.h"

#define MAFW_IRADIO_SOURCE_GET_PRIVATE(object)				\
	(G_TYPE_INSTANCE_GET_PRIVATE ((object), MAFW_TYPE_IRADIO_SOURCE,\
				      MafwIradioSourcePrivate))

extern const gchar *vendor_setup_path;
static gboolean load_vendor;

struct _MafwIradioSourcePrivate
{
	guint last_browse_id;
	GList *browse_requests;
	sqlite3_stmt *stmt_object_list;
	sqlite3_stmt *stmt_get_value;
	sqlite3_stmt *stmt_get_key_value;
	sqlite3_stmt *stmt_insert;
	sqlite3_stmt *stmt_delete_keys;
	sqlite3_stmt *stmt_delete_object;
	sqlite3_stmt *stmt_get_max_id;
	sqlite3_stmt *stmt_check_id;
};


/*----------------------------------------------------------------------------
  Static implementations
  ----------------------------------------------------------------------------*/

/**
 * Usual structure to hold the data for the idle-calls
 **/
struct data_container{
	MafwSource *self;
	gchar *object_id;
	guint64 id;
	GError *error;
	gpointer user_data;
	gchar **metadata_keys;
	void (*cb) (); /* generic function pointer */
	void (*free_data_cb)(struct data_container *data); /* How to free the*/
							    /* data*/

};

/**
 * Frees the structure, with its metadata-key list, and with the object-id
 **/
static void free_data_container_cb (struct data_container *data)
{
	g_free(data->object_id);
	g_strfreev(data->metadata_keys);
	g_free(data);
}

/**
 * Checks the database, whether an object with the gived ID exists or not
 *
 * Return: TRUE if the id is in the DB
 **/
static gboolean is_id_stored(MafwIradioSource *self, guint64 id)
{
	gboolean retval = TRUE;
	
	mafw_db_bind_int64(self->priv->stmt_check_id, 0, id);
		
	if (mafw_db_select(self->priv->stmt_check_id, FALSE) != SQLITE_ROW)
	{
		retval = FALSE;
	}
	sqlite3_reset(self->priv->stmt_check_id);
	
	return retval;
}

/**
 * Extracts the ID from the object-id. In case of wrong object-id format, 
 * @parse_error set to TRUE.
 *
 * Return: the extracted ID
 **/
static guint64 get_id_from_objectid(const gchar *object_id,
					gboolean *parse_error)
{
	guint64 new_id = 0;
	gchar *endchar = NULL;
	
	new_id = g_ascii_strtoull(object_id + sizeof(MAFW_IRADIO_SOURCE_UUID
							"::")-1,
				&endchar, 0);
	if (!new_id || (endchar && endchar[0]))
	{
		if (parse_error)
			*parse_error = TRUE;
	}
	else
	{
		if (parse_error)
			*parse_error = FALSE;
	}
	return new_id;
}

/**
 * Returns the next free ID number in the database
 **/
static guint64 get_next_id(MafwIradioSource *self)
{
	guint64 new_id;
	
	if (mafw_db_select(self->priv->stmt_get_max_id,
				FALSE) == SQLITE_ROW) {
		new_id = mafw_db_column_int64(self->priv->
							stmt_get_max_id, 0);
		new_id++;
	}
	else
		new_id = 1;
	
	sqlite3_reset(self->priv->stmt_get_max_id);
	return new_id;
}

/**
 * CB function for g_hash_table_foreach. Adds the metadatas to the DB in
 * serialized form
 **/
static void store_metadata(gchar *key, gpointer value,
				struct data_container *data)
{
	gsize str_size = 0;
	gchar *serialized_data;
	MafwIradioSourcePrivate *priv;
	
	priv = MAFW_IRADIO_SOURCE(data->self)->priv;
	if (data->error)
		return;
	
	g_assert(value);
	
	serialized_data = mafw_metadata_val_freeze(value, &str_size);
	if (serialized_data && str_size)
	{
		g_debug("Adding new metadata for the ID: %llu key: %s",
						data->id, key);
		mafw_db_bind_int64(priv->stmt_insert, 0, data->id);
		mafw_db_bind_text(priv->stmt_insert, 1, key);
		mafw_db_bind_blob(priv->stmt_insert, 2, serialized_data,
					str_size);
		
		if (mafw_db_change(priv->stmt_insert, FALSE) != SQLITE_DONE)
			goto out0;
		g_assert(mafw_db_nchanges() == 1);
		sqlite3_reset(priv->stmt_insert);
	
	}
	
	if (serialized_data)
		g_free(serialized_data);
	
	return;

out0:	/* Clean up */
	sqlite3_reset(priv->stmt_insert);
	mafw_db_rollback();
	g_critical("Database error");
	g_set_error(&data->error, MAFW_EXTENSION_ERROR, MAFW_EXTENSION_ERROR_FAILED,
			"Database error");
	
	if (serialized_data)
		g_free(serialized_data);
	
}

/**
 * Called in idle, when the object-creation is done. Calls the cb-function,
 * and emits the container-changed signal, with the new object-id
 **/
static gboolean object_creation_done(struct data_container *data)
{
	g_assert(data != NULL);

	if (data->cb != NULL)
	{
		MafwSourceObjectCreatedCb cb = (MafwSourceObjectCreatedCb)
								(data->cb);
		cb(data->self, data->error? NULL: data->object_id,
		   data->user_data, data->error);
	}

	if (!data->error)
		g_signal_emit_by_name(data->self, "container-changed",
			MAFW_IRADIO_SOURCE_UUID "::");
	else
		g_error_free(data->error);
	g_free(data->object_id);
	g_free(data);
	return FALSE;
}


/**
 * Creates a new object, adds it to the DB with its metadatas
 **/
static void create_object(MafwSource *self, const gchar *parent,
				GHashTable *metadata, 
				MafwSourceObjectCreatedCb cb,
				gpointer user_data)
{
	guint64 new_id;
	GError *error = NULL;
	
	g_debug("Creating object");
	
	g_return_if_fail(MAFW_IS_IRADIO_SOURCE(self));
	g_return_if_fail(parent);
	g_return_if_fail(metadata);
	
	/* Metadata-URI check */
	if (!mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI))
	{
		g_debug("URI is missing");
		g_set_error(&error, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
				"URI is missing");
		cb(self, NULL, user_data, error);
		g_error_free(error);
		return;
	}
	
	/* object-id check */
	if (strcmp(parent, MAFW_IRADIO_SOURCE_UUID "::"))
	{
		g_debug("Parent-id can be only " MAFW_IRADIO_SOURCE_UUID
				"::");
		g_set_error(&error, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
				"Parent-id can be only "
					MAFW_IRADIO_SOURCE_UUID "::");
		cb(self, NULL, user_data, error);
		g_error_free(error);
		return;
	}
	
	new_id = get_next_id(MAFW_IRADIO_SOURCE(self));
	
	struct data_container *create_object_data = g_new0(
						struct data_container, 1);
	create_object_data->id = new_id;
	create_object_data->cb = cb;
	create_object_data->self = self;
	create_object_data->user_data = user_data;
	create_object_data->object_id = g_strdup_printf(MAFW_IRADIO_SOURCE_UUID
					"::%lld",
					new_id);
	if (!mafw_db_begin())
		goto create_object_err0;
	g_hash_table_foreach(metadata, (GHFunc)store_metadata,
				create_object_data);
	if (create_object_data->error)
		goto create_object_err1;
	if (!mafw_db_commit())
		goto create_object_err0;
	g_idle_add((GSourceFunc)object_creation_done, create_object_data);
	
	return;
	
create_object_err0:
	mafw_db_rollback();
create_object_err1:
	g_critical("Database error");
	g_set_error(&error, MAFW_EXTENSION_ERROR,
				MAFW_EXTENSION_ERROR_FAILED,
				"Database error");
	cb(self, NULL, user_data, error);
	g_error_free(error);
	free_data_container_cb(create_object_data);
	return;
}

/**
 * Called in idle, to remove and object, and its metadatas from the DB.
 * The user-given cb will be called, and the container-changed signal
 * will be emmited too, with the root object-id
 **/
static gboolean destroy_object_cb(struct data_container *data)
{
	gint result = SQLITE_OK;
	GError *err = NULL;
	MafwIradioSource *src = MAFW_IRADIO_SOURCE(data->self);
	MafwSourceObjectDestroyedCb cb = (MafwSourceObjectDestroyedCb)data ->
						cb;

	mafw_db_bind_int64(src->priv->stmt_delete_object, 0, data->id);
	result = mafw_db_delete(src->priv->stmt_delete_object);
	sqlite3_reset(src->priv->stmt_delete_object);
	
	if (result != SQLITE_DONE) {
		g_critical("Database error: %d", result);
		err = g_error_new(MAFW_EXTENSION_ERROR,
                                             MAFW_EXTENSION_ERROR_FAILED,
                                             "Database error: %d", result);
	}
	
	cb(data->self, data->object_id, data->user_data, err);
	if (err)
		g_error_free(err);
	else
		g_signal_emit_by_name(data->self, "container-changed",
					MAFW_IRADIO_SOURCE_UUID "::");
	
	g_free(data->object_id);
	g_free(data);
	return FALSE;
}

/**
 * Destroys an object
 **/
static void destroy_object(MafwSource *self, const gchar *object_id,
				MafwSourceObjectDestroyedCb cb,
				gpointer user_data)
{
	guint64 id;
	gboolean parse_err = FALSE;
	struct data_container *cb_data;
	GError *error = NULL;

	g_debug("Destroy object");
	g_return_if_fail(MAFW_IS_IRADIO_SOURCE(self));
	g_return_if_fail(object_id);
	g_return_if_fail(g_str_has_prefix(object_id,
				MAFW_IRADIO_SOURCE_UUID "::"));
	g_return_if_fail(cb);

	id = get_id_from_objectid(object_id, &parse_err);
	if (parse_err)
	{
		g_debug("Invalid object-id");
		g_set_error(&error, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
				"Invalid object-id");
		cb(self, object_id, user_data, error);
		g_error_free(error);
		return;
	}
	cb_data = g_new0(struct data_container, 1);
	cb_data->self = self;
	cb_data->id = id;
	cb_data->cb = cb;
	cb_data->user_data = user_data;
	cb_data->object_id = g_strdup(object_id);
	g_idle_add((GSourceFunc)destroy_object_cb, (gpointer)cb_data);
	return;
}

/**
 * Called on idle, after a metadata-change. Calls the user-given cb, and emmits
 * the metadata-changed signal
 **/
static gboolean set_mdata_cb(struct data_container *data)
{
	MafwSourceMetadataSetCb cb = (MafwSourceMetadataSetCb)data->cb;

	cb(data->self, data->object_id, NULL,
			data->user_data, data->error);
	if (!data->error)
		g_signal_emit_by_name(data->self, "metadata-changed",
				data->object_id);
	else
		g_error_free(data->error);
	g_free(data->object_id);
	g_free(data);
	return FALSE;
}

/**
 * Removes all the metadata_keys for the given object, defined by the key.
 * The removed metadatas will be re-added. CB for g_hash_table_foreach
 **/
static void remove_all_key(gchar *key, gpointer value,
				struct data_container *data)
{
	MafwIradioSource *src = MAFW_IRADIO_SOURCE(data->self);
	mafw_db_bind_int64(src->priv->stmt_delete_keys, 0, data->id);
	mafw_db_bind_text(src->priv->stmt_delete_keys, 1, key);
	mafw_db_delete(src->priv->stmt_delete_keys);
	sqlite3_reset(src->priv->stmt_delete_keys);
}

static void get_keys_cb(gpointer key, gpointer val, GPtrArray *keylist)
{
	g_ptr_array_add(keylist, key);
}

static void set_metadata_error_reporter(MafwSource *self, const gchar *object_id,
				GHashTable *metadata,
				MafwSourceMetadataSetCb cb,
				gpointer user_data, GQuark domain,
				gint code, const gchar *string)
{
	GPtrArray *keylist = g_ptr_array_new();
	GError *error = NULL;

	g_hash_table_foreach(metadata, (GHFunc)get_keys_cb, keylist);
	g_ptr_array_add(keylist, NULL);
	g_set_error(&error, domain, code, "%s", string);
	cb(self,  object_id, (const gchar **)keylist->pdata, user_data, error);
	g_ptr_array_free(keylist, TRUE);
	g_error_free(error);
}

/**
 * Updates the metadata of a given object.
 **/
static void set_metadata(MafwSource *self, const gchar *object_id,
				GHashTable *metadata,
				MafwSourceMetadataSetCb cb,
				gpointer user_data)
{
	guint64 id;
	struct data_container *data;
	gboolean parse_err = FALSE;
	
	

	g_debug("Set metadata for %s", object_id);
	g_return_if_fail(MAFW_IS_IRADIO_SOURCE(self));
	g_return_if_fail(object_id);
	g_return_if_fail(g_str_has_prefix(object_id,
					MAFW_IRADIO_SOURCE_UUID "::"));
	g_return_if_fail(cb);
	g_return_if_fail(metadata);

	id = get_id_from_objectid(object_id, &parse_err);
	if (parse_err || !is_id_stored(MAFW_IRADIO_SOURCE(self), id))
	{
		
		g_debug("Invalid object-id");
		set_metadata_error_reporter(self, object_id, metadata, cb,
				user_data, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
				"Invalid object-id");
		return;
	}
	
	data = g_new0(struct data_container, 1);
	data->id = id;
	data->object_id = g_strdup(object_id);
	data->self = self;
	data->cb = cb;
	data->user_data = user_data;
	
	g_hash_table_foreach(metadata, (GHFunc)remove_all_key, data);
	if (!mafw_db_begin())
		goto set_metadata_err0;
	g_hash_table_foreach(metadata, (GHFunc)store_metadata, data);
	if (data->error)
		goto set_metadata_err1;
	if (!mafw_db_commit())
		goto set_metadata_err0;
	g_idle_add((GSourceFunc)set_mdata_cb, data);
	
	return;

set_metadata_err0:
	mafw_db_rollback();
set_metadata_err1:
	g_debug("Database error at set_metadata");
	set_metadata_error_reporter(self, object_id, metadata, cb,
				user_data, MAFW_EXTENSION_ERROR,
				MAFW_EXTENSION_ERROR_FAILED,
				"Database error");
	free_data_container_cb(data);
	return;
}

static guint get_child_count(MafwIradioSourcePrivate *privdat)
{
	guint i = 0;
	while (mafw_db_select(privdat->stmt_object_list, FALSE) == SQLITE_ROW)
	{
		i++;
	}
	sqlite3_reset(privdat->stmt_object_list);	
	return i;
}

/**
 * Return the asked metadatas on idle
 **/
static gboolean get_metadata_cb(struct data_container *data)
{
	GHashTable *metadata = NULL;
	const void *val;
	GValue *value;
	GByteArray *bary;
	GError *err = NULL;
	gint i = 0;
	guint b_size = 0;
	MafwIradioSourcePrivate *priv;
	
	MafwSourceMetadataResultCb cb =
				(MafwSourceMetadataResultCb)data->cb;
	
	priv = MAFW_IRADIO_SOURCE(data->self)->priv;
	if (data->id == -1)
	{
		metadata = mafw_metadata_new();
		if (data->metadata_keys && data->metadata_keys[i] &&
			data->metadata_keys[i][0] != '*')
		{
			while(data->metadata_keys && data->metadata_keys[i])
			{
				if (!strcmp(data->metadata_keys[i],
						MAFW_METADATA_KEY_MIME))
				{
					mafw_metadata_add_str(metadata, 
						MAFW_METADATA_KEY_MIME,
						MAFW_METADATA_VALUE_MIME_CONTAINER);
				} else if (!strcmp(data->metadata_keys[i],
						MAFW_METADATA_KEY_CHILDCOUNT))
				{
					mafw_metadata_add_int(metadata, 
						MAFW_METADATA_KEY_CHILDCOUNT,
						(gint)get_child_count(priv));
				}
				i++;
			}
		}
		else
		{
			mafw_metadata_add_str(metadata, 
						MAFW_METADATA_KEY_MIME,
						MAFW_METADATA_VALUE_MIME_CONTAINER);
			mafw_metadata_add_int(metadata, 
						MAFW_METADATA_KEY_CHILDCOUNT,
						(gint)get_child_count(priv));
			
		}
	} else if (is_id_stored(MAFW_IRADIO_SOURCE(data->self), data->id))
	{
		metadata = mafw_metadata_new();
		if (data->metadata_keys && data->metadata_keys[i] &&
			data->metadata_keys[i][0] != '*')
		{
			while(data->metadata_keys && data->metadata_keys[i])
			{
				b_size = 0;
				
				mafw_db_bind_int64(priv->stmt_get_value, 0,
								data->id);
				mafw_db_bind_text(priv->stmt_get_value, 1,
							data->metadata_keys[i]);
	
				if (mafw_db_select(priv->stmt_get_value, FALSE)
							== SQLITE_ROW)
				{
					val = mafw_db_column_blob(
						priv->stmt_get_value, 0);
					bary = g_byte_array_new();
					bary = g_byte_array_append(bary, val,
						sqlite3_column_bytes(
							priv->stmt_get_value,
							0));
					value = mafw_metadata_val_thaw_bary(
							bary,
							&b_size);
					g_hash_table_insert(metadata,
						g_strdup(data ->
							  metadata_keys[i]),
							value);
					g_byte_array_free(bary, TRUE);
				}
				sqlite3_reset(priv->stmt_get_value);
				i++;
			}
		}
		else
		{
			const gchar *key;
			
			mafw_db_bind_int64(priv->stmt_get_key_value, 0,
								data->id);

			while (mafw_db_select(priv->stmt_get_key_value, FALSE)
							== SQLITE_ROW)
			{
				b_size = 0;
				key = mafw_db_column_text(priv ->
							stmt_get_key_value, 0);
				val = mafw_db_column_blob(priv ->
							stmt_get_key_value, 1);
				bary = g_byte_array_new();
				bary = g_byte_array_append(bary, val,
					sqlite3_column_bytes(
						priv->stmt_get_key_value, 1));
				value = mafw_metadata_val_thaw_bary(bary,
						&b_size);
				g_hash_table_insert(metadata,
					g_strdup(key), value);
				g_byte_array_free(bary, TRUE);
			}
			sqlite3_reset(priv->stmt_get_key_value);
		}
	}
	else
	{
		g_debug("Invalid object-id");
		err = g_error_new(MAFW_SOURCE_ERROR,
					MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
					"Invalid object-id");
	}
	cb(data->self, data->object_id, metadata, data->user_data,
		err);
	if (err)
		g_error_free(err);
	if (data->free_data_cb)
		data->free_data_cb(data);
	return FALSE;
}

/**
 * Checks whether the metadata-key list contains the wildcard '*' or not
 * Return: TRUE, if the list contains '*'
 **/
static gboolean metadata_keys_contain_wildcard(const gchar *const
						*metadata_keys)
{
	gint i;
	if (!metadata_keys || !metadata_keys[0])
		return FALSE;
	for (i = 0; metadata_keys[i]; i++)
		if (metadata_keys[i][0] == '*')
			return TRUE;
	return FALSE;
}

/**
 * Returns the metadatas of an object
 **/
static void get_metadata(MafwSource *self, const gchar *object_id,
					 const gchar *const *metadata_keys,
					 MafwSourceMetadataResultCb cb,
					 gpointer user_data)
{
	guint64 id;
	struct data_container *data;
	gboolean parse_err = FALSE;

	g_debug("Get metadata for %s", object_id);
	g_return_if_fail(MAFW_IS_IRADIO_SOURCE(self));
	g_return_if_fail(object_id);
	g_return_if_fail(g_str_has_prefix(object_id,
					MAFW_IRADIO_SOURCE_UUID "::"));
	g_return_if_fail(cb);
	g_return_if_fail(metadata_keys && metadata_keys[0]);
	
	if (!strcmp(object_id, MAFW_IRADIO_SOURCE_UUID "::"))
	{/* Return only a statistic */
		id = -1;
	}
	else
	{
		id = get_id_from_objectid(object_id, &parse_err);
	}

	if (parse_err)
	{
		GError *error = NULL;
		g_debug("Invalid object-id");
		g_set_error(&error, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
				"Invalid object-id");
		cb(self, object_id, NULL, user_data, error);
		g_error_free(error);
		return;
	}
	data = g_new0(struct data_container, 1);

	data->object_id = g_strdup(object_id);
	data->self = self;
	if (metadata_keys_contain_wildcard(metadata_keys))
		data->metadata_keys = g_strdupv((gchar**)MAFW_SOURCE_ALL_KEYS);
	else
		data->metadata_keys = g_strdupv((gchar**)metadata_keys);
	data->cb = cb;
	data->user_data = user_data;
	data->id = id;
	data->free_data_cb = free_data_container_cb;

	g_idle_add((GSourceFunc)get_metadata_cb, data);
	
	return;
}

struct browse_data_container {
	MafwSource *self;
	MafwSourceBrowseResultCb cb;
	guint skip_count;
	guint item_count;
	gpointer user_data;
	guint64 current_id;
	gchar **metadata_keys;
	guint next_index;
	gchar **sorting_terms;
	const gchar **relevant_metadata_keys;
	MafwFilter *filter;
	GList *object_list;
	guint bid;
	guint sid;
	gboolean free_req;
};

struct metadata_data {
	GHashTable *metadata;
	guint64 id;
};

/**
 * Removes the specified pointer from the list, and releases that data with
 * its content
 **/
static GList *browse_result_free_list_item(GList *list,
					struct metadata_data *data)
{
	GList *new_list;
	mafw_metadata_release(data->metadata);
	new_list = g_list_remove(list, data);
	g_free(data);
	return new_list;
}

/**
 * Frees the given structure with its content
 **/
static void free_browse_data(struct browse_data_container *browse_data)
{
	if (browse_data->sorting_terms)
		g_strfreev(browse_data->sorting_terms);
	if (browse_data->relevant_metadata_keys)
		g_free(browse_data->relevant_metadata_keys);
	if (browse_data->filter)
		mafw_filter_free(browse_data->filter);
	if (browse_data->metadata_keys)
		g_strfreev(browse_data->metadata_keys);
	while (browse_data->object_list)
	{
		browse_data->object_list = browse_result_free_list_item(
				browse_data->object_list,
				browse_data->object_list->data);
	}

	g_free(browse_data);
}

/** 
 * Removes a browse request from the stored list
 **/
static void remove_browse_request(MafwIradioSource *src,
				struct browse_data_container *browse_data)
{
	src->priv->browse_requests = g_list_remove(src->priv->browse_requests,
					browse_data);
	free_browse_data(browse_data);
}

/**
 * Helper function to sort the items according to the sorting terms
 **/
static gint sort_metadata_cb(struct metadata_data *a, struct metadata_data *b,
				const gchar **sorting_terms)
{
	return mafw_metadata_compare(a->metadata, b->metadata, sorting_terms,
					NULL);
}

/**
 * If the result-list was not sorted, and filtered according to the skip and
 * item count, it does this preparation. After this, it calls the cb function
 * with the results, one by one
 **/
static gboolean emit_browse_res(struct browse_data_container *browse_data)
{
	gchar *current_object_id = NULL;
	struct metadata_data *current_data = NULL;
	GHashTable *current_metadata = NULL;
	MafwIradioSourcePrivate *privdat;
	
	if (browse_data->free_req)
	{
		remove_browse_request(MAFW_IRADIO_SOURCE(
						browse_data->self),
						browse_data);
		return FALSE;
	}
	
	if (browse_data->sorting_terms)
	{/* Sort the filtered results at first */
		browse_data->object_list = g_list_sort_with_data(
					browse_data->object_list,
					(GCompareDataFunc)sort_metadata_cb,
					browse_data->sorting_terms);
		g_strfreev(browse_data->sorting_terms);
		browse_data->sorting_terms = NULL;
		
		
	}
	
	if (browse_data->skip_count)
	{
		if (browse_data->skip_count >= g_list_length(
						browse_data->object_list))
		{/* list is not so long...... error */
			GError *err;
			g_debug("Skip count filtered all the results");
			err = g_error_new (MAFW_SOURCE_ERROR,
					MAFW_SOURCE_ERROR_BROWSE_RESULT_FAILED,
					"Skip count filtered all the results");
			
			browse_data->cb(browse_data->self, browse_data->bid, 0,
					0, NULL, NULL,
					browse_data->user_data, err);
			g_error_free(err);
			browse_data->free_req = TRUE;
			
			return TRUE;
		}
		/* remove not-needed items, according to item_count and
			skip_count */
		while (browse_data->skip_count && browse_data->object_list)
		{
			browse_data->object_list = browse_result_free_list_item(
						browse_data->object_list,
						browse_data->object_list->data);
			browse_data->skip_count--;
		}
	}
	
	if (browse_data->item_count &&
		(g_list_length(browse_data->object_list) >
				browse_data->item_count))
	{
		GList *last_item = g_list_nth(browse_data->object_list, 
						browse_data->item_count-1);
		while (last_item->next)
			browse_data->object_list = browse_result_free_list_item(
						browse_data->object_list,
						last_item->next->data);
		browse_data->item_count = 0;
	}
	
	privdat = MAFW_IRADIO_SOURCE(browse_data->self)->priv;
	if (browse_data->object_list)
	{
		current_data = browse_data->object_list->data;
	
		current_object_id = g_strdup_printf(MAFW_IRADIO_SOURCE_UUID
						"::%lld",
						current_data->id);
	
		if (!browse_data->metadata_keys ||
						!browse_data->metadata_keys[0])
		{
			current_metadata = NULL;
		}
		else if (browse_data->metadata_keys[0][0] == '*')
		{
			current_metadata = current_data->metadata;
		}
		else
		{/* Filter the metadata */
			gint i;
			gpointer metadata_value;
			current_metadata = g_hash_table_new_full(g_str_hash,
								g_str_equal,
								NULL, NULL);
			for (i=0; browse_data->metadata_keys[i]; i++)
			{
				metadata_value = g_hash_table_lookup(
							current_data->metadata, 
							browse_data->
							metadata_keys[i]);
				if (metadata_value)
					g_hash_table_insert(current_metadata,
							(gpointer)browse_data->
							    metadata_keys[i],
							metadata_value);
			}
			if (g_hash_table_size(current_metadata) ==0)
			{
				g_hash_table_destroy(current_metadata);
				current_metadata = NULL;
			}
		}
	}
	
	browse_data->cb(browse_data->self, browse_data->bid,
			browse_data->object_list?
				g_list_length(browse_data->object_list)-1:
						0,
			browse_data->next_index, current_object_id,
			current_metadata,
			browse_data->user_data, NULL);
	if (current_metadata && current_metadata != current_data->metadata)
		g_hash_table_destroy(current_metadata);
	g_free(current_object_id);
	browse_data->next_index++;
	
	if (browse_data->object_list)
		browse_data->object_list = browse_result_free_list_item(
							browse_data->
								object_list,
							current_data);

	if (browse_data->object_list)
		return TRUE;

	browse_data->free_req = TRUE;
	
	return TRUE;
}

/**
 * Get-metadata-cb, to process the metadata results, and create the
 * browse-result list. It filters the result, according to the given filter
 * criteria, and adds the result to a list. Filtering according to the item
 * count, and skip count, and the sorting is not done here
 **/
static void browse_metadata_cb(MafwSource *self, const gchar *object_id,
				GHashTable *metadata,
				struct browse_data_container *browse_data,
				const GError *error)
{
	if (!metadata || !browse_data->filter ||
		mafw_metadata_filter(metadata, browse_data->filter,NULL))
	{ /* Filter passed.... */
		struct metadata_data *new_metadata = g_new0(
						struct metadata_data, 1);
		
		new_metadata->metadata = metadata;
		new_metadata->id = browse_data->current_id;
		browse_data->object_list = g_list_prepend(
						browse_data->object_list,
					new_metadata);
	}
	else
	{
		mafw_metadata_release(metadata);
	}
	
}

static guint browse(MafwSource *self, const gchar *object_id,
			gboolean recursive, const MafwFilter *filter,
			const gchar *sort_criteria,
			const gchar *const *metadata_keys,
			guint skip_count, guint item_count,
			MafwSourceBrowseResultCb cb, gpointer user_data)
{
	struct browse_data_container *browse_data;
	MafwIradioSourcePrivate *privdat;
	struct data_container current_data;
	
	g_debug("Browsing %s. Recursive: %d, Filter: %s, Sort criteria: %s,"
		"Skip: %u, Item count: %u", object_id, recursive,
		filter != NULL ? "yes" : "no", sort_criteria, skip_count,
		item_count);
	
	g_return_val_if_fail(object_id != NULL, MAFW_SOURCE_INVALID_BROWSE_ID);
	g_return_val_if_fail(cb != NULL, MAFW_SOURCE_INVALID_BROWSE_ID);
	g_return_val_if_fail(strcmp(object_id, MAFW_IRADIO_SOURCE_UUID "::")
					== 0, MAFW_SOURCE_INVALID_BROWSE_ID);
	
	privdat = MAFW_IRADIO_SOURCE(self)->priv;
	
	memset(&current_data, 0, sizeof current_data);

	browse_data = g_new0(struct browse_data_container, 1);
	current_data.user_data = browse_data;
	
	browse_data->filter = mafw_filter_copy(filter);
	
	privdat->last_browse_id++;
	browse_data->bid = privdat->last_browse_id;
	
	g_debug("New browse-id: %u", browse_data->bid);
	
	/* This will filter the results */
	current_data.cb = browse_metadata_cb;
	current_data.self = self;
	browse_data->sorting_terms =
				mafw_metadata_sorting_terms(sort_criteria);
	current_data.metadata_keys = (gchar**)mafw_metadata_relevant_keys(
				metadata_keys,
				browse_data->filter, 
				(const gchar *const *)browse_data->
								sorting_terms);
	if (metadata_keys_contain_wildcard(
				(const gchar**)current_data.metadata_keys))
	{
		g_free(current_data.metadata_keys);
		current_data.metadata_keys = g_strdupv(
						(gchar**)MAFW_SOURCE_ALL_KEYS);
	}
	else if (current_data.metadata_keys)
	{
		gchar **temp = current_data.metadata_keys;
		current_data.metadata_keys = g_strdupv(temp);
		g_free(temp);
	}
	
	while (mafw_db_select(privdat->stmt_object_list, FALSE) == SQLITE_ROW)
	{
		current_data.id = browse_data->current_id =
					mafw_db_column_int64(privdat->
							stmt_object_list,
							0);
		if (current_data.metadata_keys)
			get_metadata_cb(&current_data);
		else
		{
			browse_metadata_cb(NULL, NULL, NULL, browse_data, NULL);
		}
	}
	sqlite3_reset(privdat->stmt_object_list);
	mafw_filter_free(browse_data->filter);
	browse_data->filter = NULL;
	g_strfreev(current_data.metadata_keys);
	current_data.metadata_keys = NULL;
	
	browse_data->self = self;
	browse_data->cb = cb;
	browse_data->user_data = user_data;
	browse_data->skip_count = skip_count;
	browse_data->item_count = item_count;
	if (metadata_keys)
	{
		if (metadata_keys_contain_wildcard(metadata_keys))
			browse_data->metadata_keys = g_strdupv(
						(gchar**)MAFW_SOURCE_ALL_KEYS);
		else
			browse_data->metadata_keys = g_strdupv(
							(gchar**)metadata_keys);
		
	}
	else
	{
		browse_data->metadata_keys = g_strdupv(
						(gchar**)MAFW_SOURCE_NO_KEYS);
		
	}
			
	browse_data->sid = g_idle_add((GSourceFunc)emit_browse_res,
						browse_data);
	
	privdat->browse_requests = g_list_prepend(privdat->browse_requests,
					browse_data);
	
	return browse_data->bid;
}

/**
 * Helper function to find a browse request's data
 **/
static gint find_browse_request(struct browse_data_container *a,
				gconstpointer b)
{
	return !(a->bid == GPOINTER_TO_UINT(b));
}

static gboolean cancel_browse(MafwSource *self, guint browse_id,
				GError **error)
{
	
	GList *found_request;
	MafwIradioSourcePrivate *privdat;
	struct browse_data_container *found_item;

	g_debug("Canceling browse: %u", browse_id);

	privdat = MAFW_IRADIO_SOURCE(self)->priv;
	
	found_request = g_list_find_custom(privdat->browse_requests,
					GUINT_TO_POINTER(browse_id),
					(GCompareFunc)find_browse_request);
	if (!found_request)
	{
		g_debug("Browse id %u does not exist", browse_id);
		g_set_error(error, MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_INVALID_BROWSE_ID,
				"Browse id %u does not exist", browse_id);
        
		return FALSE;
	}

	found_item = found_request->data;
	g_source_remove(found_item->sid);
	
	
	found_item->free_req = TRUE;
	
	return TRUE;
}

/**
 * Creates the DB-table for the source
 **/
static void init_db(void)
{
	sqlite3_stmt *db_check = mafw_db_prepare(
		"SELECT name FROM sqlite_master WHERE type = 'table' AND "
		"name = '" IRADIO_TABLE "'");

	if (mafw_db_select(db_check, FALSE) == SQLITE_ROW)
	{
		load_vendor = FALSE;
	}
	else
	{
		load_vendor = TRUE;
	}

	sqlite3_finalize(db_check);

	/*
	 * TABLE iradiobookmarks:
	 * * id				integer			AUTOINCREMENT
	 * * objectid			string			NOT NULL UNIQUE
	 */
	mafw_db_exec(
		"CREATE TABLE IF NOT EXISTS " IRADIO_TABLE "(\n"
		"id		INTEGER		NOT NULL,\n"
		"key		TEXT		NOT NULL,\n"
		"value		BLOB		)");
}


/*----------------------------------------------------------------------------
  Iradio source object definition
  ----------------------------------------------------------------------------*/

G_DEFINE_TYPE(MafwIradioSource, mafw_iradio_source, MAFW_TYPE_SOURCE);

static void set_vendorfile_date(MafwIradioSource *self, time_t mod_time)
{
	guint64 new_id;
	sqlite3_stmt *stmt_vendofile_setdate;

	new_id = get_next_id(self);
	stmt_vendofile_setdate = mafw_db_prepare("INSERT "
					"INTO " IRADIO_TABLE "("
						"id, key, value) "
					"VALUES(:id, '', :value)");
	g_assert(mafw_db_begin());
	mafw_db_bind_int64(stmt_vendofile_setdate, 0, new_id);
	mafw_db_bind_blob(stmt_vendofile_setdate, 1, &mod_time,
				sizeof(time_t));

	if (mafw_db_change(stmt_vendofile_setdate, FALSE) != SQLITE_DONE)
		g_assert_not_reached();
	g_assert(mafw_db_nchanges() == 1);
	g_assert(mafw_db_commit());
	sqlite3_finalize(stmt_vendofile_setdate);
}

static void mafw_iradio_source_init(MafwIradioSource *self)
{
	g_return_if_fail(MAFW_IS_IRADIO_SOURCE(self));
	self->priv = MAFW_IRADIO_SOURCE_GET_PRIVATE(self);

	self->priv->stmt_object_list = mafw_db_prepare("SELECT DISTINCT id "
					"FROM " IRADIO_TABLE " WHERE key != ''");
	self->priv->stmt_get_value = mafw_db_prepare("SELECT value FROM "
					IRADIO_TABLE " WHERE id = :id AND "
							"key = :key AND key != ''");
	self->priv->stmt_get_key_value = mafw_db_prepare("SELECT key, value "
					"FROM " IRADIO_TABLE " WHERE id = :id AND key != ''");
	self->priv->stmt_insert = mafw_db_prepare("INSERT "
					"INTO " IRADIO_TABLE "(id, "
						"key, value) "
					"VALUES(:id, :key, :value)");
	self->priv->stmt_delete_keys = mafw_db_prepare("DELETE FROM "
					IRADIO_TABLE " WHERE id = :id AND "
					"key = :key");
	self->priv->stmt_delete_object = mafw_db_prepare("DELETE FROM "
						IRADIO_TABLE " WHERE id = :id");
	self->priv->stmt_get_max_id = mafw_db_prepare("SELECT max(id) "
						"as maxid FROM " IRADIO_TABLE);
	self->priv->stmt_check_id = mafw_db_prepare("SELECT id FROM "
						IRADIO_TABLE " WHERE id = :id");

	if (load_vendor)
	{
		struct stat vendorstat;
		gchar *vendorfile = g_strdup_printf("%s/%s", vendor_setup_path,
							VENDOR_FILENAME);

		mafw_iradio_vendor_setup(self, FALSE);
		if (g_stat(vendorfile, &vendorstat) != 0)
		{
			g_free(vendorfile);
			return;
		}
		set_vendorfile_date(self, vendorstat.st_mtime);
		load_vendor = FALSE;
		g_free(vendorfile);
	}
	else
	{
		const void *val;
		time_t last_mod = 0;
		struct stat vendorstat;
		gchar *vendorfile = g_strdup_printf("%s/%s", vendor_setup_path,
							VENDOR_FILENAME);
		sqlite3_stmt *stmt_vendorfile_date;

		if (g_stat(vendorfile, &vendorstat) != 0)
		{
			g_debug("Vendor file not found: %s", vendorfile);
			g_free(vendorfile);
			return;
		}

		stmt_vendorfile_date = mafw_db_prepare("SELECT "
					"value FROM "
					IRADIO_TABLE " WHERE key = ''");
		g_assert(stmt_vendorfile_date);
		if (mafw_db_select(stmt_vendorfile_date, FALSE) == SQLITE_ROW)
		{
			val = mafw_db_column_blob(stmt_vendorfile_date, 0);
			if (val)
				last_mod = *(time_t*)val;
			else
				last_mod = 0;
		}
		else
		{
			last_mod = 0;
		}

		g_free(vendorfile);
		sqlite3_finalize(stmt_vendorfile_date);

		if (vendorstat.st_mtime != last_mod)
		{/* New vendor file.... db should be updated */
			g_debug("Updating");
			mafw_iradio_vendor_setup(self, TRUE);
			if (last_mod != 0)
				mafw_db_exec("DELETE FROM " IRADIO_TABLE 
						" WHERE key = ''");
			set_vendorfile_date(self, vendorstat.st_mtime);
		}
	}
}

static void dispose(GObject *object)
{
	MafwIradioSource *self = MAFW_IRADIO_SOURCE(object);
	MafwSourceClass *parent_class;
	MafwIradioSourceClass *klass;

	klass = MAFW_IRADIO_SOURCE_GET_CLASS(object);
	
	parent_class = g_type_class_peek_parent(klass);
	
	while (self->priv->browse_requests)
	{
		g_source_remove(((struct browse_data_container *)
				(self->priv->browse_requests->data))->sid);
		remove_browse_request(MAFW_IRADIO_SOURCE(self),
					self->priv->browse_requests->data);
	}
	
	sqlite3_finalize(self->priv->stmt_object_list);
	sqlite3_finalize(self->priv->stmt_get_value);
	sqlite3_finalize(self->priv->stmt_get_key_value);
	sqlite3_finalize(self->priv->stmt_insert);
	sqlite3_finalize(self->priv->stmt_delete_keys);
	sqlite3_finalize(self->priv->stmt_delete_object);
	sqlite3_finalize(self->priv->stmt_get_max_id);
	sqlite3_finalize(self->priv->stmt_check_id);
	
	G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void mafw_iradio_source_class_init(MafwIradioSourceClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	MafwSourceClass *source_class = MAFW_SOURCE_CLASS(klass);
	
	source_class->create_object = create_object;
	source_class->destroy_object = destroy_object;
	source_class->set_metadata = set_metadata;
	source_class->get_metadata = get_metadata;
	source_class->browse = browse;
	source_class->cancel_browse = cancel_browse;
	
	gobject_class->dispose = dispose;
	
	g_type_class_add_private(source_class,
					sizeof(MafwIradioSourcePrivate));
	
	init_db();
}

/*----------------------------------------------------------------------------
  Public API
  ----------------------------------------------------------------------------*/

GObject *mafw_iradio_source_new(void)
{
	return g_object_new(MAFW_TYPE_IRADIO_SOURCE,
			    "plugin", MAFW_IRADIO_SOURCE_PLUGIN_NAME,
			    "name", MAFW_IRADIO_SOURCE_NAME,
			    "uuid", MAFW_IRADIO_SOURCE_UUID,
			    NULL);
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
