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
#define GLUSTER_VALIDATE_RETURN_STATUS(rc) do {	\
	if (rc != 0) {					\
		status = gluster2fsal_error(errno);	\
		goto out;				\
	}						\
	} while(0)		

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
		GLAPI_UUID_LENGTH )

/*
 * Macros related to ACL processing
 */

/* Flags to determine if ACLs are supported */
#define NFSv4_ACL_SUPPORT (glfs_export->acl_enable)
#define POSIX_ACL_CONVERSION 0

/* define flags for attr_valid */
#define XATTR_STAT      (1 << 0) // 01
#define XATTR_ACL       (1 << 1) // 02

/* Checks to verify if appropriate masks are set */
#define IS_READ_OWNER(mask) (S_IRUSR & mask)
#define IS_WRITE_OWNER(mask) (S_IWUSR & mask)
#define IS_EXECUTE_OWNER(mask) (S_IXUSR & mask)
#define IS_READ_GROUP(mask) (S_IRGRP & mask)
#define IS_WRITE_GROUP(mask) (S_IWGRP & mask)
#define IS_EXECUTE_GROUP(mask) (S_IXGRP & mask)
#define IS_READ_OTHERS(mask) (S_IROTH & mask)
#define IS_WRITE_OTHERS(mask) (S_IWOTH & mask)
#define IS_EXECUTE_OTHERS(mask) (S_IXOTH & mask)

/* Bits which need to be cleared before calculating equivalent
 * modebits from an ACL
 */
#define CLEAR_MODE_BITS ( (0xFFFF &	\
			    ~(S_IRUSR | S_IWUSR | S_IXUSR |  \
			      S_IRGRP | S_IWGRP | S_IXGRP |  \
			      S_IROTH | S_IWOTH | S_IXOTH)))

/* ACL types (acl_type field in glusterfs_acl_t) */
#define GLUSTERFS_ACL_TYPE_ACCESS  1
#define GLUSTERFS_ACL_TYPE_DEFAULT 2
#define GLUSTERFS_ACL_TYPE_NFS4    3

/* Defined values for glusterfs_aclVersion_t */
#define GLUSTERFS_ACL_VERSION_POSIX   2
#define GLUSTERFS_ACL_VERSION_NFS4    4

/* Values for glusterfs_aclLevel_t  */
#define GLUSTERFS_ACL_LEVEL_BASE    0 /* compatible with all acl_version values */
#define GLUSTERFS_ACL_LEVEL_V4FLAGS 1 /* requires GLUSTERFS_ACL_VERSION_NFS4 */

/* Values for glusterfs_aceType_t (ACL_VERSION_POSIX) */
#define GLUSTERFS_ACL_UNDEFINED_TAG	0
#define GLUSTERFS_ACL_USER_OBJ		1
#define GLUSTERFS_ACL_USER  		2
#define GLUSTERFS_ACL_GROUP_OBJ 	4
#define GLUSTERFS_ACL_GROUP 		8	
#define GLUSTERFS_ACL_MASK     		10 
#define GLUSTERFS_ACL_OTHER     	20

/* Values for glusterfs_acePerm_t (ACL_VERSION_POSIX) */
#define GLUSTERFS_ACL_EXECUTE 001
#define GLUSTERFS_ACL_WRITE   002
#define GLUSTERFS_ACL_READ    004

/* Values for glusterfs_uid_t (ACL_VERSION_POSIX) */
#define GLUSTERFS_ACL_UNDEFINED_ID  (-1)

/* Values for glusterfs_aceType_t (ACL_VERSION_NFS4) */
#define ACE4_TYPE_ALLOW 0
#define ACE4_TYPE_DENY  1
#define ACE4_TYPE_AUDIT 2
#define ACE4_TYPE_ALARM 3

/* Values for glusterfs_aceFlags_t (ACL_VERSION_NFS4) */
#define ACE4_FLAG_FILE_INHERIT    0x00000001
#define ACE4_FLAG_DIR_INHERIT     0x00000002
#define ACE4_FLAG_NO_PROPAGATE    0x00000004
#define ACE4_FLAG_INHERIT_ONLY    0x00000008
#define ACE4_FLAG_SUCCESSFUL      0x00000010
#define ACE4_FLAG_FAILED          0x00000020
#define ACE4_FLAG_GROUP_ID        0x00000040
#define ACE4_FLAG_INHERITED       0x00000080

/* Currently we do not support Inherited ACLs or AUDIT/ALARM ACLs.
 * Hence the only ACE flag supported is ACE4_FLAG_GROUP_ID */
#define ACE4_FLAG_SUPPORTED	( ACE4_FLAG_GROUP_ID )

/* GLUSTERFS-defined flags.  Placed in a seperate ACL field to avoid
   ever running into newly defined NFSv4 flags. */
#define ACE4_IFLAG_SPECIAL_ID     0x80000000

/* Values for glusterfs_aceMask_t (ACL_VERSION_NFS4) */
#define ACE4_MASK_READ_DATA         0x00000001
#define ACE4_MASK_LIST_DIRECTORY    0x00000001
#define ACE4_MASK_WRITE_DATA        0x00000002
#define ACE4_MASK_ADD_FILE     	    0x00000002
#define ACE4_MASK_APPEND_DATA       0x00000004
#define ACE4_MASK_ADD_SUBDIRECTORY  0x00000004
#define ACE4_MASK_READ_NAMED_ATTRS  0x00000008
#define ACE4_MASK_WRITE_NAMED_ATTRS 0x00000010
#define ACE4_MASK_EXECUTE           0x00000020

/* The rfc doesn't provide a mask equivalent to "search" ("x" on a
 * directory in posix), but it also doesn't say that its EXECUTE
 * is to have this dual use (even though it does so for other dual
 * use permissions such as read/list.  Going to make the assumption
 * here that the EXECUTE bit has this dual meaning... otherwise
 * we're left with no control over search.
 */
#define ACE4_MASK_SEARCH            0x00000020

#define ACE4_MASK_DELETE_CHILD      0x00000040
#define ACE4_MASK_READ_ATTRIBUTES   0x00000080
#define ACE4_MASK_WRITE_ATTRIBUTES  0x00000100
#define ACE4_MASK_DELETE            0x00010000
#define ACE4_MASK_READ_ACL          0x00020000
#define ACE4_MASK_WRITE_ACL         0x00040000
#define ACE4_MASK_WRITE_OWNER       0x00080000
#define ACE4_MASK_SYNCHRONIZE       0x00100000
#define ACE4_MASK_ALL               0x001f01ff

/* To set appropriate ACE masks as per perms set */
#define ACE4_READ_ALL	    ( ACE4_MASK_READ_DATA   | \
			      ACE4_MASK_READ_NAMED_ATTRS )
#define ACE4_WRITE_ALL	    ( ACE4_MASK_WRITE_DATA  | \
			      ACE4_MASK_APPEND_DATA | \
			      ACE4_MASK_WRITE_NAMED_ATTRS     )
#define ACE4_READ_DIR_ALL   ( ACE4_MASK_LIST_DIRECTORY   | \
			      ACE4_MASK_READ_NAMED_ATTRS )
#define ACE4_WRITE_DIR_ALL  ( ACE4_MASK_ADD_FILE  | \
			      ACE4_MASK_ADD_SUBDIRECTORY | \
			      ACE4_MASK_DELETE_CHILD | \
			      ACE4_MASK_WRITE_NAMED_ATTRS     )
#define ACE4_EXECUTE_ALL    ( ACE4_MASK_EXECUTE     | \
			      ACE4_MASK_READ_DATA      )
#define ACE4_WRITE	    ( ACE4_MASK_WRITE_DATA | \
			      ACE4_MASK_APPEND_DATA ) 
#define ACE4_OWNER_AUTOSET  ( ACE4_MASK_WRITE_ACL | \
			      ACE4_MASK_WRITE_ATTRIBUTES | \
			      ACE4_MASK_READ_ACL | \
			      ACE4_MASK_READ_ATTRIBUTES | \
			      ACE4_MASK_SYNCHRONIZE )
#define ACE4_OTHERS_AUTOSET ( ACE4_MASK_READ_ACL | \
			      ACE4_MASK_READ_ATTRIBUTES | \
			      ACE4_MASK_SYNCHRONIZE )

/* Checks to verify ACE mask values */
#define IS_ACE4_READ(mask)  ( mask & ACE4_MASK_READ_DATA )		
#define IS_ACE4_WRITE(mask) ( ( mask & ACE4_MASK_WRITE_DATA) && \
			       ( mask & ACE4_MASK_APPEND_DATA)  )
#define IS_ACE4_EXECUTE(mask)  ( mask & ACE4_MASK_EXECUTE )  			 

/* Conversion from ACE to mode-bits */
#define CHANGE_MODE_BITS(ACE) do {			\
	if (IS_FSAL_ACE_SPECIAL_OWNER(ACE)) {	\
		*st_mode |= IS_ACE4_READ(GET_FSAL_ACE_PERM(ACE)) ? S_IRUSR : 0; \
		*st_mode |= IS_ACE4_WRITE(GET_FSAL_ACE_PERM(ACE)) ? S_IWUSR : 0; \
		*st_mode |= IS_ACE4_EXECUTE(GET_FSAL_ACE_PERM(ACE)) ? S_IXUSR : 0; \
	}					\
	else if (IS_FSAL_ACE_SPECIAL_GROUP(ACE)) {	\
		*st_mode |= IS_ACE4_READ(GET_FSAL_ACE_PERM(ACE)) ? S_IRGRP : 0; \
		*st_mode |= IS_ACE4_WRITE(GET_FSAL_ACE_PERM(ACE)) ? S_IWGRP : 0; \
		*st_mode |= IS_ACE4_EXECUTE(GET_FSAL_ACE_PERM(ACE)) ? S_IXGRP : 0; \
	}					\
	else if(IS_FSAL_ACE_SPECIAL_EVERYONE(ACE)) {	\
		*st_mode |= IS_ACE4_READ(GET_FSAL_ACE_PERM(ACE)) ? S_IROTH : 0; \
		*st_mode |= IS_ACE4_WRITE(GET_FSAL_ACE_PERM(ACE)) ? S_IWOTH : 0; \
		*st_mode |= IS_ACE4_EXECUTE(GET_FSAL_ACE_PERM(ACE)) ? S_IXOTH : 0; \
	}					\
	else { \
	}	\
	} while(0)

/* Values for glusterfs_uid_t (ACL_VERSION_NFS4) */
#define ACE4_SPECIAL_OWNER              1
#define ACE4_SPECIAL_GROUP              2
#define ACE4_SPECIAL_EVERYONE           3

/* per-ACL flags imported from a Windows security descriptor object */
#define ACL4_FLAG_OWNER_DEFAULTED               0x00000100
#define ACL4_FLAG_GROUP_DEFAULTED               0x00000200
#define ACL4_FLAG_DACL_PRESENT                  0x00000400
#define ACL4_FLAG_DACL_DEFAULTED                0x00000800
#define ACL4_FLAG_SACL_PRESENT                  0x00001000
#define ACL4_FLAG_SACL_DEFAULTED                0x00002000
#define ACL4_FLAG_DACL_UNTRUSTED                0x00004000
#define ACL4_FLAG_SERVER_SECURITY               0x00008000
#define ACL4_FLAG_DACL_AUTO_INHERIT_REQ         0x00010000
#define ACL4_FLAG_SACL_AUTO_INHERIT_REQ         0x00020000
#define ACL4_FLAG_DACL_AUTO_INHERITED           0x00040000
#define ACL4_FLAG_SACL_AUTO_INHERITED           0x00080000
#define ACL4_FLAG_DACL_PROTECTED                0x00100000
#define ACL4_FLAG_SACL_PROTECTED                0x00200000
#define ACL4_FLAG_RM_CONTROL_VALID              0x00400000
#define ACL4_FLAG_NULL_DACL                     0x00800000
#define ACL4_FLAG_NULL_SACL                     0x01000000
#define ACL4_FLAG_VALID_FLAGS                   0x01ffff00


typedef unsigned int glusterfs_uid_t;

/* Externalized ACL defintions */
typedef unsigned int glusterfs_aclType_t;
typedef unsigned int glusterfs_aclLen_t;
typedef unsigned int glusterfs_aclLevel_t;
typedef unsigned int glusterfs_aclVersion_t;
typedef unsigned int glusterfs_aclCount_t;
typedef unsigned int glusterfs_aclFlag_t;

typedef unsigned int glusterfs_aceType_t;
typedef unsigned int glusterfs_aceFlags_t;
typedef unsigned int glusterfs_acePerm_t;
typedef unsigned int glusterfs_aceMask_t;

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
	struct fsal_staticfsinfo_t fs_info;
	struct fsal_module fsal;
};

struct glusterfs_export {
	glfs_t *gl_fs;
        char *mount_path;
	char *export_path;
	uid_t saveduid;
	gid_t savedgid;
	struct fsal_export export;
	bool acl_enable;
};

struct glusterfs_handle {
	struct glfs_object *glhandle;
	unsigned char globjhdl[GLAPI_HANDLE_LENGTH];	/* handle 
							   descriptor, for wire handle */
	struct glfs_fd *glfd;
	fsal_openflags_t openflags;
	struct fsal_obj_handle handle;	/* public FSAL handle */
};

/* A POSIX ACL Entry */
typedef struct glusterfs_ace_v1
{
  glusterfs_aceType_t  ace_tag; /* POSIX ACE type */
  glusterfs_acePerm_t  ace_perm; /* POSIX permissions */
  glusterfs_uid_t      ace_id;  /* uid/gid */
} glusterfs_ace_v1_t;

/* A NFSv4 ACL Entry */
typedef struct glusterfs_ace_v4
{
  glusterfs_aceType_t  aceType;   /* Allow or Deny */
  glusterfs_aceFlags_t aceFlags;  /* Inherit specifications, etc. */
  glusterfs_aceFlags_t aceIFlags; /* GLUSTERFS Internal flags */
  glusterfs_aceMask_t  aceMask;   /* NFSv4 mask specification */
  glusterfs_uid_t      aceWho;    /* User/Group identification */
} glusterfs_ace_v4_t;

/* when GLUSTERFS_ACL_VERSION_NFS4, and GLUSTERFS_ACL_LEVEL_V4FLAGS */
typedef struct v4Level1_ext /* ACL extension */
{
  glusterfs_aclFlag_t acl_flags; /* per-ACL flags */
  glusterfs_ace_v4_t ace_v4[1];
} v4Level1_t;

/* Define the buffer size for GLUSTERFS NFS4 ACL. */
#define GLFS_ACL_BUF_SIZE 0x1000

/* A set of buffers to retrieve multiple attributes at the same time. */
typedef struct fsal_xstat__ {
	int attr_valid;
	struct stat buffstat;
	char buffacl[GLFS_ACL_BUF_SIZE];
} glusterfs_fsal_xstat_t;

/* GLUSTERFS ACL */
typedef struct glusterfs_acl
{
  glusterfs_aclLen_t     acl_len;     /* Total length of this ACL in bytes */
  glusterfs_aclLevel_t   acl_level;   /* Reserved (must be zero) */
  glusterfs_aclType_t    acl_type;    /* Access, Default, or NFS4 */
  glusterfs_aclCount_t   acl_nace;    /* Number of Entries that follow */
  glusterfs_aclVersion_t acl_version; /* POSIX or NFS4 ACL */
  union
  {
    glusterfs_ace_v1_t  ace_v1[1]; /* when GLUSTERFS_ACL_VERSION_POSIX */
    glusterfs_ace_v4_t  ace_v4[1]; /* when GLUSTERFS_ACL_VERSION_NFS4  */
    v4Level1_t     v4Level1;  /* when GLUSTERFS_ACL_LEVEL_V4FLAGS */
  };
} glusterfs_acl_t;

/* POSIX ACL */
typedef struct glusterfs_posix_acl_header {
	uint32_t version;
	glusterfs_ace_v1_t ace_v1[];
} glusterfs_posix_acl_t;

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
		     int len, struct glusterfs_handle **obj, const char *vol_uuid);

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      void *parse_node,
				      const struct fsal_up_vector *up_ops);

void gluster_cleanup_vars(struct glfs_object *glhandle);

bool fs_specific_has(const char *fs_specific, const char *key, char *val,
		     int *max_val_bytes);

int setglustercreds(struct glusterfs_export *glfs_export, uid_t * uid,
		    gid_t * gid, unsigned int ngrps, gid_t * groups);

fsal_status_t glusterfs_get_acl (struct glusterfs_export *glfs_export,
				 struct glfs_object *objhandle,
				 glusterfs_fsal_xstat_t *buffxstat,
				 struct attrlist *fsalattr);

fsal_status_t glusterfs_set_acl (struct glusterfs_export *glfs_export,
				 struct glusterfs_handle *objhandle,
				 glusterfs_fsal_xstat_t *buffxstat);

fsal_status_t fsal_acl_2_glusterfs_acl(fsal_acl_t *p_fsalacl,
				       char *p_buffacl,
				       uint32_t *st_mode);

int glusterfs_acl_2_fsal_acl(struct attrlist *p_object_attributes,
			     glusterfs_acl_t *p_glusterfsacl);

fsal_status_t fsal_acl_2_glusterfs_posix_acl(fsal_acl_t *p_fsalacl,
				  	     char *p_buffacl);

fsal_status_t glusterfs_process_acl(struct glfs *fs,
				    struct glfs_object *object,
				    struct attrlist *attrs,
				    glusterfs_fsal_xstat_t *buffxstat);

fsal_status_t mode_bits_to_acl(struct glfs *fs,
			       struct glusterfs_handle *objhandle,
			       struct attrlist *attrs, int *attrs_valid,
			       glusterfs_fsal_xstat_t *buffxstat,
			       bool is_dir);

#endif				/* GLUSTER_INTERNAL */
