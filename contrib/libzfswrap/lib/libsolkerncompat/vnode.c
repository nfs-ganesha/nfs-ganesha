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
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/rwstlock.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/cmn_err.h>
#include <sys/atomic.h>
#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/pathname.h>
#include <fs/fs_subr.h>

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/taskq.h>

#include <sys/ioctl.h>
/* LINUX BLKGETSIZE64 */
#include <sys/mount.h>

#define VOPSTATS_UPDATE(vp, counter) ((void) 0)
#define VOPSTATS_UPDATE_IO(vp, readdir, readdir_bytes, x) ((void) 0)

/* Determine if this vnode is a file that is read-only */
#define ISROFILE(vp) \
	((vp)->v_type != VCHR && (vp)->v_type != VBLK && \
	    (vp)->v_type != VFIFO && vn_is_readonly(vp))

/*
 * Convert stat(2) formats to vnode types and vice versa.  (Knows about
 * numerical order of S_IFMT and vnode types.)
 */
enum vtype iftovt_tab[] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VNON
};

ushort_t vttoif_tab[] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, S_IFIFO,
	0, 0, S_IFSOCK, 0, 0
};

extern struct vnodeops *root_fvnodeops;
extern struct vnodeops *fd_fvnodeops;

/*
 * Vnode operations vector.
 */

static const fs_operation_trans_def_t vn_ops_table[] = {
	VOPNAME_OPEN, offsetof(struct vnodeops, vop_open),
	    fs_nosys, fs_nosys,

	VOPNAME_CLOSE, offsetof(struct vnodeops, vop_close),
	    fs_nosys, fs_nosys,

	VOPNAME_READ, offsetof(struct vnodeops, vop_read),
	    fs_nosys, fs_nosys,

	VOPNAME_WRITE, offsetof(struct vnodeops, vop_write),
	    fs_nosys, fs_nosys,

	VOPNAME_IOCTL, offsetof(struct vnodeops, vop_ioctl),
	    fs_nosys, fs_nosys,

	VOPNAME_SETFL, offsetof(struct vnodeops, vop_setfl),
	    fs_setfl, fs_nosys,

	VOPNAME_GETATTR, offsetof(struct vnodeops, vop_getattr),
	    fs_nosys, fs_nosys,

	VOPNAME_SETATTR, offsetof(struct vnodeops, vop_setattr),
	    fs_nosys, fs_nosys,

	VOPNAME_ACCESS, offsetof(struct vnodeops, vop_access),
	    fs_nosys, fs_nosys,

	VOPNAME_LOOKUP, offsetof(struct vnodeops, vop_lookup),
	    fs_nosys, fs_nosys,

	VOPNAME_CREATE, offsetof(struct vnodeops, vop_create),
	    fs_nosys, fs_nosys,

	VOPNAME_REMOVE, offsetof(struct vnodeops, vop_remove),
	    fs_nosys, fs_nosys,

	VOPNAME_LINK, offsetof(struct vnodeops, vop_link),
	    fs_nosys, fs_nosys,

	VOPNAME_RENAME, offsetof(struct vnodeops, vop_rename),
	    fs_nosys, fs_nosys,

	VOPNAME_MKDIR, offsetof(struct vnodeops, vop_mkdir),
	    fs_nosys, fs_nosys,

	VOPNAME_RMDIR, offsetof(struct vnodeops, vop_rmdir),
	    fs_nosys, fs_nosys,

	VOPNAME_READDIR, offsetof(struct vnodeops, vop_readdir),
	    fs_nosys, fs_nosys,

	VOPNAME_SYMLINK, offsetof(struct vnodeops, vop_symlink),
	    fs_nosys, fs_nosys,

	VOPNAME_READLINK, offsetof(struct vnodeops, vop_readlink),
	    fs_nosys, fs_nosys,

	VOPNAME_FSYNC, offsetof(struct vnodeops, vop_fsync),
	    fs_nosys, fs_nosys,

	VOPNAME_INACTIVE, offsetof(struct vnodeops, vop_inactive),
	    fs_nosys, fs_nosys,

	VOPNAME_FID, offsetof(struct vnodeops, vop_fid),
	    fs_nosys, fs_nosys,

	VOPNAME_RWLOCK, offsetof(struct vnodeops, vop_rwlock),
	    fs_rwlock, fs_rwlock,

	VOPNAME_RWUNLOCK, offsetof(struct vnodeops, vop_rwunlock),
	    (fs_generic_func_p) fs_rwunlock,
	    (fs_generic_func_p) fs_rwunlock,	/* no errors allowed */

	VOPNAME_SEEK, offsetof(struct vnodeops, vop_seek),
	    fs_nosys, fs_nosys,

	VOPNAME_CMP, offsetof(struct vnodeops, vop_cmp),
	    fs_cmp, fs_cmp,		/* no errors allowed */

	VOPNAME_FRLOCK, offsetof(struct vnodeops, vop_frlock),
	    fs_frlock, fs_nosys,

	VOPNAME_SPACE, offsetof(struct vnodeops, vop_space),
	    fs_nosys, fs_nosys,

	VOPNAME_REALVP, offsetof(struct vnodeops, vop_realvp),
	    fs_nosys, fs_nosys,

	VOPNAME_GETPAGE, offsetof(struct vnodeops, vop_getpage),
	    fs_nosys, fs_nosys,

	VOPNAME_PUTPAGE, offsetof(struct vnodeops, vop_putpage),
	    fs_nosys, fs_nosys,

	VOPNAME_MAP, offsetof(struct vnodeops, vop_map),
	    (fs_generic_func_p) fs_nosys_map,
	    (fs_generic_func_p) fs_nosys_map,

	VOPNAME_ADDMAP, offsetof(struct vnodeops, vop_addmap),
	    (fs_generic_func_p) fs_nosys_addmap,
	    (fs_generic_func_p) fs_nosys_addmap,

	VOPNAME_DELMAP, offsetof(struct vnodeops, vop_delmap),
	    fs_nosys, fs_nosys,

	VOPNAME_POLL, offsetof(struct vnodeops, vop_poll),
	    (fs_generic_func_p) fs_poll, (fs_generic_func_p) fs_nosys_poll,

	VOPNAME_DUMP, offsetof(struct vnodeops, vop_dump),
	    fs_nosys, fs_nosys,

	VOPNAME_PATHCONF, offsetof(struct vnodeops, vop_pathconf),
	    fs_pathconf, fs_nosys,

	VOPNAME_PAGEIO, offsetof(struct vnodeops, vop_pageio),
	    fs_nosys, fs_nosys,

	VOPNAME_DUMPCTL, offsetof(struct vnodeops, vop_dumpctl),
	    fs_nosys, fs_nosys,

	VOPNAME_DISPOSE, offsetof(struct vnodeops, vop_dispose),
	    (fs_generic_func_p) fs_dispose,
	    (fs_generic_func_p) fs_nodispose,

	VOPNAME_SETSECATTR, offsetof(struct vnodeops, vop_setsecattr),
	    fs_nosys, fs_nosys,

	VOPNAME_GETSECATTR, offsetof(struct vnodeops, vop_getsecattr),
	    fs_fab_acl, fs_nosys,

	VOPNAME_SHRLOCK, offsetof(struct vnodeops, vop_shrlock),
	    fs_shrlock, fs_nosys,

	VOPNAME_VNEVENT, offsetof(struct vnodeops, vop_vnevent),
	    (fs_generic_func_p) fs_vnevent_nosupport,
	    (fs_generic_func_p) fs_vnevent_nosupport,

	NULL, 0, NULL, NULL
};

/*
 * vn_vfswlock is used to implement a lock which is logically a writers lock
 * protecting the v_vfsmountedhere field.
 */
int
vn_vfswlock(vnode_t *vp)
{
	vn_vfslocks_entry_t *vpvfsentry;

	/*
	 * If vp is NULL then somebody is trying to lock the covered vnode
	 * of /.  (vfs_vnodecovered is NULL for /).  This situation will
	 * only happen when unmounting /.  Since that operation will fail
	 * anyway, return EBUSY here instead of in VFS_UNMOUNT.
	 */
	if (vp == NULL)
		return (EBUSY);

	vpvfsentry = vn_vfslocks_getlock_vnode(vp);

	if (rwst_tryenter(&vpvfsentry->ve_lock, RW_WRITER))
		return (0);

	vn_vfslocks_rele(vpvfsentry);
	return (EBUSY);
}

void
vn_vfsunlock(vnode_t *vp)
{
	vn_vfslocks_entry_t *vpvfsentry;

	/*
	 * ve_refcnt needs to be decremented twice.
	 * 1. To release refernce after a call to vn_vfslocks_getlock()
	 * 2. To release the reference from the locking routines like
	 *    vn_vfsrlock/vn_vfswlock etc,.
	 */
	vpvfsentry = vn_vfslocks_getlock(vp);
	vn_vfslocks_rele(vpvfsentry);

	rwst_exit(&vpvfsentry->ve_lock);
	vn_vfslocks_rele(vpvfsentry);
}

vnode_t *vn_alloc(int kmflag)
{
	ASSERT(kmflag == 0 || kmflag == UMEM_NOFAIL);

	vnode_t *vp;

	vp = kmem_cache_alloc(vnode_cache, kmflag);

	/* taken from vn_cache_constructor */
	mutex_init(&vp->v_lock, NULL, MUTEX_DEFAULT, NULL);
	rwst_init(&vp->v_vfsmhlock.ve_lock, NULL, RW_DEFAULT, NULL);

	if(vp != NULL) {
		vp->v_path = NULL;
		vp->v_data = NULL;
		vn_reinit(vp);
	}

	/*fprintf(stderr, "VNode %p alloc'ed\n", vp);*/
	return vp;
}

void vn_reinit(vnode_t *vp)
{
	vp->v_vfsp = NULL;
	vp->v_fd = -1;
	vp->v_size = 0;
	vp->v_count = 1;

	vn_recycle(vp);
}

void vn_recycle(vnode_t *vp)
{
	/*
	 * XXX - This really belongs in vn_reinit(), but we have some issues
	 * with the counts.  Best to have it here for clean initialization.
	 */
	vp->v_rdcnt = 0;
	vp->v_wrcnt = 0;

	if(vp->v_path != NULL) {
		free(vp->v_path);
		vp->v_path = NULL;
	}
}

void vn_free(vnode_t *vp)
{
	ASSERT(vp->v_count == 0 || vp->v_count == 1);

	vn_close(vp);
}

/*
 * Given a starting vnode and a path, updates the path in the target vnode in
 * a safe manner.  If the vnode already has path information embedded, then the
 * cached path is left untouched.
 */
void
vn_setpath(vnode_t *rootvp, struct vnode *startvp, struct vnode *vp,
    const char *path, size_t plen)
{
	char	*rpath;
	vnode_t	*base;
	size_t	rpathlen, rpathalloc;
	int	doslash = 1;

	if (*path == '/') {
		base = rootvp;
		path++;
		plen--;
	} else {
		base = startvp;
	}

	/*
	 * We cannot grab base->v_lock while we hold vp->v_lock because of
	 * the potential for deadlock.
	 */
	mutex_enter(&base->v_lock);
	if (base->v_path == NULL) {
		mutex_exit(&base->v_lock);
		return;
	}

	rpathlen = strlen(base->v_path);
	rpathalloc = rpathlen + plen + 1;
	/* Avoid adding a slash if there's already one there */
	if (base->v_path[rpathlen-1] == '/')
		doslash = 0;
	else
		rpathalloc++;

	/*
	 * We don't want to call kmem_alloc(KM_SLEEP) with kernel locks held,
	 * so we must do this dance.  If, by chance, something changes the path,
	 * just give up since there is no real harm.
	 */
	mutex_exit(&base->v_lock);

	rpath = kmem_alloc(rpathalloc, KM_SLEEP);

	mutex_enter(&base->v_lock);
	if (base->v_path == NULL || strlen(base->v_path) != rpathlen) {
		mutex_exit(&base->v_lock);
		kmem_free(rpath, rpathalloc);
		return;
	}
	bcopy(base->v_path, rpath, rpathlen);
	mutex_exit(&base->v_lock);

	if (doslash)
		rpath[rpathlen++] = '/';
	bcopy(path, rpath + rpathlen, plen);
	rpath[rpathlen + plen] = '\0';

	mutex_enter(&vp->v_lock);
	if (vp->v_path != NULL) {
		mutex_exit(&vp->v_lock);
		kmem_free(rpath, rpathalloc);
	} else {
		vp->v_path = rpath;
		mutex_exit(&vp->v_lock);
	}
}

/*
 * Called from within filesystem's vop_rename() to handle renames once the
 * target vnode is available.
 */
void
vn_renamepath(vnode_t *dvp, vnode_t *vp, const char *nm, size_t len)
{
	char *tmp;

	mutex_enter(&vp->v_lock);
	tmp = vp->v_path;
	vp->v_path = NULL;
	mutex_exit(&vp->v_lock);
	vn_setpath(rootdir, dvp, vp, nm, len);
	if (tmp != NULL)
		kmem_free(tmp, strlen(tmp) + 1);
}

/*
 * Similar to vn_setpath_str(), this function sets the path of the destination
 * vnode to the be the same as the source vnode.
 */
void
vn_copypath(struct vnode *src, struct vnode *dst)
{
	char *buf;
	int alloc;

	mutex_enter(&src->v_lock);
	if (src->v_path == NULL) {
		mutex_exit(&src->v_lock);
		return;
	}
	alloc = strlen(src->v_path) + 1;

	/* avoid kmem_alloc() with lock held */
	mutex_exit(&src->v_lock);
	buf = kmem_alloc(alloc, KM_SLEEP);
	mutex_enter(&src->v_lock);
	if (src->v_path == NULL || strlen(src->v_path) + 1 != alloc) {
		mutex_exit(&src->v_lock);
		kmem_free(buf, alloc);
		return;
	}
	bcopy(src->v_path, buf, alloc);
	mutex_exit(&src->v_lock);

	mutex_enter(&dst->v_lock);
	if (dst->v_path != NULL) {
		mutex_exit(&dst->v_lock);
		kmem_free(buf, alloc);
		return;
	}
	dst->v_path = buf;
	mutex_exit(&dst->v_lock);
}

int vn_fromfd(int fd, char *path, int flags, struct vnode **vpp, boolean_t fromfd)
{
	vnode_t *vp;

	*vpp = vp = kmem_cache_alloc(vnode_cache, KM_SLEEP);
	memset(vp, 0, sizeof(vnode_t));

	if (fstat64(fd, &vp->v_stat) == -1) {
		close(fd);
		return (errno);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	vp->v_fd = fd;
	if(S_ISBLK(vp->v_stat.st_mode)) {
		/* LINUX */
		if(ioctl(fd, BLKGETSIZE64, &vp->v_size) != 0)
			return errno;
	} else
		vp->v_size = vp->v_stat.st_size;
	vp->v_path = strdup(path);

	vp->v_type = VNON;

	if(fromfd)
		vn_setops(vp, fd_fvnodeops);
	else
		vn_setops(vp, root_fvnodeops);

	if(S_ISREG(vp->v_stat.st_mode)) {
		vp->v_type = VREG;
		if (flags & FREAD)
			atomic_add_32(&((*vpp)->v_rdcnt), 1);
		if (flags & FWRITE)
			atomic_add_32(&((*vpp)->v_wrcnt), 1);
	} else if(S_ISDIR(vp->v_stat.st_mode))
		vp->v_type = VDIR;
	else if(S_ISCHR(vp->v_stat.st_mode))
		vp->v_type = VCHR;
	else if(S_ISBLK(vp->v_stat.st_mode))
		vp->v_type = VBLK;
	else if(S_ISFIFO(vp->v_stat.st_mode))
		vp->v_type = VFIFO;
	else if(S_ISLNK(vp->v_stat.st_mode))
		vp->v_type = VLNK;
	else if(S_ISSOCK(vp->v_stat.st_mode))
		vp->v_type = VSOCK;

	VERIFY(vp->v_type != VNON);

	zmutex_init(&vp->v_lock);
	rwst_init(&vp->v_vfsmhlock.ve_lock, NULL, RW_DEFAULT, NULL);

	vp->v_count = 1;
	vp->v_vfsp = rootvfs;

	/*fprintf(stderr, "VNode %p created at vn_open (%s)\n", *vpp, path);*/
	return (0);
}

/*
 * Note: for the xxxat() versions of these functions, we assume that the
 * starting vp is always rootdir (which is true for spa_directory.c, the only
 * ZFS consumer of these interfaces).  We assert this is true, and then emulate
 * them by adding '/' in front of the path.
 */

/*ARGSUSED*/
int
vn_open(char *path, enum uio_seg x1, int flags, int mode, vnode_t **vpp, enum create x2, mode_t x3)
{
	int fd;
	int old_umask = 0;
	struct stat64 st;

	if (!(flags & FCREAT) && stat64(path, &st) == -1)
		return (errno);

	if (flags & FCREAT)
		old_umask = umask(0);

	if (!(flags & FCREAT) && S_ISBLK(st.st_mode)) {
		flags |= O_DIRECT;
		/* O_EXCL can't be passed for hot spares : they can be shared
		 * between pools */
	/*	if (flags & FWRITE)
			flags |= O_EXCL; */
	}

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(path, flags - FREAD, mode);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (fd == -1)
		return (errno);

	return vn_fromfd(fd, path, flags, vpp, B_FALSE);
}

int
vn_openat(char *path, enum uio_seg x1, int flags, int mode, vnode_t **vpp, enum create x2,
    mode_t x3, vnode_t *startvp, int fd)
{
	char *realpath = kmem_alloc(strlen(path) + 2, KM_SLEEP);
	int ret;

	ASSERT(startvp == rootdir);
	(void) sprintf(realpath, "/%s", path);

	/* fd ignored for now, need if want to simulate nbmand support */
	ret = vn_open(realpath, x1, flags, mode, vpp, x2, x3);

	kmem_free(realpath, strlen(path) + 2);

	return (ret);
}

/*
 * Read or write a vnode.  Called from kernel code.
 */
int
vn_rdwr(
	enum uio_rw rw,
	struct vnode *vp,
	caddr_t base,
	ssize_t len,
	offset_t offset,
	enum uio_seg seg,
	int ioflag,
	rlim64_t ulimit,	/* meaningful only if rw is UIO_WRITE */
	cred_t *cr,
	ssize_t *residp)
{
	struct uio uio;
	struct iovec iov;
	int error;

	if (rw == UIO_WRITE && ISROFILE(vp))
		return (EROFS);

	if (len < 0)
		return (EIO);

	iov.iov_base = base;
	iov.iov_len = len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_loffset = offset;
	uio.uio_segflg = (short)seg;
	uio.uio_resid = len;
	uio.uio_llimit = ulimit;

	(void) VOP_RWLOCK(vp,
		rw == UIO_WRITE ? V_WRITELOCK_TRUE : V_WRITELOCK_FALSE, NULL);
	if (rw == UIO_WRITE) {
		uio.uio_fmode = FWRITE;
		uio.uio_extflg = UIO_COPY_DEFAULT;
		error = VOP_WRITE(vp, &uio, ioflag, cr, NULL);
	} else {
		uio.uio_fmode = FREAD;
		uio.uio_extflg = UIO_COPY_CACHED;
		error = VOP_READ(vp, &uio, ioflag, cr, NULL);
	}
	VOP_RWUNLOCK(vp, rw == UIO_WRITE ? V_WRITELOCK_TRUE : V_WRITELOCK_FALSE,
									NULL);
	if (residp)
		*residp = uio.uio_resid;
	else if (uio.uio_resid)
		error = EIO;

	return (error);
}

void vn_rele(vnode_t *vp)
{
	if (vp->v_count == 0)
		cmn_err(CE_PANIC, "vn_rele: vnode ref count 0");

	mutex_enter(&vp->v_lock);
	if(vp->v_count == 1) {
		mutex_exit(&vp->v_lock);
		/* fprintf(stderr, "VNode %p inactive\n", vp); */
		VOP_INACTIVE(vp, CRED(), NULL);
	} else {
		vp->v_count--;
		mutex_exit(&vp->v_lock);
	}
}

static void vn_rele_inactive(vnode_t *vp)
{
        VOP_INACTIVE(vp, CRED(), NULL);
}

void vn_rele_async(vnode_t *vp, taskq_t *taskq)
{
	VERIFY(vp->v_count > 0);
	mutex_enter(&vp->v_lock);
	if (vp->v_count == 1) {
		mutex_exit(&vp->v_lock);
		VERIFY(taskq_dispatch(taskq, (task_func_t *)vn_rele_inactive,
		    vp, UMEM_NOFAIL) != 0);
		return;
	}
	vp->v_count--;
	mutex_exit(&vp->v_lock);
}

void vn_close(vnode_t *vp)
{
	rwst_destroy(&vp->v_vfsmhlock.ve_lock);
	zmutex_destroy(&vp->v_lock);
	if(vp->v_fd != -1)
		close(vp->v_fd);
	if(vp->v_path != NULL)
		free(vp->v_path);
	kmem_cache_free(vnode_cache, vp);
	/* fprintf(stderr, "VNode %p freed\n", vp); */
}

int
vn_make_ops(
	const char *name,			/* Name of file system */
	const fs_operation_def_t *templ,	/* Operation specification */
	vnodeops_t **actual)			/* Return the vnodeops */
{
	int unused_ops;
	int error;

	*actual = (vnodeops_t *)kmem_alloc(sizeof (vnodeops_t), KM_SLEEP);

	(*actual)->vnop_name = name;

	error = fs_build_vector(*actual, &unused_ops, vn_ops_table, templ);
	if (error) {
		kmem_free(*actual, sizeof (vnodeops_t));
	}

#if DEBUG
	if (unused_ops != 0)
		cmn_err(CE_WARN, "vn_make_ops: %s: %d operations supplied "
		    "but not used", name, unused_ops);
#endif

	return (error);
}

/*
 * Free the vnodeops created as a result of vn_make_ops()
 */
void
vn_freevnodeops(vnodeops_t *vnops)
{
	kmem_free(vnops, sizeof (vnodeops_t));
}

/*
 * Set the operations vector for a vnode.
 */
void
vn_setops(vnode_t *vp, vnodeops_t *vnodeops)
{
	ASSERT(vp != NULL);
	ASSERT(vnodeops != NULL);

	vp->v_op = vnodeops;
}

int
vn_is_readonly(vnode_t *vp)
{
	return (vp->v_vfsp->vfs_flag & VFS_RDONLY);
}

int
fop_open(
	vnode_t **vpp,
	int mode,
	cred_t *cr,
	caller_context_t *ct)
{
	int ret;
	vnode_t *vp = *vpp;

	VN_HOLD(vp);
	/*
	 * Adding to the vnode counts before calling open
	 * avoids the need for a mutex. It circumvents a race
	 * condition where a query made on the vnode counts results in a
	 * false negative. The inquirer goes away believing the file is
	 * not open when there is an open on the file already under way.
	 *
	 * The counts are meant to prevent NFS from granting a delegation
	 * when it would be dangerous to do so.
	 *
	 * The vnode counts are only kept on regular files
	 */
	if ((*vpp)->v_type == VREG) {
		if (mode & FREAD)
			atomic_add_32(&((*vpp)->v_rdcnt), 1);
		if (mode & FWRITE)
			atomic_add_32(&((*vpp)->v_wrcnt), 1);
	}

	ret = (*(*(vpp))->v_op->vop_open)(vpp, mode, cr, ct);

	if (ret) {
		/*
		 * Use the saved vp just in case the vnode ptr got trashed
		 * by the error.
		 */
		VOPSTATS_UPDATE(vp, open);
		if ((vp->v_type == VREG) && (mode & FREAD))
			atomic_add_32(&(vp->v_rdcnt), -1);
		if ((vp->v_type == VREG) && (mode & FWRITE))
			atomic_add_32(&(vp->v_wrcnt), -1);
	} else {
		/*
		 * Some filesystems will return a different vnode,
		 * but the same path was still used to open it.
		 * So if we do change the vnode and need to
		 * copy over the path, do so here, rather than special
		 * casing each filesystem. Adjust the vnode counts to
		 * reflect the vnode switch.
		 */
		VOPSTATS_UPDATE(*vpp, open);
		if (*vpp != vp && *vpp != NULL) {
			vn_copypath(vp, *vpp);
			if (((*vpp)->v_type == VREG) && (mode & FREAD))
				atomic_add_32(&((*vpp)->v_rdcnt), 1);
			if ((vp->v_type == VREG) && (mode & FREAD))
				atomic_add_32(&(vp->v_rdcnt), -1);
			if (((*vpp)->v_type == VREG) && (mode & FWRITE))
				atomic_add_32(&((*vpp)->v_wrcnt), 1);
			if ((vp->v_type == VREG) && (mode & FWRITE))
				atomic_add_32(&(vp->v_wrcnt), -1);
		}
	}
	VN_RELE(vp);
	return (ret);
}

int
fop_close(
	vnode_t *vp,
	int flag,
	int count,
	offset_t offset,
	cred_t *cr,
	caller_context_t *ct)
{
	int err;

	err = (*(vp)->v_op->vop_close)(vp, flag, count, offset, cr, ct);
	VOPSTATS_UPDATE(vp, close);
	/*
	 * Check passed in count to handle possible dups. Vnode counts are only
	 * kept on regular files
	 */
	if ((vp->v_type == VREG) && (count == 1))  {
		if (flag & FREAD) {
			ASSERT(vp->v_rdcnt > 0);
			atomic_add_32(&(vp->v_rdcnt), -1);
		}
		if (flag & FWRITE) {
			ASSERT(vp->v_wrcnt > 0);
			atomic_add_32(&(vp->v_wrcnt), -1);
		}
	}
	return (err);
}

int
fop_read(
	vnode_t *vp,
	uio_t *uiop,
	int ioflag,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;
	/* ssize_t	resid_start = uiop->uio_resid; */

	err = (*(vp)->v_op->vop_read)(vp, uiop, ioflag, cr, ct);
	VOPSTATS_UPDATE_IO(vp, read,
	    read_bytes, (resid_start - uiop->uio_resid));
	return (err);
}

int
fop_readlink(
	vnode_t *vp,
	uio_t *uiop,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_readlink)(vp, uiop, cr, ct);
	VOPSTATS_UPDATE(vp, readlink);
	return (err);
}

int
fop_fsync(
	vnode_t *vp,
	int syncflag,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_fsync)(vp, syncflag, cr, ct);
	VOPSTATS_UPDATE(vp, fsync);
	return (err);
}

int
fop_getattr(
	vnode_t *vp,
	vattr_t *vap,
	int flags,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_getattr)(vp, vap, flags, cr, ct);
	VOPSTATS_UPDATE(vp, getattr);
	return (err);
}

void
fop_inactive(
	vnode_t *vp,
	cred_t *cr,
	caller_context_t *ct)
{
	/* Need to update stats before vop call since we may lose the vnode */
	VOPSTATS_UPDATE(vp, inactive);
	(*(vp)->v_op->vop_inactive)(vp, cr, ct);
}

int
fop_putpage(
	vnode_t *vp,
	offset_t off,
	size_t len,
	int flags,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_putpage)(vp, off, len, flags, cr, ct);
	VOPSTATS_UPDATE(vp, putpage);
	return (err);
}

int
fop_realvp(
	vnode_t *vp,
	vnode_t **vpp,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_realvp)(vp, vpp, ct);
	VOPSTATS_UPDATE(vp, realvp);
	return (err);
}

int
fop_lookup(
	vnode_t *dvp,
	char *nm,
	vnode_t **vpp,
	pathname_t *pnp,
	int flags,
	vnode_t *rdir,
	cred_t *cr,
	caller_context_t *ct,
	int *deflags,         /* Returned per-dirent flags */
	pathname_t *ppnp)     /* Returned case-preserved name in directory */
{
	int ret;

	ret = (*(dvp)->v_op->vop_lookup)
	    (dvp, nm, vpp, pnp, flags, rdir, cr, ct, deflags, ppnp);
	if (ret == 0 && *vpp) {
		VOPSTATS_UPDATE(*vpp, lookup);
		if ((*vpp)->v_path == NULL) {
			vn_setpath(rootdir, dvp, *vpp, nm, strlen(nm));
		}
	}

	return (ret);
}

int
fop_readdir(
	vnode_t *vp,
	uio_t *uiop,
	cred_t *cr,
	int *eofp,
	caller_context_t *ct,
	int flags)
{
	int	err;
	/* ssize_t	resid_start = uiop->uio_resid; */

	err = (*(vp)->v_op->vop_readdir)(vp, uiop, cr, eofp, ct, flags);
	VOPSTATS_UPDATE_IO(vp, readdir,
	    readdir_bytes, (resid_start - uiop->uio_resid));
	return (err);
}

int
fop_create(
	vnode_t *dvp,
	char *name,
	vattr_t *vap,
	vcexcl_t excl,
	int mode,
	vnode_t **vpp,
	cred_t *cr,
	int flags,
	caller_context_t *ct,
	vsecattr_t *vsecp)   /* ACL to set during create */
{
	int ret;

	ret = (*(dvp)->v_op->vop_create)
	    (dvp, name, vap, excl, mode, vpp, cr, flags, ct, vsecp);
	if (ret == 0 && *vpp) {
		VOPSTATS_UPDATE(*vpp, create);
		if ((*vpp)->v_path == NULL) {
			vn_setpath(rootdir, dvp, *vpp, name, strlen(name));
		}
	}

	return (ret);
}

int
fop_mkdir(
	vnode_t *dvp,
	char *dirname,
	vattr_t *vap,
	vnode_t **vpp,
	cred_t *cr,
	caller_context_t *ct,
	int flags,
	vsecattr_t *vsecp)    /* ACL to set during create */
{
	int ret;

	ret = (*(dvp)->v_op->vop_mkdir)
	    (dvp, dirname, vap, vpp, cr, ct, flags, vsecp);
	if (ret == 0 && *vpp) {
		VOPSTATS_UPDATE(*vpp, mkdir);
		if ((*vpp)->v_path == NULL) {
			vn_setpath(rootdir, dvp, *vpp, dirname,
			    strlen(dirname));
		}
	}

	return (ret);
}

int
fop_symlink(
	vnode_t *dvp,
	char *linkname,
	vattr_t *vap,
	char *target,
	cred_t *cr,
	caller_context_t *ct,
	int flags)
{
	int	err;

	err = (*(dvp)->v_op->vop_symlink)
	    (dvp, linkname, vap, target, cr, ct, flags);
	VOPSTATS_UPDATE(dvp, symlink);
	return (err);
}

int
fop_remove(
	vnode_t *dvp,
	char *nm,
	cred_t *cr,
	caller_context_t *ct,
	int flags)
{
	int	err;

	err = (*(dvp)->v_op->vop_remove)(dvp, nm, cr, ct, flags);
	VOPSTATS_UPDATE(dvp, remove);
	return (err);
}

int
fop_rmdir(
	vnode_t *dvp,
	char *nm,
	vnode_t *cdir,
	cred_t *cr,
	caller_context_t *ct,
	int flags)
{
	int	err;

	err = (*(dvp)->v_op->vop_rmdir)(dvp, nm, cdir, cr, ct, flags);
	VOPSTATS_UPDATE(dvp, rmdir);
	return (err);
}

int
fop_link(
	vnode_t *tdvp,
	vnode_t *svp,
	char *tnm,
	cred_t *cr,
	caller_context_t *ct,
	int flags)
{
	int	err;

	err = (*(tdvp)->v_op->vop_link)(tdvp, svp, tnm, cr, ct, flags);
	VOPSTATS_UPDATE(tdvp, link);
	return (err);
}

int
fop_rename(
	vnode_t *sdvp,
	char *snm,
	vnode_t *tdvp,
	char *tnm,
	cred_t *cr,
	caller_context_t *ct,
	int flags)
{
	int	err;

	err = (*(sdvp)->v_op->vop_rename)(sdvp, snm, tdvp, tnm, cr, ct, flags);
	VOPSTATS_UPDATE(sdvp, rename);
	return (err);
}

int
fop_space(
	vnode_t *vp,
	int cmd,
	flock64_t *bfp,
	int flag,
	offset_t offset,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_space)(vp, cmd, bfp, flag, offset, cr, ct);
	VOPSTATS_UPDATE(vp, space);
	return (err);
}

int
fop_setattr(
	vnode_t *vp,
	vattr_t *vap,
	int flags,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_setattr)(vp, vap, flags, cr, ct);
	VOPSTATS_UPDATE(vp, setattr);
	return (err);
}

int
fop_setsecattr(
	vnode_t *vp,
	vsecattr_t *vsap,
	int flag,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_setsecattr) (vp, vsap, flag, cr, ct);
	VOPSTATS_UPDATE(vp, setsecattr);
	return (err);
}

int
fop_getsecattr(
	vnode_t *vp,
	vsecattr_t *vsap,
	int flag,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_getsecattr) (vp, vsap, flag, cr, ct);
	VOPSTATS_UPDATE(vp, getsecattr);
	return (err);
}

int
fop_access(
	vnode_t *vp,
	int mode,
	int flags,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_access)(vp, mode, flags, cr, ct);
	VOPSTATS_UPDATE(vp, access);
	return (err);
}

int
fop_write(
	vnode_t *vp,
	uio_t *uiop,
	int ioflag,
	cred_t *cr,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_write)(vp, uiop, ioflag, cr, ct);
	VOPSTATS_UPDATE_IO(vp, write,
	    write_bytes, (resid_start - uiop->uio_resid));
	return (err);
}

int
fop_rwlock(
	vnode_t *vp,
	int write_lock,
	caller_context_t *ct)
{
	int	ret;

	ret = ((*(vp)->v_op->vop_rwlock)(vp, write_lock, ct));
	VOPSTATS_UPDATE(vp, rwlock);
	return (ret);
}

void
fop_rwunlock(
	vnode_t *vp,
	int write_lock,
	caller_context_t *ct)
{
	(*(vp)->v_op->vop_rwunlock)(vp, write_lock, ct);
	VOPSTATS_UPDATE(vp, rwunlock);
}

int
fop_seek(
	vnode_t *vp,
	offset_t ooff,
	offset_t *noffp,
	caller_context_t *ct)
{
	int	err;

	err = (*(vp)->v_op->vop_seek)(vp, ooff, noffp, ct);
	VOPSTATS_UPDATE(vp, seek);
	return (err);
}

static int
root_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr)
{
	VERIFY(vp->v_fd != -1);
	vap->va_size = vp->v_size;
	return 0;
}

static int
root_fsync(vnode_t *vp, int syncflag, cred_t *cr)
{
	VERIFY(vp->v_fd != -1);
	/* fprintf(stderr, "fsync!: %i\n", vp->v_fd); */
	return fsync(vp->v_fd);
}

static int
root_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
{
	VERIFY(vp->v_fd != -1);
	return close(vp->v_fd);
}

static int
root_read(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr, caller_context_t *ct)
{
	ASSERT(vp->v_fd != -1);
	ASSERT(vp->v_type != VBLK || IS_P2ALIGNED(uiop->uio_loffset, 512));
	ASSERT(vp->v_type != VBLK || IS_P2ALIGNED(uiop->uio_iov->iov_len, 512));

	int error = 0;

	ssize_t iolen = pread64(vp->v_fd, uiop->uio_iov->iov_base, uiop->uio_iov->iov_len, uiop->uio_loffset);
	if(iolen == -1) {
		error = errno;
		perror("pread64");
	}

	if(iolen != uiop->uio_iov->iov_len)
		fprintf(stderr, "root_read(): len: %lli iolen: %lli offset: %lli file: %s\n", (longlong_t) uiop->uio_iov->iov_len, (longlong_t) iolen, (longlong_t) uiop->uio_loffset, vp->v_path);

	if(error)
		return error;

	uiop->uio_resid -= iolen;

	return 0;
}

static int
root_write(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr, caller_context_t *ct)
{
	ASSERT(vp->v_fd != -1);
	ASSERT(vp->v_type != VBLK || IS_P2ALIGNED(uiop->uio_loffset, 512));
	ASSERT(vp->v_type != VBLK || IS_P2ALIGNED(uiop->uio_iov->iov_len, 512));

	int error = 0;

	ssize_t iolen = pwrite64(vp->v_fd, uiop->uio_iov->iov_base, uiop->uio_iov->iov_len, uiop->uio_loffset);
	if(iolen == -1) {
		error = errno;
		perror("pwrite64");
	}

	if(iolen != uiop->uio_iov->iov_len)
		fprintf(stderr, "root_write(): len: %lli iolen: %lli offset: %lli file: %s\n", (longlong_t) uiop->uio_iov->iov_len, (longlong_t) iolen, (longlong_t) uiop->uio_loffset, vp->v_path);

	if(error)
		return error;

	uiop->uio_resid -= iolen;

	return 0;
}

static int
fd_read(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr, caller_context_t *ct)
{
	VERIFY(vp->v_fd != -1);

	int error = 0;

	ssize_t iolen = read(vp->v_fd, uiop->uio_iov->iov_base, uiop->uio_iov->iov_len);
	if(iolen == -1) {
		error = errno;
		perror("read");
	}

	if(error)
		return error;

	uiop->uio_resid -= iolen;

	return 0;
}

static int
fd_write(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr, caller_context_t *ct)
{
	VERIFY(vp->v_fd != -1);

	int error = 0;

	ssize_t iolen = write(vp->v_fd, uiop->uio_iov->iov_base, uiop->uio_iov->iov_len);
	if(iolen == -1) {
		error = errno;
		perror("write");
	}

	if(iolen != uiop->uio_iov->iov_len)
		fprintf(stderr, "fd_write(): len: %lli iolen: %lli offset: %lli file: %s\n", (longlong_t) uiop->uio_iov->iov_len, (longlong_t) iolen, (longlong_t) uiop->uio_loffset, vp->v_path);

	if(error)
		return error;

	uiop->uio_resid -= iolen;

	return 0;
}

const fs_operation_def_t root_fvnodeops_template[] = {
	VOPNAME_GETATTR, root_getattr,
	VOPNAME_FSYNC, root_fsync,
	VOPNAME_CLOSE, root_close,
	VOPNAME_READ, root_read,
	VOPNAME_WRITE, root_write,
	NULL, NULL
};

const fs_operation_def_t fd_fvnodeops_template[] = {
	VOPNAME_GETATTR, root_getattr,
	VOPNAME_FSYNC, root_fsync,
	VOPNAME_READ, fd_read,
	VOPNAME_WRITE, fd_write,
	VOPNAME_CLOSE, root_close,
	NULL, NULL
};

/* Extensible attribute (xva) routines. */

/*
 * Zero out the structure, set the size of the requested/returned bitmaps,
 * set AT_XVATTR in the embedded vattr_t's va_mask, and set up the pointer
 * to the returned attributes array.
 */
void
xva_init(xvattr_t *xvap)
{
	bzero(xvap, sizeof (xvattr_t));
	xvap->xva_mapsize = XVA_MAPSIZE;
	xvap->xva_magic = XVA_MAGIC;
	xvap->xva_vattr.va_mask = AT_XVATTR;
	xvap->xva_rtnattrmapp = &(xvap->xva_rtnattrmap)[0];
}

/*
 * If AT_XVATTR is set, returns a pointer to the embedded xoptattr_t
 * structure.  Otherwise, returns NULL.
 */
xoptattr_t *
xva_getxoptattr(xvattr_t *xvap)
{
	xoptattr_t *xoap = NULL;
	if (xvap->xva_vattr.va_mask & AT_XVATTR)
		xoap = &xvap->xva_xoptattrs;
	return (xoap);
}
