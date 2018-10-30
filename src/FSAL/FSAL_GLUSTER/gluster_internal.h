/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#ifndef GLUSTER_INTERNAL
#define GLUSTER_INTERNAL

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "posix_acls.h"
#include "FSAL/fsal_commonlib.h"
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include "nfs_exports.h"

#define GLUSTER_VOLNAME_KEY  "volume"
#define GLUSTER_HOSTNAME_KEY "hostname"
#define GLUSTER_VOLPATH_KEY  "volpath"

/* defined the set of attributes supported with POSIX */
#define GLUSTERFS_SUPPORTED_ATTRIBUTES (ATTRS_POSIX | ATTR_ACL)

/**
 * The attributes this FSAL can set.
 */

#define GLUSTERFS_SETTABLE_ATTRIBUTES (		  \
ATTR_MODE     | ATTR_OWNER	  | ATTR_GROUP	      |  \
ATTR_ATIME    | ATTR_CTIME	  | ATTR_MTIME	      |  \
ATTR_SIZE     | ATTR_MTIME_SERVER | ATTR_ATIME_SERVER |  \
ATTR_ACL)

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

/* UUID length for the object returned from glfs_get_volume_id */
#define GLAPI_UUID_LENGTH   16

/*
 * GFAPI_HANDLE_LENGTH is the handle length for object handles
 * returned from glfs_h_extract_handle or glfs_h_create_from_handle
 *
 * GLAPI_HANDLE_LENGTH is the total length of the handle descriptor
 * to be used in the wire handle.
 */
#define GLAPI_HANDLE_LENGTH (		\
		GFAPI_HANDLE_LENGTH +	\
		GLAPI_UUID_LENGTH)

/* Flags to determine if ACLs are supported */
#define NFSv4_ACL_SUPPORT (!op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL))

/* define flags for attr_valid */
#define XATTR_STAT      (1 << 0) /* 01 */
#define XATTR_ACL       (1 << 1) /* 02 */

/* END Override */

#ifdef GLTIMING
typedef enum {
	lat_handle_release = 0,
	lat_lookup,
	lat_create,
	lat_getattrs,
	lat_handle_to_wire,
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
	lat_file_seek,
	lat_file_write,
	lat_commit,
	lat_file_close,
	lat_makesymlink,
	lat_readsymlink,
	lat_linkfile,
	lat_renamefile,
	lat_lock_op,
	lat_end_slots
} latency_slots_t;
#define LATENCY_SLOTS 24

struct latency_data {
	uint64_t count;
	nsecs_elapsed_t overall_time;
};
#endif

struct glusterfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
	struct glist_head  fs_obj; /* list of glusterfs_fs filesystem objects */
	pthread_mutex_t   lock; /* lock to protect above list */
};
extern struct glusterfs_fsal_module GlusterFS;

struct glusterfs_fs {
	struct glist_head fs_obj; /* link to glusterfs_fs filesystem objects */
	char      *volname;
	glfs_t    *fs;
	const struct fsal_up_vector *up_ops;    /*< Upcall operations */
	int64_t    refcnt;
	pthread_t  up_thread; /* upcall thread */
	int8_t destroy_mode;
	uint64_t up_poll_usec;
	bool   enable_upcall;
};

struct glusterfs_export {
	struct glusterfs_fs *gl_fs;
	char *mount_path;
	char *export_path;
	uid_t saveduid;
	gid_t savedgid;
	struct fsal_export export;
	bool pnfs_ds_enabled;
	bool pnfs_mds_enabled;
};

#ifdef USE_GLUSTER_DELEGATION
#define GLAPI_LEASE_ID_SIZE GLFS_LEASE_ID_SIZE
#endif

struct glusterfs_fd {
	/** The open and share mode etc. This MUST be first in every
	 *  file descriptor structure.
	 */
	fsal_openflags_t openflags;

	/* rw lock to protect the file descriptor */
	pthread_rwlock_t fdlock;

	/** Gluster file descriptor. */
	struct glfs_fd *glfd;
	struct user_cred creds; /* user creds opening fd*/
#ifdef USE_GLUSTER_DELEGATION
	char lease_id[GLAPI_LEASE_ID_SIZE];
#endif
};

struct glusterfs_handle {
	struct glfs_object *glhandle;
	unsigned char globjhdl[GLAPI_HANDLE_LENGTH];	/* handle descriptor,
							   for wire handle */
	struct glusterfs_fd globalfd;
	struct fsal_obj_handle handle;	/* public FSAL handle */
	struct fsal_share share; /* share_reservations */

	/* following added for pNFS support */
	uint64_t rd_issued;
	uint64_t rd_serial;
	uint64_t rw_issued;
	uint64_t rw_serial;
	uint64_t rw_max_len;
#ifdef USE_GLUSTER_DELEGATION
	glfs_lease_types_t lease_type; /* Store lease_type granted to
					 this file: NONE,RD or RW */
#endif
};

/* Structures defined for PNFS */
struct glfs_ds_handle {
	struct fsal_ds_handle ds;
	struct glfs_object *glhandle;
	stable_how4  stability_got;
	bool connected;
};

struct glfs_file_layout {
	uint32_t stripe_length;
	uint64_t stripe_type;
	uint32_t devid;
};

struct glfs_ds_wire {
	unsigned char gfid[16];
	struct glfs_file_layout layout; /*< Layout information */
};

/* Define the buffer size for GLUSTERFS NFS4 ACL. */
#define GLFS_ACL_BUF_SIZE 0x1000

/* A set of buffers to retrieve multiple attributes at the same time. */
typedef struct fsal_xstat__ {
	int attr_valid;
	struct stat buffstat;
	acl_t e_acl; /* stores effective acl */
	acl_t i_acl; /* stores inherited acl */
	bool is_dir;
} glusterfs_fsal_xstat_t;

static inline void glusterfs_fsal_clean_xstat(glusterfs_fsal_xstat_t *buffxstat)
{
	if (buffxstat->e_acl) {
		acl_free(buffxstat->e_acl);
		buffxstat->e_acl = NULL;
	}

	if (buffxstat->i_acl) {
		acl_free(buffxstat->i_acl);
		buffxstat->i_acl = NULL;
	}
}

struct glusterfs_state_fd {
	struct state_t state;
	struct glusterfs_fd glusterfs_fd;
};

void setglustercreds(struct glusterfs_export *glfs_export, uid_t *uid,
		     gid_t *gid, unsigned int ngrps, gid_t *groups,
		     char *client_addr, unsigned int client_addr_len,
		     char *file, int line, char *function);

#define SET_GLUSTER_CREDS(glfs_export, uid, gid, glen, garray, client_addr, \
			  client_addr_len)				    \
do {									    \
	int old_errno = errno;						    \
	((void) setglustercreds(glfs_export, uid, gid, glen,		    \
			       garray, client_addr, client_addr_len,	    \
			       (char *) __FILE__,			    \
			       __LINE__, (char *) __func__));		    \
	errno = old_errno;						    \
} while (0)

#ifdef GLTIMING
struct latency_data glfsal_latencies[LATENCY_SLOTS];

void latency_update(struct timespec *s_time, struct timespec *e_time,
		    int opnum);

void latency_dump(void);
#endif

void handle_ops_init(struct fsal_obj_ops *ops);

fsal_status_t gluster2fsal_error(const int gluster_errorcode);

void stat2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr);

void construct_handle(struct glusterfs_export *glexport, const struct stat *st,
		      struct glfs_object *glhandle, unsigned char *globjhdl,
		      struct glusterfs_handle **obj, const char *vol_uuid);

fsal_status_t glfs2fsal_handle(struct glusterfs_export *glfs_export,
			       struct glfs_object *glhandle,
			       struct fsal_obj_handle **pub_handle,
			       struct stat *sb,
			       struct attrlist *attrs_out);

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      void *parse_node,
				      struct config_error_type *err_type,
				      const struct fsal_up_vector *up_ops);

void gluster_cleanup_vars(struct glfs_object *glhandle);

bool fs_specific_has(const char *fs_specific, const char *key, char *val,
		     int *max_val_bytes);

fsal_status_t glusterfs_get_acl(struct glusterfs_export *glfs_export,
				 struct glfs_object *objhandle,
				 glusterfs_fsal_xstat_t *buffxstat,
				 struct attrlist *fsalattr);

fsal_status_t glusterfs_set_acl(struct glusterfs_export *glfs_export,
				 struct glusterfs_handle *objhandle,
				 glusterfs_fsal_xstat_t *buffxstat);

fsal_status_t glusterfs_process_acl(struct glfs *fs,
				    struct glfs_object *object,
				    struct attrlist *attrs,
				    glusterfs_fsal_xstat_t *buffxstat);

void glusterfs_free_fs(struct glusterfs_fs *gl_fs);

/*
 * Following have been introduced for pNFS support
 */

/* Need to call this to initialize export_ops for pnfs */
void export_ops_pnfs(struct export_ops *ops);

/* Need to call this to initialize obj_ops for pnfs */
void handle_ops_pnfs(struct fsal_obj_ops *ops);

/* Need to call this to initialize ops for pnfs */
void fsal_ops_pnfs(struct fsal_ops *ops);

void dsh_ops_init(struct fsal_dsh_ops *ops);

void pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops);

nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl,
			XDR *da_addr_body, const layouttype4 type,
			const struct pnfs_deviceid *deviceid);

/* UP thread routines */
void *GLUSTERFSAL_UP_Thread(void *Arg);
int initiate_up_thread(struct glusterfs_fs *gl_fs);
int up_process_event_object(struct glusterfs_fs *gl_fs,
			    struct glfs_object *object,
			    enum glfs_upcall_reason reason);
void gluster_process_upcall(struct glfs_upcall *cbk, void *data);

fsal_status_t glusterfs_close_my_fd(struct glusterfs_fd *my_fd);
#endif				/* GLUSTER_INTERNAL */
