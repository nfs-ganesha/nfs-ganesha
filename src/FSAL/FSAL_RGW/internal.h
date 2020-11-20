/*
 * Copyright © Red Hat 2015
 * Author: Orit Wasserman <owasserm@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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

/**
 * @file   internal.h
 * @brief Internal declarations for the RGW FSAL
 *
 * This file includes declarations of data types, functions,
 * variables, and constants for the RGW FSAL.
 */

#ifndef FSAL_RGW_INTERNAL_INTERNAL
#define FSAL_RGW_INTERNAL_INTERNAL

#include <stdbool.h>
#include <uuid/uuid.h>
#include <dirent.h> /* NAME_MAX */

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include "sal_data.h"

#include <include/rados/librgw.h>
#include <include/rados/rgw_file.h>

#if (!GSH_CHECK_VERSION(1, 1, 7, LIBRGW_FILE_VER_MAJOR, LIBRGW_FILE_VER_MINOR, \
			LIBRGW_FILE_VER_EXTRA))
#error rados/rgw_file.h version unsupported (require >= 1.1.7)
#endif

/**
 * RGW Main (global) module object
 */

struct rgw_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
	char *conf_path;
	char *name;
	char *cluster;
	char *init_args;
	librgw_t rgw;
};
extern struct rgw_fsal_module RGWFSM;


#define MAXUIDLEN 32
#define MAXKEYLEN 20
#define MAXSECRETLEN 40

/**
 * RGW internal export object
 */

struct rgw_export {
	struct fsal_export export;	/*< The public export object */
	struct rgw_fs *rgw_fs;		/*< "Opaque" fs handle */
	struct rgw_handle *root;    /*< root handle */
	char *rgw_name;
	char *rgw_user_id;
	char *rgw_access_key_id;
	char *rgw_secret_access_key;
};

/**
 * The RGW FSAL internal handle
 */

struct rgw_handle {
	struct fsal_obj_handle handle;	/*< The public handle */
	struct rgw_file_handle *rgw_fh;  /*< RGW-internal file handle */
	/* XXXX remove ptr to up-ops--we can always follow export! */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	struct rgw_export *export;	/*< The first export this handle
					 *< belongs to */
	struct fsal_share share;
	fsal_openflags_t openflags;
};

/**
 * RGW "file descriptor"
 */
struct rgw_open_state {
	struct state_t gsh_open;
	fsal_openflags_t openflags;
};

/**
 * The attributes this FSAL can interpret or supply.
 * Currently FSAL_RGW uses posix2fsal_attributes, so we should indicate support
 * for at least those attributes.
 */
#define RGW_SUPPORTED_ATTRIBUTES ((ATTRS_POSIX | ATTR4_XATTR))

/**
 * The attributes this FSAL can set.
 */
#define RGW_SETTABLE_ATTRIBUTES ((const attrmask_t) (			\
	ATTR_MODE  | ATTR_OWNER | ATTR_GROUP | ATTR_ATIME	 |\
	ATTR_CTIME | ATTR_MTIME | ATTR_SIZE  | ATTR_MTIME_SERVER |\
	ATTR_ATIME_SERVER | ATTR4_XATTR))

/**
 * Linux supports a stripe pattern with no more than 4096 stripes, but
 * for now we stick to 1024 to keep them da_addrs from being too
 * gigantic.
 */

static const size_t BIGGEST_PATTERN = 1024;

/* Prototypes */
int construct_handle(struct rgw_export *export,
		     struct rgw_file_handle *rgw_file_handle,
		     struct stat *st,
		     struct rgw_handle **obj);
void deconstruct_handle(struct rgw_handle *obj);

fsal_status_t rgw2fsal_error(const int errorcode);
void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);
struct state_t *rgw_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state);
void rgw_fs_invalidate(void *handle, struct rgw_fh_hk fh_hk);
#endif				/* !FSAL_RGW_INTERNAL_INTERNAL */
