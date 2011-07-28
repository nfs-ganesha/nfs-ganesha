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

#include <sys/debug.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/cmn_err.h>
#include <sys/mntent.h>
#include <fs/fs_subr.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

struct vfs st_rootvfs = {};

static vnode_t st_rootdir = {
	.v_fd = -1,
	.v_type = VDIR,
	.v_count = 1
};
static pthread_rwlock_t vfslist;

struct vfssw vfssw[] = {
	{ "BADVFS" },
	{ "zfs" },
	{ "" }
};

/*
 * VFS global data.
 */
vnode_t *rootdir = &st_rootdir; /* pointer to root vnode. */
struct vfs *rootvfs = &st_rootvfs;

vnodeops_t *root_fvnodeops;
vnodeops_t *fd_fvnodeops;

extern const fs_operation_def_t root_fvnodeops_template[];
extern const fs_operation_def_t fd_fvnodeops_template[];

void vfs_init()
{
	VERIFY(pthread_rwlock_init(&vfslist, NULL) == 0);

	rootvfs->vfs_next = rootvfs;
	rootvfs->vfs_prev = rootvfs;

	rootdir->v_vfsp = rootvfs;

	int error = vn_make_ops(MNTTYPE_ROOT, root_fvnodeops_template,
	    &root_fvnodeops);
	if (error)
		abort();

	error = vn_make_ops(MNTTYPE_FD, fd_fvnodeops_template,
	    &fd_fvnodeops);
	if (error)
		abort();
}

void vfs_list_lock()
{
	VERIFY(pthread_rwlock_wrlock(&vfslist) == 0);
}

void vfs_list_read_lock()
{
	VERIFY(pthread_rwlock_rdlock(&vfslist) == 0);
}

void vfs_list_unlock()
{
	VERIFY(pthread_rwlock_unlock(&vfslist) == 0);
}

void vfs_exit()
{
	VERIFY(pthread_rwlock_destroy(&vfslist) == 0);
}

/*
 * Increments the vfs reference count by one atomically.
 */
void
vfs_hold(vfs_t *vfsp)
{
	atomic_add_32(&vfsp->vfs_count, 1);
	ASSERT(vfsp->vfs_count != 0);
}

/*
 * Decrements the vfs reference count by one atomically. When
 * vfs reference count becomes zero, it calls the file system
 * specific vfs_freevfs() to free up the resources.
 */
void
vfs_rele(vfs_t *vfsp)
{
	ASSERT(vfsp->vfs_count != 0);
	if (atomic_add_32_nv(&vfsp->vfs_count, -1) == 0) {
#ifdef DEBUG
		fprintf(stderr, "VFS is being freed\n");
#endif
		VFS_FREEVFS(vfsp);
/*		vfs_freemnttab(vfsp);
		if (vfsp->vfs_implp)
			vfsimpl_teardown(vfsp);
		sema_destroy(&vfsp->vfs_reflock);*/
		kmem_free(vfsp, sizeof (*vfsp));
	}
}

/* Placeholder functions, should never be called. */

int
fs_error(void)
{
	cmn_err(CE_PANIC, "fs_error called");
	return (0);
}

int
fs_default(void)
{
	cmn_err(CE_PANIC, "fs_default called");
	return (0);
}

int
fs_build_vector(void *vector, int *unused_ops,
    const fs_operation_trans_def_t *translation,
    const fs_operation_def_t *operations)
{
	int i, num_trans, num_ops, used;

	/*
	 * Count the number of translations and the number of supplied
	 * operations.
	 */

	{
		const fs_operation_trans_def_t *p;

		for (num_trans = 0, p = translation;
		    p->name != NULL;
		    num_trans++, p++)
			;
	}

	{
		const fs_operation_def_t *p;

		for (num_ops = 0, p = operations;
		    p->name != NULL;
		    num_ops++, p++)
			;
	}

	/* Walk through each operation known to our caller.  There will be */
	/* one entry in the supplied "translation table" for each. */

	used = 0;

	for (i = 0; i < num_trans; i++) {
		int j, found;
		char *curname;
		fs_generic_func_p result;
		fs_generic_func_p *location;

		curname = translation[i].name;

		/* Look for a matching operation in the list supplied by the */
		/* file system. */

		found = 0;

		for (j = 0; j < num_ops; j++) {
			if (strcmp(operations[j].name, curname) == 0) {
				used++;
				found = 1;
				break;
			}
		}

		/*
		 * If the file system is using a "placeholder" for default
		 * or error functions, grab the appropriate function out of
		 * the translation table.  If the file system didn't supply
		 * this operation at all, use the default function.
		 */

		if (found) {
			result = operations[j].func.fs_generic;
			if (result == fs_default) {
				result = translation[i].defaultFunc;
			} else if (result == fs_error) {
				result = translation[i].errorFunc;
			} else if (result == NULL) {
				/* Null values are PROHIBITED */
				return (EINVAL);
			}
		} else {
			result = translation[i].defaultFunc;
		}

		/* Now store the function into the operations vector. */

		location = (fs_generic_func_p *)
		    (((char *)vector) + translation[i].offset);

		*location = result;
	}

	*unused_ops = num_ops - used;

	return (0);
}

/*
 * File system operation dispatch functions.
 */

int
fsop_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	return (*(vfsp)->vfs_op->vfs_mount)(vfsp, mvp, uap, cr);
}

int
fsop_unmount(vfs_t *vfsp, int flag, cred_t *cr)
{
	return (*(vfsp)->vfs_op->vfs_unmount)(vfsp, flag, cr);
}

int
fsop_statfs(vfs_t *vfsp, statvfs64_t *sp)
{
	return (*(vfsp)->vfs_op->vfs_statvfs)(vfsp, sp);
}

void
fsop_freefs(vfs_t *vfsp)
{
	(*(vfsp)->vfs_op->vfs_freevfs)(vfsp);
}

int
fsop_sync(vfs_t *vfsp, short flag, cred_t *cr)
{
	return (*(vfsp)->vfs_op->vfs_sync)(vfsp, flag, cr);
}

/*
 * File system initialization.  vfs_setfsops() must be called from a file
 * system's init routine.
 */

static int
fs_copyfsops(const fs_operation_def_t *template, vfsops_t *actual,
    int *unused_ops)
{
	static const fs_operation_trans_def_t vfs_ops_table[] = {
		VFSNAME_MOUNT, offsetof(vfsops_t, vfs_mount),
			fs_nosys, fs_nosys,

		VFSNAME_UNMOUNT, offsetof(vfsops_t, vfs_unmount),
			fs_nosys, fs_nosys,

		VFSNAME_ROOT, offsetof(vfsops_t, vfs_root),
			fs_nosys, fs_nosys,

		VFSNAME_STATVFS, offsetof(vfsops_t, vfs_statvfs),
			fs_nosys, fs_nosys,

		VFSNAME_SYNC, offsetof(vfsops_t, vfs_sync),
			(fs_generic_func_p) fs_sync,
			(fs_generic_func_p) fs_sync,	/* No errors allowed */

		VFSNAME_VGET, offsetof(vfsops_t, vfs_vget),
			fs_nosys, fs_nosys,

		VFSNAME_MOUNTROOT, offsetof(vfsops_t, vfs_mountroot),
			fs_nosys, fs_nosys,

		VFSNAME_FREEVFS, offsetof(vfsops_t, vfs_freevfs),
			(fs_generic_func_p)fs_freevfs,
			(fs_generic_func_p)fs_freevfs,	/* Shouldn't fail */

		VFSNAME_VNSTATE, offsetof(vfsops_t, vfs_vnstate),
			(fs_generic_func_p)fs_nosys,
			(fs_generic_func_p)fs_nosys,

		NULL, 0, NULL, NULL
	};

	return (fs_build_vector(actual, unused_ops, vfs_ops_table, template));
}

int
vfs_setfsops(int fstype, const fs_operation_def_t *template, vfsops_t **actual)
{
	int error;
	int unused_ops;

	/* Verify that fstype refers to a loaded fs (and not fsid 0). */

	if ((fstype <= 0) || (fstype >= nfstype))
		return (EINVAL);

	/* Set up the operations vector. */

	error = fs_copyfsops(template, &vfssw[fstype].vsw_vfsops, &unused_ops);

	if (error != 0)
		return (error);

	if (actual != NULL)
		*actual = &vfssw[fstype].vsw_vfsops;

#if DEBUG
	if (unused_ops != 0)
		cmn_err(CE_WARN, "vfs_setfsops: %s: %d operations supplied "
		    "but not used", vfssw[fstype].vsw_name, unused_ops);
#endif

	return (0);
}

int vfs_freevfsops_by_type(int t)
{
	// cmn_err(CE_WARN, "vfs.c: vfs_freevfsops_by_type unimplemented");
	/* Apparently this is here for compatibility with the vfs layer in
	 * opensolaris, but this function is not supposed to do anything at all
	 * since the vfs operations are not really allocated for zfs. */
	return 0;
}
