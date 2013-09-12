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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include <glusterfs/api/glfs.h>

#define GLUSTER_GFID_SIZE 16
#define GLUSTER_VOLNAME_KEY "volume"
#define GLUSTER_HOSTNAME_KEY "hostname"
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

typedef enum {
	lat_handle_release = 0,
	lat_lookup,
	lat_create,
	lat_getattrs,
	lat_handle_digest,
	lat_handle_to_key,
	lat_extract_handle,
	lat_create_handle,
	lat_end_slots
} latency_slots_t;
#define LATENCY_SLOTS 8

struct latency_data {
	uint64_t        count;
	nsecs_elapsed_t overall_time;
};

struct glusterfs_fsal_module {	
	struct fsal_staticfsinfo_t fs_info;
	struct fsal_module fsal;
};

struct glusterfs_export {
	glfs_t             *gl_fs;
	char               *export_path;
	struct fsal_export  export;
};

struct glusterfs_handle{
	struct glfs_object    *glhandle;
	struct glfs_gfid      *gfid; /* handle descriptor, for wire handle */
	struct glfs_fd        *glfd;
	fsal_openflags_t       openflags;
	struct fsal_obj_handle handle; /* public FSAL handle */
};

struct latency_data glfsal_latencies[LATENCY_SLOTS];

fsal_status_t gluster2fsal_error(const int gluster_errorcode);

void stat2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr);

struct fsal_staticfsinfo_t *gluster_staticinfo(struct fsal_module *hdl);

int construct_handle(struct glusterfs_export *glexport, const struct stat *st, 
		     struct glfs_object *glhandle, struct glfs_gfid *gfid, 
		     struct glusterfs_handle **obj);

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      const char *export_path,
				      const char *fs_options,
				      struct exportlist *exp_entry,
				      struct fsal_module *next_fsal,
				      const struct fsal_up_vector *up_ops,
				      struct fsal_export **export);

void gluster_cleanup_vars(struct glfs_object *glhandle, struct glfs_gfid *gfid);

bool fs_specific_has(const char *fs_specific, const char* key,char *val, 
		     int max_val_bytes);

int setids(uid_t nuid, uid_t ngid, uid_t *suid, uid_t *sgid);

void latency_update(struct timespec *s_time, struct timespec *e_time, 
		    int opnum);

void latency_dump(void);


pthread_key_t *uid_key;
pthread_key_t *gid_key;

/**
 * @brief pthread specific helper functions for uid/gid mapping
 *        to be used by gluster apis
 */
int glfs_uid_keyinit( void );
int glfs_gid_keyinit( void );
void *glfs_uid_get( void );
int glfs_uidgid_set( pthread_key_t *key, const void *val );
void *glfs_gid_get( void );



