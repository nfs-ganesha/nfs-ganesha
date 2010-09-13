/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fsnotify.h>
#include <linux/quotaops.h>
#include <asm/uaccess.h>

/* Ugly GPFS hack!!! */
#define NFSEXP_NOSUBTREECHECK	0x0400
struct cache_head
{
    struct cache_head *next;
    time_t expiry_time;           /* After time time, don't use the data */
    time_t last_refresh;          /* If CACHE_PENDING, this is when upcall
                                   * was sent, else this is when update was received
                                   */
    struct kref ref;
    unsigned long flags;
};

struct auth_ops;
struct auth_domain
{
    struct kref ref;
    struct hlist_node hash;
    char *name;
    struct auth_ops *flavour;
};

struct handle_svc_export
{
    struct cache_head h;
    struct auth_domain *ex_client;
    int ex_flags;
};

#include "handle.h"

extern struct export_operations export_op_default;
#define CALL(ops,fun) ((ops->fun)?(ops->fun):export_op_default.fun)

/* limit the handle size to some value */
#define MAX_HANDLE_SZ 4096
static long do_sys_name_to_handle(struct nameidata *nd, struct file_handle __user * ufh)
{
    long retval;
    int handle_size;
    struct file_handle f_handle;
    struct file_handle *handle = NULL;
    const struct export_operations *exop = nd->dentry->d_sb->s_export_op;

    if(copy_from_user(&f_handle, ufh, sizeof(struct file_handle)))
        {
            retval = -EFAULT;
            goto err_out;
        }
    if(f_handle.handle_size > MAX_HANDLE_SZ)
        {
            retval = -EINVAL;
            goto err_out;
        }
    handle = kmalloc(sizeof(struct file_handle) + f_handle.handle_size, GFP_KERNEL);
    if(!handle)
        {
            retval = -ENOMEM;
            goto err_out;
        }

    handle_size = f_handle.handle_size >> 2;
    /* we ask for a non connected handle */
    retval = CALL(exop, encode_fh) (nd->dentry, (__u32 *) handle->f_handle,
                                    &handle_size, 0);
    /* convert handle size to bytes */
    handle_size *= sizeof(u32);
    handle->handle_size = handle_size;
    if(retval == 255)
        {
            retval = -ENOSPC;

        }
    else
        {
            handle->handle_type = retval;
            retval = 0;
            if(copy_to_user(ufh, handle, sizeof(struct file_handle) + handle_size))
                retval = -EFAULT;
        }

    kfree(handle);
err_out:
    return retval;
}

long name_to_handle_at(int dfd, const char __user * name,
                       struct file_handle __user * handle, int flag)
{

    int follow;
    long ret = -EINVAL;
    struct nameidata nd;
    struct file *file = NULL;

    if(!capable(CAP_DAC_OVERRIDE))
        {
            ret = -EPERM;
            goto err_out;
        }
    if((flag & ~AT_SYMLINK_FOLLOW) != 0)
        goto err_out;

    if(name == NULL && dfd != AT_FDCWD)
        {
            file = fget(dfd);

            if(file)
                nd.dentry = file->f_dentry;
            else
                {
                    ret = -EBADF;
                    goto err_out;
                }
        }
    else
        {
            follow = (flag & AT_SYMLINK_FOLLOW) ? LOOKUP_FOLLOW : 0;
            ret = __user_walk_fd(dfd, name, follow, &nd);
            if(ret)
                goto err_out;
        }
    ret = do_sys_name_to_handle(&nd, handle);
    if(file)
        fput(file);
    else
        path_release(&nd);

err_out:
    return ret;
}

static struct vfsmount *get_vfsmount_from_fd(int fd)
{
    int fput_needed;
    struct vfsmount *mnt;
    struct file *filep;

    if(fd == AT_FDCWD)
        {
            struct fs_struct *fs = current->fs;
            read_lock(&fs->lock);
            mnt = fs->pwdmnt;
            mntget(mnt);
            read_unlock(&fs->lock);
        }
    else
        {
            filep = fget_light(fd, &fput_needed);
            if(!filep)
                return ERR_PTR(-EBADF);
            mnt = filep->f_vfsmnt;
            mntget(mnt);
            fput_light(filep, fput_needed);
        }
    return mnt;
}

static int vfs_dentry_acceptable(void *context, struct dentry *dentry)
{
    return 1;
}

static struct dentry *handle_to_dentry(int mountdirfd,
                                       struct file_handle *handle, struct vfsmount **mntp)
{
    int retval;
    int handle_size;
    struct vfsmount *mnt;
    struct dentry *dentry;
    struct handle_svc_export svc;
    const struct export_operations *exop;

    mnt = get_vfsmount_from_fd(mountdirfd);
    if(IS_ERR(mnt))
        {
            retval = PTR_ERR(mnt);
            goto out_err;
        }
    exop = mnt->mnt_sb->s_export_op;
    /* change the handle size to multiple of sizeof(u32) */
    handle_size = handle->handle_size >> 2;
    /*
     * GPFS overload acceptable callback. To make GPFS return
     * 1 set svc->ex_flags
     */
    memset(&svc, 0, sizeof(svc));
    svc.ex_flags = NFSEXP_NOSUBTREECHECK;
    dentry = CALL(exop, decode_fh) (mnt->mnt_sb,
                                    (__u32 *) handle->f_handle,
                                    handle_size, handle->handle_type,
                                    vfs_dentry_acceptable, &svc);
    if(IS_ERR(dentry))
        {
            retval = PTR_ERR(dentry);
            goto out_mnt;
        }
    if(!dentry)
        {
            retval = -ESTALE;
            goto out_mnt;
        }
    *mntp = mnt;
    return dentry;
out_mnt:
    mntput(mnt);
out_err:
    return ERR_PTR(retval);
}

#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

int should_remove_suid(struct dentry *dentry)
{
    mode_t mode = dentry->d_inode->i_mode;
    int kill = 0;

    /* suid always must be killed */
    if(unlikely(mode & S_ISUID))
        kill = ATTR_KILL_SUID;

    /*
     * sgid without any exec bits is just a mandatory locking mark; leave
     * it alone.  If some exec bits are set, it's a real sgid; kill it.
     */
    if(unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
        kill |= ATTR_KILL_SGID;

    if(unlikely(kill && !capable(CAP_FSETID)))
        return kill;

    return 0;
}

int handle_truncate(struct dentry *dentry, loff_t length, unsigned int time_attrs)
{
    int err;
    struct iattr newattrs;

    /* Not pretty: "inode->i_size" shouldn't really be signed. But it is. */
    if(length < 0)
        return -EINVAL;

    newattrs.ia_size = length;
    newattrs.ia_valid = ATTR_SIZE | time_attrs;

    /* Remove suid/sgid on truncate too */
    newattrs.ia_valid |= should_remove_suid(dentry);

    mutex_lock(&dentry->d_inode->i_mutex);
    err = notify_change(dentry, &newattrs);
    mutex_unlock(&dentry->d_inode->i_mutex);
    return err;
}

int handle_permission(struct inode *inode, int mask)
{
	int submask;
	umode_t mode = inode->i_mode;

	if (mask & MAY_WRITE) {

		/*
		 * Nobody gets write access to a read-only fs.
		 */
		if (IS_RDONLY(inode) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;

		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	/*
	 * MAY_EXEC on regular files requires special handling: We override
	 * filesystem execute permissions if the mode bits aren't set.
	 */
	if ((mask & MAY_EXEC) && S_ISREG(mode) && !(mode & S_IXUGO))
		return -EACCES;

	/* Ordinary permission routines do not understand MAY_APPEND. */
	submask = mask & ~MAY_APPEND;
#ifdef IN_KERNEL_CHANGE_NOT_SUPP

	/* FIXME! we don't have nameidata for handle lookup. So not sure how
         * we can check for inode->permissions. We already limit the call
         * to CAP_DAC_OVERRIDE. So we should be able to skip the ACL check.
         * But we also skip security_inode_permission below.
         */

	if (inode->i_op && inode->i_op->permission)
		retval = inode->i_op->permission(inode, submask, nd);
	else
		retval = generic_permission(inode, submask, NULL);
	if (retval)
		return retval;

	return security_inode_permission(inode, mask, nd);
#else
        return generic_permission(inode, submask, NULL);
#endif

}

static int may_handle_open(struct dentry *dentry, int open_flag)
{
    int acc_mode;
    int error;
    struct inode *inode = dentry->d_inode;

    if((open_flag + 1) & O_ACCMODE)
        open_flag++;

    acc_mode = ACC_MODE(open_flag);

    /* O_TRUNC implies we need access checks for write permissions */
    if(open_flag & O_TRUNC)
        acc_mode |= MAY_WRITE;

    /* Allow the LSM permission hook to distinguish append 
       access from general write access. */
    if(open_flag & O_APPEND)
        acc_mode |= MAY_APPEND;

    if(S_ISDIR(inode->i_mode) && (acc_mode & MAY_WRITE))
        return -EISDIR;

    error = handle_permission(inode, acc_mode);
    if (error)
        return error;

    if(S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode))
        {
            open_flag &= ~O_TRUNC;

        }
    else if(S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode))
        {
#ifdef IN_KERNEL_CHANGE_NOT_SUPP
            if(nd->mnt->mnt_flags & MNT_NODEV)
                return -EACCES;
#endif
            open_flag &= ~O_TRUNC;
        }
    else if(IS_RDONLY(inode) && (acc_mode & MAY_WRITE))
        return -EROFS;
    /*
     * An append-only file must be opened in append mode for writing.
     */
    if(IS_APPEND(inode))
        {
            if((open_flag & FMODE_WRITE) && !(open_flag & O_APPEND))
                return -EPERM;
            if(open_flag & O_TRUNC)
                return -EPERM;
        }

    /* O_NOATIME can only be set by the owner or superuser */
    if(open_flag & O_NOATIME)
        if(current->fsuid != inode->i_uid && !capable(CAP_FOWNER))
            return -EPERM;

    /*
     * Ensure there are no outstanding leases on the file.
     */
    error = break_lease(inode, open_flag);
    if(error)
        return error;

    if(open_flag & O_TRUNC)
        {
            error = get_write_access(inode);
            if(error)
                return error;

#ifdef IN_KERNEL_CHANGE_NOT_SUPP
            /*
             * Refuse to truncate files with mandatory locks held on them.
             */
            error = locks_verify_locked(inode);
#endif
            if(!error)
                {
                    DQUOT_INIT(inode);
                    error = handle_truncate(dentry, 0, ATTR_MTIME | ATTR_CTIME);
                }
            put_write_access(inode);
            if(error)
                return error;
        }
    else if(open_flag & FMODE_WRITE)
        DQUOT_INIT(inode);
    return 0;
}

long do_sys_open_by_handle(int mountdirfd, struct file_handle __user * ufh, int open_flag)
{
    int fd;
    long retval = 0;
    struct file *filp;
    struct dentry *dentry;
    struct file_handle f_handle;
    struct vfsmount *mnt = NULL;
    struct file_handle *handle = NULL;

    /* can't use O_CREATE with open_by_handle */
    if(open_flag & O_CREAT)
        {
            retval = -EINVAL;
            goto out_err;
        }
    if(copy_from_user(&f_handle, ufh, sizeof(struct file_handle)))
        {
            retval = -EFAULT;
            goto out_err;
        }
    if((f_handle.handle_size > MAX_HANDLE_SZ) || (f_handle.handle_size <= 0))
        {
            retval = -EINVAL;
            goto out_err;
        }
    handle = kmalloc(sizeof(struct file_handle) + f_handle.handle_size, GFP_KERNEL);
    if(!handle)
        {
            retval = -ENOMEM;
            goto out_err;
        }
    /* copy the full handle */
    if(copy_from_user(handle, ufh, sizeof(struct file_handle) + f_handle.handle_size))
        {
            retval = -EFAULT;
            goto out_handle;
        }
    dentry = handle_to_dentry(mountdirfd, handle, &mnt);
    if(IS_ERR(dentry))
        {
            retval = PTR_ERR(dentry);
            goto out_handle;
        }
    retval = may_handle_open(dentry, open_flag);
    if(retval)
        goto out_handle;
    fd = get_unused_fd();
    if(fd < 0)
        {
            retval = fd;
            goto out_dentry;
        }
    filp = dentry_open(dentry, mnt, open_flag);
    if(IS_ERR(filp))
        {
            put_unused_fd(fd);
            retval = PTR_ERR(filp);
        }
    else
        {
            retval = fd;
            fsnotify_open(filp->f_dentry);
            fd_install(fd, filp);
        }
    kfree(handle);
    return retval;

out_dentry:
    dput(dentry);
    mntput(mnt);
out_handle:
    kfree(handle);
out_err:
    return retval;
}

long open_by_handle(int mountdirfd, struct file_handle __user * handle, int flags)
{
    long ret;

    if(!capable(CAP_DAC_OVERRIDE))
        return -EPERM;

    if(force_o_largefile())
        flags |= O_LARGEFILE;

    ret = do_sys_open_by_handle(mountdirfd, handle, flags);
    return ret;
}

long link_by_fd(int file_fd, int newdfd, const char __user * newname)
{
    int error;
    int fput_needed;
    struct file *filep;
    struct nameidata nd;
    struct dentry *new_dentry;

    filep = fget_light(file_fd, &fput_needed);
    if(!filep)
        return -EBADF;
    error = __user_walk_fd(newdfd, newname, LOOKUP_PARENT, &nd);
    if(error)
        goto file_out;
    error = -EXDEV;
    if(filep->f_vfsmnt != nd.mnt)
        goto out_release;
    new_dentry = lookup_create(&nd, 0);
    error = PTR_ERR(new_dentry);
    if(!IS_ERR(new_dentry))
        {
            error = vfs_link(filep->f_dentry, nd.dentry->d_inode, new_dentry);
            dput(new_dentry);
        }
    mutex_unlock(&nd.dentry->d_inode->i_mutex);
out_release:
    path_release(&nd);
file_out:
    fput_light(filep, fput_needed);

    return error;
}

long readlink_by_fd(int fd, char __user * buf, int buffsize)
{
    int error = 0;
    int fput_needed;
    struct file *filep;
    struct inode *inode;

    filep = fget_light(fd, &fput_needed);
    if(!filep)
        return -EBADF;

    inode = filep->f_dentry->d_inode;
    error = -EINVAL;

    if(inode->i_op && inode->i_op->readlink)
        {
            touch_atime(filep->f_vfsmnt, filep->f_dentry);
            error = inode->i_op->readlink(filep->f_dentry, buf, buffsize);
        }

    fput_light(filep, fput_needed);
    return error;
}
