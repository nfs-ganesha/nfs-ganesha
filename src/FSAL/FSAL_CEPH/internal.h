/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul  9 13:33:32 2012
 *
 * @brief Internal declarations for the Ceph FSAL
 *
 * This file includes declarations of data types, functions,
 * variables, and constants for the Ceph FSAL.
 */

#ifndef FSAL_CEPH_INTERNAL_INTERNAL__
#define FSAL_CEPH_INTERNAL_INTERNAL__

#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include <stdbool.h>

/**
 * Ceph private export object
 */

struct export {
	struct ceph_mount_info *cmount;	/*< The mount object used to
					   access all Ceph methods on
					   this export. */
	struct fsal_export export;	/*< The public export object */
	struct handle *root;	/*< The root handle */
};

/**
 * The 'private' the Ceph FSAL handle
 */

struct handle {
	vinodeno_t vi;		/*< The object identifier */
	struct fsal_obj_handle handle;	/*< The public handle */
	Fh *fd;
	struct Inode *i;	/*< The Ceph inode */
	fsal_openflags_t openflags;
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	struct export *export;	/*< The first export this handle belongs to */
#ifdef CEPH_PNFS
	uint64_t rd_issued;
	uint64_t rd_serial;
	uint64_t rw_issued;
	uint64_t rw_serial;
	uint64_t rw_max_len;
#endif				/* CEPH_PNFS */
};

#ifdef CEPH_PNFS

/**
 * The wire content of a DS (data server) handle
 */

struct ds_wire {
	struct wire_handle wire; /*< All the information of a regualr handle */
	struct ceph_file_layout layout;	/*< Layout information */
	uint64_t snapseq; /*< And a single entry giving a degernate
			      snaprealm. */
};

/**
 * The full, 'private' DS (data server) handle
 */

struct ds {
	struct ds_wire wire;	/*< Wire data */
	struct fsal_ds_handle ds;	/*< Public DS handle */
	bool connected;		/*< True if the handle has been connected
				   (in Ceph) */
};

#endif				/* CEPH_PNFS */

#ifndef CEPH_INTERNAL_C
/* Keep internal.c from clashing with itself */
extern attrmask_t supported_attributes;
extern attrmask_t settable_attributes;
#endif				/* !CEPH_INTERNAL_C */

/**
 * Linux supports a stripe pattern with no more than 4096 stripes, but
 * for now we stick to 1024 to keep them da_addrs from being too
 * gigantic.
 */

static const size_t BIGGEST_PATTERN = 1024;

/* Prototypes */

int construct_handle(const struct stat *st, struct Inode *i,
		     struct export *export, struct handle **obj);
void deconstruct_handle(struct handle *obj);
fsal_status_t ceph2fsal_error(const int ceph_errorcode);
void ceph2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr);
void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);
#ifdef CEPH_PNFS
void ds_ops_init(struct fsal_ds_ops *ops);
void export_ops_pnfs(struct export_ops *ops);
void handle_ops_pnfs(struct fsal_obj_ops *ops);
#endif				/* CEPH_PNFS */

#endif				/* !FSAL_CEPH_INTERNAL_INTERNAL__ */
