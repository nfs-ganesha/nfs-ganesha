/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file    include/fsal_types.h
 */

#ifndef _FSAL_TYPES_H
#define _FSAL_TYPES_H

#include <openssl/md5.h>

/*
 * labels in the config file
 */

static const char *CONF_LABEL_FSAL __attribute__((unused)) = "FSAL";
static const char *CONF_LABEL_FS_COMMON __attribute__((unused)) = "FileSystem";

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "err_fsal.h"
#include "ganesha_rpc.h"
#include "ganesha_types.h"

/* Forward declarations */
struct fsal_staticfsinfo_t;

/* Cookie to be used in FSAL_ListXAttrs() to bypass RO xattr */
static const uint32_t FSAL_XATTR_RW_COOKIE = ~0;

/**
 * @brief Object file type within the system
 */
typedef enum {
        NO_FILE_TYPE = 0, /* sanity check to ignore type */
        REGULAR_FILE = 1,
        CHARACTER_FILE = 2,
        BLOCK_FILE = 3,
        SYMBOLIC_LINK = 4,
        SOCKET_FILE = 5,
        FIFO_FILE = 6,
        DIRECTORY = 7,
        FS_JUNCTION = 8,
        EXTENDED_ATTR = 9
} object_file_type_t;

/**
 * As per chat with lieb, the call-limit should be FSAL internal, not
 * global.  If there are FSAL configuration options that cut across
 * FSALs, they should go here.
 */

typedef struct fsal_init_info__
{
} fsal_init_info_t;


/* ---------------
 *  FS dependant :
 * --------------*/

/* export object
 * Created by fsal and referenced by the export list
 */

/* handle descriptor
 * used primarily to extract the bits of a file object handle from
 * protocol buffers and for calculating hashes.
 * This points into a buffer allocated and passed by the caller.
 * len is set to the buffer size when passed.  It is updated to
 * the actual copy length on return.
 */

/** object name.  */

/* Used to record the uid and gid of the client that made a request. */
struct user_cred {
  uid_t caller_uid;
  gid_t caller_gid;
  unsigned int caller_glen;
  gid_t *caller_garray;
};

/**
 * @brief request op context
 *
 * This is created early in the operation with the context of the
 * operation.  The difference between "context" and request parameters
 * or arguments is that the context is derived information such as
 * the resolved credentials, socket (network and client host) data
 * and other bits of environment associated with the request.  It gets
 * passed down the call chain only as far as it needs to go for the op
 * i.e. don't put it in the function/method proto "just because".
 *
 * The lifetime of this structure and all the data it points to is the
 * operation for V2,3 and the compound for V4+.  All elements and what
 * they point to are invariant for the lifetime.
 *
 * NOTE: This is a across-the-api shared structure.  It must survive with
 *       older consumers of its contents.  Future development
 *       can change this struct so long as it follows the rules:
 *
 *       1. New elements are appended at the end, never inserted in the middle.
 *
 *       2. This structure _only_ contains pointers and simple scalar values.
 *
 *       3. Changing an already defined struct pointer is strictly not allowed.
 *
 *       4. This struct is always passed by reference, never by value.
 *
 *       5. This struct is never copied/saved.
 *
 *       6. Code changes are first introduced in the core.  Assume the fsal
 *          module does not know and the code will still do the right thing.
 */

struct req_op_context {
        struct user_cred *creds; /*< resolved user creds from request */
        sockaddr_t *caller_addr; /*< IP connection info */
        const uint64_t*clientid; /*< Client ID of caller, NULL if
                                     unknown/not applicable. */
	uint32_t nfs_vers;       /*< NFS protocol version of request */
	uint32_t req_type;       /*< request_type */
        /* add new context members here */
};


/** filesystem identifier */

typedef struct fsal_fsid__
{
        uint64_t major;
        uint64_t minor;
} fsal_fsid_t;

/** raw device spec */

typedef struct fsal_dev__
{
  uint64_t major;
  uint64_t minor;
} fsal_dev_t;

/* The maximum ACLs that a file can support */
/* #define FSAL_MAX_ACL  10 */
#define FSAL_MAX_ACL  2

/* constants for specifying which ACL types are supported  */

typedef uint16_t fsal_aclsupp_t;
#define FSAL_ACLSUPPORT_ALLOW 0x01
#define FSAL_ACLSUPPORT_DENY  0x02

/** ACE types */

typedef uint32_t fsal_acetype_t;

#define FSAL_ACE_TYPE_ALLOW 0
#define FSAL_ACE_TYPE_DENY  1
#define FSAL_ACE_TYPE_AUDIT 2
#define FSAL_ACE_TYPE_ALARM 3


/** ACE flag */

typedef uint32_t fsal_aceflag_t;

#define FSAL_ACE_FLAG_FILE_INHERIT    0x00000001
#define FSAL_ACE_FLAG_DIR_INHERIT     0x00000002
#define FSAL_ACE_FLAG_NO_PROPAGATE    0x00000004
#define FSAL_ACE_FLAG_INHERIT_ONLY    0x00000008
#define FSAL_ACE_FLAG_SUCCESSFUL      0x00000010
#define FSAL_ACE_FLAG_FAILED          0x00000020
#define FSAL_ACE_FLAG_GROUP_ID        0x00000040
#define FSAL_ACE_FLAG_INHERITED       0x00000080

/** ACE internal flags */

#define FSAL_ACE_IFLAG_EXCLUDE_FILES  0x40000000
#define FSAL_ACE_IFLAG_EXCLUDE_DIRS   0x20000000
#define FSAL_ACE_IFLAG_SPECIAL_ID     0x80000000

#define FSAL_ACE_FLAG_INHERIT (FSAL_ACE_FLAG_FILE_INHERIT | FSAL_ACE_FLAG_DIR_INHERIT |\
                           FSAL_ACE_FLAG_INHERIT_ONLY)

/** ACE permissions */

typedef uint32_t fsal_aceperm_t;

#define FSAL_ACE_PERM_READ_DATA         0x00000001
#define FSAL_ACE_PERM_LIST_DIR          0x00000001
#define FSAL_ACE_PERM_WRITE_DATA        0x00000002
#define FSAL_ACE_PERM_ADD_FILE          0x00000002
#define FSAL_ACE_PERM_APPEND_DATA       0x00000004
#define FSAL_ACE_PERM_ADD_SUBDIRECTORY  0x00000004
#define FSAL_ACE_PERM_READ_NAMED_ATTR   0x00000008
#define FSAL_ACE_PERM_WRITE_NAMED_ATTR  0x00000010
#define FSAL_ACE_PERM_EXECUTE           0x00000020
#define FSAL_ACE_PERM_DELETE_CHILD      0x00000040
#define FSAL_ACE_PERM_READ_ATTR         0x00000080
#define FSAL_ACE_PERM_WRITE_ATTR        0x00000100
#define FSAL_ACE_PERM_DELETE            0x00010000
#define FSAL_ACE_PERM_READ_ACL          0x00020000
#define FSAL_ACE_PERM_WRITE_ACL         0x00040000
#define FSAL_ACE_PERM_WRITE_OWNER       0x00080000
#define FSAL_ACE_PERM_SYNCHRONIZE       0x00100000

/** ACE who */

#define FSAL_ACE_NORMAL_WHO                 0
#define FSAL_ACE_SPECIAL_OWNER              1
#define FSAL_ACE_SPECIAL_GROUP              2
#define FSAL_ACE_SPECIAL_EVERYONE           3

typedef struct fsal_ace__
{

  fsal_acetype_t type;
  fsal_aceperm_t perm;

  fsal_aceflag_t flag;
  fsal_aceflag_t iflag;  /* Internal flags. */
  union
  {
    uid_t uid;
    gid_t gid;
  } who;
} fsal_ace_t;

typedef struct fsal_acl__
{
  uint32_t naces;
  fsal_ace_t *aces;
  pthread_rwlock_t lock;
  uint32_t ref;
} fsal_acl_t;

typedef struct fsal_acl_data__
{
  uint32_t naces;
  fsal_ace_t *aces;
} fsal_acl_data_t;

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH  16
#endif

typedef struct fsal_acl_key__
{
  char digest[MD5_DIGEST_LENGTH];
} fsal_acl_key_t;

/* Macros for NFS4 ACE flags, masks, and special who values. */

#define GET_FSAL_ACE_TYPE(ACE)            (ACE).type
#define GET_FSAL_ACE_PERM(ACE)            (ACE).perm
#define GET_FSAL_ACE_FLAG(ACE)            (ACE).flag
#define GET_FSAL_ACE_IFLAG(ACE)           (ACE).iflag
#define GET_FSAL_ACE_USER(ACE)            (ACE).who.uid
#define GET_FSAL_ACE_GROUP(ACE)           (ACE).who.gid

#define IS_FSAL_ACE_BIT(WORD, BIT)        (0 != ((WORD) & (BIT)))
#define IS_FSAL_ACE_ALL_BITS(WORD, BITS)  (BITS == ((WORD) & (BITS)))

#define IS_FSAL_ACE_TYPE(ACE, VALUE)      ((GET_FSAL_ACE_TYPE(ACE)) == (VALUE))
#define IS_FSAL_ACE_USER(ACE, VALUE)      ((GET_FSAL_ACE_USER(ACE)) == (VALUE))
#define IS_FSAL_ACE_GROUP(ACE, VALUE)     ((GET_FSAL_ACE_GROUP(ACE)) == (VALUE))

#define IS_FSAL_ACE_ALLOW(ACE)            IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_ALLOW)
#define IS_FSAL_ACE_DENY(ACE)             IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_DENY)
#define IS_FSAL_ACE_AUDIT(ACE)            IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_AUDIT)
#define IS_FSAL_ACE_ALRAM(ACE)            IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_ALARM)

#define IS_FSAL_ACE_FILE_INHERIT(ACE)     IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_FILE_INHERIT)
#define IS_FSAL_ACE_DIR_INHERIT(ACE)      IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_DIR_INHERIT)
#define IS_FSAL_ACE_NO_PROPAGATE(ACE)     IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_NO_PROPAGATE)
#define IS_FSAL_ACE_INHERIT_ONLY(ACE)     IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_INHERIT_ONLY)
#define IS_FSAL_ACE_FLAG_SUCCESSFUL(ACE)  IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_SUCCESSFUL)
#define IS_FSAL_ACE_AUDIT_FAILURE(ACE)    IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_FAILED)
#define IS_FSAL_ACE_GROUP_ID(ACE)         IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_GROUP_ID)
#define IS_FSAL_ACE_INHERIT(ACE)          IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_INHERIT)
#define IS_FSAL_ACE_INHERTED(ACE)         IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE),FSAL_ACE_FLAG_INHERITED)

#define GET_FSAL_ACE_WHO_TYPE(ACE)        IS_FSAL_ACE_GROUP_ID(ACE) ? "gid" : "uid"
#define GET_FSAL_ACE_WHO(ACE)             IS_FSAL_ACE_GROUP_ID(ACE) ? (ACE).who.gid : (ACE).who.uid

#define IS_FSAL_ACE_SPECIAL_OWNER(ACE)    IS_FSAL_ACE_USER(ACE,FSAL_ACE_SPECIAL_OWNER)
#define IS_FSAL_ACE_SPECIAL_GROUP(ACE)    IS_FSAL_ACE_USER(ACE,FSAL_ACE_SPECIAL_GROUP)
#define IS_FSAL_ACE_SPECIAL_EVERYONE(ACE) IS_FSAL_ACE_USER(ACE,FSAL_ACE_SPECIAL_EVERYONE)

/* Macros for internal NFS4 ACE flags. */

#define IS_FSAL_ACE_SPECIAL_ID(ACE)       IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE),FSAL_ACE_IFLAG_SPECIAL_ID)
#define IS_FSAL_FILE_APPLICABLE(ACE) \
        (!IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE),FSAL_ACE_IFLAG_EXCLUDE_FILES))
#define IS_FSAL_DIR_APPLICABLE(ACE)  \
        (!IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE),FSAL_ACE_IFLAG_EXCLUDE_DIRS))

/* Macros for NFS4 ACE permissions. */

#define IS_FSAL_ACE_READ_DATA(ACE)        IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_READ_DATA)
#define IS_FSAL_ACE_LIST_DIR(ACE)         IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_LIST_DIR)
#define IS_FSAL_ACE_WRITE_DATA(ACE)       IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_WRITE_DATA)
#define IS_FSAL_ACE_ADD_FIILE(ACE)        IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_ADD_FILE)
#define IS_FSAL_ACE_APPEND_DATA(ACE)      IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_APPEND_DATA)
#define IS_FSAL_ACE_ADD_SUBDIRECTORY(ACE) IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_ADD_SUBDIRECTORY)
#define IS_FSAL_ACE_READ_NAMED_ATTR(ACE)  IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_READ_NAMED_ATTR)
#define IS_FSAL_ACE_WRITE_NAMED_ATTR(ACE) IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_WRITE_NAMED_ATTR)
#define IS_FSAL_ACE_EXECUTE(ACE)          IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_EXECUTE)
#define IS_FSAL_ACE_DELETE_CHILD(ACE)     IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_DELETE_CHILD)
#define IS_FSAL_ACE_READ_ATTR(ACE)        IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_READ_ATTR)
#define IS_FSAL_ACE_WRITE_ATTR(ACE)       IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_WRITE_ATTR)
#define IS_FSAL_ACE_DELETE(ACE)           IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_DELETE)
#define IS_FSAL_ACE_READ_ACL(ACE)         IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_READ_ACL)
#define IS_FSAL_ACE_WRITE_ACL(ACE)        IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_WRITE_ACL)
#define IS_FSAL_ACE_WRITE_OWNER(ACE)      IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_WRITE_OWNER)
#define IS_FSAL_ACE_SYNCHRONIZE(ACE)      IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE),FSAL_ACE_PERM_SYNCHRONIZE)

/**
 * Defines an attribute mask.
 *
 * Do not just use OR and AND to test these, use the macros.
 */
typedef uint64_t attrmask_t;

/**
 * Attribute masks.
 */

/* supported attributes */
#define ATTR_SUPPATTR 0x0000000000000001
/* file type */
#define ATTR_TYPE 0x0000000000000002
/* file size */
#define ATTR_SIZE 0x0000000000000004
/* filesystem id */
#define ATTR_FSID 0x0000000000000008
/* ACL */
#define ATTR_ACL 0x0000000000000020
/* file id */
#define ATTR_FILEID 0x0000000000000040
/* Access permission flag */
#define ATTR_MODE 0x0000000000000080
/* Number of hard links */
#define ATTR_NUMLINKS 0x0000000000000100
/* owner ID */
#define ATTR_OWNER 0x0000000000000200
/* group ID */
#define ATTR_GROUP 0x0000000000000400
/* ID of device for block special or character special files*/
#define ATTR_RAWDEV 0x0000000000000800
/* Access time */
#define ATTR_ATIME 0x0000000000001000
/* Creation time */
#define ATTR_CREATION 0x0000000000002000
/* Metadata modification time */
#define ATTR_CTIME 0x0000000000004000
/* data modification time */
#define ATTR_MTIME 0x0000000000008000
/* space used by this file. */
#define ATTR_SPACEUSED 0x0000000000010000
/* Mounted on fileid */
#define ATTR_MOUNTFILEID 0x0000000000020000
/* NFS4 change_time like attribute */
#define ATTR_CHGTIME 0x0000000000040000
/* This bit indicates that an error occured during getting object attributes */
#define ATTR_RDATTR_ERR 0x8000000000000000
/* Generation number */
#define ATTR_GENERATION 0x0000000000080000
/* Change attribute */
#define ATTR_CHANGE 0x0000000000100000


/* "classic" attributes sets : */

/* NFSv4 Mandatory attributes */

#define ATTRS_FSAL_MANDATORY (ATTR_SUPPATTR | \
                              ATTR_TYPE     | \
                              ATTR_SIZE     | \
                              ATTR_FSID     | \
                              ATTR_CHGTIME)

/* attributes that are returned by the POSIX stat function */

#define ATTRS_POSIX  (ATTR_MODE     | ATTR_FILEID   | \
                      ATTR_FSID     | ATTR_RAWDEV   | \
                      ATTR_NUMLINKS | ATTR_OWNER    | \
                      ATTR_GROUP    | ATTR_SIZE     | \
                      ATTR_ATIME    | ATTR_MTIME    | \
                      ATTR_CTIME    | ATTR_SPACEUSED)


/**
 * @brief A list of FS object's attributes.
 */

struct attrlist {
        attrmask_t mask; /*< Indicates the attributes to be set or
                             that have been filled in by the FSAL. */
        attrmask_t supported_attributes; /*< Attributes this export
                                             supports. */
        object_file_type_t type; /*< Type of this object */
        uint64_t filesize; /*< Logical size (amount of data that can be
                               read) */
        fsal_fsid_t fsid; /*< Filesystem on which this object is
                              stored */
        fsal_acl_t *acl; /*< ACL for this object */
        uint64_t fileid; /*< Unique identifier for this object within
                             the scope of the fsid, (e.g. inode number) */
        uint32_t mode; /*< POSIX access mode */
        uint32_t numlinks; /*< Number of links to this file */
        uint64_t owner; /*< Owner ID */
        uint64_t group; /*< Group ID */
        fsal_dev_t rawdev; /*< Major/minor device number (only
                               meaningful for character/block special
                               files.) */
        struct timespec atime; /*< Time of last access */
        struct timespec creation; /*< Creation time */
        struct timespec ctime; /*< Inode modification time (a la stat.
                              NOT creation.) */
        struct timespec mtime; /*< Time of last modification */
        struct timespec chgtime; /*< Time of last 'change' */
        uint64_t spaceused; /*< Space used on underlying filesystem */
        uint64_t change; /*< A 'change id' */
        uint64_t mounted_on_fileid; /*< If this is the root directory
                                        of a filesystem, the fileid of
                                        the directory on which the
                                        filesystem is mounted. */
        uint64_t generation; /*< Generation number for this file */
};

/** Mask for permission testing. Both mode and ace4 mask are encoded. */

typedef enum {
        FSAL_OWNER_OK = 0x08000000, /*< Allow owner override */
        FSAL_R_OK = 0x04000000, /*< Test for Read permission */
        FSAL_W_OK = 0x02000000, /*< Test for Write permission */
        FSAL_X_OK = 0x01000000, /*< Test for execute permission */
        FSAL_F_OK = 0x10000000, /*< Test for existence of File */
        FSAL_ACCESS_OK = 0x00000000, /*< Allow */
        FSAL_ACCESS_FLAG_BIT_MASK = 0x80000000,
        FSAL_MODE_BIT_MASK = 0x7F000000,
        FSAL_ACE4_BIT_MASK = 0x00FFFFFF,
        FSAL_MODE_MASK_FLAG = 0x00000000,
        FSAL_ACE4_MASK_FLAG = 0x80000000
} fsal_accessflags_t;

static inline fsal_accessflags_t
FSAL_MODE_MASK(fsal_accessflags_t access)
{
        return (access & FSAL_MODE_BIT_MASK);
}

static inline fsal_accessflags_t
FSAL_ACE4_MASK(fsal_accessflags_t access)
{
        return (access & FSAL_ACE4_BIT_MASK);
}

#define FSAL_MODE_MASK_SET(access) (access | FSAL_MODE_MASK_FLAG)
#define FSAL_ACE4_MASK_SET(access) (access | FSAL_ACE4_MASK_FLAG)

#define IS_FSAL_MODE_MASK_VALID(access)  ((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_MODE_MASK_FLAG)
#define IS_FSAL_ACE4_MASK_VALID(access)  ((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_ACE4_MASK_FLAG)

#define FSAL_WRITE_ACCESS (FSAL_MODE_MASK_SET(FSAL_W_OK) | \
                           FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA | \
                                              FSAL_ACE_PERM_APPEND_DATA))
#define FSAL_READ_ACCESS (FSAL_MODE_MASK_SET(FSAL_R_OK) | \
                          FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_DATA))


/* Object handle LRU resource actions
 */

/**
 * @todo ACE: These should probably be moved to cache_inode_lru.h
 */

typedef enum {
        LRU_CLOSE_FILES = 0x1,
        LRU_FREE_MEMORY = 0x2
} lru_actions_t;


/** FSAL_open behavior. */

typedef uint16_t fsal_openflags_t;

#define FSAL_O_CLOSED   0x0000  /* Closed */
#define FSAL_O_READ     0x0001  /* read */
#define FSAL_O_WRITE    0x0002  /* write */
#define FSAL_O_RDWR     (FSAL_O_READ|FSAL_O_WRITE)  /* read/write: both flags explicitly or'd together so that FSAL_O_RDWR can be used as a mask */
#define FSAL_O_SYNC     0x0004  /* sync */

/** FH expire type (mask). */

typedef uint16_t fsal_fhexptype_t;

/* filehandle never expires until
 * the object is deleted : */
#define FSAL_EXPTYPE_PERSISTENT   0x0001

/* filehandle may expire at any time
 * except as specified in
 * FSAL_EXPTYPE_NOEXP_OPEN flag if used : */
#define FSAL_EXPTYPE_VOLATILE     0x0002

/* if used toghether with FSAL_EXPTYPE_VOLATILE
 * an handle can't expired when it is opened : */
#define FSAL_EXPTYPE_NOEXP_OPEN   0x0004

/* An handle can expire on migration
 * (redundant if FSAL_EXPTYPE_VOLATILE is specified). */
#define FSAL_EXPTYPE_MIGRATE      0x0008

/* An handle can expire on renaming object
 * (redundant if FSAL_EXPTYPE_VOLATILE is specified). */
#define FSAL_EXPTYPE_RENAME       0x0010

/** File system static info. */

/* enums for accessing
 * boolean fields of staticfsinfo
 */
typedef enum enum_fsal_fsinfo_options {
        fso_no_trunc,
        fso_chown_restricted,
        fso_case_insensitive,
        fso_case_preserving,
        fso_link_support,
        fso_symlink_support,
        fso_lock_support,
        fso_lock_support_owner,
        fso_lock_support_async_block,
        fso_named_attr,
        fso_unique_handles,
        fso_cansettime,
        fso_homogenous,
        fso_auth_exportpath_xdev,
        fso_dirs_have_sticky_bit,
        fso_delegations,
        fso_accesscheck_support,
        fso_share_support,
        fso_share_support_owner,
        fso_pnfs_ds_supported
} fsal_fsinfo_options_t;

struct fsal_staticfsinfo_t
{
        uint64_t maxfilesize; /*< maximum allowed filesize     */
        uint32_t maxlink; /*< maximum hard links on a file */
        uint32_t maxnamelen; /*< maximum name length */
        uint32_t maxpathlen; /*< maximum path length */
        bool no_trunc; /*< is it errorneous when name>maxnamelen? */
        bool chown_restricted; /*< is chown limited to super-user.*/
        bool case_insensitive;  /*< case insensitive FS? */
        bool case_preserving;  /*< FS preserves case? */
        bool fh_expire_type; /*< handle persistency indicator */
        bool link_support; /*< FS supports hardlinks? */
        bool symlink_support;  /*< FS supports symlinks? */
        bool lock_support; /*< FS supports file locking? */
        bool lock_support_owner; /*< FS supports lock owners? */
        bool lock_support_async_block; /*< FS supports blocking locks? */
        bool named_attr; /*< FS supports named attributes. */
        bool unique_handles; /*< Handles are unique and persistent.*/
        struct timespec lease_time; /*< Duration of lease at FS in seconds */
        fsal_aclsupp_t acl_support; /*< what type of ACLs are supported */
        bool cansettime; /*< Is it possible to change file times
                             using SETATTR. */
        bool homogenous; /*< Are supported attributes the same
                             for all objects of this filesystem. */
        attrmask_t supported_attrs;  /*< If the FS is homogenous, this
                                         indicates the set of
                                         supported attributes. */
        uint32_t maxread; /*< Max read size */
        uint32_t maxwrite; /*< Max write size */
        uint32_t umask;/*< This mask is applied to the mode of created
                           objects */
        bool auth_exportpath_xdev;  /*< This flag indicates weither
                                        it is possible to cross junctions
                                        for resolving an NFS export path. */

        uint32_t xattr_access_rights; /*< This indicates who is allowed
                                          to read/modify xattrs value. */
        bool accesscheck_support; /*< This indicates whether access check
                                      will be done in FSAL. */
        bool share_support; /*< FS supports share reservation? */
        bool share_support_owner;  /*< FS supports share reservation
                                       with open owners ? */
        bool dirs_have_sticky_bit; /*< fsal does bsd/posix "sticky bit" */
        bool delegations; /*< fsal supports delegations */
        bool pnfs_file;   /*< fsal supports file pnfs */
};

/**
 * @brief The return status of FSAL calls.
 */

typedef struct fsal_status__ {
        fsal_errors_t major; /*< FSAL status code */
        int minor; /*< Other error code (usually POSIX) */
} fsal_status_t;


/**
 * @brief File system dynamic info.
 */

typedef struct fsal_dynamicfsinfo__
{
        uint64_t total_bytes;
        uint64_t free_bytes;
        uint64_t avail_bytes;
        uint64_t total_files;
        uint64_t free_files;
        uint64_t avail_files;
        struct timespec time_delta;
} fsal_dynamicfsinfo_t;

/**
 * Status of FSAL operations
 */

/* quotas */
typedef struct fsal_quota__
{
        uint64_t bhardlimit;
        uint64_t bsoftlimit;
        uint64_t curblocks;
        uint64_t fhardlimit;
        uint64_t fsoftlimit;
        uint64_t curfiles;
        uint64_t btimeleft;
        uint64_t ftimeleft;
        uint64_t bsize;
} fsal_quota_t;

typedef enum {
        FSAL_QUOTA_BLOCKS = 1,
        FSAL_QUOTA_INODES = 2
} fsal_quota_type_t;

/**
 * @brief Digest types
 */

typedef enum fsal_digesttype_t {
     FSAL_DIGEST_SIZEOF, /* just tell me how big... */
     /* NFS handles */
     FSAL_DIGEST_NFSV2,
     FSAL_DIGEST_NFSV3,
     FSAL_DIGEST_NFSV4,

     /* Unique file identifier (for digest only) */
     FSAL_DIGEST_FILEID2,
     FSAL_DIGEST_FILEID3,
     FSAL_DIGEST_FILEID4
} fsal_digesttype_t;

/* output digest sizes */

static const size_t FSAL_DIGEST_SIZE_HDLV2 = 29;
static const size_t FSAL_DIGEST_SIZE_HDLV3 = 61;
static const size_t FSAL_DIGEST_SIZE_HDLV4 = 108;
static const size_t FSAL_DIGEST_SIZE_FILEID2 = sizeof(uint32_t);
static const size_t FSAL_DIGEST_SIZE_FILEID3 = sizeof(uint64_t);
static const size_t FSAL_DIGEST_SIZE_FILEID4 = sizeof(uint64_t);

typedef enum
{
        FSAL_OP_LOCKT,  /*< test if this lock may be applied      */
        FSAL_OP_LOCK,   /*< request a non-blocking lock           */
        FSAL_OP_LOCKB,  /*< request a blocking lock         (NEW) */
        FSAL_OP_UNLOCK, /*< release a lock                        */
        FSAL_OP_CANCEL  /*< cancel a blocking lock          (NEW) */

} fsal_lock_op_t;

typedef enum {
        FSAL_LOCK_R,
        FSAL_LOCK_W,
        FSAL_NO_LOCK
} fsal_lock_t;

typedef enum fsal_sle_type_t
{
  FSAL_POSIX_LOCK,
  FSAL_LEASE_LOCK
} fsal_sle_type_t;

typedef struct fsal_lock_param_t
{
        fsal_sle_type_t lock_sle_type;
        fsal_lock_t lock_type;
        uint64_t lock_start;
        uint64_t lock_length;
} fsal_lock_param_t;

typedef struct fsal_share_param_t
{
        uint32_t share_access;
        uint32_t share_deny;
} fsal_share_param_t;

#endif /* _FSAL_TYPES_H */
/** @} */
