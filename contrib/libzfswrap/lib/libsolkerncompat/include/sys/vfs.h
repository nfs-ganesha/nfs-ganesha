/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifndef _SYS_VFS_H
#define _SYS_VFS_H

#include <sys/types.h>
#include <sys/refstr.h>
#include <sys/cred.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>

typedef struct vfsops vfsops_t;

#define FUSE_VFS_HAS_DEFAULT_PERM 1

/* Please look at vfs_init() if you change this structure */
typedef struct vfs {
	struct vfs   *vfs_next;
	struct vfs   *vfs_prev;

	vfsops_t     *vfs_op;    /* operations on VFS */
	struct vnode *vfs_vnodecovered;
	uint_t        vfs_flag;
	uint_t        vfs_bsize;
	int           vfs_fstype;
	void         *vfs_data;
	dev_t         vfs_dev;
	ulong_t       vfs_bcount;
	uint_t        vfs_count;
	refstr_t     *vfs_resource;
	int	      fuse_attribute;
} vfs_t;

/*
 * Argument structure for mount(2).
 *
 * Flags are defined in <sys/mount.h>.
 *
 * Note that if the MS_SYSSPACE bit is set in flags, the pointer fields in
 * this structure are to be interpreted as kernel addresses.  File systems
 * should be prepared for this possibility.
 */
struct mounta {
	char *spec;
	char *dir;
	int   flags;
	char *fstype;
	char *dataptr;
	int   datalen;
	char *optptr;
	int   optlen;
};

extern struct vfs *rootvfs;

extern void vfs_list_lock();
extern void vfs_list_read_lock();
extern void vfs_list_unlock();

extern void vfs_hold(vfs_t *);
extern void vfs_rele(vfs_t *);
extern void fs_freevfs(vfs_t *);

extern int dounmount(struct vfs *, int, cred_t *);

#define VFS_INIT(vfsp, vfsops, data) ((vfsp)->vfs_op = (vfsops))
#define VFS_HOLD(vfsp) vfs_hold(vfsp)
#define VFS_RELE(vfsp) vfs_rele(vfsp)

#define vfs_devismounted(dev) (0)
#define vfs_clearmntopt(vfs,opt) ((void) 0)
#define vfs_setmntopt(vfs,opt,a,b) ((void) 0)
#define vfs_optionisset(vfs,opt,a) (0)
#define vfs_set_feature(vfs,f) ((void) 0)

/*
 * VFS flags.
 */
#define VFS_RDONLY     0x01   /* read-only vfs */
#define VFS_MLOCK      0x02   /* lock vfs so that subtree is stable */
#define VFS_MWAIT      0x04   /* someone is waiting for lock */
#define VFS_NOSETUID   0x08   /* setuid disallowed */
#define VFS_REMOUNT    0x10   /* modify mount options only */
#define VFS_NOTRUNC    0x20   /* does not truncate long file names */
#define VFS_UNLINKABLE 0x40   /* unlink(2) can be applied to root */
#define VFS_PXFS       0x80   /* clustering: global fs proxy vfs */
#define VFS_UNMOUNTED  0x100  /* file system has been unmounted */
#define VFS_NBMAND     0x200  /* allow non-blocking mandatory locks */
#define VFS_XATTR      0x400  /* fs supports extended attributes */
#define VFS_NODEVICES  0x800  /* device-special files disallowed */
#define VFS_NOEXEC     0x1000 /* executables disallowed */
#define VFS_STATS      0x2000 /* file system can collect stats */
#define VFS_XID        0x4000 /* file system supports extended ids */

#define VFS_NORESOURCE  "unspecified_resource"
#define VFS_NOMNTPT     "unspecified_mountpoint"

/*
 * VFS features are implemented as bits set in the vfs_t.
 * The vfs_feature_t typedef is a 64-bit number that will translate
 * into an element in an array of bitmaps and a bit in that element.
 * Developers must not depend on the implementation of this and
 * need to use vfs_has_feature()/vfs_set_feature() routines.
 */
typedef uint64_t        vfs_feature_t;

#define VFSFT_XVATTR            0x100000001     /* Supports xvattr for attrs */
#define VFSFT_CASEINSENSITIVE   0x100000002     /* Supports case-insensitive */
#define VFSFT_NOCASESENSITIVE   0x100000004     /* NOT case-sensitive */
#define VFSFT_DIRENTFLAGS       0x100000008     /* Supports dirent flags */
#define VFSFT_ACLONCREATE       0x100000010     /* Supports ACL on create */
#define VFSFT_ACEMASKONACCESS   0x100000020     /* Can use ACEMASK for access */
#define	VFSFT_SYSATTR_VIEWS	0x100000040	/* Supports sysattr view i/f */
#define	VFSFT_ACCESS_FILTER	0x100000080	/* dirents filtered by access */


/*
 * File identifier.  Should be unique per filesystem on a single
 * machine.  This is typically called by a stateless file server
 * in order to generate "file handles".
 *
 * Do not change the definition of struct fid ... fid_t without
 * letting the CacheFS group know about it!  They will have to do at
 * least two things, in the same change that changes this structure:
 *   1. change CFSVERSION in usr/src/uts/common/sys/fs/cachefs_fs.h
 *   2. put the old version # in the canupgrade array
 *	in cachfs_upgrade() in usr/src/cmd/fs.d/cachefs/fsck/fsck.c
 * This is necessary because CacheFS stores FIDs on disk.
 *
 * Many underlying file systems cast a struct fid into other
 * file system dependent structures which may require 4 byte alignment.
 * Because a fid starts with a short it may not be 4 byte aligned, the
 * fid_pad will force the alignment.
 */
#define MAXFIDSZ     64
#define OLD_MAXFIDSZ 16

typedef struct fid {
	union {
		long fid_pad;
		struct {
			ushort_t len;            /* length of data in bytes */
			char     data[MAXFIDSZ]; /* data (variable len) */
		} _fid;
	} un;
} fid_t;

#define fid_len  un._fid.len
#define fid_data un._fid.data

/*
 * Reasons for calling the vfs_mountroot() operation.
 */
enum whymountroot { ROOT_INIT, ROOT_REMOUNT, ROOT_UNMOUNT};
typedef enum whymountroot whymountroot_t;

/*
 * Reasons for calling the VFS_VNSTATE():
 */
enum vntrans {
	VNTRANS_EXISTS,
	VNTRANS_IDLED,
	VNTRANS_RECLAIMED,
	VNTRANS_DESTROYED
};
typedef enum vntrans vntrans_t;

/*
 * VFS_OPS defines all the vfs operations.  It is used to define
 * the vfsops structure (below) and the fs_func_p union (vfs_opreg.h).
 */
#define	VFS_OPS								\
	int	(*vfs_mount)(vfs_t *, vnode_t *, struct mounta *, cred_t *); \
	int	(*vfs_unmount)(vfs_t *, int, cred_t *);			\
	int	(*vfs_root)(vfs_t *, vnode_t **);			\
	int	(*vfs_statvfs)(vfs_t *, statvfs64_t *);			\
	int	(*vfs_sync)(vfs_t *, short, cred_t *);			\
	int	(*vfs_vget)(vfs_t *, vnode_t **, fid_t *);		\
	int	(*vfs_mountroot)(vfs_t *, enum whymountroot);		\
	void	(*vfs_freevfs)(vfs_t *);				\
	int	(*vfs_vnstate)(vfs_t *, vnode_t *, vntrans_t)	/* NB: No ";" */

/*
 * Operations supported on virtual file system.
 */
struct vfsops {
	VFS_OPS;	/* Signature of all vfs operations (vfsops) */
};

extern int  fsop_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
extern int  fsop_unmount(vfs_t *, int, cred_t *);
extern int  fsop_statfs(vfs_t *, statvfs64_t *);
extern void fsop_freefs(vfs_t *);
extern int  fsop_sync(vfs_t *, short, cred_t *);

#define VFS_MOUNT(vfsp, mvp, uap, cr) fsop_mount(vfsp, mvp, uap, cr)
#define VFS_UNMOUNT(vfsp, flag, cr)   fsop_unmount(vfsp, flag, cr)
#define VFS_STATVFS(vfsp, sp)         fsop_statfs(vfsp, sp)
#define VFS_FREEVFS(vfsp)             fsop_freefs(vfsp)
#define VFS_SYNC(vfsp, flag, cr)      fsop_sync(vfsp, flag, cr)

#define VFSNAME_MOUNT     "mount"
#define VFSNAME_UNMOUNT   "unmount"
#define VFSNAME_ROOT      "root"
#define VFSNAME_STATVFS   "statvfs"
#define VFSNAME_SYNC      "sync"
#define VFSNAME_VGET      "vget"
#define VFSNAME_MOUNTROOT "mountroot"
#define VFSNAME_FREEVFS   "freevfs"
#define VFSNAME_VNSTATE   "vnstate"

/*
 * Filesystem type switch table.
 */

typedef struct vfssw {
	char *vsw_name;                /* type name -- max len _ST_FSTYPSZ */
	vfsops_t vsw_vfsops;           /* filesystem operations vector */
} vfssw_t;

extern struct vfssw vfssw[]; /* table of filesystem types */

#define zfstype 1
#define nfstype 2

#endif
