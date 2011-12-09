/*
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    fsal_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 *
 *
 */

#ifndef _FSAL_TYPES_H
#define _FSAL_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _USE_NFS4_ACL
#include <openssl/md5.h>
#endif                          /* _USE_NFS4_ACL */

#ifdef _SOLARIS
#ifndef MAXNAMLEN
#define MAXNAMLEN 512
#endif
typedef unsigned int u_int32_t;
typedef unsigned short int u_int16_t;
typedef unsigned long long int u_int64_t;
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_FSAL          "FSAL"
#define CONF_LABEL_FS_COMMON     "FileSystem"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "err_fsal.h"
#include "RW_Lock.h"

/* Forward declarations */
typedef struct fsal_staticfsinfo_t fsal_staticfsinfo_t;

/* FSAL function indexes, and names */

#define INDEX_FSAL_lookup                0
#define INDEX_FSAL_access                1
#define INDEX_FSAL_create                2
#define INDEX_FSAL_mkdir                 3
#define INDEX_FSAL_truncate              4
#define INDEX_FSAL_getattrs              5
#define INDEX_FSAL_setattrs              6
#define INDEX_FSAL_link                  7
#define INDEX_FSAL_opendir               8
#define INDEX_FSAL_readdir               9
#define INDEX_FSAL_closedir             10
#define INDEX_FSAL_open                 11
#define INDEX_FSAL_read                 12
#define INDEX_FSAL_write                13
#define INDEX_FSAL_close                14
#define INDEX_FSAL_readlink             15
#define INDEX_FSAL_symlink              16
#define INDEX_FSAL_rename               17
#define INDEX_FSAL_unlink               18
#define INDEX_FSAL_mknode               19
#define INDEX_FSAL_unused_20            20
#define INDEX_FSAL_dynamic_fsinfo       21
#define INDEX_FSAL_rcp                  22
#define INDEX_FSAL_Init                 23
#define INDEX_FSAL_get_stats            24
#define INDEX_FSAL_unused_25            25
#define INDEX_FSAL_unused_26            26
#define INDEX_FSAL_unused_27            27
#define INDEX_FSAL_BuildExportContext   28
#define INDEX_FSAL_InitClientContext    29
#define INDEX_FSAL_GetClientContext     30
#define INDEX_FSAL_lookupPath           31
#define INDEX_FSAL_lookupJunction       32
#define INDEX_FSAL_test_access          33
#define INDEX_FSAL_rmdir                34
#define INDEX_FSAL_CleanObjectResources 35
#define INDEX_FSAL_open_by_name         36
#define INDEX_FSAL_open_by_fileid       37
#define INDEX_FSAL_ListXAttrs           38
#define INDEX_FSAL_GetXAttrValue        39
#define INDEX_FSAL_SetXAttrValue        40
#define INDEX_FSAL_GetXAttrAttrs        41
#define INDEX_FSAL_close_by_fileid      42
#define INDEX_FSAL_setattr_access       43
#define INDEX_FSAL_merge_attrs          44
#define INDEX_FSAL_rename_access        45
#define INDEX_FSAL_unlink_access        46
#define INDEX_FSAL_link_access          47
#define INDEX_FSAL_create_access        48
#define INDEX_FSAL_unused_49	        49
#define INDEX_FSAL_CleanUpExportContext 50
#define INDEX_FSAL_getextattrs          51
#define INDEX_FSAL_sync                 52
#define INDEX_FSAL_getattrs_descriptor  53
#define INDEX_FSAL_lock_op              54
#define INDEX_FSAL_UP_init              55
#define INDEX_FSAL_UP_addfilter         56
#define INDEX_FSAL_UP_getevents         57
#define INDEX_FSAL_unused_58            58

/* number of FSAL functions */
#define FSAL_NB_FUNC  59

extern const char *fsal_function_names[];

typedef unsigned long long fsal_u64_t;    /**< 64 bit unsigned integer.     */
typedef unsigned int fsal_uint_t;         /**< 32 bit unsigned integer.     */
typedef unsigned short fsal_ushort_t;     /**< 16 bit unsigned integer.     */
typedef int fsal_boolean_t;                  /**< boolean                   */

typedef fsal_u64_t fsal_size_t;               /**< data size           */
typedef off_t fsal_off_t;                     /**< offset              */
typedef unsigned long fsal_mdsize_t;          /**< metadata size       */
typedef unsigned long fsal_count_t;           /**< FS object count.    */
typedef uid_t fsal_uid_t;                     /**< Owner               */
typedef gid_t fsal_gid_t;                     /**< Group               */
typedef mode_t fsal_accessmode_t;              /**< Access mode (32 bits) */

typedef struct fsal_time__
{
  fsal_uint_t seconds;
  fsal_uint_t nseconds;
} fsal_time_t;       /**< time */

/** Behavior for init values */
typedef enum fsal_initflag__
{
  FSAL_INIT_FS_DEFAULT = 0,     /**< keep FS default value */
  FSAL_INIT_FORCE_VALUE,        /**< force a value */
  FSAL_INIT_MAX_LIMIT,          /**< force a value if default is greater */
  FSAL_INIT_MIN_LIMIT           /**< force a value if default is smaller */
      /* Note : for booleans, we considerate that TRUE > FALSE */
} fsal_initflag_t;

/** FS object types */
typedef enum fsal_nodetype__
{
  FSAL_TYPE_FIFO = 0x1,  /**< Fifo              */
  FSAL_TYPE_CHR = 0x2,   /**< character special */
  FSAL_TYPE_DIR = 0x4,   /**< directory         */
  FSAL_TYPE_BLK = 0x6,   /**< block special     */
  FSAL_TYPE_FILE = 0x8,  /**< regular file      */
  FSAL_TYPE_LNK = 0xA,   /**< symbolic link     */
  FSAL_TYPE_SOCK = 0xC,  /**< socket            */
  FSAL_TYPE_XATTR = 0xE, /**< extended attribute */
  FSAL_TYPE_JUNCTION = 0xF /**< junction to another fileset */
} fsal_nodetype_t;

/* ---------------
 *  FS dependant :
 * --------------*/

/* prefered readdir size */
#define FSAL_READDIR_SIZE 2048

#define FSAL_MAX_NAME_LEN   NAME_MAX
#define FSAL_MAX_PATH_LEN   PATH_MAX

#define FSAL_NGROUPS_MAX  32

/** object name.  */

struct user_credentials {
	uid_t user;
	gid_t group;
	int nbgroups;
	gid_t alt_groups[FSAL_NGROUPS_MAX];
};

typedef struct fsal_name__
{
  char name[FSAL_MAX_NAME_LEN];
  unsigned int len;
} fsal_name_t;

/** object path.  */

typedef struct fsal_path__
{
  char path[FSAL_MAX_PATH_LEN];
  unsigned int len;
} fsal_path_t;

static const fsal_name_t FSAL_DOT = { ".", 1 };
static const fsal_name_t FSAL_DOT_DOT = { "..", 2 };

#define FSAL_NAME_INITIALIZER {"",0}
#define FSAL_PATH_INITIALIZER {"",0}

/* Do not include fsal_types for the FSAL is compiled with dlopen */
#ifndef _USE_SHARED_FSAL

#ifdef _USE_GHOSTFS
#include "FSAL/FSAL_GHOST_FS/fsal_types.h"
#elif defined(_USE_HPSS)
#include "FSAL/FSAL_HPSS/fsal_types.h"
#elif defined ( _USE_PROXY )
#include "FSAL/FSAL_PROXY/fsal_types.h"
#elif defined ( _USE_POSIX )
#include "FSAL/FSAL_POSIX/fsal_types.h"
#include "FSAL/FSAL_POSIX/posixdb.h"
#elif defined ( _USE_SNMP )
#include "FSAL/FSAL_SNMP/fsal_types.h"
#elif defined ( _USE_FUSE )
#include "FSAL/FSAL_FUSELIKE/fsal_types.h"
#elif defined ( _USE_LUSTRE )
#include "FSAL/FSAL_LUSTRE/fsal_types.h"
#elif defined ( _USE_XFS )
#include "FSAL/FSAL_XFS/fsal_types.h"
#elif defined ( _USE_GPFS )
#include "FSAL/FSAL_GPFS/fsal_types.h"
#elif defined ( _USE_VFS )
#include "FSAL/FSAL_VFS/fsal_types.h"
#elif defined ( _USE_ZFS )
#include "FSAL/FSAL_ZFS/fsal_types.h"
#elif defined ( _USE_TEMPLATE ) /* <- place here your own define */
#include "FSAL/FSAL_TEMPLATE/fsal_types.h"
#else                           /* no _USE_<filesystem> flag ! */
#error "No filesystem compilation flag set for the FSAL."
#endif /* _USE_GHOSTFS */

#else /* _USE_SHARED_FSAL */
#define CONF_LABEL_FS_SPECIFIC   "DYNFSAL"

#endif                          /* _USE_SHARED_FSAL */

#include "fsal_glue.h"

/*---------  end of FS dependant definitions ---------*/

/** filesystem identifier */

typedef struct fsal_fsid__
{
  fsal_u64_t major;
  fsal_u64_t minor;
} fsal_fsid_t;

/** raw device spec */

typedef struct fsal_dev__
{
  fsal_uint_t major;
  fsal_uint_t minor;
} fsal_dev_t;

/* The maximum ACLs that a file can support */
/* #define FSAL_MAX_ACL  10 */
#define FSAL_MAX_ACL  2

/* constants for specifying which ACL types are supported  */

typedef fsal_ushort_t fsal_aclsupp_t;
#define FSAL_ACLSUPPORT_ALLOW 0x01
#define FSAL_ACLSUPPORT_DENY  0x02

/** ACE types */

typedef unsigned int fsal_acetype_t;

#define FSAL_ACE_TYPE_ALLOW 0
#define FSAL_ACE_TYPE_DENY  1
#define FSAL_ACE_TYPE_AUDIT 2
#define FSAL_ACE_TYPE_ALARM 3


/** ACE flag */

typedef unsigned int fsal_aceflag_t;

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

typedef fsal_uint_t fsal_aceperm_t;

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
    fsal_uid_t uid;
    fsal_gid_t gid;
  } who;

} fsal_ace_t;

typedef struct fsal_acl__
{
  fsal_uint_t naces;
  fsal_ace_t *aces;
  rw_lock_t lock;
  fsal_uint_t ref;
} fsal_acl_t;

typedef struct fsal_acl_data__
{
  fsal_uint_t naces;
  fsal_ace_t *aces;
} fsal_acl_data_t;

#ifndef _USE_NFS4_ACL
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

/** Modes constants */

#define FSAL_MODE_SUID  04000   /* set uid bit */
#define FSAL_MODE_SGID  02000   /* set gid bit */
#define FSAL_MODE_SVTX  01000   /* sticky bit  */

#define FSAL_MODE_RWXU  00700   /* all permissions for owner */

#define FSAL_MODE_RUSR  00400   /* read permission for owner */
#define FSAL_MODE_WUSR  00200   /* write permission for owner */
#define FSAL_MODE_XUSR  00100   /* exec permission for owner */

#define FSAL_MODE_RWXG  00070   /* all permissions for group */

#define FSAL_MODE_RGRP  00040   /* read permission for group */
#define FSAL_MODE_WGRP  00020   /* write permission for group */
#define FSAL_MODE_XGRP  00010   /* exec permission for group */

#define FSAL_MODE_RWXO  00007   /* all permissions for other */

#define FSAL_MODE_ROTH  00004   /* read permission for other */
#define FSAL_MODE_WOTH  00002   /* write permission for other */
#define FSAL_MODE_XOTH  00001   /* exec permission for other */

/**
 * Defines an attribute mask.
 * A mask is an inclusive OR between
 * some FSAL_ATTR_xxx values.
 */
typedef fsal_u64_t fsal_attrib_mask_t;

/**
 * Attribute masks.
 */

/* supported attributes */
#define FSAL_ATTR_SUPPATTR      ((fsal_attrib_mask_t) 0x0000000000000001LL )
/* file type */
#define FSAL_ATTR_TYPE          ((fsal_attrib_mask_t) 0x0000000000000002LL )
/* file size */
#define FSAL_ATTR_SIZE          ((fsal_attrib_mask_t) 0x0000000000000004LL )
/* filesystem id */
#define FSAL_ATTR_FSID          ((fsal_attrib_mask_t) 0x0000000000000008LL )
/* ACL */
#define FSAL_ATTR_ACL           ((fsal_attrib_mask_t) 0x0000000000000020LL )
/* file id */
#define FSAL_ATTR_FILEID        ((fsal_attrib_mask_t) 0x0000000000000040LL )
/* Access permission flag */
#define FSAL_ATTR_MODE          ((fsal_attrib_mask_t) 0x0000000000000080LL )
/* Number of hard links */
#define FSAL_ATTR_NUMLINKS      ((fsal_attrib_mask_t) 0x0000000000000100LL )
/* owner ID */
#define FSAL_ATTR_OWNER         ((fsal_attrib_mask_t) 0x0000000000000200LL )
/* group ID */
#define FSAL_ATTR_GROUP         ((fsal_attrib_mask_t) 0x0000000000000400LL )
/* ID of device for block special or character special files*/
#define FSAL_ATTR_RAWDEV        ((fsal_attrib_mask_t) 0x0000000000000800LL )
/* Access time */
#define FSAL_ATTR_ATIME         ((fsal_attrib_mask_t) 0x0000000000001000LL )
/* Creation time */
#define FSAL_ATTR_CREATION      ((fsal_attrib_mask_t) 0x0000000000002000LL )
/* Metadata modification time */
#define FSAL_ATTR_CTIME         ((fsal_attrib_mask_t) 0x0000000000004000LL )
/* data modification time */
#define FSAL_ATTR_MTIME         ((fsal_attrib_mask_t) 0x0000000000008000LL )
/* space used by this file. */
#define FSAL_ATTR_SPACEUSED     ((fsal_attrib_mask_t) 0x0000000000010000LL )
/* Mounted on fileid */
#define FSAL_ATTR_MOUNTFILEID   ((fsal_attrib_mask_t) 0x0000000000020000LL )

/* NFS4 change_time like attribute */
#define FSAL_ATTR_CHGTIME        ((fsal_attrib_mask_t) 0x0000000000040000LL )

/* This bit indicates that an error occured during getting object attributes */
#define FSAL_ATTR_RDATTR_ERR    ((fsal_attrib_mask_t) 0x8000000000000000LL )

/* Generation number */
#define FSAL_ATTR_GENERATION    ((fsal_attrib_mask_t) 0x0000000000080000LL )

/* Change attribute */
#define FSAL_ATTR_CHANGE    ((fsal_attrib_mask_t) 0x0000000000100000LL )


/* "classic" attributes sets : */

/* NFSv4 Mandatory attributes */

#define FSAL_ATTRS_MANDATORY  ( FSAL_ATTR_SUPPATTR |  FSAL_ATTR_TYPE | \
                                FSAL_ATTR_SIZE     |  FSAL_ATTR_FSID | FSAL_ATTR_CHGTIME )

/* attributes that are returned by the POSIX stat function */

#define FSAL_ATTRS_POSIX  ( FSAL_ATTR_MODE |  FSAL_ATTR_FILEID |  \
                            FSAL_ATTR_FSID |  FSAL_ATTR_RAWDEV |  \
                            FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER |\
                            FSAL_ATTR_GROUP | FSAL_ATTR_SIZE  |   \
                            FSAL_ATTR_ATIME | FSAL_ATTR_MTIME |   \
                            FSAL_ATTR_CTIME | FSAL_ATTR_SPACEUSED )

/** A list of FS object's attributes. */

typedef struct fsal_attrib_list__
{

  fsal_attrib_mask_t asked_attributes;      /**< Indicates the attributes
                                             *   to be got or set,
                                             *   i.e. significative fields
                                             *   in this structure.
                                             */

  fsal_attrib_mask_t supported_attributes;
  fsal_nodetype_t type;
  fsal_size_t filesize;
  fsal_fsid_t fsid;
  fsal_acl_t *acl;
  fsal_u64_t fileid;
  fsal_accessmode_t mode;
  fsal_count_t numlinks;
  fsal_uid_t owner;
  fsal_gid_t group;
  fsal_dev_t rawdev;
  fsal_time_t atime;
  fsal_time_t creation;
  fsal_time_t ctime;
  fsal_time_t mtime;
  fsal_time_t chgtime;
  fsal_size_t spaceused;
  fsal_u64_t change;
  fsal_u64_t mounted_on_fileid;

} fsal_attrib_list_t;

/** A list of FS object's extended attributes (like generation numbers or creation time). */

typedef struct fsal_extattrib_list__
{
 fsal_attrib_mask_t asked_attributes;      /**< Indicates the attributes
                                            * to be got or set,
                                            * i.e. significative fields
                                            *  in this structure.
                                            */
 fsal_uint_t   generation ;
} fsal_extattrib_list_t ;

/** Mask for permission testing. Both mode and ace4 mask are encoded. */

typedef fsal_uint_t fsal_accessflags_t;

#define	FSAL_OWNER_OK   0x08000000       /* Allow owner override */
#define	FSAL_R_OK	0x04000000       /* Test for Read permission */
#define	FSAL_W_OK	0x02000000       /* Test for Write permission */
#define	FSAL_X_OK	0x01000000       /* Test for execute permission */
#define	FSAL_F_OK	0x10000000       /* Test for existence of File */
#define FSAL_ACCESS_OK  0x00000000       /* Allow */

#define FSAL_ACCESS_FLAG_BIT_MASK  0x80000000
#define FSAL_MODE_BIT_MASK         0x7F000000
#define FSAL_ACE4_BIT_MASK         0x00FFFFFF

#define FSAL_MODE_MASK(access)     (access & FSAL_MODE_BIT_MASK)
#define FSAL_ACE4_MASK(access)     (access & FSAL_ACE4_BIT_MASK)

#define FSAL_MODE_MASK_FLAG        0x00000000
#define FSAL_ACE4_MASK_FLAG        0x80000000

#define FSAL_MODE_MASK_SET(access) (access | FSAL_MODE_MASK_FLAG)
#define FSAL_ACE4_MASK_SET(access) (access | FSAL_ACE4_MASK_FLAG)

#define IS_FSAL_MODE_MASK_VALID(access)  ((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_MODE_MASK_FLAG)
#define IS_FSAL_ACE4_MASK_VALID(access)  ((access & FSAL_ACCESS_FLAG_BIT_MASK) == FSAL_ACE4_MASK_FLAG)

#define IS_FSAL_DIR(filetype)  (filetype == FSAL_TYPE_DIR)

#define FSAL_WRITE_ACCESS (FSAL_MODE_MASK_SET(FSAL_W_OK) | \
                           FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA | \
                                              FSAL_ACE_PERM_APPEND_DATA))
#define FSAL_READ_ACCESS (FSAL_MODE_MASK_SET(FSAL_R_OK) | \
                          FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_DATA))


/** directory entry */

typedef struct fsal_dirent__
{

  fsal_handle_t handle;             /**< directory entry handle. */
  fsal_name_t name;                 /**< directory entry name.   */
  fsal_cookie_t cookie;             /**< cookie for reading dir
                                         from this entry         */
  fsal_attrib_list_t attributes;    /**< entry attributes        */
  struct fsal_dirent__ *nextentry;  /**< pointer to the next entry*/

} fsal_dirent_t;

/** FSAL_open behavior. */

typedef fsal_ushort_t fsal_openflags_t;

#define FSAL_O_RDONLY   0x0001  /* read only  */
#define FSAL_O_RDWR     0x0002  /* read/write */
#define FSAL_O_WRONLY   0x0004  /* write only */
#define FSAL_O_APPEND   0x0008  /* append     */
#define FSAL_O_TRUNC    0x0010  /* truncate   */
#define FSAL_O_CREATE   0x0020  /* truncate   */

/** Describes an absolute or relative
 *  position in a file.
 *
 * FSAL_SEEK_SET
 * Set position equal to offset bytes.
 * FSAL_SEEK_CUR
 * Set position to current location plus offset.
 * FSAL_SEEK_END
 * Set position to EOF plus offset.
 */
typedef enum fsal_seektype__
{
  FSAL_SEEK_SET,
  FSAL_SEEK_CUR,
  FSAL_SEEK_END
} fsal_seektype_t;

typedef struct fsal_seek__
{
  fsal_seektype_t whence;
  fsal_off_t offset;
} fsal_seek_t;

/** FH expire type (mask). */

typedef fsal_ushort_t fsal_fhexptype_t;

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

#define SET_INTEGER_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      cfg._field = (p_init_info)->values._field;                  \
      break;                                                      \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( cfg._field > (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( cfg._field < (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

#define SET_BITMAP_PARAM( cfg, p_init_info, _field )              \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a bit AND */                                     \
      cfg._field &= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a bit OR */                                      \
      cfg._field |= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

#define SET_BOOLEAN_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a boolean AND */                                 \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a boolean OR */                                  \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

struct fsal_staticfsinfo_t
{

  fsal_size_t maxfilesize;          /**< maximum allowed filesize     */
  fsal_count_t maxlink;             /**< maximum hard links on a file */
  fsal_mdsize_t maxnamelen;         /**< maximum name length          */
  fsal_mdsize_t maxpathlen;         /**< maximum path length          */
  fsal_boolean_t no_trunc;          /**< is it errorneous when name>maxnamelen ?*/
  fsal_boolean_t chown_restricted;  /**< is chown limited to super-user.*/
  fsal_boolean_t case_insensitive;  /**< case insensitive FS ?          */
  fsal_boolean_t case_preserving;   /**< FS preserves case ?            */
  fsal_fhexptype_t fh_expire_type;  /**< handle persistency indicator   */
  fsal_boolean_t link_support;      /**< FS supports hardlinks ?        */
  fsal_boolean_t symlink_support;   /**< FS supports symlinks  ?        */
  fsal_boolean_t lock_support;             /**< FS supports file locking ?     */
  fsal_boolean_t lock_support_owner;       /**< FS supports lock owners ?    */
  fsal_boolean_t lock_support_async_block; /**< FS supports async blocking locks ? */
  fsal_boolean_t named_attr;        /**< FS supports named attributes.  */
  fsal_boolean_t unique_handles;    /**< Handles are unique and persistent.*/
  fsal_time_t lease_time;           /**< Duration of lease at FS in seconds */
  fsal_aclsupp_t acl_support;       /**< what type of ACLs are supported */
  fsal_boolean_t cansettime;        /**< Is it possible to change file times
                                       using SETATTR. */
  fsal_boolean_t homogenous;        /**< Are supported attributes the same
                                       for all objects of this filesystem. */

  fsal_attrib_mask_t supported_attrs;  /**< If the FS is homogenous, this
                                            indicates the set of supported
                                            attributes. */

  fsal_size_t maxread;              /**< Max read size */
  fsal_size_t maxwrite;             /**< Max write size */

  fsal_accessmode_t umask;          /**< This mask is applied to the mode
                                        of created objects */

  fsal_boolean_t auth_exportpath_xdev;  /**< This flag indicates weither
                                         *  it is possible to cross junctions
                                         *  for resolving an NFS export path.
                                         */

  fsal_accessmode_t xattr_access_rights;  /**< This indicates who is allowed
                                           *   to read/modify xattrs value.
                                           */

};

/** File system dynamic info. */

typedef struct fsal_dynamicfsinfo__
{

  fsal_size_t total_bytes;
  fsal_size_t free_bytes;
  fsal_size_t avail_bytes;
  fsal_u64_t total_files;
  fsal_u64_t free_files;
  fsal_u64_t avail_files;
  fsal_time_t time_delta;

} fsal_dynamicfsinfo_t;

/** Defines the direction for the rcp operation */

typedef fsal_ushort_t fsal_rcpflag_t;

/* Copies the file from the filesystem to a local path. */
#define  FSAL_RCP_FS_TO_LOCAL 0x0001

/* Copies the file from local path to the filesystem. */
#define  FSAL_RCP_LOCAL_TO_FS 0x0002

/* create the target local file if it doesn't exist */
#define  FSAL_RCP_LOCAL_CREAT 0x0010

/* error if the target local file already exists */
#define  FSAL_RCP_LOCAL_EXCL 0x0020

/** Configuration info for every type of filesystem. */

typedef struct fs_common_initinfo__
{

  /* specifies the behavior for each init value. */
  struct _behavior_
  {
    fsal_initflag_t
        maxfilesize, maxlink, maxnamelen, maxpathlen,
        no_trunc, chown_restricted, case_insensitive,
        case_preserving, fh_expire_type, link_support, symlink_support, lock_support,
        lock_support_owner, lock_support_async_block,
        named_attr, unique_handles, lease_time, acl_support, cansettime,
        homogenous, supported_attrs, maxread, maxwrite, umask,
        auth_exportpath_xdev, xattr_access_rights;
  } behaviors;

  /* specifies the values to be set if behavior <> FSAL_INIT_FS_DEFAULT */
  fsal_staticfsinfo_t values;

} fs_common_initinfo_t;

/** Configuration info for the fsal */

typedef struct fsal_init_info__
{
  unsigned int max_fs_calls;  /**< max number of FS calls. 0 = infinite */
} fsal_init_info_t;

/** FSAL_Init parameter. */

typedef struct fsal_parameter__
{

  fsal_init_info_t fsal_info;               /**< fsal configuration info  */
  fs_common_initinfo_t fs_common_info;      /**< filesystem common info   */
  fs_specific_initinfo_t fs_specific_info;  /**< filesystem dependant info*/

} fsal_parameter_t;

/** Statistics about the use of the Filesystem abstraction layer. */

typedef struct fsal_statistics__
{
  struct func_fsak_stats__
  {
    unsigned int nb_call[FSAL_NB_FUNC];            /**< Total number of calls to fsal              */
    unsigned int nb_success[FSAL_NB_FUNC];         /**< Total number of successfull calls          */
    unsigned int nb_err_retryable[FSAL_NB_FUNC];   /**< Total number of failed/retryable calls     */
    unsigned int nb_err_unrecover[FSAL_NB_FUNC];   /**< Total number of failed/unrecoverable calls */
  } func_stats;

} fsal_statistics_t;

/** Status of FSAL operations */

typedef struct fsal_status__
{
  fsal_errors_t major;    /**< major error code */
  int           minor;    /**< minor error code */
} fsal_status_t;

/* No error constant */
static fsal_status_t __attribute__ ((__unused__)) FSAL_STATUS_NO_ERROR =
{
ERR_FSAL_NO_ERROR, 0};

/** Buffer descriptor similar to utf8 strings. */

typedef struct fsal_buffdesc__
{
  unsigned int len;
  char *pointer;
} fsal_buffdesc_t;

/* quotas */
typedef struct fsal_quota__
{
  u_int bhardlimit;
  u_int bsoftlimit;
  u_int curblocks;
  u_int fhardlimit;
  u_int fsoftlimit;
  u_int curfiles;
  u_int btimeleft;
  u_int ftimeleft;
  u_int bsize;
} fsal_quota_t;

/* output digest sizes */

#define FSAL_DIGEST_SIZE_HDLV2 29
#define FSAL_DIGEST_SIZE_HDLV3 61

#ifdef _USE_PROXY
#define FSAL_DIGEST_SIZE_HDLV4 108 
#else
#define FSAL_DIGEST_SIZE_HDLV4 61 
#endif /* _USE_FSAL_PROXY */



#define FSAL_DIGEST_SIZE_FILEID2 (sizeof(int))
#define FSAL_DIGEST_SIZE_FILEID3 (sizeof(fsal_u64_t))
#define FSAL_DIGEST_SIZE_FILEID4 (sizeof(fsal_u64_t))

#if defined(_USE_HPSS) || defined(_USE_POSIX)
  /* In HPSS, the file handle contains object type. */
#define FSAL_DIGEST_SIZE_NODETYPE (sizeof(fsal_nodetype_t))

#endif

/* digest types  */

typedef enum fsal_digesttype_t
{

  /* NFS handles */
  FSAL_DIGEST_NFSV2,
  FSAL_DIGEST_NFSV3,
  FSAL_DIGEST_NFSV4,

  /* unic file identifier (for digest only) */
  FSAL_DIGEST_FILEID2,
  FSAL_DIGEST_FILEID3,
  FSAL_DIGEST_FILEID4
#if defined(_USE_HPSS) || defined(_USE_POSIX)
      /* In HPSS, the object type can be
       * extracted from the handle.
       */
      , FSAL_DIGEST_NODETYPE
#endif
} fsal_digesttype_t;

typedef enum fsal_lock_op_t
{
  FSAL_OP_LOCKT,  /* test if this lock may be applied      */
  FSAL_OP_LOCK,   /* request a non-blocking lock           */
  FSAL_OP_LOCKB,  /* request a blocking lock         (NEW) */
  FSAL_OP_UNLOCK, /* release a lock                        */
  FSAL_OP_CANCEL  /* cancel a blocking lock          (NEW) */

} fsal_lock_op_t;

typedef enum fsal_lock_t
{
  FSAL_LOCK_R,
  FSAL_LOCK_W,
  FSAL_NO_LOCK
} fsal_lock_t;

typedef struct fsal_lock_param_t
{
  fsal_lock_t         lock_type;
  fsal_size_t         lock_start;
  fsal_size_t         lock_length;
  pid_t               lock_owner;
} fsal_lock_param_t;

#endif                          /* _FSAL_TYPES_H */
