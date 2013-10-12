/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2011
 * Author: Anand Subramanian anands@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

#ifndef GLUSTER_INTERNAL
#define GLUSTER_INTERNAL

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define GLUSTER_VOLNAME_KEY  "volume"
#define GLUSTER_HOSTNAME_KEY "hostname"
#define GLUSTER_VOLPATH_KEY  "volpath"
#define MAX_2( x, y )	 ( (x) > (y) ? (x) : (y) )

/* defined the set of attributes supported with POSIX */
#define GLUSTERFS_SUPPORTED_ATTRIBUTES (         \
ATTR_TYPE     | ATTR_SIZE     |                  \
ATTR_FSID     | ATTR_FILEID   |                  \
ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
ATTR_CHGTIME  )

/**
 * The attributes this FSAL can set.
 */

#define GLUSTERFS_SETTABLE_ATTRIBUTES (                  \
ATTR_MODE     | ATTR_OWNER	  | ATTR_GROUP	      |  \
ATTR_ATIME    | ATTR_CTIME	  | ATTR_MTIME	      |  \
ATTR_SIZE     | ATTR_MTIME_SERVER | ATTR_ATIME_SERVER)   \

/**
 * Override internal Gluster defines for the time being.
 */
/* Values for valid falgs to be used when using XXXsetattr, to set multiple
 a ttribute values passed via t*he related stat structure.
 */
#define GLAPI_SET_ATTR_MODE  GFAPI_SET_ATTR_MODE
#define GLAPI_SET_ATTR_UID   GFAPI_SET_ATTR_UID
#define GLAPI_SET_ATTR_GID   GFAPI_SET_ATTR_GID
#define GLAPI_SET_ATTR_SIZE  GFAPI_SET_ATTR_SIZE
#define GLAPI_SET_ATTR_ATIME GFAPI_SET_ATTR_ATIME
#define GLAPI_SET_ATTR_MTIME GFAPI_SET_ATTR_MTIME

/* Handle length for object handles returned from glfs_h_extract_handle or
 * glfs_h_create_from_handle */
#define GLAPI_HANDLE_LENGTH GFAPI_HANDLE_LENGTH

/* END Override */

#ifdef GLTIMING
typedef enum {
	lat_handle_release = 0,
	lat_lookup,
	lat_create,
	lat_getattrs,
	lat_handle_digest,
	lat_handle_to_key,
	lat_extract_handle,
	lat_create_handle,
	lat_read_dirents,
	lat_makedir,
	lat_makenode,
	lat_setattrs,
	lat_file_unlink,
	lat_file_open,
	lat_file_read,
	lat_file_write,
	lat_commit,
	lat_file_close,
	lat_lru_cleanup,
	lat_makesymlink,
	lat_readsymlink,
	lat_linkfile,
	lat_renamefile,
	lat_end_slots
} latency_slots_t;
#define LATENCY_SLOTS 23

struct latency_data {
	uint64_t count;
	nsecs_elapsed_t overall_time;
};
#endif

struct glusterfs_fsal_module {
	struct fsal_staticfsinfo_t fs_info;
	struct fsal_module fsal;
};

struct glusterfs_export {
	glfs_t *gl_fs;
	char *export_path;
	uid_t saveduid;
	gid_t savedgid;
	struct fsal_export export;
};

struct glusterfs_handle {
	struct glfs_object *glhandle;
	unsigned char globjhdl[GLAPI_HANDLE_LENGTH];	/* handle 
							   descriptor, for wire handle */
	struct glfs_fd *glfd;
	fsal_openflags_t openflags;
	struct fsal_obj_handle handle;	/* public FSAL handle */
};

#ifdef GLTIMING
struct latency_data glfsal_latencies[LATENCY_SLOTS];

void latency_update(struct timespec *s_time, struct timespec *e_time,
		    int opnum);

void latency_dump(void);
#endif

fsal_status_t gluster2fsal_error(const int gluster_errorcode);

void stat2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr);

struct fsal_staticfsinfo_t *gluster_staticinfo(struct fsal_module *hdl);

int construct_handle(struct glusterfs_export *glexport, const struct stat *st,
		     struct glfs_object *glhandle, unsigned char *globjhdl,
		     int len, struct glusterfs_handle **obj);

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      const char *export_path,
				      const char *fs_options,
				      struct exportlist *exp_entry,
				      struct fsal_module *next_fsal,
				      const struct fsal_up_vector *up_ops,
				      struct fsal_export **export);

void gluster_cleanup_vars(struct glfs_object *glhandle);

bool fs_specific_has(const char *fs_specific, const char *key, char *val,
		     int *max_val_bytes);

int setglustercreds(struct glusterfs_export *glfs_export, uid_t * uid,
		    gid_t * gid, unsigned int ngrps, gid_t * groups);

#endif				/* GLUSTER_INTERNAL */
