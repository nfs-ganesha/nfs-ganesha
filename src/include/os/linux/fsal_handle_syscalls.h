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

/**
 * @file include/os/linux/fsal_handle_syscalls.h
 * @brief System calls for the Linux handle calls
 *
 */

#ifndef HANDLE_LINUX_H
#define HANDLE_LINUX_H

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH           0x1000
#endif

/*
 * This is the Linux-specific variation for the by-handle or "at" syscalls.
 * The vfs_file_handle_t is a redefinition of struct file_handle so
 * the code here just overlays the two structures on top of each other.
 */

#ifndef MAX_HANDLE_SZ

/* syscalls introduced in 2.6.39 and enabled in glibc 2.14
 * if we are not building against 2.14, create our own versions
 * as inlines. Glibc versions are externs to glibc...
 */

#define MAX_HANDLE_SZ 128
typedef unsigned int __u32;

struct file_handle {
	__u32 handle_bytes;
	int handle_type;
	/* file identifier */
	unsigned char f_handle[0];
};

#if defined(__i386__)
#define __NR_name_to_handle_at  341
#define __NR_open_by_handle_at  342
#elif defined(__x86_64__)
#define __NR_name_to_handle_at  303
#define __NR_open_by_handle_at  304
#endif

static inline int name_to_handle_at(int mdirfd, const char *name,
				    struct file_handle * handle, int *mnt_id, int flags)
{
	return syscall(__NR_name_to_handle_at, mdirfd, name, handle, mnt_id, flags);
}

static inline int open_by_handle_at(int mdirfd, struct file_handle * handle,
				    int flags)
{
	return syscall(__NR_open_by_handle_at, mdirfd, handle, flags);
}
#endif /* MAX_HANDLE_SZ */

#ifndef O_PATH
#define O_PATH 010000000
#endif

#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

#ifndef O_NOACCESS
#define O_NOACCESS O_ACCMODE
#endif

static inline size_t vfs_sizeof_handle(vfs_file_handle_t *fh)
{
	struct file_handle *hdl = (struct file_handle *)fh;
	return offsetof(struct file_handle, f_handle) + hdl->handle_bytes;
}

#define vfs_alloc_handle(fh) \
	(fh) = alloca(sizeof(struct file_handle) + MAX_HANDLE_SZ); \
	memset((fh), 0, (sizeof(struct file_handle) + MAX_HANDLE_SZ)); \
	(fh)->handle_bytes = MAX_HANDLE_SZ;

static inline int vfs_fd_to_handle(int fd, vfs_file_handle_t *fh, int *mnt_id)
{
	return name_to_handle_at(fd, "", (struct file_handle *)fh, mnt_id, AT_EMPTY_PATH);
}

static inline int vfs_name_to_handle_at(int atfd, const char *name, vfs_file_handle_t *fh)
{
	int mnt_id = 0;
	return name_to_handle_at(atfd, name,(struct file_handle *)fh, &mnt_id, 0);
}

static inline int vfs_open_by_handle(int mountfd, vfs_file_handle_t *fh, int flags)
{
	return open_by_handle_at(mountfd, (struct file_handle *)fh, flags);
}

static inline int vfs_stat_by_handle(int mountfd, vfs_file_handle_t *fh, struct stat *buf,
				   int flags)
{
	/* Must use fstatat() even though fstat() seems like it might work, the Linux
	 * version rejects the file descriptor we've obtained with the O_NOACCESS flag */
	return fstatat(mountfd, "", buf, AT_EMPTY_PATH);
}

static inline int vfs_chown_by_handle(int mountfd, vfs_file_handle_t *fh, uid_t owner, gid_t group)
{
        int fd, ret;
        fd = vfs_open_by_handle(mountfd, fh, (O_PATH|O_NOACCESS));
        if (fd < 0)
                return fd;
        /* Must use fchownat - cannot use fchown on this kind of file descriptor */
        ret = fchownat(fd, "", owner, group, AT_EMPTY_PATH);
        close(fd);
        return ret;
}


#endif /* HANDLE_LINUX_H */
/** @} */
