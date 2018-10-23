/*
 * Copyright © 2012-2014, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
 *		  Marcus Watts <mdw@cohortfs.com>
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
 * @author William Allen Simpson <bill@cohortfs.com>
 * @date Wed Oct 22 13:24:33 2014
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
#include "fsal_convert.h"
#include <stdbool.h>
#include <uuid/uuid.h>
#include "statx_compat.h"
#include "FSAL/fsal_commonlib.h"

/* Max length of a user_id string that we pass to ceph_mount */
#define MAXUIDLEN	(64)

/* Max length of a secret key for this user */
#define MAXSECRETLEN	(88)

/**
 * Ceph Main (global) module object
 */

struct ceph_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
	char *conf_path;
};
extern struct ceph_fsal_module CephFSM;

/**
 * Ceph private export object
 */

struct ceph_export {
	struct fsal_export export;	/*< The public export object */
	struct ceph_mount_info *cmount;	/*< The mount object used to
					   access all Ceph methods on
					   this export. */
	struct ceph_handle *root;	/*< The root handle */
	char *user_id;			/* cephx user_id for this mount */
	char *secret_key;
	char *sec_label_xattr;		/* name of xattr for security label */
	char *fs_name;			/* filesystem name */
	int64_t fscid;			/* Cluster fsid for named fs' */
};

struct ceph_fd {
	/** The open and share mode etc. */
	fsal_openflags_t openflags;
	/* rw lock to protect the file descriptor */
	pthread_rwlock_t fdlock;
	/** The cephfs file descriptor. */
	Fh *fd;
};

struct ceph_state_fd {
	struct state_t state;
	struct ceph_fd ceph_fd;
};

/**
 * The 'private' Ceph FSAL handle
 */

struct ceph_handle_key {
	vinodeno_t	chk_vi;
	int64_t		chk_fscid;
} __attribute__ ((__packed__));

struct ceph_handle {
	struct fsal_obj_handle handle;	/*< The public handle */
	struct ceph_fd fd;
	struct Inode *i;	/*< The Ceph inode */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	/*< The first export this handle belongs to */
	struct ceph_export *export;
	struct ceph_handle_key key;
	struct fsal_share share;
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
	struct fsal_ds_handle ds;	/*< Public DS handle */
	struct ds_wire wire;	/*< Wire data */
	bool connected;		/*< True if the handle has been connected
				   (in Ceph) */
};

#endif				/* CEPH_PNFS */

#define CEPH_SUPPORTED_ATTRS ((const attrmask_t) (ATTRS_POSIX|ATTR4_SEC_LABEL))

#define CEPH_SETTABLE_ATTRIBUTES ((const attrmask_t) (			\
	ATTR_MODE  | ATTR_OWNER | ATTR_GROUP | ATTR_ATIME	 |	\
	ATTR_CTIME | ATTR_MTIME | ATTR_SIZE  | ATTR_MTIME_SERVER |	\
	ATTR_ATIME_SERVER | ATTR4_SEC_LABEL))

/* Prototypes */

void construct_handle(const struct ceph_statx *stx,
					struct Inode *i,
					struct ceph_export *export,
					struct ceph_handle **obj);
void deconstruct_handle(struct ceph_handle *obj);

/**
 * @brief FSAL status from Ceph error
 *
 * This function returns a fsal_status_t with the FSAL error as the
 * major, and the posix error as minor. (Ceph's error codes are just
 * negative signed versions of POSIX error codes.)
 *
 * @param[in] ceph_errorcode Ceph error (negative Posix)
 *
 * @return FSAL status.
 */
static inline fsal_status_t ceph2fsal_error(const int ceph_errorcode)
{
	return fsalstat(posix2fsal_error(-ceph_errorcode), -ceph_errorcode);
}

unsigned int attrmask2ceph_want(attrmask_t mask);
void ceph2fsal_attributes(const struct ceph_statx *stx,
			  struct attrlist *fsalattr);

void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);
#ifdef CEPH_PNFS
void pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops);
void export_ops_pnfs(struct export_ops *ops);
void handle_ops_pnfs(struct fsal_obj_ops *ops);
#endif				/* CEPH_PNFS */

struct state_t *ceph_alloc_state(struct fsal_export *exp_hdl,
				 enum state_type state_type,
				 struct state_t *related_state);

void ceph_free_state(struct fsal_export *exp_hdl, struct state_t *state);

#endif				/* !FSAL_CEPH_INTERNAL_INTERNAL__ */
