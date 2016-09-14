/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Scality Inc., 2016
 * Author: Guillaume Gimenez ploki@blackmilk.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* dbd_rest_client.h
 */

#ifndef __DBD_REST_CLIENT_H__
#define __DBD_REST_CLIENT_H__
typedef enum {
	DBD_LOOKUP_IS_LAST,
	DBD_LOOKUP_IS_NOT_LAST,
	DBD_LOOKUP_ENOENT,
	DBD_LOOKUP_ERROR,
} dbd_is_last_result_t;

/**
 * @brief check if an object is the last in its prefix
 *
 * @param export - the export context
 * @param dir_hdl - used as prefix in the query
 * @return the result of the lookup query
 */
dbd_is_last_result_t
dbd_is_last(struct scality_fsal_export *export,
	    struct scality_fsal_obj_handle *dir_hdl);


typedef enum {
	DBD_DTYPE_IOERR = -1,
	DBD_DTYPE_ENOENT,
	DBD_DTYPE_REGULAR,
	DBD_DTYPE_DIRECTORY
} dbd_dtype_t;

/**
 * @brief perform a lookup in a directory
 *
 * @param export - the export context
 * @param parent_hdl - the object handle of the directory in which to perform the lookup
 * @param name - the name of the object (either a regular file or a directory)
 *                in the directory to lookup
 * @param[out] dtypep - the type of the object looked up or DBD_DTUP_ENOENT
 * @return 0 on success
 */
int
dbd_lookup(struct scality_fsal_export *export,
	   struct scality_fsal_obj_handle *parent_hdl,
	   const char *name,
	   dbd_dtype_t *dtypep);

/**
 * @brief perform a lookup in a bucket
 *
 * @param export - the export context
 * @param object - the path to the object
 * @param[out] dtypep - the type of the object looked up or DBD_DTUP_ENOENT
 * @return 0 on success
 */
int
dbd_lookup_object(struct scality_fsal_export *export,
		  const char *object,
		  dbd_dtype_t *dtypep);


typedef bool (*dbd_readdir_cb)(const char *name, void *user_data, fsal_cookie_t *cookiep);


int
dbd_readdir(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *myself,
	    const fsal_cookie_t *whence,
	    void *dir_state,
	    dbd_readdir_cb cb,
	    bool *eof);
int
dbd_getattr(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *object_hdl);

int
dbd_collect_bucket_attributes(struct scality_fsal_export *export);

int
dbd_delete(struct scality_fsal_export *export,
	   const char *object);
int
dbd_post(struct scality_fsal_export* export,
	 struct scality_fsal_obj_handle *object_hdl);

#endif /* __DBD_REST_CLIENT_H__ */
