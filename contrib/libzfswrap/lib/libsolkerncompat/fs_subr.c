/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <sys/statvfs.h>
#include <sys/unistd.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <vm/page.h>
#include <stdlib.h>
#include <strings.h>

/*
 * The associated operation is not supported by the file system.
 */
int
fs_nosys()
{
	return (ENOSYS);
}

/*
 * The associated operation is invalid (on this vnode).
 */
int
fs_inval()
{
	return (EINVAL);
}

/*
 * The associated operation is valid only for directories.
 */
int
fs_notdir()
{
	return (ENOTDIR);
}

/*ARGSUSED1*/
int
fs_vnevent_nosupport(vnode_t *vp, vnevent_t vnevent)
{
	ASSERT(vp != NULL);
	return (ENOTSUP);
}

/*ARGSUSED1*/
int
fs_vnevent_support(vnode_t *vp, vnevent_t vnevent)
{
	ASSERT(vp != NULL);
	return 0;
}

/*
 * Allow any flags.
 */
/* ARGSUSED */
int
fs_setfl(vnode_t *vp, int oflags, int nflags, cred_t *cr)
{
	return (0);
}

/*
 * Read/write lock/unlock.  Does nothing.
 */
/* ARGSUSED */
int
fs_rwlock(vnode_t *vp, int write_lock, caller_context_t *ctp)
{
	return (-1);
}

/* ARGSUSED */
void
fs_rwunlock(vnode_t *vp, int write_lock, caller_context_t *ctp)
{
}

/*
 * Compare two vnodes.
 */
int
fs_cmp(vnode_t *vp1, vnode_t *vp2)
{
	return (vp1 == vp2);
}

/*
 * File and record locking.
 */
/* ARGSUSED */
int
fs_frlock(register vnode_t *vp, int cmd, struct flock64 *bfp, int flag,
	offset_t offset, flk_callback_t *flk_cbp, cred_t *cr)
{
	/* ZFSFUSE: not implemented */
	abort();
}

/* ARGSUSED */
int
fs_nosys_map(struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	size_t len,
	uchar_t prot,
	uchar_t maxprot,
	uint_t flags,
	struct cred *cr)
{
	return (ENOSYS);
}

/* ARGSUSED */
int
fs_nosys_addmap(struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	size_t len,
	uchar_t prot,
	uchar_t maxprot,
	uint_t flags,
	struct cred *cr)
{
	return (ENOSYS);
}

/* ARGSUSED */
int
fs_nosys_poll(vnode_t *vp,
	register short events,
	int anyyet,
	register short *reventsp,
	struct pollhead **phpp)
{
	return (ENOSYS);
}

/* ARGSUSED */
int
fs_poll(vnode_t *vp,
	register short events,
	int anyyet,
	register short *reventsp,
	struct pollhead **phpp)
{
	/* ZFSFUSE: not implemented */
	abort();
}

/*
 * POSIX pathconf() support.
 */
/* ARGSUSED */
int
fs_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr)
{
	register ulong_t val;
	register int error = 0;
	struct statvfs64 vfsbuf;

	switch (cmd) {

	case _PC_LINK_MAX:
		val = MAXLINK;
		break;

	case _PC_MAX_CANON:
		val = MAX_CANON;
		break;

	case _PC_MAX_INPUT:
		val = MAX_INPUT;
		break;

	case _PC_NAME_MAX:
		bzero(&vfsbuf, sizeof (vfsbuf));
		if (error = VFS_STATVFS(vp->v_vfsp, &vfsbuf))
			break;
		val = vfsbuf.f_namemax;
		break;

	case _PC_PATH_MAX:
	case _PC_SYMLINK_MAX:
		val = MAXPATHLEN;
		break;

	case _PC_PIPE_BUF:
		val = PIPE_BUF;
		break;

	case _PC_NO_TRUNC:
		if (vp->v_vfsp->vfs_flag & VFS_NOTRUNC)
			val = 1;	/* NOTRUNC is enabled for vp */
		else
			val = (ulong_t)-1;
		break;

	case _PC_VDISABLE:
		val = _POSIX_VDISABLE;
		break;

	case _PC_CHOWN_RESTRICTED:
		val = 1;
		break;

	case _PC_FILESIZEBITS:

		/*
		 * If ever we come here it means that underlying file system
		 * does not recognise the command and therefore this
		 * configurable limit cannot be determined. We return -1
		 * and don't change errno.
		 */

		val = (ulong_t)-1;    /* large file support */
		break;

	default:
		error = EINVAL;
		break;
	}

	if (error == 0)
		*valp = val;
	return (error);
}

/*
 * Dispose of a page.
 */
/* ARGSUSED */
void
fs_dispose(struct vnode *vp, page_t *pp, int fl, int dn, struct cred *cr)
{
	/* ZFSFUSE: not used */
	abort();
}

/* ARGSUSED */
void
fs_nodispose(struct vnode *vp, page_t *pp, int fl, int dn, struct cred *cr)
{
	cmn_err(CE_PANIC, "fs_nodispose invoked");
}

/*
 * fabricate acls for file systems that do not support acls.
 */
/* ARGSUSED */
int
fs_fab_acl(vp, vsecattr, flag, cr)
vnode_t		*vp;
vsecattr_t	*vsecattr;
int		flag;
cred_t		*cr;
{
	/* ZFSFUSE: not used */
	abort();
}

/*
 * Common code for implementing DOS share reservations
 */
/* ARGSUSED4 */
int
fs_shrlock(struct vnode *vp, int cmd, struct shrlock *shr, int flag, cred_t *cr)
{
	/* ZFSFUSE: not used */
	abort();
}

/*
 * The file system has nothing to sync to disk.  However, the
 * VFS_SYNC operation must not fail.
 */
/* ARGSUSED */
int
fs_sync(struct vfs *vfspp, short flag, cred_t *cr)
{
	cmn_err(CE_WARN, "fs_sync ignored");
	return (0);
}

/*
 * Free the file system specific resources. For the file systems that
 * do not support the forced unmount, it will be a nop function.
 */

/*ARGSUSED*/
void
fs_freevfs(vfs_t *vfsp)
{
	cmn_err(CE_WARN, "fs_freevfs ignored");
}
