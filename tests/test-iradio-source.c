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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <checkmore.h>
#include <check.h>
#include <glib.h>
#include <libmafw/mafw.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <utime.h>

#include "iradio-source/mafw-iradio-source.h"
#include "iradio-source/mafw-iradio-vendor-setup.h"

#define ADDED_ITEM_NR 20

extern const gchar *vendor_setup_path;

START_TEST(test_plugin)
{
	MafwRegistry *reg = mafw_registry_get_instance();
	gboolean retv;
	GError *err = NULL;
	GList *tlist;
	
	err = NULL;
	retv = mafw_registry_load_plugin(reg,
				"mafw-iradio-source", &err);
	fail_unless(retv && !err);
	
	tlist = mafw_registry_list_plugins(reg);
	fail_unless(g_list_length(tlist) == 1);
	g_list_free(tlist);
	tlist = mafw_registry_get_sources(MAFW_REGISTRY(reg)); /* Do not free this list */
	fail_unless(g_list_length(tlist) == 1);
	tlist = mafw_registry_get_renderers(MAFW_REGISTRY(reg)); /* Do not free this list */
	fail_unless(g_list_length(tlist) == 0);
}
END_TEST

static GList *created_ob_ids;

static void check_dups(gchar* data, gchar *new_id)
{
	fail_if(strcmp(data, new_id) == 0);
}

static void obi_created(MafwSource *self, const gchar *object_id,
			gpointer user_data, const GError *error)
{
	fail_if(error);
	fail_unless(object_id != NULL);
	fail_unless(g_str_has_prefix(object_id, MAFW_IRADIO_SOURCE_UUID "::"));
	g_list_foreach(created_ob_ids, (GFunc)check_dups, (gpointer)object_id);
	created_ob_ids = g_list_prepend(created_ob_ids,g_strdup(object_id));
}

static void obi_created_not_called(MafwSource *self, const gchar *object_id,
			gpointer user_data, const GError *error)
{
	g_assert_not_reached();
}

static void obi_created_error(MafwSource *self, const gchar *object_id,
			gpointer user_data, const GError *error)
{
	fail_if(error == NULL);
}

static void obi_destroyed(MafwSource *self, const gchar *object_id,
				gpointer user_data, const GError *error)
{
	GList *item_to_rm = g_list_find_custom(created_ob_ids, object_id,
					(GCompareFunc)strcmp);
	fail_if(error);
	fail_unless(item_to_rm != NULL);
	g_free(item_to_rm->data);
	created_ob_ids = g_list_delete_link(created_ob_ids, item_to_rm);
}

static void cont_chd_cb(MafwIradioSource *radio_src, gchar *oid, gpointer udata)
{
	fail_unless(oid != NULL);
	fail_unless((strcmp(oid, MAFW_IRADIO_SOURCE_UUID "::") == 0) ||
			(strcmp(oid, created_ob_ids->data) == 0));
	checkmore_stop_loop();
}

START_TEST(test_add_remove)
{
	MafwIradioSource *radio_src;
	GHashTable *mdat;
	gint i;
	
	radio_src = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	
	g_signal_connect(radio_src,"container-changed", (GCallback)cont_chd_cb,
				NULL);
	
	mdat = mafw_metadata_new();
	
	fail_unless(radio_src != NULL);
	
	/* Should not crash.... */
	/* Metadata checks */
	/* Everything is OK, but the mdata is empty */
	mafw_source_create_object(MAFW_SOURCE(radio_src),
						MAFW_IRADIO_SOURCE_UUID "::",
						mdat, obi_created_error,
						NULL);
	mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_URI,
					"mms://test.uri/test.wav");
	
	/* Object-ids */
	mafw_source_create_object(MAFW_SOURCE(radio_src), NULL,
						mdat, obi_created_not_called,
						NULL);

	mafw_source_create_object(MAFW_SOURCE(radio_src),
						"wrong::oid", mdat,
						obi_created_error,
						NULL);
	mafw_source_create_object(MAFW_SOURCE(radio_src),
						MAFW_IRADIO_SOURCE_UUID"::wrng",
						mdat, obi_created_error,
						NULL);


	/* This should not fail.... */
	for (i =0; i < ADDED_ITEM_NR; i++)
	{
		mafw_source_create_object(MAFW_SOURCE(radio_src),
						MAFW_IRADIO_SOURCE_UUID "::",
						mdat, obi_created, NULL);
		checkmore_spin_loop(-1);
		fail_unless(g_list_length(created_ob_ids) == i+1);
	}

	/* Destroy tests */
	/* failing cases */
	mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				NULL, obi_created_error, NULL);
	mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				"wrong::oid", obi_created_error, NULL);
	mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::",
				obi_created_error, NULL);
	mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				created_ob_ids->data, NULL,
				NULL);
	
	/* This should not fail */
	while (created_ob_ids)
	{
		mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				created_ob_ids->data, obi_destroyed,
				NULL);
		checkmore_spin_loop(-1);
	}
	
	mafw_metadata_release(mdat);
	g_object_unref(radio_src);
						
}
END_TEST

static void mdat_set_cb(MafwSource *self, const gchar *object_id,
				const gchar **failed_keys, gpointer user_data,
				const GError *error)
{
	fail_if(failed_keys != NULL);
	fail_if(error != NULL);
	fail_unless(object_id != NULL);
	fail_if(strcmp(object_id, created_ob_ids->data));
}

static void mdat_set_not_called_cb(MafwSource *self, const gchar *object_id,
				const gchar **failed_keys, gpointer user_data,
				const GError *error)
{
	g_assert_not_reached();
}

static void mdat_set_error_cb(MafwSource *self, const gchar *object_id,
				const gchar **failed_keys, gpointer user_data,
				const GError *error)
{
	fail_if(error == NULL);
}

static void mdat_get_cb(MafwSource *self, const gchar *object_id,
			GHashTable *metadata, gpointer mime,
			const GError *error)
{
	fail_if(error);
	fail_unless(metadata != NULL);
	fail_unless(mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI)
				!= NULL);
	fail_unless(mafw_metadata_first(metadata, MAFW_METADATA_KEY_MIME)
				!= NULL);
	fail_if(error != NULL);
	fail_unless(object_id != NULL);
	fail_if(strcmp(object_id, created_ob_ids->data) != 0);
	fail_if(strcmp(g_value_get_string(
				mafw_metadata_first(metadata,
					MAFW_METADATA_KEY_MIME)),
				mime) != 0);
	fail_if(strcmp(g_value_get_string(
				mafw_metadata_first(metadata, 
					MAFW_METADATA_KEY_URI)), 
				"http://test.uri/test.wav") != 0);
	mafw_metadata_release(metadata);
	checkmore_stop_loop();
}

static void mdat_get_not_called_cb(MafwSource *self, const gchar *object_id,
			GHashTable *metadata, gpointer mime,
			const GError *error)
{
	g_assert_not_reached();
}
static void mdat_get_root_cb(MafwSource *self, const gchar *object_id,
			GHashTable *metadata, gpointer mime,
			const GError *error)
{
	fail_if(error);
	fail_unless(metadata != NULL);
	fail_unless(mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI)
				== NULL);
	fail_unless(mafw_metadata_first(metadata, MAFW_METADATA_KEY_MIME)
				!= NULL);
	fail_unless(mafw_metadata_first(metadata, MAFW_METADATA_KEY_CHILDCOUNT_1)
				!= NULL);

	fail_if(error != NULL);
	fail_unless(object_id != NULL);
	fail_if(strcmp(object_id, MAFW_IRADIO_SOURCE_UUID "::") != 0);
	fail_if(strcmp(g_value_get_string(
				mafw_metadata_first(metadata,
					MAFW_METADATA_KEY_MIME)),
				MAFW_METADATA_VALUE_MIME_CONTAINER) != 0);
	fail_if(g_value_get_int(mafw_metadata_first(metadata, MAFW_METADATA_KEY_CHILDCOUNT_1))
				!= g_list_length(created_ob_ids));
	mafw_metadata_release(metadata);
	checkmore_stop_loop();
}


static void mdat_get_wrong_cb(MafwSource *self, const gchar *object_id,
			GHashTable *metadata, gpointer mime,
			const GError *error)
{
	fail_unless(metadata == NULL);
	fail_if(error == NULL);
	fail_unless(object_id != NULL);
	fail_if(strcmp(object_id, MAFW_IRADIO_SOURCE_UUID "::999") != 0);
	checkmore_stop_loop();
}

static void mdata_chd_cb(MafwIradioSource *radio_src, gchar *oid,
				gpointer udata)
{
	fail_unless(oid != NULL);
	fail_if(strcmp(oid, created_ob_ids->data));
	checkmore_stop_loop();
}

START_TEST(test_get_set_metadata)
{
	MafwIradioSource *radio_src;
	GHashTable *mdat;
	
	radio_src = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	mdat = mafw_metadata_new();
	
	g_signal_connect(radio_src,"container-changed", (GCallback)cont_chd_cb,
				NULL);
	g_signal_connect(radio_src,"metadata-changed", (GCallback)mdata_chd_cb,
				NULL);
	
	fail_unless(radio_src != NULL);
	
	mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_URI,
					"http://test.uri/test.wav");
	mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_MIME,
					"audio/wav");
	
	mafw_source_create_object(MAFW_SOURCE(radio_src),
						MAFW_IRADIO_SOURCE_UUID "::",
						mdat, obi_created, NULL);
	checkmore_spin_loop(-1);
	mafw_metadata_release(mdat);
	

	/* GET metadata */
	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						NULL,
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME),
						mdat_get_not_called_cb, 
						NULL);

	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
						NULL,
						mdat_get_not_called_cb, 
						(gpointer)"audio/wav");

	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME),
						NULL, 
						NULL);
	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
						MAFW_SOURCE_NO_KEYS,
						mdat_get_not_called_cb, 
						(gpointer)"audio/wav");

	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						MAFW_IRADIO_SOURCE_UUID "::999",
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME),
						mdat_get_wrong_cb, 
						(gpointer)"audio/wav");

	checkmore_spin_loop(-1);
	
	
	/* Error-free cb */
	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						MAFW_IRADIO_SOURCE_UUID "::",
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME,
							MAFW_METADATA_KEY_CHILDCOUNT_1),
						mdat_get_root_cb, 
						NULL);
	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME),
						mdat_get_cb, 
						(gpointer)"audio/wav");

	checkmore_spin_loop(-1);

	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
						MAFW_SOURCE_ALL_KEYS,
						mdat_get_cb, 
						(gpointer)"audio/wav");

	checkmore_spin_loop(-1);

	/* SET metadata */
	mdat = mafw_metadata_new();
	mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_MIME,
					"audio/sound");

	/* This should not crash */
	mafw_source_set_metadata(MAFW_SOURCE(radio_src), 
						NULL,
						mdat,
						mdat_set_not_called_cb, NULL);

	mafw_source_set_metadata(MAFW_SOURCE(radio_src), 
						MAFW_IRADIO_SOURCE_UUID "::",
						mdat,
						mdat_set_error_cb, NULL);

	mafw_source_set_metadata(MAFW_SOURCE(radio_src), 
						"wrong::id",
						mdat,
						mdat_set_not_called_cb, NULL);

	mafw_source_set_metadata(MAFW_SOURCE(radio_src), 
						MAFW_IRADIO_SOURCE_UUID "::999",
						mdat,
						mdat_set_error_cb, NULL);

	mafw_source_set_metadata(MAFW_SOURCE(radio_src),
						created_ob_ids->data, NULL,
						mdat_set_not_called_cb, NULL);

	mafw_source_set_metadata(MAFW_SOURCE(radio_src),
						created_ob_ids->data, mdat,
						NULL, NULL);

	/* Error free parameters */
	mafw_source_set_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
						mdat,
						mdat_set_cb, NULL);
	checkmore_spin_loop(-1);

	mafw_source_get_metadata(MAFW_SOURCE(radio_src), 
						created_ob_ids->data,
					MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI,
							MAFW_METADATA_KEY_MIME),
						mdat_get_cb,
						(gpointer)"audio/sound");
	checkmore_spin_loop(-1);

	while (created_ob_ids)
	{
		mafw_source_destroy_object(MAFW_SOURCE(radio_src),
				created_ob_ids->data, obi_destroyed,
				NULL);
		checkmore_spin_loop(-1);
	}

	mafw_metadata_release(mdat);
	g_object_unref(radio_src);
}
END_TEST

static guint b_cb_called;

struct browse_res_comp{
	guint bid;
	gchar **ob_id_list; /* list of obid, in order */
	GPtrArray *metadatas; /* reference metadatas*/
	gboolean has_error;
};

static void free_mdat_array(gpointer data, gpointer user_data)
{
	mafw_metadata_release(data);
}

static void compare_ref(gchar *key, GValue *refval, GHashTable *metadata)
{
	GValue *cur_val;
	gchar *cur_val_ser, *refval_ser;
	
	cur_val = mafw_metadata_first(metadata, key);
	fail_if(cur_val == NULL);
	fail_if(refval == NULL,
			"Reference value missing");
	cur_val_ser = g_strdup_value_contents(cur_val);
	refval_ser = g_strdup_value_contents(refval);
	
	fail_if(strcmp(cur_val_ser, refval_ser) != 0,
		"Comparing values failed....: %s vs. %s",
		cur_val_ser, refval_ser);
	g_free(cur_val_ser);
	g_free(refval_ser);
}

static void browse_empty_res(MafwSource *self, guint browse_id, gint remaining_count,
			guint index, const gchar *object_id,
			GHashTable *metadata, struct browse_res_comp *ref_data,
			const GError *error)
{
	fail_if(browse_id != ref_data->bid);
	fail_if(remaining_count != 0);
	fail_if(index != 0);
	fail_if(object_id != NULL);
	fail_if(metadata != NULL);
	fail_if(error != NULL);
	
	b_cb_called++;
	checkmore_stop_loop();
}

static void browse_res(MafwSource *self, guint browse_id, gint remaining_count,
			guint index, const gchar *object_id,
			GHashTable *metadata, struct browse_res_comp *ref_data,
			const GError *error)
{
	fail_if(browse_id != ref_data->bid);
	if (ref_data->has_error)
	{
		fail_if(remaining_count != 0);
		fail_if(index != 0);
		fail_if(object_id != NULL);
		fail_if(metadata != NULL);
		fail_if(error == NULL);
		checkmore_stop_loop();
	}
	else
	{
		GList *item_to_find = g_list_find_custom(created_ob_ids,
					object_id, (GCompareFunc)strcmp);
		fail_if(error);
		/* Returned object should be in the list */
		fail_unless(item_to_find != NULL);
		
		if (ref_data->ob_id_list)
			fail_if(strcmp(ref_data->ob_id_list[index], object_id) 
					!= 0, "Wrong objectid. Ref: %s CB: %s",
					ref_data->ob_id_list[index], object_id);
		if (ref_data->metadatas)
		{
			GHashTable *cur_ref_mdat = g_ptr_array_index(
							ref_data->metadatas,
							index);
			
			fail_if(metadata == NULL);
			fail_if(cur_ref_mdat == NULL, "Reference mdata missing");
			
			g_hash_table_foreach(cur_ref_mdat, (GHFunc)compare_ref,
						metadata);
			
			fail_if(g_hash_table_size(metadata) !=
					g_hash_table_size(cur_ref_mdat),
						"More mdata??");
			
		}
		else
		{
			fail_if(metadata != NULL);
		}
		
		if (!remaining_count)
			checkmore_stop_loop();
	}
	b_cb_called++;
}

static void browse_res_cancel(MafwSource *self, guint browse_id, gint remaining_count,
			guint index, const gchar *object_id,
			GHashTable *metadata, struct browse_res_comp *ref_data,
			const GError *error)
{
	fail_if(browse_id != ref_data->bid);
	fail_unless(mafw_source_cancel_browse(self,
				browse_id, NULL));
	b_cb_called++;
}

START_TEST(test_browse)
{
	MafwIradioSource *radio_src;
	struct browse_res_comp br_res_ref;
	GHashTable *mdat, *temp_ht;
	gchar *n_uri;
	gint i;
	MafwFilter *filter = NULL;
	
	memset(&br_res_ref, 0, sizeof br_res_ref);
	radio_src = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	mdat = mafw_metadata_new();
	
	fail_unless(radio_src != NULL);
	
	/* Call with empty DB */
	b_cb_called = 0;
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_empty_res,
				&br_res_ref)) ==
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 1);
	
	mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_MIME,
					"audio/wav");
	for (i=0; i < 10 ; i++)
	{
		n_uri = g_strdup_printf("http://test.uri/%d.wav", i);
		mafw_metadata_add_str(mdat, MAFW_METADATA_KEY_URI, n_uri);
		g_free(n_uri);
	
		mafw_metadata_add_int(mdat, MAFW_METADATA_KEY_AUDIO_BITRATE, i);
		mafw_metadata_add_int(mdat, MAFW_METADATA_KEY_DURATION, i);
		mafw_source_create_object(MAFW_SOURCE(radio_src),
						MAFW_IRADIO_SOURCE_UUID "::",
						mdat, obi_created, NULL);
		
		g_hash_table_remove(mdat, MAFW_METADATA_KEY_URI);
		g_hash_table_remove(mdat, MAFW_METADATA_KEY_AUDIO_BITRATE);
		g_hash_table_remove(mdat, MAFW_METADATA_KEY_DURATION);
	}
	mafw_metadata_release(mdat);
	
	/* Failing cases */	
	expect_fallback(mafw_source_browse(MAFW_SOURCE(radio_src), NULL, FALSE,
				NULL, NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_res, NULL),
					MAFW_SOURCE_INVALID_BROWSE_ID);
	expect_fallback(mafw_source_browse(MAFW_SOURCE(radio_src), "wrong::oid",
				FALSE, NULL, NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_res, NULL),
				MAFW_SOURCE_INVALID_BROWSE_ID);
	expect_fallback(mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 0, 0,
				NULL, NULL),
				MAFW_SOURCE_INVALID_BROWSE_ID);
	
	br_res_ref.has_error = FALSE;
	/* Should not fail */
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_res, NULL))
					== MAFW_SOURCE_INVALID_BROWSE_ID);
	/* main-loop is not running, so a browse-cb should not be called with
	 * the last bID */
	/* Wrong cancel parameter */
	fail_if(mafw_source_cancel_browse(MAFW_SOURCE(radio_src),
				br_res_ref.bid+1, NULL));
	/* Good BID */
	fail_unless(mafw_source_cancel_browse(MAFW_SOURCE(radio_src),
				br_res_ref.bid, NULL));
	
	b_cb_called = 0;
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) ==
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 10);
	b_cb_called = 0;
	
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 0, 0,
				(MafwSourceBrowseResultCb)browse_res_cancel,
				&br_res_ref)) ==
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(1000);
	fail_if(b_cb_called != 1);
	b_cb_called = 0;

	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, MAFW_SOURCE_NO_KEYS, 0, 0,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) == 
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 10);

	/* Do it with a skip and item value */
	b_cb_called = 0;
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 5, 3,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) == 
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 3);
	
	/* CB should have an error */
	br_res_ref.has_error = TRUE;
	b_cb_called = 0;
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, NULL,
				NULL, NULL, 10, 10,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) == 
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 1);
	
	/* Complicated browse, to test the filter, sort, and mdata-keys, with
	skip, item-count */
	br_res_ref.has_error = FALSE;
		
	br_res_ref.ob_id_list = g_new0(gchar*, 3);
	br_res_ref.ob_id_list[0] = g_list_nth_data(created_ob_ids, 2);
	br_res_ref.ob_id_list[1] = g_list_nth_data(created_ob_ids, 3);
	
	br_res_ref.metadatas = g_ptr_array_new();
	
	temp_ht = mafw_metadata_new();
	mafw_metadata_add_str(temp_ht, MAFW_METADATA_KEY_URI,
					"http://test.uri/7.wav");
	g_ptr_array_add(br_res_ref.metadatas, temp_ht);
	temp_ht = mafw_metadata_new();
	mafw_metadata_add_str(temp_ht, MAFW_METADATA_KEY_URI,
					"http://test.uri/6.wav");
	g_ptr_array_add(br_res_ref.metadatas, temp_ht);
	
	b_cb_called = 0;
	filter = mafw_filter_parse("("MAFW_METADATA_KEY_AUDIO_BITRATE">4)");
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, 
				filter,
				"-"MAFW_METADATA_KEY_DURATION,
				MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI), 2, 2,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) == 
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 2);
	mafw_filter_free(filter);
	
	/* The same, but now wiht all the metadata keys */
	b_cb_called = 0;
	
	temp_ht = g_ptr_array_index(br_res_ref.metadatas, 0);
	mafw_metadata_add_int(temp_ht, MAFW_METADATA_KEY_AUDIO_BITRATE, 7);
	mafw_metadata_add_int(temp_ht, MAFW_METADATA_KEY_DURATION, 7);
	mafw_metadata_add_str(temp_ht, MAFW_METADATA_KEY_MIME, "audio/wav");
	temp_ht = g_ptr_array_index(br_res_ref.metadatas, 1);
	mafw_metadata_add_int(temp_ht, MAFW_METADATA_KEY_AUDIO_BITRATE, 6);
	mafw_metadata_add_int(temp_ht, MAFW_METADATA_KEY_DURATION, 6);
	mafw_metadata_add_str(temp_ht, MAFW_METADATA_KEY_MIME, "audio/wav");

	filter = mafw_filter_parse("("MAFW_METADATA_KEY_AUDIO_BITRATE">4)");
	fail_if((br_res_ref.bid = mafw_source_browse(MAFW_SOURCE(radio_src),
				MAFW_IRADIO_SOURCE_UUID "::", FALSE, 
				filter,
				"-"MAFW_METADATA_KEY_DURATION,
				MAFW_SOURCE_ALL_KEYS, 2, 2,
				(MafwSourceBrowseResultCb)browse_res,
				&br_res_ref)) == 
				MAFW_SOURCE_INVALID_BROWSE_ID);
	checkmore_spin_loop(-1);
	fail_if(b_cb_called != 2);
	mafw_filter_free(filter);
	
	g_free(br_res_ref.ob_id_list);
	g_ptr_array_foreach(br_res_ref.metadatas, free_mdat_array, NULL);
	g_ptr_array_free(br_res_ref.metadatas, TRUE);	
	
	g_object_unref(radio_src);
	
}
END_TEST

/*---------------------------------------------------------------------------
 Vendor bookmarks testing
 ----------------------------------------------------------------------------*/
static gint _customization_browse_results = 0;
static time_t ref_time;

static void vendor_obi_destroyed(MafwSource *self, const gchar *object_id,
				gpointer user_data, const GError *error)
{
	fail_if(error != NULL);
	checkmore_stop_loop();
}

static gboolean browse_results[6] = {0, };

static void confml_browse_result(MafwSource *self, guint browse_id,
				 gint remaining_count, guint index,
				 const gchar *object_id, GHashTable *metadata,
				 gpointer user_data, const GError *error)
{
	GValue* value;
	const gchar* str;
	gchar* path;
	gint numval;
	glong stored_time;
	gint round = 0;
	
	if (user_data)
		round = 1;

	_customization_browse_results++;

	fail_unless(error == NULL);

	/* Title */
	value = mafw_metadata_first(metadata,
				     MAFW_METADATA_KEY_TITLE);
	str = g_value_get_string(value);
	if (strcmp(str, "VideoStream2") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "mms://videobroadcast.com/someothervideo") == 0);

		/* Thumbnail URI */
		path = g_strdup_printf("file://%s/icon2.png", vendor_setup_path);
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_THUMBNAIL_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, path) == 0, "%s vs %s", str, path);
		g_free(path);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);
		fail_if(browse_results[0]);
		browse_results[0] = TRUE;
	} else if (strcmp(str, "VideoStream1") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "mms://videobroadcast.com/somevideo") == 0);

		/* Thumbnail URI */
		path = g_strdup_printf("file://%s/icon1.png", vendor_setup_path);
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_THUMBNAIL_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, path) == 0);
		g_free(path);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);
		fail_if(browse_results[1]);
		browse_results[1] = TRUE;
	} else if (strcmp(str, "BBC World News Summary") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "http://www.bbc.co.uk/worldservice/meta/tx/nb/summary5min_au_nb.ram") == 0);

		/* Duration */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_DURATION);
		fail_if(!value);
		numval = g_value_get_int(value);
		fail_if(numval != 234);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);
		fail_if(browse_results[2]);
		browse_results[2] = TRUE;
	} else if (strcmp(str, "BBC Sport Roundup") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "http://www.bbc.co.uk/worldservice/ram/sportsroundup.ram") == 0);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);
		fail_if(browse_results[3]);
		browse_results[3] = TRUE;
	} else if (strcmp(str, "BBC World Service") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "http://www.bbc.co.uk/worldservice/meta/tx/nb/live_news_au_nb.ram") == 0);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);
		fail_if(browse_results[4]);
		browse_results[4] = TRUE;
	} else if (strcmp(str, "BBC Radio 1") == 0)
	{
		/* URI */
		value = mafw_metadata_first(metadata, MAFW_METADATA_KEY_URI);
		str = g_value_get_string(value);
		fail_unless(strcmp(str, "http://www.bbc.co.uk/radio1/realaudio/media/r1live.ram") == 0);

		/* Added */
		value = mafw_metadata_first(metadata,
					     MAFW_METADATA_KEY_ADDED);
		fail_if(value == NULL);
		stored_time = g_value_get_long(value);
		fail_unless(stored_time < ref_time || ref_time + 3 > stored_time);

		fail_if(browse_results[5]);
		browse_results[5] = TRUE;
	} else
		g_assert_not_reached();
	
	switch (index)
	{
	case 0:
		fail_unless(remaining_count == 5 - round,
			    "Expected %d, but got %d", 5 - round, remaining_count);
		break;
	case 1:
		fail_unless(remaining_count == 4 - round,
			    "Expected %d, but got %d", 4 - round, remaining_count);
		break;
	case 2:
		fail_unless(remaining_count == 3 - round,
			    "Expected %d, but got %d", 3 - round, remaining_count);
		break;
	case 3:
		fail_unless(remaining_count == 2 - round,
			    "Expected %d, but got %d", 2 - round, remaining_count);
		break;
	case 4:
		fail_unless(remaining_count == 1 - round,
			    "Expected %d, but got %d", 1 - round, remaining_count);
		if (user_data)
			checkmore_stop_loop();
		break;
	case 5:
		fail_unless(remaining_count == 0,
			    "Expected 0, but got %d", remaining_count);

		mafw_source_destroy_object(self, object_id, vendor_obi_destroyed, NULL);
		break;
	default:
		fail("Too many objects parsed to the database");
	}
}

/* Returns URI containing the absolute path for a file 
 * this is done for make distcheck to work
 */
static gchar * uri_path(gchar *filename)
{
  gchar *srcdir ;
  gchar *uri ;
  char realsrcdir[PATH_MAX];

  srcdir = g_strdup(g_getenv("srcdir"));
  if (!srcdir)
	  srcdir = g_get_current_dir();

   g_assert(realpath(srcdir, realsrcdir));
   uri = g_strdup_printf("file://%s/%s", realsrcdir, filename);
   g_free(srcdir);

   return uri;
}
				       
START_TEST(test_confml_parse)
{
	MafwIradioSource* source;
	gchar *uri;
	struct stat vendorstat;
	struct utimbuf newmodtime;

	vendor_setup_path = TEST_DIR;
	/* Get rid of other stuff within the database */
	unlink("test-iradiosource.db");

	/* Create a new iradio source */
	source = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	fail_if(source == NULL, "Unable to create an IRadio source");

	/* Parse the test content into the database */
	uri = uri_path("bookmarks.xml");
	ref_time = time(NULL);

	/* Browse for the test content */
	mafw_source_browse(MAFW_SOURCE(source),
			    MAFW_IRADIO_SOURCE_UUID "::",
			    FALSE,
			    NULL,
			    NULL,
			    MAFW_SOURCE_ALL_KEYS,
			    0,
			    MAFW_SOURCE_BROWSE_ALL,
			    confml_browse_result,
			    NULL);
	checkmore_spin_loop(-1);

	fail_unless(_customization_browse_results == 6,
		    "Not enough browse results for customization: %d",
		    _customization_browse_results);
	g_object_unref(source);
	_customization_browse_results = 0;

	source = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	fail_if(source == NULL, "Unable to create an IRadio source again");

	memset(&browse_results, 0, sizeof(gboolean) * 6);
	/* Browse for the test content... it should not add the missing items */
	mafw_source_browse(MAFW_SOURCE(source),
			    MAFW_IRADIO_SOURCE_UUID "::",
			    FALSE,
			    NULL,
			    NULL,
			    MAFW_SOURCE_ALL_KEYS,
			    0,
			    MAFW_SOURCE_BROWSE_ALL,
			    confml_browse_result,
			    (gpointer)source);

	checkmore_spin_loop(-1);
	fail_unless(_customization_browse_results == 5,
		    "Not enough browse results for customization: %d",
		    _customization_browse_results);
	g_object_unref(source);
	_customization_browse_results = 0;

	fail_if(g_stat(uri+7, &vendorstat) != 0);
	newmodtime.actime = vendorstat.st_atime;
	newmodtime.modtime = vendorstat.st_mtime + 5;
	fail_if(g_utime(uri+7, &newmodtime) != 0);

	source = MAFW_IRADIO_SOURCE(mafw_iradio_source_new());
	fail_if(source == NULL, "Unable to create an IRadio source again");

	ref_time = time(NULL);

	memset(&browse_results, 0, sizeof(gboolean) * 6);
	/* Browse for the test content */
	mafw_source_browse(MAFW_SOURCE(source),
			    MAFW_IRADIO_SOURCE_UUID "::",
			    FALSE,
			    NULL,
			    NULL,
			    MAFW_SOURCE_ALL_KEYS,
			    0,
			    MAFW_SOURCE_BROWSE_ALL,
			    confml_browse_result,
			    NULL);

	checkmore_spin_loop(-1);

	fail_unless(_customization_browse_results == 6,
		    "Not enough browse results for customization: %d",
		    _customization_browse_results);
	g_object_unref(source);

	g_free(uri);
}
END_TEST

/*---------------------------------------------------------------------------
 Main
 ----------------------------------------------------------------------------*/
 
int main(void)
{
	TCase *tc;
	Suite *suite;

	g_type_init();

	unlink("test-iradiosource.db");
	g_setenv("MAFW_DB", "test-iradiosource.db", TRUE);
	
	suite = suite_create("MafwIRadioSource");

	/* Browse tests */
	tc = tcase_create("Browse");
	suite_add_tcase(suite, tc);
if (1)	tcase_add_test(tc, test_plugin);
if (1)	tcase_add_test(tc, test_add_remove);
if (1)	tcase_add_test(tc, test_get_set_metadata);
if (1)	tcase_add_test(tc, test_browse);
	tcase_set_timeout(tc, 60); /* With valgrind, it could need more time */

	tc = tcase_create("Customization");
if (1)	suite_add_tcase(suite, tc);
	tcase_add_test(tc, test_confml_parse);
	tcase_set_timeout(tc, 60); /* With valgrind, it could need more time */

        return checkmore_run(srunner_create(suite), FALSE);
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
