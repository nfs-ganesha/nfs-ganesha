/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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

#include <stddef.h>
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>		/* for MAXNAMLEN */

#include "uid2grp.h"
#include "nfsv41.h"

/* Cookie to be used in FSAL_ListXAttrs() to bypass RO xattr */
#define FSAL_XATTR_RW_COOKIE (~0)

/**
 * @brief Object file type within the system
 */
typedef enum {
	NO_FILE_TYPE = 0,	/* sanity check to ignore type */
	REGULAR_FILE = 1,
	CHARACTER_FILE = 2,
	BLOCK_FILE = 3,
	SYMBOLIC_LINK = 4,
	SOCKET_FILE = 5,
	FIFO_FILE = 6,
	DIRECTORY = 7,
	EXTENDED_ATTR = 8
} object_file_type_t;

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

struct export_perms {
	/** root uid when no root access is available uid when access is
	 *  available but all users are being squashed.
	 */
	uid_t anonymous_uid;
	/** root gid when no root access is available gid when access is
	 *  available but all users are being squashed.
	 */
	gid_t anonymous_gid;
	/** Expiration time interval in seconds for attributes.  Settable with
	    Attr_Expiration_Time (should never be set for client export_perms.
	 */
	int32_t  expire_time_attr;
	/** available export options */
	uint32_t options;
	/** Permission Options that have been set */
	uint32_t set;
};

/* Define bit values for cred_flags */
#define CREDS_LOADED	0x01
#define CREDS_ANON	0x02
#define UID_SQUASHED	0x04
#define GID_SQUASHED	0x08
#define GARRAY_SQUASHED	0x10
#define MANAGED_GIDS	0x20

/** filesystem identifier */

typedef struct fsal_fsid__ {
	uint64_t major;
	uint64_t minor;
} fsal_fsid_t;

/** raw device spec */

typedef struct fsal_dev__ {
	uint64_t major;
	uint64_t minor;
} fsal_dev_t;

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
#define FSAL_ACE_TYPE_MAX   4

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
#define FSAL_ACE_FLAG_MASK_READ_DENY    0x00000100
#define FSAL_ACE_FLAG_MASK_WRITE_DENY   0x00000200
#define FSAL_ACE_FLAG_MASK_EXECUTE_DENY 0x00000400

/** ACE internal flags */

#define FSAL_ACE_IFLAG_MODE_GEN       0x10000000
#define FSAL_ACE_IFLAG_EXCLUDE_FILES  0x40000000
#define FSAL_ACE_IFLAG_EXCLUDE_DIRS   0x20000000
#define FSAL_ACE_IFLAG_SPECIAL_ID     0x80000000

#define FSAL_ACE_FLAG_INHERIT (FSAL_ACE_FLAG_FILE_INHERIT | \
			       FSAL_ACE_FLAG_DIR_INHERIT | \
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
#define FSAL_ACE_SPECIAL_MASK               4

typedef struct fsal_ace__ {

	fsal_acetype_t type;
	fsal_aceperm_t perm;

	fsal_aceflag_t flag;
	fsal_aceflag_t iflag;	/* Internal flags. */
	union {
		uid_t uid;
		gid_t gid;
	} who;
} fsal_ace_t;

typedef struct fsal_acl__ {
	uint32_t naces;
	fsal_ace_t *aces;
	pthread_rwlock_t lock;
	uint32_t ref;
} fsal_acl_t;

typedef struct fsal_acl_data__ {
	uint32_t naces;
	fsal_ace_t *aces;
} fsal_acl_data_t;

/* Macros for NFS4 ACE flags, masks, and special who values. */

#define GET_FSAL_ACE_TYPE(ACE)            ((ACE).type)
#define GET_FSAL_ACE_PERM(ACE)            ((ACE).perm)
#define GET_FSAL_ACE_FLAG(ACE)            ((ACE).flag)
#define GET_FSAL_ACE_IFLAG(ACE)           ((ACE).iflag)
#define GET_FSAL_ACE_USER(ACE)            ((ACE).who.uid)
#define GET_FSAL_ACE_GROUP(ACE)           ((ACE).who.gid)

#define IS_FSAL_ACE_BIT(WORD, BIT)        (0 != ((WORD) & (BIT)))
#define IS_FSAL_ACE_ALL_BITS(WORD, BITS)  (BITS == ((WORD) & (BITS)))

#define IS_FSAL_ACE_TYPE(ACE, VALUE) \
	((GET_FSAL_ACE_TYPE(ACE)) == (VALUE))
#define IS_FSAL_ACE_USER(ACE, VALUE) \
	((GET_FSAL_ACE_USER(ACE)) == (VALUE))
#define IS_FSAL_ACE_GROUP(ACE, VALUE) \
	((GET_FSAL_ACE_GROUP(ACE)) == (VALUE))

#define IS_FSAL_ACE_ALLOW(ACE) \
	IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_ALLOW)
#define IS_FSAL_ACE_DENY(ACE) \
	IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_DENY)
#define IS_FSAL_ACE_AUDIT(ACE) \
	IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_AUDIT)
#define IS_FSAL_ACE_ALRAM(ACE) \
	IS_FSAL_ACE_TYPE(ACE, FSAL_ACE_TYPE_ALARM)
#define IS_FSAL_ACE_PERM(ACE) \
	(IS_FSAL_ACE_ALLOW(ACE) || IS_FSAL_ACE_DENY(ACE))

#define IS_FSAL_ACE_FLAG(ACE, BIT) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_FLAG(ACE), (BIT))
#define IS_FSAL_ACE_FILE_INHERIT(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_FILE_INHERIT)
#define IS_FSAL_ACE_DIR_INHERIT(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_DIR_INHERIT)
#define IS_FSAL_ACE_NO_PROPAGATE(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_NO_PROPAGATE)
#define IS_FSAL_ACE_INHERIT_ONLY(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_INHERIT_ONLY)
#define IS_FSAL_ACE_FLAG_SUCCESSFUL(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_SUCCESSFUL)
#define IS_FSAL_ACE_AUDIT_FAILURE(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_FAILED)
#define IS_FSAL_ACE_GROUP_ID(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_GROUP_ID)
#define IS_FSAL_ACE_INHERIT(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_INHERIT)
#define IS_FSAL_ACE_INHERTED(ACE) \
	IS_FSAL_ACE_FLAG(ACE, FSAL_ACE_FLAG_INHERITED)

#define GET_FSAL_ACE_WHO_TYPE(ACE) \
	(IS_FSAL_ACE_GROUP_ID(ACE) ? "gid" : "uid")
#define GET_FSAL_ACE_WHO(ACE) \
	(IS_FSAL_ACE_GROUP_ID(ACE) ? (ACE).who.gid : (ACE).who.uid)

#define IS_FSAL_ACE_SPECIAL_OWNER(ACE) \
	IS_FSAL_ACE_USER(ACE, FSAL_ACE_SPECIAL_OWNER)
#define IS_FSAL_ACE_SPECIAL_GROUP(ACE) \
	IS_FSAL_ACE_USER(ACE, FSAL_ACE_SPECIAL_GROUP)
#define IS_FSAL_ACE_SPECIAL_EVERYONE(ACE) \
	  IS_FSAL_ACE_USER(ACE, FSAL_ACE_SPECIAL_EVERYONE)
#define IS_FSAL_ACE_SPECIAL_MASK(ACE) \
	IS_FSAL_ACE_USER(ACE, FSAL_ACE_SPECIAL_MASK)
#define IS_FSAL_ACE_SPECIAL(ACE) \
	(IS_FSAL_ACE_SPECIAL_OWNER(ACE) || \
	 IS_FSAL_ACE_SPECIAL_GROUP(ACE) || \
	 IS_FSAL_ACE_SPECIAL_EVERYONE(ACE))

/* Macros for internal NFS4 ACE flags. */

#define IS_FSAL_ACE_MODE_GEN(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE), \
			FSAL_ACE_IFLAG_MODE_GEN)
#define IS_FSAL_ACE_SPECIAL_ID(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE), \
			FSAL_ACE_IFLAG_SPECIAL_ID)
#define IS_FSAL_FILE_APPLICABLE(ACE) \
	(!IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE),		\
			  FSAL_ACE_IFLAG_EXCLUDE_FILES))
#define IS_FSAL_DIR_APPLICABLE(ACE)  \
	(!IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE),	\
			  FSAL_ACE_IFLAG_EXCLUDE_DIRS))
#define IS_FSAL_ACE_IFLAG(ACE, BIT) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_IFLAG(ACE), (BIT))

/* Macros for NFS4 ACE permissions. */

#define IS_FSAL_ACE_READ_DATA(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_READ_DATA)
#define IS_FSAL_ACE_LIST_DIR(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_LIST_DIR)
#define IS_FSAL_ACE_WRITE_DATA(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_WRITE_DATA)
#define IS_FSAL_ACE_ADD_FIILE(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_ADD_FILE)
#define IS_FSAL_ACE_APPEND_DATA(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_APPEND_DATA)
#define IS_FSAL_ACE_ADD_SUBDIRECTORY(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_ADD_SUBDIRECTORY)
#define IS_FSAL_ACE_READ_NAMED_ATTR(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_READ_NAMED_ATTR)
#define IS_FSAL_ACE_WRITE_NAMED_ATTR(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_WRITE_NAMED_ATTR)
#define IS_FSAL_ACE_EXECUTE(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_EXECUTE)
#define IS_FSAL_ACE_DELETE_CHILD(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_DELETE_CHILD)
#define IS_FSAL_ACE_READ_ATTR(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_READ_ATTR)
#define IS_FSAL_ACE_WRITE_ATTR(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_WRITE_ATTR)
#define IS_FSAL_ACE_DELETE(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_DELETE)
#define IS_FSAL_ACE_READ_ACL(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_READ_ACL)
#define IS_FSAL_ACE_WRITE_ACL(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_WRITE_ACL)
#define IS_FSAL_ACE_WRITE_OWNER(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_WRITE_OWNER)
#define IS_FSAL_ACE_SYNCHRONIZE(ACE) \
	IS_FSAL_ACE_BIT(GET_FSAL_ACE_PERM(ACE), FSAL_ACE_PERM_SYNCHRONIZE)

/**
 * Stores root and fs locations. fs locations format is as follows
 *
 * <server>:<path>
 */
typedef struct fsal_fs_locations {
	uint32_t ref;
	uint32_t nservers;	/* size of server array */
	pthread_rwlock_t lock;
	char *fs_root;
	char *rootpath;
	utf8string *server;
} fsal_fs_locations_t;

/**
 * Defines an attribute mask.
 *
 * Do not just use OR and AND to test these, use the macros.
 */
typedef uint64_t attrmask_t;

/** For stackable FSALs that just pass through dealings with attributes */
#define ALL_ATTRIBUTES UINT64_MAX

/**
 * Attribute masks.
 */

/* supported attributes (Obsolete)
#define ATTR_SUPPATTR 0x0000000000000001 */
/* file type */
#define ATTR_TYPE 0x0000000000000002LL
/* file size */
#define ATTR_SIZE 0x0000000000000004LL
/* filesystem id */
#define ATTR_FSID 0x0000000000000008LL
/* file space reserve */
#define ATTR4_SPACE_RESERVED 0x0000000000000010LL
/* ACL */
#define ATTR_ACL 0x0000000000000020LL
/* file id */
#define ATTR_FILEID 0x0000000000000040LL
/* Access permission flag */
#define ATTR_MODE 0x0000000000000080LL
/* Number of hard links */
#define ATTR_NUMLINKS 0x0000000000000100LL
/* owner ID */
#define ATTR_OWNER 0x0000000000000200LL
/* group ID */
#define ATTR_GROUP 0x0000000000000400LL
/* ID of device for block special or character special files*/
#define ATTR_RAWDEV 0x0000000000000800LL
/* Access time */
#define ATTR_ATIME 0x0000000000001000LL
/* Creation time */
#define ATTR_CREATION 0x0000000000002000LL
/* Metadata modification time */
#define ATTR_CTIME 0x0000000000004000LL
/* data modification time */
#define ATTR_MTIME 0x0000000000008000LL
/* space used by this file. */
#define ATTR_SPACEUSED 0x0000000000010000LL
/* This bit indicates that an error occured during getting object attributes */
#define ATTR_RDATTR_ERR 0x8000000000000000LL
/* Generation number */
#define ATTR_GENERATION 0x0000000000080000LL
/* Change attribute */
#define ATTR_CHANGE 0x0000000000100000LL
/* Set atime to server time */
#define ATTR_ATIME_SERVER  0x0000000000200000LL
/* Set mtime to server time */
#define ATTR_MTIME_SERVER  0x0000000000400000LL
/* Set fs locations */
#define ATTR4_FS_LOCATIONS  0x0000000000800000LL
/* xattr supported */
#define ATTR4_XATTR  0x0000000001000000LL
/* security labels supported */
#define ATTR4_SEC_LABEL  0x0000000002000000LL

/* attributes that used for NFS v3 */

#define ATTRS_NFS3   (ATTR_MODE     | ATTR_FILEID    | \
		      ATTR_TYPE     | ATTR_RAWDEV    | \
		      ATTR_NUMLINKS | ATTR_OWNER     | \
		      ATTR_GROUP    | ATTR_SIZE      | \
		      ATTR_ATIME    | ATTR_MTIME     | \
		      ATTR_CTIME    | ATTR_SPACEUSED | \
		      ATTR_FSID)

#define ATTRS_NFS3_ACL (ATTRS_NFS3 | ATTR_ACL)

#define ATTRS_TIME (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME)
#define ATTRS_CREDS (ATTR_OWNER | ATTR_GROUP)

#define CREATE_MASK_NON_REG_NFS3 (ATTRS_TIME)
#define CREATE_MASK_NON_REG_NFS4 (ATTRS_TIME | ATTR_ACL)

#define CREATE_MASK_REG_NFS3 (CREATE_MASK_NON_REG_NFS3 | ATTR_SIZE)
#define CREATE_MASK_REG_NFS4 (CREATE_MASK_NON_REG_NFS4 | ATTR_SIZE)

#define ATTRS_SET_TIME (ATTR_ATIME | ATTR_MTIME | \
			ATTR_ATIME_SERVER | ATTR_MTIME_SERVER)

/** Define the set of attributes contained or derrived from struct stat that
 *  are supplied by posix2fsal_attributes.
 */
#define ATTRS_POSIX (ATTR_TYPE | ATTR_SIZE | ATTR_FSID | ATTR_FILEID |     \
		     ATTR_MODE | ATTR_NUMLINKS | ATTR_OWNER | ATTR_GROUP | \
		     ATTR_ATIME | ATTR_CTIME | ATTR_MTIME | \
		     ATTR_CHANGE | ATTR_SPACEUSED | ATTR_RAWDEV)

/**
 * @brief A list of FS object's attributes.
 */

struct fsal_attrlist {
	attrmask_t request_mask; /*< Indicates the requested from the FSAL. */
	attrmask_t valid_mask;	/*< Indicates the attributes to be set or
				   that have been filled in by the FSAL. */
	attrmask_t supported;	/*< Indicates which attributes the FSAL
				    supports. */
	object_file_type_t type;	/*< Type of this object */
	uint64_t filesize;	/*< Logical size (amount of data that can be
				   read) */
	fsal_fsid_t fsid;	/*< Filesystem on which this object is
				   stored */
	uint64_t fsid3;		/*< Squashed fsid for NFS v3 */
	fsal_acl_t *acl;	/*< ACL for this object */
	uint64_t fileid;	/*< Unique identifier for this object within
				   the scope of the fsid, (e.g. inode number) */
	uint32_t mode;		/*< POSIX access mode */
	uint32_t numlinks;	/*< Number of links to this file */
	uint64_t owner;		/*< Owner ID */
	uint64_t group;		/*< Group ID */
	fsal_dev_t rawdev;	/*< Major/minor device number (only
				   meaningful for character/block special
				   files.) */
	struct timespec atime;	/*< Time of last access */
	struct timespec creation;	/*< Creation time */
	struct timespec ctime;	/*< Inode modification time (a la stat.
				   NOT creation.) */
	struct timespec mtime;	/*< Time of last modification */
	uint64_t spaceused;	/*< Space used on underlying filesystem */
	uint64_t change;	/*< A 'change id' */
	uint64_t generation;	/*< Generation number for this file */
	int32_t expire_time_attr;	/*< Expiration time interval in seconds
					   for attributes. Settable by FSAL. */
	fsal_fs_locations_t *fs_locations;	/*< fs locations for this
						    object if any */
	struct sec_label4 sec_label;
};

/******************************************************
 *            Attribute mask management.
 ******************************************************/

/** this macro tests if an attribute is set
 *  example :
 *  FSAL_TEST_MASK( attrib_list.mask, FSAL_ATTR_CREATION )
 */
#define FSAL_TEST_MASK(_attrib_mask_, _attr_const_) \
	((_attrib_mask_) & (_attr_const_))

/** this macro sets an attribute
 *  example :
 *  FSAL_SET_MASK( attrib_list.mask, FSAL_ATTR_CREATION )
 */
#define FSAL_SET_MASK(_attrib_mask_, _attr_const_) \
	((_attrib_mask_) |= (_attr_const_))

#define FSAL_UNSET_MASK(_attrib_mask_, _attr_const_) \
	((_attrib_mask_) &= ~(_attr_const_))

/** this macro clears the attribute mask
 *  example :
 *  FSAL_CLEAR_MASK( attrib_list.asked_attributes )
 */
#define FSAL_CLEAR_MASK(_attrib_mask_) \
	((_attrib_mask_) = 0LL)

/******************************************************
 *          FSAL extended attributes management.
 ******************************************************/

#define XATTR_NAME_SIZE (MAXNAMLEN + 1)

/** An extented attribute entry */
typedef struct fsal_xattrent {
	uint64_t xattr_id;	/*< xattr index */
	uint64_t xattr_cookie;	/*< cookie for the next entry */
	char xattr_name[XATTR_NAME_SIZE];	/*< attribute name  */
} fsal_xattrent_t;

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200
/* function for getting an attribute value */
#define XATTR_RW_COOKIE ~0

/* Flags representing if an FSAL supports read or write delegations */
#define FSAL_OPTION_FILE_READ_DELEG 0x00000001	/*< File read delegations */
#define FSAL_OPTION_FILE_WRITE_DELEG 0x00000002	/*< File write delegations */
#define FSAL_OPTION_FILE_DELEGATIONS (FSAL_OPTION_FILE_READ_DELEG | \
					   FSAL_OPTION_FILE_WRITE_DELEG)
#define FSAL_OPTION_NO_DELEGATIONS 0

/** Mask for permission testing. Both mode and ace4 mask are encoded. */

typedef enum {
	FSAL_R_OK = 0x04000000,	/*< Test for Read permission */
	FSAL_W_OK = 0x02000000,	/*< Test for Write permission */
	FSAL_X_OK = 0x01000000,	/*< Test for execute permission */
	FSAL_ACCESS_OK = 0x00000000,	/*< Allow */
	FSAL_ACCESS_FLAG_BIT_MASK = 0x80000000,
	FSAL_MODE_BIT_MASK = 0x07000000,
	FSAL_ACE4_BIT_MASK = 0x50FFFFFF,
	FSAL_MODE_MASK_FLAG = 0x00000000,
	FSAL_ACE4_MASK_FLAG = 0x80000000,
	FSAL_ACE4_PERM_CONTINUE = 0x40000000,	/*< Indicate ACL evaluation
						 * should continue */
	FSAL_ACE4_REQ_FLAG = 0x10000000, /*< Indicate required ACL allow */
} fsal_accessflags_t;

static inline fsal_accessflags_t FSAL_MODE_MASK(fsal_accessflags_t access)
{
	unsigned long acc = access & FSAL_MODE_BIT_MASK;

	return (fsal_accessflags_t) acc;
}

static inline fsal_accessflags_t FSAL_ACE4_MASK(fsal_accessflags_t access)
{
	unsigned long acc = access & FSAL_ACE4_BIT_MASK;

	return (fsal_accessflags_t) acc;
}

#define FSAL_MODE_MASK_SET(access) (access | FSAL_MODE_MASK_FLAG)
#define FSAL_ACE4_MASK_SET(access) (access | FSAL_ACE4_MASK_FLAG)

#define IS_FSAL_MODE_MASK_VALID(access) \
	((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_MODE_MASK_FLAG)
#define IS_FSAL_ACE4_MASK_VALID(access) \
	((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_ACE4_MASK_FLAG)

#define IS_FSAL_ACE4_REQ(access) (access & FSAL_ACE4_REQ_FLAG)

#define FSAL_WRITE_ACCESS (FSAL_MODE_MASK_SET(FSAL_W_OK) | \
			   FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA | \
					      FSAL_ACE_PERM_APPEND_DATA))
#define FSAL_READ_ACCESS (FSAL_MODE_MASK_SET(FSAL_R_OK) | \
			  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_DATA))
#define FSAL_EXECUTE_ACCESS (FSAL_MODE_MASK_SET(FSAL_X_OK) | \
			  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE))

/** FSAL_open behavior. */

typedef uint16_t fsal_openflags_t;

#define FSAL_O_CLOSED     0x0000  /* Closed */
#define FSAL_O_READ       0x0001  /* read */
#define FSAL_O_WRITE      0x0002  /* write */
#define FSAL_O_RDWR       (FSAL_O_READ|FSAL_O_WRITE)  /* read/write: both flags
						     * explicitly or'd together
						     * so that FSAL_O_RDWR can
						     * be used as a mask */
#define FSAL_O_RECLAIM         0x0008  /* open reclaim */
#define FSAL_O_ANY             0x0020  /* any open file descriptor is usable */
#define FSAL_O_TRUNC           0x0040  /* Truncate file on open */
#define FSAL_O_DENY_READ       0x0100
#define FSAL_O_DENY_WRITE      0x0200
#define FSAL_O_DENY_WRITE_MAND 0x0400  /* Mandatory deny-write (i.e. NFSv4) */
#define FSAL_O_DENY_NONE       0x0000

/* NFS open flags */
#define FSAL_O_OPENFLAGS (FSAL_O_RDWR | FSAL_O_DENY_READ | \
		 FSAL_O_DENY_WRITE | FSAL_O_DENY_WRITE_MAND)

/* Extract NFS open flags */
#define FSAL_O_NFS_FLAGS(flags) ((flags) & (FSAL_O_OPENFLAGS))

enum fsal_create_mode {
	FSAL_NO_CREATE = 0,
	FSAL_UNCHECKED = 1,
	FSAL_GUARDED = 2,
	FSAL_EXCLUSIVE = 3,
	FSAL_EXCLUSIVE_41 = 4,
	FSAL_EXCLUSIVE_9P,
};

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
	fso_lock_support_async_block,
	fso_named_attr,
	fso_unique_handles,
	fso_cansettime,
	fso_homogenous,
	fso_auth_exportpath_xdev,
	fso_delegations_r,
	fso_delegations_w,
	fso_pnfs_ds_supported,
	fso_pnfs_mds_supported,
	fso_grace_method,
	fso_link_supports_permission_checks,
	fso_rename_changes_key,
	fso_compute_readdir_cookie,
	fso_whence_is_name,
	fso_readdir_plus,
	fso_compliant_eof_behavior,
	fso_xattr_support,
} fsal_fsinfo_options_t;

/* The largest maxread and maxwrite value */
#define FSAL_MAXIOSIZE XDR_BYTES_MAXLEN_IO

/* Default time_delta nanoseconds.
 * FSALs may set time_delta.tv_nsec to this value or override with a different
 * value or even fill in programatically with the result from some call.
 */
#define FSAL_DEFAULT_TIME_DELTA_NSEC 100

typedef struct fsal_staticfsinfo_t {
	uint64_t maxfilesize;	/*< maximum allowed filesize     */
	uint32_t maxlink;	/*< maximum hard links on a file */
	uint32_t maxnamelen;	/*< maximum name length */
	uint32_t maxpathlen;	/*< maximum path length */
	bool no_trunc;		/*< is it errorneous when name>maxnamelen? */
	bool chown_restricted;	/*< is chown limited to super-user. */
	bool case_insensitive;	/*< case insensitive FS? */
	bool case_preserving;	/*< FS preserves case? */
	bool link_support;	/*< FS supports hardlinks? */
	bool symlink_support;	/*< FS supports symlinks? */
	bool lock_support;	/*< FS supports file locking? */
	bool lock_support_async_block;	/*< FS supports blocking locks? */
	bool named_attr;	/*< FS supports named attributes. */
	bool unique_handles;	/*< Handles are unique and persistent. */
	fsal_aclsupp_t acl_support;	/*< what type of ACLs are supported */
	bool cansettime;	/*< Is it possible to change file times
				   using SETATTR. */
	bool homogenous;	/*< Are supported attributes the same
				   for all objects of this filesystem. */
	attrmask_t supported_attrs;	/*< If the FS is homogenous, this
					   indicates the set of
					   supported attributes. */
	uint64_t maxread;	/*< Max read size */
	uint64_t maxwrite;	/*< Max write size */
	uint32_t umask;		/*< This mask is applied to the mode of created
				   objects */
	bool auth_exportpath_xdev;	/*< This flag indicates weither
					   it is possible to cross junctions
					   for resolving an NFS export path. */

	uint32_t delegations;	/*< fsal supports delegations */
	bool pnfs_mds;		/*< fsal supports file pnfs MDS */
	bool pnfs_ds;		/*< fsal supports file pnfs DS */
	bool fsal_trace;	/*< fsal trace supports */
	bool fsal_grace;	/*< fsal will handle grace */
	bool link_supports_permission_checks;
	bool rename_changes_key;/*< Handle key is changed across rename */
	bool compute_readdir_cookie;
	bool whence_is_name;
	bool readdir_plus;	/*< FSAL supports readdir_plus */
	bool compliant_eof_behavior;	/* FSAL compliant to end-of-file NFS
					  v3 read behavior. */
	bool xattr_support;	/*< fsal can support xattrs */
	int32_t expire_time_parent; /*< Expiration time interval in
				       seconds for parent handle.
				       If FS gives information about parent
				       change for a directory with an upcall,
				       set this to -1. Else set it to some
				       positive value. Defaults to -1. */
} fsal_staticfsinfo_t;

/**
 * @brief The return error values of FSAL calls.
 */

typedef enum fsal_errors_t {
	ERR_FSAL_NO_ERROR = 0,
	ERR_FSAL_PERM = 1,
	ERR_FSAL_NOENT = 2,
	ERR_FSAL_IO = 5,
	ERR_FSAL_NXIO = 6,
	ERR_FSAL_NOMEM = 12,
	ERR_FSAL_ACCESS = 13,
	ERR_FSAL_FAULT = 14,
	ERR_FSAL_STILL_IN_USE = 16,
	ERR_FSAL_EXIST = 17,
	ERR_FSAL_XDEV = 18,
	ERR_FSAL_NOTDIR = 20,
	ERR_FSAL_ISDIR = 21,
	ERR_FSAL_INVAL = 22,
	ERR_FSAL_FBIG = 27,
	ERR_FSAL_NOSPC = 28,
	ERR_FSAL_ROFS = 30,
	ERR_FSAL_MLINK = 31,
	ERR_FSAL_DQUOT = 49,
	ERR_FSAL_NO_DATA = 61,
	ERR_FSAL_NAMETOOLONG = 78,
	ERR_FSAL_NOTEMPTY = 93,
	ERR_FSAL_STALE = 151,
	ERR_FSAL_BADHANDLE = 10001,
	ERR_FSAL_BADCOOKIE = 10003,
	ERR_FSAL_NOTSUPP = 10004,
	ERR_FSAL_TOOSMALL = 10005,
	ERR_FSAL_SERVERFAULT = 10006,
	ERR_FSAL_BADTYPE = 10007,
	ERR_FSAL_DELAY = 10008,
	ERR_FSAL_LOCKED = 10012,
	ERR_FSAL_FHEXPIRED = 10014,
	ERR_FSAL_SHARE_DENIED = 10015,
	ERR_FSAL_SYMLINK = 10029,
	ERR_FSAL_ATTRNOTSUPP = 10032,
	ERR_FSAL_BAD_RANGE = 10042,
	ERR_FSAL_NOT_INIT = 20001,
	ERR_FSAL_ALREADY_INIT = 20002,
	ERR_FSAL_BAD_INIT = 20003,
	ERR_FSAL_SEC = 20004,
	ERR_FSAL_NO_QUOTA = 20005,
	ERR_FSAL_NOT_OPENED = 20010,
	ERR_FSAL_DEADLOCK = 20011,
	ERR_FSAL_OVERFLOW = 20012,
	ERR_FSAL_INTERRUPT = 20013,
	ERR_FSAL_BLOCKED = 20014,
	ERR_FSAL_TIMEOUT = 20015,
	ERR_FSAL_FILE_OPEN = 10046,
	ERR_FSAL_UNION_NOTSUPP = 10090,
	ERR_FSAL_IN_GRACE = 10095,
	ERR_FSAL_NO_ACE = 10096,
	ERR_FSAL_CROSS_JUNCTION = 10097,
	ERR_FSAL_BADNAME = 10098,
	ERR_FSAL_NOXATTR = 10099,
	ERR_FSAL_XATTR2BIG = 10100,
} fsal_errors_t;

/**
 * @brief The return status of FSAL calls.
 */

typedef struct fsal_status__ {
	fsal_errors_t major;	/*< FSAL status code */
	int minor;		/*< Other error code (usually POSIX) */
} fsal_status_t;

/******************************************************
 *              FSAL Returns macros
 ******************************************************/

/**
 *  fsalstat (was ReturnCode) :
 *  Macro for returning a fsal_status_t without trace nor stats increment.
 */
static inline fsal_status_t fsalstat(fsal_errors_t major, int minor)
{
	fsal_status_t status = {major, minor};
	return status;
}

/******************************************************
 *              FSAL Errors handling.
 ******************************************************/

/** Tests whether the returned status is erroneous.
 *  Example :
 *  if (FSAL_IS_ERROR(status = FSAL_call(...))) {
 *     printf("ERROR status = %d, %d\n", status.major,status.minor);
 *  }
 */
#define FSAL_IS_SUCCESS(_status_) ((_status_).major == ERR_FSAL_NO_ERROR)
#define FSAL_IS_ERROR(_status_) (!FSAL_IS_SUCCESS(_status_))

/**
 * @brief File system dynamic info.
 */

typedef struct fsal_dynamicfsinfo__ {
	uint64_t total_bytes;
	uint64_t free_bytes;
	uint64_t avail_bytes;
	uint64_t total_files;
	uint64_t free_files;
	uint64_t avail_files;
	uint64_t maxread;
	uint64_t maxwrite;
	struct timespec time_delta;
} fsal_dynamicfsinfo_t;

/**
 * Status of FSAL operations
 */

/* quotas */
typedef struct fsal_quota__ {
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
	/* NFS handles */
	FSAL_DIGEST_NFSV3,
	FSAL_DIGEST_NFSV4,
} fsal_digesttype_t;

typedef enum {
	FSAL_OP_LOCKT,		/*< test if this lock may be applied      */
	FSAL_OP_LOCK,		/*< request a non-blocking lock           */
	FSAL_OP_LOCKB,		/*< request a blocking lock         (NEW) */
	FSAL_OP_UNLOCK,		/*< release a lock                        */
	FSAL_OP_CANCEL		/*< cancel a blocking lock          (NEW) */
} fsal_lock_op_t;

typedef enum {
	FSAL_LOCK_R,
	FSAL_LOCK_W,
	FSAL_NO_LOCK
} fsal_lock_t;

enum fsal_sle_type {
	FSAL_POSIX_LOCK,
	FSAL_LEASE_LOCK
};

typedef struct fsal_lock_param_t {
	enum fsal_sle_type lock_sle_type;
	fsal_lock_t lock_type;
	uint64_t lock_start;
	uint64_t lock_length;
	bool lock_reclaim;
} fsal_lock_param_t;

typedef struct fsal_share_param_t {
	uint32_t share_access;
	uint32_t share_deny;
	bool share_reclaim;
} fsal_share_param_t;

typedef enum {
	FSAL_DELEG_NONE,
	FSAL_DELEG_RD,
	FSAL_DELEG_WR
} fsal_deleg_t;

typedef char fsal_verifier_t[NFS4_VERIFIER_SIZE];

/**
 * @brief Generic file handle.
 */
struct fsal_fd {
	/** The open and share mode etc. This MUST be first in every
	 *  file descriptor structure.
	 */
	fsal_openflags_t openflags;
};

/**
 * @brief The ref counted share reservation state.
 *
 * Each field represents the count of instances of that flag being present
 * in a share reservation.
 *
 * There is a separate count of mandatory deny write flags so that they can be
 * enforced against all writes (non-mandatory deny write is only enforced
 * against indicated operations).
 */
struct fsal_share {
	unsigned int share_access_read;
	unsigned int share_access_write;
	unsigned int share_deny_read;
	unsigned int share_deny_write;
	/**< Count of mandatory share deny write */
	unsigned int share_deny_write_mand;
};

#endif				/* _FSAL_TYPES_H */
/** @} */
