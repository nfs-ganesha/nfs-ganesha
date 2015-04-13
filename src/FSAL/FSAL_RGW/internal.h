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

#ifndef FSAL_RGW_INTERNAL_INTERNAL__
#define FSAL_RGW_INTERNAL_INTERNAL__

#include <stdbool.h>
#include <uuid/uuid.h>

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"


/**
 * RGW Main (global) module object
 */

struct rgw_fsal_module {
	struct fsal_module fsal;
	fsal_staticfsinfo_t fs_info;
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
	struct rgw_handle *root;	/*< The root handle */
	char rgw_user_id[MAXUIDLEN + 1];
	char rgw_access_key_id[MAXKEYLEN + 1];
	char rgw_secret_access_key[MAXSECRETLEN + 1];
};

/**
 * The RGW FSAL internal handle
 */

struct rgw_handle {
	struct fsal_obj_handle handle;	/*< The public handle */
	fsal_openflags_t openflags;
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	struct rgw_export *export;	/*< The first export this handle
					 *< belongs to */
	uint64_t nfs_handle;
};

#ifndef RGW_INTERNAL_C
/* Keep internal.c from clashing with itself */
extern attrmask_t rgw_supported_attributes;
extern attrmask_t rgw_settable_attributes;
#endif				/* !RGW_INTERNAL_C */

/**
 * Linux supports a stripe pattern with no more than 4096 stripes, but
 * for now we stick to 1024 to keep them da_addrs from being too
 * gigantic.
 */

static const size_t BIGGEST_PATTERN = 1024;

static inline fsal_staticfsinfo_t *rgw_staticinfo(struct fsal_module *hdl)
{
	struct rgw_fsal_module *myself =
	    container_of(hdl, struct rgw_fsal_module, fsal);
	return &myself->fs_info;
}

/* Prototypes */
int construct_handle(const struct stat *st, uint64_t nfs_handle,
		     struct rgw_export *export, struct rgw_handle **obj);
void deconstruct_handle(struct rgw_handle *obj);

fsal_status_t rgw2fsal_error(const int errorcode);
void rgw2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr);
void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);

#endif				/* !FSAL_RGW_INTERNAL_INTERNAL__ */
