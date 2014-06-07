/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 *
 * \file    fsal_internal.h
 * \version Revision: 1.12
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 *
 */

#ifndef _LUSTRE_FSAL_INTERNAL_H
#define _LUSTRE_FSAL_INTERNAL_H


#include "fsal.h"
#include <sys/stat.h>
#include "fsal_pnfs.h"
#include "lustre_extended_types.h"

#define min(a, b)          \
	({ typeof(a) _a = (a);     \
	typeof(b) _b = (b);        \
	_a < _b ? _a : _b; })

/* this needs to be refactored to put ipport inside sockaddr_in */
struct lustre_pnfs_ds_parameter {
	struct glist_head ds_list;
	struct sockaddr_storage ipaddr; /* sockaddr_storage would be better */
	unsigned short ipport;
	unsigned int id;
};

struct lustre_pnfs_parameter {
	unsigned int stripe_size; /* unused */
	unsigned int stripe_width;
	struct glist_head ds_list;
};

/* defined the set of attributes supported with POSIX */
#define LUSTRE_SUPPORTED_ATTRIBUTES (                                       \
	ATTR_TYPE     | ATTR_SIZE     |                  \
	ATTR_FSID     | ATTR_FILEID   |                  \
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
	ATTR_CHGTIME)

#define BIGGEST_PATTERN 1024

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern struct fsal_staticfsinfo_t global_fs_info;

/* export_context_t is not given to every function, but
 * most functions need to use the open-by-handle funcionality.
 */
extern char open_by_handle_path[MAXPATHLEN];
extern int open_by_handle_fd;

#endif

#ifdef USE_FSAL_SHOOK
/*
 * Shook related stuff
 */
fsal_status_t lustre_shook_restore(struct fsal_obj_handle *obj_hdl,
				   bool do_truncate,
				   int *trunc_done);
#endif


/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);

void set_credentials(struct user_cred *creds);
void set_creds_to_root();

struct lustre_ds {
	struct lustre_file_handle wire; /*< Wire data */
	struct fsal_ds_handle ds; /*< Public DS handle */
	struct lustre_filesystem *lustre_fs; /*< Related Lustre filesystem */
	bool connected; /*< True if the handle has been connected */
};

void lustre_handle_ops_init(struct fsal_obj_ops *ops);
extern bool pnfs_enabled;

/* Add missing prototype in vfs.h */
int fd_to_handle(int fd, void **hanp, size_t *hlen);
void lustre_export_ops_init(struct export_ops *ops);
void lustre_handle_ops_init(struct fsal_obj_ops *ops);
extern struct lustre_pnfs_parameter pnfs_param;

/* LUSTRE methods for pnfs
 */

nfsstat4 lustre_getdeviceinfo(struct fsal_module *fsal_hdl,
			      XDR *da_addr_body,
			      const layouttype4 type,
			      const struct pnfs_deviceid *deviceid);

size_t lustre_fs_da_addr_size(struct fsal_module *fsal_hdl);

#endif
