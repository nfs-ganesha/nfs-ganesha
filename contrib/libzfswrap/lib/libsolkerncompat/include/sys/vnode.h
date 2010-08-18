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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifndef _SYS_VNODE_H
#define _SYS_VNODE_H

#include <sys/types.h>
#include <sys/rwstlock.h>
#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/kmem.h>
#include <vm/seg_enum.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/taskq.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern kmem_cache_t *vnode_cache;

typedef int (*fs_generic_func_p) ();	/* Generic vop/vfsop/femop/fsemop ptr */

typedef struct vn_vfslocks_entry {
	rwslock_t ve_lock;
} vn_vfslocks_entry_t;

/*
 * vnode types.  VNON means no type.  These values are unrelated to
 * values in on-disk inodes.
 */
typedef enum vtype {
	VNON  = 0,
	VREG  = 1,
	VDIR  = 2,
	VBLK  = 3,
	VCHR  = 4,
	VLNK  = 5,
	VFIFO = 6,
	VDOOR = 7,
	VPROC = 8,
	VSOCK = 9,
	VPORT = 10,
	VBAD  = 11
} vtype_t;

/*
 * vnode flags.
 */
#define VROOT      0x01   /* root of its file system */
#define VNOCACHE   0x02   /* don't keep cache pages on vnode */
#define VNOMAP     0x04   /* file cannot be mapped/faulted */
#define VDUP       0x08   /* file should be dup'ed rather then opened */
#define VNOSWAP    0x10   /* file cannot be used as virtual swap device */
#define VNOMOUNT   0x20   /* file cannot be covered by mount */
#define VISSWAP    0x40   /* vnode is being used for swap */
#define VSWAPLIKE  0x80   /* vnode acts like swap (but may not be) */

#define V_XATTRDIR 0x4000 /* attribute unnamed directory */
#define VMODSORT   0x10000

/*
 * Flags for VOP_LOOKUP
 */
#define LOOKUP_DIR       0x01 /* want parent dir vp */
#define LOOKUP_XATTR     0x02 /* lookup up extended attr dir */
#define CREATE_XATTR_DIR 0x04 /* Create extended attr dir */

/*
 * Flags for VOP_READDIR
 */
#define V_RDDIR_ENTFLAGS 0x01 /* request dirent flags */
#define	V_RDDIR_ACCFILTER	0x02	/* filter out inaccessible dirents */

/*
 * Flags for VOP_RWLOCK/VOP_RWUNLOCK
 * VOP_RWLOCK will return the flag that was actually set, or -1 if none.
 */
#define V_WRITELOCK_TRUE  (1) /* Request write-lock on the vnode */
#define V_WRITELOCK_FALSE (0) /* Request read-lock on the vnode */

/*
 *  Modes.  Some values same as S_xxx entries from stat.h for convenience.
 */
#define VSUID    04000 /* set user id on execution */
#define VSGID    02000 /* set group id on execution */
#define VSVTX    01000 /* save swapped text even after use */

/*
 * Permissions.
 */
#define VREAD    00400
#define VWRITE   00200
#define VEXEC    00100

#define MODEMASK 07777
#define PERMMASK 00777

/*
 * VOP_ACCESS flags
 */
#define V_ACE_MASK      0x1     /* mask represents  NFSv4 ACE permissions */
#define V_APPEND        0x2     /* want to do append only check */

/*
 * Check whether mandatory file locking is enabled.
 */

#define MANDMODE(mode)     (((mode) & (VSGID|(VEXEC>>3))) == VSGID)
#define MANDLOCK(vp, mode) ((vp)->v_type == VREG && MANDMODE(mode))

#define IS_SWAPVP(vp) (((vp)->v_flag & (VISSWAP | VSWAPLIKE)) != 0)
#define IS_DEVVP(vp) \
	((vp)->v_type == VCHR || (vp)->v_type == VBLK || (vp)->v_type == VFIFO)

/* Please look at vfs_init() if you change this structure */
typedef struct vnode {
	kmutex_t             v_lock;      /* protects vnode fields */
	uint_t               v_flag;      /* vnode flags (see below) */
	struct vfs          *v_vfsp;      /* ptr to containing VFS */
	vn_vfslocks_entry_t  v_vfsmhlock; /* protects v_vfsmountedhere */
	int                  v_fd;
	uint64_t             v_size;
	char                *v_path;      /* cached path */
	uint_t               v_rdcnt;     /* open for read count  (VREG only) */
	uint_t               v_wrcnt;     /* open for write count (VREG only) */
	void                *v_data;      /* private data for fs */
	uint_t               v_count;     /* reference count */
	enum vtype           v_type;      /* vnode type */
	dev_t                v_rdev;      /* device (VCHR, VBLK) */
	struct vnodeops      *v_op;       /* vnode operations */
	struct stat64        v_stat;      /* stat info */
} vnode_t;

typedef struct vattr {
	uint_t       va_mask;    /* bit-mask of attributes */
	vtype_t      va_type;    /* vnode type (for create) */
	mode_t       va_mode;    /* file access mode */
	uid_t        va_uid;     /* owner user id */
	gid_t        va_gid;     /* owner group id */
	dev_t        va_fsid;    /* file system id (dev for now) */
	u_longlong_t va_nodeid;  /* node id */
	nlink_t      va_nlink;   /* number of references to file */
	u_offset_t   va_size;    /* file size in bytes */
	timestruc_t  va_atime;   /* time of last access */
	timestruc_t  va_mtime;   /* time of last modification */
	timestruc_t  va_ctime;   /* time of last status change */
	dev_t        va_rdev;    /* device the file represents */
	uint_t       va_blksize; /* fundamental block size */
	u_longlong_t va_nblocks; /* # of blocks allocated */
	uint_t       va_seq;     /* sequence number */
} vattr_t;

/*
 * Structure used on VOP_GETSECATTR and VOP_SETSECATTR operations
 */

typedef struct vsecattr {
	uint_t		vsa_mask;	/* See below */
	int		vsa_aclcnt;	/* ACL entry count */
	void		*vsa_aclentp;	/* pointer to ACL entries */
	int		vsa_dfaclcnt;	/* default ACL entry count */
	void		*vsa_dfaclentp;	/* pointer to default ACL entries */
	size_t		vsa_aclentsz;	/* ACE size in bytes of vsa_aclentp */
	uint_t		vsa_aclflags;	/* ACE ACL flags */
} vsecattr_t;

/* vsa_mask values */
#define VSA_ACL                 0x0001
#define VSA_ACLCNT              0x0002
#define VSA_DFACL               0x0004
#define VSA_DFACLCNT            0x0008
#define VSA_ACE                 0x0010
#define VSA_ACECNT              0x0020
#define VSA_ACE_ALLTYPES        0x0040
#define VSA_ACE_ACLFLAGS        0x0080  /* get/set ACE ACL flags */

typedef int caller_context_t;

/*
 * Structure tags for function prototypes, defined elsewhere.
 */
struct pathname;
struct fid;
struct flock64;
struct flk_callback;
struct shrlock;
struct page;
struct seg;
struct as;
struct pollhead;

#define AT_TYPE    0x0001
#define AT_MODE    0x0002
#define AT_UID     0x0004
#define AT_GID     0x0008
#define AT_FSID    0x0010
#define AT_NODEID  0x0020
#define AT_NLINK   0x0040
#define AT_SIZE    0x0080
#define AT_ATIME   0x0100
#define AT_MTIME   0x0200
#define AT_CTIME   0x0400
#define AT_RDEV    0x0800
#define AT_BLKSIZE 0x1000
#define AT_NBLOCKS 0x2000
#define AT_SEQ     0x8000
#define AT_XVATTR  0x10000

#define AT_ALL   (AT_TYPE|AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|\
                 AT_NLINK|AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|\
                 AT_RDEV|AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

#define AT_STAT  (AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|AT_NLINK|\
                 AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|AT_RDEV|AT_TYPE)

#define AT_TIMES (AT_ATIME|AT_MTIME|AT_CTIME)
#define AT_NOSET (AT_NLINK|AT_RDEV|AT_FSID|AT_NODEID|AT_TYPE|\
                 AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

/*
 * Flags for vnode operations.
 */
enum rm { RMFILE, RMDIRECTORY };           /* rm or rmdir (remove) */
enum symfollow { NO_FOLLOW, FOLLOW };      /* follow symlinks (or not) */
enum vcexcl { NONEXCL, EXCL };             /* (non)excl create */
enum create { CRCREAT, CRMKNOD, CRMKDIR }; /* reason for create */

/*
 * Flags to VOP_SETATTR/VOP_GETATTR.
 */
#define ATTR_UTIME      0x01    /* non-default utime(2) request */
#define ATTR_EXEC       0x02    /* invocation from exec(2) */
#define ATTR_COMM       0x04    /* yield common vp attributes */
#define ATTR_HINT       0x08    /* information returned will be `hint' */
#define ATTR_REAL       0x10    /* yield attributes of the real vp */
#define ATTR_NOACLCHECK 0x20    /* Don't check ACL when checking permissions */
#define ATTR_TRIGGER    0x40    /* Mount first if vnode is a trigger mount */

/* Vnode Events - Used by VOP_VNEVENT */
typedef enum vnevent	{
	VE_SUPPORT	= 0,	/* Query */
	VE_RENAME_SRC	= 1,	/* Rename, with vnode as source */
	VE_RENAME_DEST	= 2,	/* Rename, with vnode as target/destination */
	VE_REMOVE	= 3,	/* Remove of vnode's name */
	VE_RMDIR	= 4	/* Remove of directory vnode's name */
} vnevent_t;

typedef enum rm        rm_t;
typedef enum symfollow symfollow_t;
typedef enum vcexcl    vcexcl_t;
typedef enum create    create_t;

extern int vn_vfswlock(vnode_t *);
extern void vn_vfsunlock(vnode_t *vp);
/*
 * I don't think fancy hash tables are needed in zfs-fuse
 */
#define vn_vfslocks_getlock(vn)       (&(vn)->v_vfsmhlock)
#define vn_vfslocks_getlock_vnode(vn) vn_vfslocks_getlock(vn)
#define vn_vfslocks_rele(x)           ((void) (0))

#if 0
#define VOP_GETATTR(vp, vap, fl, cr)    ((vap)->va_size = (vp)->v_size, 0)
#define VOP_FSYNC(vp, f, cr)            fsync((vp)->v_fd)
#define VOP_PUTPAGE(vp, of, sz, fl, cr) 0
#define VOP_CLOSE(vp, f, c, o, cr)      0
#endif

#define VN_RELE(vp)                     vn_rele(vp)
#define	VN_RELE_ASYNC(vp, taskq)	{ \
	vn_rele_async(vp, taskq); \
}

#define	VN_HOLD(vp) { \
	mutex_enter(&(vp)->v_lock); \
	(vp)->v_count++; \
	mutex_exit(&(vp)->v_lock); \
}

extern vnode_t *vn_alloc(int kmflag);
extern void vn_reinit(vnode_t *vp);
extern void vn_recycle(vnode_t *vp);
extern void vn_free(vnode_t *vp);
extern void vn_rele(vnode_t *vp);
extern void vn_rele_async(struct vnode *vp, struct taskq *taskq);

extern int vn_open(char *pnamep, enum uio_seg seg, int filemode, int createmode, struct vnode **vpp, enum create crwhy, mode_t umask);
extern int vn_openat(char *pnamep, enum uio_seg seg, int filemode, int createmode, struct vnode **vpp, enum create crwhy, mode_t umask, struct vnode *startvp, int fd);
extern int vn_rdwr(enum uio_rw rw, struct vnode *vp, caddr_t base, ssize_t len, offset_t offset, enum uio_seg seg, int ioflag, rlim64_t ulimit, cred_t *cr, ssize_t *residp);
extern void vn_close(vnode_t *vp);

/* ZFSFUSE */
extern int vn_fromfd(int fd, char *path, int flags, struct vnode **vpp, boolean_t fromfd);

#define vn_invalid(vp)           ((void) 0)
#define vn_has_cached_data(v)    (0)
#define vn_in_dnlc(v)            (0)

/* FIXME */
#define vn_remove(path,x1,x2)    remove(path)
#define vn_rename(from,to,seg)   rename((from), (to))
#define vn_exists(vp)            ((void) 0)

void vn_renamepath(vnode_t *dvp, vnode_t *vp, const char *nm, size_t len);

/* Vnode event notification */
/* Not implemented in zfs-fuse */
#define vnevent_rename_src(v,v2,c,ct)  ((void) 0)
#define vnevent_rename_dest(v,v2,c,ct) ((void) 0)
#define vnevent_rename_dest_dir(v,ct)  ((void) 0)
#define vnevent_remove(v,v2,c,ct)      ((void) 0)
#define vnevent_rmdir(v,v2,c,ct)       ((void) 0)
#define vnevent_create(v,ct)           ((void) 0)
#define vnevent_link(v,ct)             ((void) 0)
#define vnevent_support(v)             (EINVAL)

#if 0
#define vn_setops(vn,ops)        ((void) 0)
#define vn_make_ops(a,b,c)       (0)
#endif

/* FIXME FIXME FIXME */
#define vn_ismntpt(vp) B_FALSE

struct vnodeops;

extern int vn_is_readonly(vnode_t *vp);
extern void vn_setops(vnode_t *vp, struct vnodeops *vnodeops);

#ifdef	_KERNEL

/*
 * VNODE_OPS defines all the vnode operations.  It is used to define
 * the vnodeops structure (below) and the fs_func_p union (vfs_opreg.h).
 */
#define	VNODE_OPS							\
	int	(*vop_open)(vnode_t **, int, cred_t *,			\
				caller_context_t *);			\
	int	(*vop_close)(vnode_t *, int, int, offset_t, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_read)(vnode_t *, uio_t *, int, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_write)(vnode_t *, uio_t *, int, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_ioctl)(vnode_t *, int, intptr_t, int, cred_t *,	\
				int *, caller_context_t *);		\
	int	(*vop_setfl)(vnode_t *, int, int, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_getattr)(vnode_t *, vattr_t *, int, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_setattr)(vnode_t *, vattr_t *, int, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_access)(vnode_t *, int, int, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_lookup)(vnode_t *, char *, vnode_t **,		\
				struct pathname *,			\
				int, vnode_t *, cred_t *,		\
				caller_context_t *, int *,		\
				struct pathname *);			\
	int	(*vop_create)(vnode_t *, char *, vattr_t *, vcexcl_t,	\
				int, vnode_t **, cred_t *, int,		\
				caller_context_t *, vsecattr_t *);	\
	int	(*vop_remove)(vnode_t *, char *, cred_t *,		\
				caller_context_t *, int);		\
	int	(*vop_link)(vnode_t *, vnode_t *, char *, cred_t *,	\
				caller_context_t *, int);		\
	int	(*vop_rename)(vnode_t *, char *, vnode_t *, char *,	\
				cred_t *, caller_context_t *, int);	\
	int	(*vop_mkdir)(vnode_t *, char *, vattr_t *, vnode_t **,	\
				cred_t *, caller_context_t *, int,	\
				vsecattr_t *);				\
	int	(*vop_rmdir)(vnode_t *, char *, vnode_t *, cred_t *,	\
				caller_context_t *, int);		\
	int	(*vop_readdir)(vnode_t *, uio_t *, cred_t *, int *,	\
				caller_context_t *, int);		\
	int	(*vop_symlink)(vnode_t *, char *, vattr_t *, char *,	\
				cred_t *, caller_context_t *, int);	\
	int	(*vop_readlink)(vnode_t *, uio_t *, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_fsync)(vnode_t *, int, cred_t *,			\
				caller_context_t *);			\
	void	(*vop_inactive)(vnode_t *, cred_t *,			\
				caller_context_t *);			\
	int	(*vop_fid)(vnode_t *, struct fid *,			\
				caller_context_t *);			\
	int	(*vop_rwlock)(vnode_t *, int, caller_context_t *);	\
	void	(*vop_rwunlock)(vnode_t *, int, caller_context_t *);	\
	int	(*vop_seek)(vnode_t *, offset_t, offset_t *,		\
				caller_context_t *);			\
	int	(*vop_cmp)(vnode_t *, vnode_t *, caller_context_t *);	\
	int	(*vop_frlock)(vnode_t *, int, struct flock64 *,		\
				int, offset_t,				\
				struct flk_callback *, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_space)(vnode_t *, int, struct flock64 *,		\
				int, offset_t,				\
				cred_t *, caller_context_t *);		\
	int	(*vop_realvp)(vnode_t *, vnode_t **,			\
				caller_context_t *);			\
	int	(*vop_getpage)(vnode_t *, offset_t, size_t, uint_t *,	\
				struct page **, size_t, struct seg *,	\
				caddr_t, enum seg_rw, cred_t *,		\
				caller_context_t *);			\
	int	(*vop_putpage)(vnode_t *, offset_t, size_t,		\
				int, cred_t *, caller_context_t *);	\
	int	(*vop_map)(vnode_t *, offset_t, struct as *,		\
				caddr_t *, size_t,			\
				uchar_t, uchar_t, uint_t, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_addmap)(vnode_t *, offset_t, struct as *,		\
				caddr_t, size_t,			\
				uchar_t, uchar_t, uint_t, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_delmap)(vnode_t *, offset_t, struct as *,		\
				caddr_t, size_t,			\
				uint_t, uint_t, uint_t, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_poll)(vnode_t *, short, int, short *,		\
				struct pollhead **,			\
				caller_context_t *);			\
	int	(*vop_dump)(vnode_t *, caddr_t, int, int,		\
				caller_context_t *);			\
	int	(*vop_pathconf)(vnode_t *, int, ulong_t *, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_pageio)(vnode_t *, struct page *,			\
				u_offset_t, size_t, int, cred_t *,	\
				caller_context_t *);			\
	int	(*vop_dumpctl)(vnode_t *, int, int *,			\
				caller_context_t *);			\
	void	(*vop_dispose)(vnode_t *, struct page *,		\
				int, int, cred_t *,			\
				caller_context_t *);			\
	int	(*vop_setsecattr)(vnode_t *, vsecattr_t *,		\
				int, cred_t *, caller_context_t *);	\
	int	(*vop_getsecattr)(vnode_t *, vsecattr_t *,		\
				int, cred_t *, caller_context_t *);	\
	int	(*vop_shrlock)(vnode_t *, int, struct shrlock *,	\
				int, cred_t *, caller_context_t *);	\
	int	(*vop_vnevent)(vnode_t *, vnevent_t, vnode_t *,		\
				char *, caller_context_t *)
	/* NB: No ";" */

/*
 * Operations on vnodes.  Note: File systems must never operate directly
 * on a 'vnodeops' structure -- it WILL change in future releases!  They
 * must use vn_make_ops() to create the structure.
 */
typedef struct vnodeops {
	const char *vnop_name;
	VNODE_OPS;	/* Signatures of all vnode operations (vops) */
} vnodeops_t;

extern int	fop_open(vnode_t **, int, cred_t *, caller_context_t *);
extern int	fop_close(vnode_t *, int, int, offset_t, cred_t *,
				caller_context_t *);
extern int	fop_read(vnode_t *, uio_t *, int, cred_t *, caller_context_t *);
extern int	fop_write(vnode_t *, uio_t *, int, cred_t *,
				caller_context_t *);
extern int	fop_ioctl(vnode_t *, int, intptr_t, int, cred_t *, int *,
				caller_context_t *);
extern int	fop_setfl(vnode_t *, int, int, cred_t *, caller_context_t *);
extern int	fop_getattr(vnode_t *, vattr_t *, int, cred_t *,
				caller_context_t *);
extern int	fop_setattr(vnode_t *, vattr_t *, int, cred_t *,
				caller_context_t *);
extern int	fop_access(vnode_t *, int, int, cred_t *, caller_context_t *);
extern int	fop_lookup(vnode_t *, char *, vnode_t **, struct pathname *,
				int, vnode_t *, cred_t *, caller_context_t *,
				int *, struct pathname *);
extern int	fop_create(vnode_t *, char *, vattr_t *, vcexcl_t, int,
				vnode_t **, cred_t *, int, caller_context_t *,
				vsecattr_t *);
extern int	fop_remove(vnode_t *vp, char *, cred_t *, caller_context_t *,
				int);
extern int	fop_link(vnode_t *, vnode_t *, char *, cred_t *,
				caller_context_t *, int);
extern int	fop_rename(vnode_t *, char *, vnode_t *, char *, cred_t *,
				caller_context_t *, int);
extern int	fop_mkdir(vnode_t *, char *, vattr_t *, vnode_t **, cred_t *,
				caller_context_t *, int, vsecattr_t *);
extern int	fop_rmdir(vnode_t *, char *, vnode_t *, cred_t *,
				caller_context_t *, int);
extern int	fop_readdir(vnode_t *, uio_t *, cred_t *, int *,
				caller_context_t *, int);
extern int	fop_symlink(vnode_t *, char *, vattr_t *, char *, cred_t *,
				caller_context_t *, int);
extern int	fop_readlink(vnode_t *, uio_t *, cred_t *, caller_context_t *);
extern int	fop_fsync(vnode_t *, int, cred_t *, caller_context_t *);
extern void	fop_inactive(vnode_t *, cred_t *, caller_context_t *);
extern int	fop_fid(vnode_t *, struct fid *, caller_context_t *);
extern int	fop_rwlock(vnode_t *, int, caller_context_t *);
extern void	fop_rwunlock(vnode_t *, int, caller_context_t *);
extern int	fop_seek(vnode_t *, offset_t, offset_t *, caller_context_t *);
extern int	fop_cmp(vnode_t *, vnode_t *, caller_context_t *);
extern int	fop_frlock(vnode_t *, int, struct flock64 *, int, offset_t,
				struct flk_callback *, cred_t *,
				caller_context_t *);
extern int	fop_space(vnode_t *, int, struct flock64 *, int, offset_t,
				cred_t *, caller_context_t *);
extern int	fop_realvp(vnode_t *, vnode_t **, caller_context_t *);
extern int	fop_getpage(vnode_t *, offset_t, size_t, uint_t *,
				struct page **, size_t, struct seg *,
				caddr_t, enum seg_rw, cred_t *,
				caller_context_t *);
extern int	fop_putpage(vnode_t *, offset_t, size_t, int, cred_t *,
				caller_context_t *);
extern int	fop_map(vnode_t *, offset_t, struct as *, caddr_t *, size_t,
				uchar_t, uchar_t, uint_t, cred_t *cr,
				caller_context_t *);
extern int	fop_addmap(vnode_t *, offset_t, struct as *, caddr_t, size_t,
				uchar_t, uchar_t, uint_t, cred_t *,
				caller_context_t *);
extern int	fop_delmap(vnode_t *, offset_t, struct as *, caddr_t, size_t,
				uint_t, uint_t, uint_t, cred_t *,
				caller_context_t *);
extern int	fop_poll(vnode_t *, short, int, short *, struct pollhead **,
				caller_context_t *);
extern int	fop_dump(vnode_t *, caddr_t, int, int, caller_context_t *);
extern int	fop_pathconf(vnode_t *, int, ulong_t *, cred_t *,
				caller_context_t *);
extern int	fop_pageio(vnode_t *, struct page *, u_offset_t, size_t, int,
				cred_t *, caller_context_t *);
extern int	fop_dumpctl(vnode_t *, int, int *, caller_context_t *);
extern void	fop_dispose(vnode_t *, struct page *, int, int, cred_t *,
				caller_context_t *);
extern int	fop_setsecattr(vnode_t *, vsecattr_t *, int, cred_t *,
				caller_context_t *);
extern int	fop_getsecattr(vnode_t *, vsecattr_t *, int, cred_t *,
				caller_context_t *);
extern int	fop_shrlock(vnode_t *, int, struct shrlock *, int, cred_t *,
				caller_context_t *);
extern int	fop_vnevent(vnode_t *, vnevent_t, vnode_t *, char *,
				caller_context_t *);

#endif	/* _KERNEL */

#define	VOP_OPEN(vpp, mode, cr, ct) \
	fop_open(vpp, mode, cr, ct)
#define	VOP_CLOSE(vp, f, c, o, cr, ct) \
	fop_close(vp, f, c, o, cr, ct)
#define	VOP_READ(vp, uiop, iof, cr, ct) \
	fop_read(vp, uiop, iof, cr, ct)
#define	VOP_WRITE(vp, uiop, iof, cr, ct) \
	fop_write(vp, uiop, iof, cr, ct)
#define	VOP_IOCTL(vp, cmd, a, f, cr, rvp, ct) \
	fop_ioctl(vp, cmd, a, f, cr, rvp, ct)
#define	VOP_SETFL(vp, f, a, cr, ct) \
	fop_setfl(vp, f, a, cr, ct)
#define	VOP_GETATTR(vp, vap, f, cr, ct) \
	fop_getattr(vp, vap, f, cr, ct)
#define	VOP_SETATTR(vp, vap, f, cr, ct) \
	fop_setattr(vp, vap, f, cr, ct)
#define	VOP_ACCESS(vp, mode, f, cr, ct) \
	fop_access(vp, mode, f, cr, ct)
#define	VOP_LOOKUP(vp, cp, vpp, pnp, f, rdir, cr, ct, defp, rpnp) \
	fop_lookup(vp, cp, vpp, pnp, f, rdir, cr, ct, defp, rpnp)
#define	VOP_CREATE(dvp, p, vap, ex, mode, vpp, cr, flag, ct, vsap) \
	fop_create(dvp, p, vap, ex, mode, vpp, cr, flag, ct, vsap)
#define	VOP_REMOVE(dvp, p, cr, ct, f) \
	fop_remove(dvp, p, cr, ct, f)
#define	VOP_LINK(tdvp, fvp, p, cr, ct, f) \
	fop_link(tdvp, fvp, p, cr, ct, f)
#define	VOP_RENAME(fvp, fnm, tdvp, tnm, cr, ct, f) \
	fop_rename(fvp, fnm, tdvp, tnm, cr, ct, f)
#define	VOP_MKDIR(dp, p, vap, vpp, cr, ct, f, vsap) \
	fop_mkdir(dp, p, vap, vpp, cr, ct, f, vsap)
#define	VOP_RMDIR(dp, p, cdir, cr, ct, f) \
	fop_rmdir(dp, p, cdir, cr, ct, f)
#define	VOP_READDIR(vp, uiop, cr, eofp, ct, f) \
	fop_readdir(vp, uiop, cr, eofp, ct, f)
#define	VOP_SYMLINK(dvp, lnm, vap, tnm, cr, ct, f) \
	fop_symlink(dvp, lnm, vap, tnm, cr, ct, f)
#define	VOP_READLINK(vp, uiop, cr, ct) \
	fop_readlink(vp, uiop, cr, ct)
#define	VOP_FSYNC(vp, syncflag, cr, ct) \
	fop_fsync(vp, syncflag, cr, ct)
#define	VOP_INACTIVE(vp, cr, ct) \
	fop_inactive(vp, cr, ct)
#define	VOP_FID(vp, fidp, ct) \
	fop_fid(vp, fidp, ct)
#define	VOP_RWLOCK(vp, w, ct) \
	fop_rwlock(vp, w, ct)
#define	VOP_RWUNLOCK(vp, w, ct) \
	fop_rwunlock(vp, w, ct)
#define	VOP_SEEK(vp, ooff, noffp, ct) \
	fop_seek(vp, ooff, noffp, ct)
#define	VOP_CMP(vp1, vp2, ct) \
	fop_cmp(vp1, vp2, ct)
#define	VOP_FRLOCK(vp, cmd, a, f, o, cb, cr, ct) \
	fop_frlock(vp, cmd, a, f, o, cb, cr, ct)
#define	VOP_SPACE(vp, cmd, a, f, o, cr, ct) \
	fop_space(vp, cmd, a, f, o, cr, ct)
#define	VOP_REALVP(vp1, vp2, ct) \
	fop_realvp(vp1, vp2, ct)
#define	VOP_GETPAGE(vp, of, sz, pr, pl, ps, sg, a, rw, cr, ct) \
	fop_getpage(vp, of, sz, pr, pl, ps, sg, a, rw, cr, ct)
#define	VOP_PUTPAGE(vp, of, sz, fl, cr, ct) \
	fop_putpage(vp, of, sz, fl, cr, ct)
#define	VOP_MAP(vp, of, as, a, sz, p, mp, fl, cr, ct) \
	fop_map(vp, of, as, a, sz, p, mp, fl, cr, ct)
#define	VOP_ADDMAP(vp, of, as, a, sz, p, mp, fl, cr, ct) \
	fop_addmap(vp, of, as, a, sz, p, mp, fl, cr, ct)
#define	VOP_DELMAP(vp, of, as, a, sz, p, mp, fl, cr, ct) \
	fop_delmap(vp, of, as, a, sz, p, mp, fl, cr, ct)
#define	VOP_POLL(vp, events, anyyet, reventsp, phpp, ct) \
	fop_poll(vp, events, anyyet, reventsp, phpp, ct)
#define	VOP_DUMP(vp, addr, bn, count, ct) \
	fop_dump(vp, addr, bn, count, ct)
#define	VOP_PATHCONF(vp, cmd, valp, cr, ct) \
	fop_pathconf(vp, cmd, valp, cr, ct)
#define	VOP_PAGEIO(vp, pp, io_off, io_len, flags, cr, ct) \
	fop_pageio(vp, pp, io_off, io_len, flags, cr, ct)
#define	VOP_DUMPCTL(vp, action, blkp, ct) \
	fop_dumpctl(vp, action, blkp, ct)
#define	VOP_DISPOSE(vp, pp, flag, dn, cr, ct) \
	fop_dispose(vp, pp, flag, dn, cr, ct)
#define	VOP_GETSECATTR(vp, vsap, f, cr, ct) \
	fop_getsecattr(vp, vsap, f, cr, ct)
#define	VOP_SETSECATTR(vp, vsap, f, cr, ct) \
	fop_setsecattr(vp, vsap, f, cr, ct)
#define	VOP_SHRLOCK(vp, cmd, shr, f, cr, ct) \
	fop_shrlock(vp, cmd, shr, f, cr, ct)
#define	VOP_VNEVENT(vp, vnevent, dvp, fnm, ct) \
	fop_vnevent(vp, vnevent, dvp, fnm, ct)

#define	VOPNAME_OPEN		"open"
#define	VOPNAME_CLOSE		"close"
#define	VOPNAME_READ		"read"
#define	VOPNAME_WRITE		"write"
#define	VOPNAME_IOCTL		"ioctl"
#define	VOPNAME_SETFL		"setfl"
#define	VOPNAME_GETATTR		"getattr"
#define	VOPNAME_SETATTR		"setattr"
#define	VOPNAME_ACCESS		"access"
#define	VOPNAME_LOOKUP		"lookup"
#define	VOPNAME_CREATE		"create"
#define	VOPNAME_REMOVE		"remove"
#define	VOPNAME_LINK		"link"
#define	VOPNAME_RENAME		"rename"
#define	VOPNAME_MKDIR		"mkdir"
#define	VOPNAME_RMDIR		"rmdir"
#define	VOPNAME_READDIR		"readdir"
#define	VOPNAME_SYMLINK		"symlink"
#define	VOPNAME_READLINK	"readlink"
#define	VOPNAME_FSYNC		"fsync"
#define	VOPNAME_INACTIVE	"inactive"
#define	VOPNAME_FID		"fid"
#define	VOPNAME_RWLOCK		"rwlock"
#define	VOPNAME_RWUNLOCK	"rwunlock"
#define	VOPNAME_SEEK		"seek"
#define	VOPNAME_CMP		"cmp"
#define	VOPNAME_FRLOCK		"frlock"
#define	VOPNAME_SPACE		"space"
#define	VOPNAME_REALVP		"realvp"
#define	VOPNAME_GETPAGE		"getpage"
#define	VOPNAME_PUTPAGE		"putpage"
#define	VOPNAME_MAP		"map"
#define	VOPNAME_ADDMAP		"addmap"
#define	VOPNAME_DELMAP		"delmap"
#define	VOPNAME_POLL		"poll"
#define	VOPNAME_DUMP		"dump"
#define	VOPNAME_PATHCONF	"pathconf"
#define	VOPNAME_PAGEIO		"pageio"
#define	VOPNAME_DUMPCTL		"dumpctl"
#define	VOPNAME_DISPOSE		"dispose"
#define	VOPNAME_GETSECATTR	"getsecattr"
#define	VOPNAME_SETSECATTR	"setsecattr"
#define	VOPNAME_SHRLOCK		"shrlock"
#define	VOPNAME_VNEVENT		"vnevent"

#define AV_SCANSTAMP_SZ 32              /* length of anti-virus scanstamp */

/*
 * Structure of all optional attributes.
 */
typedef struct xoptattr {
        timestruc_t     xoa_createtime; /* Create time of file */
        uint8_t         xoa_archive;
        uint8_t         xoa_system;
        uint8_t         xoa_readonly;
        uint8_t         xoa_hidden;
        uint8_t         xoa_nounlink;
        uint8_t         xoa_immutable;
        uint8_t         xoa_appendonly;
        uint8_t         xoa_nodump;
        uint8_t         xoa_opaque;
        uint8_t         xoa_av_quarantined;
        uint8_t         xoa_av_modified;
        uint8_t         xoa_av_scanstamp[AV_SCANSTAMP_SZ];
	uint8_t		xoa_reparse;
} xoptattr_t;

/*
 * The xvattr structure is really a variable length structure that
 * is made up of:
 * - The classic vattr_t (xva_vattr)
 * - a 32 bit quantity (xva_mapsize) that specifies the size of the
 *   attribute bitmaps in 32 bit words.
 * - A pointer to the returned attribute bitmap (needed because the
 *   previous element, the requested attribute bitmap) is variable lenth.
 * - The requested attribute bitmap, which is an array of 32 bit words.
 *   Callers use the XVA_SET_REQ() macro to set the bits corresponding to
 *   the attributes that are being requested.
 * - The returned attribute bitmap, which is an array of 32 bit words.
 *   File systems that support optional attributes use the XVA_SET_RTN()
 *   macro to set the bits corresponding to the attributes that are being
 *   returned.
 * - The xoptattr_t structure which contains the attribute values
 *
 * xva_mapsize determines how many words in the attribute bitmaps.
 * Immediately following the attribute bitmaps is the xoptattr_t.
 * xva_getxoptattr() is used to get the pointer to the xoptattr_t
 * section.
 */

#define XVA_MAPSIZE     3               /* Size of attr bitmaps */
#define XVA_MAGIC       0x78766174      /* Magic # for verification */

/*
 * The xvattr structure is an extensible structure which permits optional
 * attributes to be requested/returned.  File systems may or may not support
 * optional attributes.  They do so at their own discretion but if they do
 * support optional attributes, they must register the VFSFT_XVATTR feature
 * so that the optional attributes can be set/retrived.
 *
 * The fields of the xvattr structure are:
 *
 * xva_vattr - The first element of an xvattr is a legacy vattr structure
 * which includes the common attributes.  If AT_XVATTR is set in the va_mask
 * then the entire structure is treated as an xvattr.  If AT_XVATTR is not
 * set, then only the xva_vattr structure can be used.
 *
 * xva_magic - 0x78766174 (hex for "xvat"). Magic number for verification.
 *
 * xva_mapsize - Size of requested and returned attribute bitmaps.
 *
 * xva_rtnattrmapp - Pointer to xva_rtnattrmap[].  We need this since the
 * size of the array before it, xva_reqattrmap[], could change which means
 * the location of xva_rtnattrmap[] could change.  This will allow unbundled
 * file systems to find the location of xva_rtnattrmap[] when the sizes change.
 *
 * xva_reqattrmap[] - Array of requested attributes.  Attributes are
 * represented by a specific bit in a specific element of the attribute
 * map array.  Callers set the bits corresponding to the attributes
 * that the caller wants to get/set.
 *
 * xva_rtnattrmap[] - Array of attributes that the file system was able to
 * process.  Not all file systems support all optional attributes.  This map
 * informs the caller which attributes the underlying file system was able
 * to set/get.  (Same structure as the requested attributes array in terms
 * of each attribute  corresponding to specific bits and array elements.)
 *
 * xva_xoptattrs - Structure containing values of optional attributes.
 * These values are only valid if the corresponding bits in xva_reqattrmap
 * are set and the underlying file system supports those attributes.
 */
typedef struct xvattr {
	vattr_t         xva_vattr;      /* Embedded vattr structure */
	uint32_t        xva_magic;      /* Magic Number */
	uint32_t        xva_mapsize;    /* Size of attr bitmap (32-bit words) */
	uint32_t        *xva_rtnattrmapp;       /* Ptr to xva_rtnattrmap[] */
	uint32_t        xva_reqattrmap[XVA_MAPSIZE];    /* Requested attrs */
	uint32_t        xva_rtnattrmap[XVA_MAPSIZE];    /* Returned attrs */
	xoptattr_t      xva_xoptattrs;  /* Optional attributes */
} xvattr_t;

/*
 * Extensible vnode attribute (xva) routines:
 * xva_init() initializes an xvattr_t (zero struct, init mapsize, set AT_XATTR)
 * xva_getxoptattr() returns a ponter to the xoptattr_t section of xvattr_t
 */
void            xva_init(xvattr_t *);
xoptattr_t      *xva_getxoptattr(xvattr_t *);   /* Get ptr to xoptattr_t */

/*
 * Attribute bits used in the extensible attribute's (xva's) attribute
 * bitmaps.  Note that the bitmaps are made up of a variable length number
 * of 32-bit words.  The convention is to use XAT{n}_{attrname} where "n"
 * is the element in the bitmap (starting at 1).  This convention is for
 * the convenience of the maintainer to keep track of which element each
 * attribute belongs to.
 *
 * NOTE THAT CONSUMERS MUST *NOT* USE THE XATn_* DEFINES DIRECTLY.  CONSUMERS
 * MUST USE THE XAT_* DEFINES.
 */
#define XAT0_INDEX      0LL             /* Index into bitmap for XAT0 attrs */
#define XAT0_CREATETIME 0x00000001      /* Create time of file */
#define XAT0_ARCHIVE    0x00000002      /* Archive */
#define XAT0_SYSTEM     0x00000004      /* System */
#define XAT0_READONLY   0x00000008      /* Readonly */
#define XAT0_HIDDEN     0x00000010      /* Hidden */
#define XAT0_NOUNLINK   0x00000020      /* Nounlink */
#define XAT0_IMMUTABLE  0x00000040      /* immutable */
#define XAT0_APPENDONLY 0x00000080      /* appendonly */
#define XAT0_NODUMP     0x00000100      /* nodump */
#define XAT0_OPAQUE     0x00000200      /* opaque */
#define XAT0_AV_QUARANTINED     0x00000400      /* anti-virus quarantine */
#define XAT0_AV_MODIFIED        0x00000800      /* anti-virus modified */
#define XAT0_AV_SCANSTAMP       0x00001000      /* anti-virus scanstamp */
#define	XAT0_REPARSE	0x00002000	/* FS reparse point */

#define XAT0_ALL_ATTRS  (XAT0_CREATETIME|XAT0_ARCHIVE|XAT0_SYSTEM| \
    XAT0_READONLY|XAT0_HIDDEN|XAT0_NOUNLINK|XAT0_IMMUTABLE|XAT0_APPENDONLY| \
    XAT0_NODUMP|XAT0_OPAQUE|XAT0_AV_QUARANTINED| \
    XAT0_AV_MODIFIED|XAT0_AV_SCANSTAMP|XAT0_REPARSE)

/* Support for XAT_* optional attributes */
#define XVA_MASK                0xffffffff      /* Used to mask off 32 bits */
#define XVA_SHFT                32              /* Used to shift index */

/*
 * Used to pry out the index and attribute bits from the XAT_* attributes
 * defined below.  Note that we're masking things down to 32 bits then
 * casting to uint32_t.
 */
#define XVA_INDEX(attr)         ((uint32_t)(((attr) >> XVA_SHFT) & XVA_MASK))
#define XVA_ATTRBIT(attr)       ((uint32_t)((attr) & XVA_MASK))

/*
 * The following defines present a "flat namespace" so that consumers don't
 * need to keep track of which element belongs to which bitmap entry.
 *
 * NOTE THAT THESE MUST NEVER BE OR-ed TOGETHER
 */
#define XAT_CREATETIME          ((XAT0_INDEX << XVA_SHFT) | XAT0_CREATETIME)
#define XAT_ARCHIVE             ((XAT0_INDEX << XVA_SHFT) | XAT0_ARCHIVE)
#define XAT_SYSTEM              ((XAT0_INDEX << XVA_SHFT) | XAT0_SYSTEM)
#define XAT_READONLY            ((XAT0_INDEX << XVA_SHFT) | XAT0_READONLY)
#define XAT_HIDDEN              ((XAT0_INDEX << XVA_SHFT) | XAT0_HIDDEN)
#define XAT_NOUNLINK            ((XAT0_INDEX << XVA_SHFT) | XAT0_NOUNLINK)
#define XAT_IMMUTABLE           ((XAT0_INDEX << XVA_SHFT) | XAT0_IMMUTABLE)
#define XAT_APPENDONLY          ((XAT0_INDEX << XVA_SHFT) | XAT0_APPENDONLY)
#define XAT_NODUMP              ((XAT0_INDEX << XVA_SHFT) | XAT0_NODUMP)
#define XAT_OPAQUE              ((XAT0_INDEX << XVA_SHFT) | XAT0_OPAQUE)
#define XAT_AV_QUARANTINED      ((XAT0_INDEX << XVA_SHFT) | XAT0_AV_QUARANTINED)
#define XAT_AV_MODIFIED         ((XAT0_INDEX << XVA_SHFT) | XAT0_AV_MODIFIED)
#define XAT_AV_SCANSTAMP        ((XAT0_INDEX << XVA_SHFT) | XAT0_AV_SCANSTAMP)
#define	XAT_REPARSE		((XAT0_INDEX << XVA_SHFT) | XAT0_REPARSE)

/*
 * The returned attribute map array (xva_rtnattrmap[]) is located past the
 * requested attribute map array (xva_reqattrmap[]).  Its location changes
 * when the array sizes change.  We use a separate pointer in a known location
 * (xva_rtnattrmapp) to hold the location of xva_rtnattrmap[].  This is
 * set in xva_init()
 */
#define XVA_RTNATTRMAP(xvap)    ((xvap)->xva_rtnattrmapp)

/*
 * XVA_SET_REQ() sets an attribute bit in the proper element in the bitmap
 * of requested attributes (xva_reqattrmap[]).
 */
#define XVA_SET_REQ(xvap, attr)                                 \
        ASSERT((xvap)->xva_vattr.va_mask | AT_XVATTR);          \
        ASSERT((xvap)->xva_magic == XVA_MAGIC);                 \
        (xvap)->xva_reqattrmap[XVA_INDEX(attr)] |= XVA_ATTRBIT(attr)

/*
 * XVA_CLR_REQ() clears an attribute bit in the proper element in the bitmap
 * of requested attributes (xva_reqattrmap[]).
 */
#define	XVA_CLR_REQ(xvap, attr)					\
	ASSERT((xvap)->xva_vattr.va_mask | AT_XVATTR);		\
	ASSERT((xvap)->xva_magic == XVA_MAGIC);			\
	(xvap)->xva_reqattrmap[XVA_INDEX(attr)] &= ~XVA_ATTRBIT(attr)

/*
 * XVA_SET_RTN() sets an attribute bit in the proper element in the bitmap
 * of returned attributes (xva_rtnattrmap[]).
 */
#define XVA_SET_RTN(xvap, attr)                                 \
        ASSERT((xvap)->xva_vattr.va_mask | AT_XVATTR);          \
        ASSERT((xvap)->xva_magic == XVA_MAGIC);                 \
        (XVA_RTNATTRMAP(xvap))[XVA_INDEX(attr)] |= XVA_ATTRBIT(attr)

/*
 * XVA_ISSET_REQ() checks the requested attribute bitmap (xva_reqattrmap[])
 * to see of the corresponding attribute bit is set.  If so, returns non-zero.
 */
#define XVA_ISSET_REQ(xvap, attr)                                       \
        ((((xvap)->xva_vattr.va_mask | AT_XVATTR) &&                    \
                ((xvap)->xva_magic == XVA_MAGIC) &&                     \
                ((xvap)->xva_mapsize > XVA_INDEX(attr))) ?              \
        ((xvap)->xva_reqattrmap[XVA_INDEX(attr)] & XVA_ATTRBIT(attr)) : 0)

/*
 * XVA_ISSET_RTN() checks the returned attribute bitmap (xva_rtnattrmap[])
 * to see of the corresponding attribute bit is set.  If so, returns non-zero.
 */
#define XVA_ISSET_RTN(xvap, attr)                                       \
        ((((xvap)->xva_vattr.va_mask | AT_XVATTR) &&                    \
                ((xvap)->xva_magic == XVA_MAGIC) &&                     \
                ((xvap)->xva_mapsize > XVA_INDEX(attr))) ?              \
        ((XVA_RTNATTRMAP(xvap))[XVA_INDEX(attr)] & XVA_ATTRBIT(attr)) : 0)

#endif

#ifdef	__cplusplus
}
#endif
