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
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>	
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/avl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/fs/zfs.h>

#include "zfsfuse_socket.h"

__thread int cur_fd = -1;

avl_tree_t fd_avl;
pthread_mutex_t fd_avl_mtx = PTHREAD_MUTEX_INITIALIZER;

/*
 * AVL comparison function used to order the fd tree
 */
int
zfsfuse_fd_compare(const void *arg1, const void *arg2)
{
	const file_t *f1 = arg1;
	const file_t *f2 = arg2;

	if (f1->f_client > f2->f_client)
		return 1;
	if (f1->f_client < f2->f_client)
		return -1;

	if (f1->f_oldfd > f2->f_oldfd)
		return 1;
	if (f1->f_oldfd < f2->f_oldfd)
		return -1;

	return 0;
}

int zfsfuse_socket_create()
{
	struct sockaddr_un name;

	int sock;
	size_t size;

	/* Create the socket. */
	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if(sock == -1) {
		int err = errno;
		cmn_err(CE_WARN, "Error creating UNIX socket: %s.", strerror(err));
		return -1;
	}

	/* Try to create the directory, ignore errors */
	mkdir(ZPOOL_CACHE_DIR, 0700);
	mkdir(ZFS_SOCK_DIR, 0700);

	/* Bind a name to the socket. */
	name.sun_family = AF_LOCAL;
	strncpy(name.sun_path, ZFS_SOCK_NAME, sizeof(name.sun_path));

	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	size = SUN_LEN(&name);

	unlink(ZFS_SOCK_NAME);

	if(bind(sock, &name, size) != 0) {
		int err = errno;
		cmn_err(CE_WARN, "Error binding UNIX socket to %s: %s.", ZFS_SOCK_NAME, strerror(err));
		return -1;
	}

	if(listen(sock, 5) != 0) {
		int err = errno;
		cmn_err(CE_WARN, "Listening error on UNIX socket %s: %s.", ZFS_SOCK_NAME, strerror(err));
		return -1;
	}

	avl_create(&fd_avl, zfsfuse_fd_compare, sizeof(file_t), offsetof(file_t, f_node));

	return sock;
}

void zfsfuse_socket_close(int fd)
{
	close(fd);

	unlink(ZFS_SOCK_NAME);

	avl_destroy(&fd_avl);
}

/*
 * This function is repeated in lib/libzfs/libzfs_zfsfuse.c
 * and in zfs-fuse/fuse_listener.c
 */
int zfsfuse_socket_read_loop(int fd, void *buf, int bytes)
{
	int read_bytes = 0;
	int left_bytes = bytes;

	while(left_bytes > 0) {
		int ret = recvfrom(fd, ((char *) buf) + read_bytes, left_bytes, 0, NULL, NULL);
		if(ret == 0)
			return -1;

		if(ret == -1) {
			if(errno == EINTR)
				continue;
			return -1;
		}
		read_bytes += ret;
		left_bytes -= ret;
	}
	return 0;
}

int zfsfuse_socket_ioctl_write(int fd, int ret)
{
#ifdef DEBUG
	/* Clear valgrind's uninitialized byte(s) warning */
	zfsfuse_cmd_t cmd = { 0 };
#else
	zfsfuse_cmd_t cmd;
#endif

	cmd.cmd_type = IOCTL_ANS;
	cmd.cmd_u.ioctl_ans_ret = ret;

	if(write(fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t))
		return -1;

	return 0;
}

int xcopyin(const void *src, void *dest, size_t size)
{
#ifdef DEBUG
	/* Clear valgrind's uninitialized byte(s) warning */
	zfsfuse_cmd_t cmd = { 0 };
#else
	zfsfuse_cmd_t cmd;
#endif

	/* This should catch stray xcopyin()s in the code.. */
	VERIFY(cur_fd >= 0);

	cmd.cmd_type = COPYIN_REQ;
	cmd.cmd_u.copy_req.ptr = (uint64_t)(uintptr_t) src;
	cmd.cmd_u.copy_req.size = size;

	if(write(cur_fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t))
		return EFAULT;

	if(zfsfuse_socket_read_loop(cur_fd, dest, size) != 0)
		return EFAULT;

	return 0;
}

int copyinstr(const char *from, char *to, size_t max, size_t *len)
{
	if(max == 0)
		return ENAMETOOLONG;
	if(max < 0)
		return EFAULT;

#ifdef DEBUG
	/* Clear valgrind's uninitialized byte(s) warning */
	zfsfuse_cmd_t cmd = { 0 };
#else
	zfsfuse_cmd_t cmd;
#endif

	/* This should catch stray copyinstr()s in the code.. */
	VERIFY(cur_fd >= 0);

	cmd.cmd_type = COPYINSTR_REQ;
	cmd.cmd_u.copy_req.ptr = (uint64_t)(uintptr_t) from;
	cmd.cmd_u.copy_req.size = max;

	if(write(cur_fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t))
		return EFAULT;

	if(zfsfuse_socket_read_loop(cur_fd, &cmd, sizeof(zfsfuse_cmd_t)) != 0)
		return EFAULT;

	VERIFY(cmd.cmd_type = COPYINSTR_ANS);

	uint64_t lencpy = cmd.cmd_u.copy_ans.lencopied;

	if(lencpy > 0)
		if(zfsfuse_socket_read_loop(cur_fd, to, lencpy) != 0)
			return EFAULT;

	to[lencpy] = '\0';

	if(len != NULL)
		*len = lencpy + 1;

	return cmd.cmd_u.copy_ans.ret;
}

int xcopyout(const void *src, void *dest, size_t size)
{
#ifdef DEBUG
	/* Clear valgrind's uninitialized byte(s) warning */
	zfsfuse_cmd_t cmd = { 0 };
#else
	zfsfuse_cmd_t cmd;
#endif

	/* This should catch stray xcopyout()s in the code.. */
	VERIFY(cur_fd >= 0);

	cmd.cmd_type = COPYOUT_REQ;
	cmd.cmd_u.copy_req.ptr = (uint64_t)(uintptr_t) dest;
	cmd.cmd_u.copy_req.size = size;

	if(write(cur_fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t))
		return EFAULT;

	if(write(cur_fd, src, size) != size)
		return EFAULT;

	return 0;
}

/*
 * Request a file descriptor from the "user" process.
 * The file descriptor is passed through the UNIX socket.
 *
 * This function is declared in libsolkerncompat/include/sys/file.h
 */
file_t *getf(int fd)
{
#ifdef DEBUG
	/* Clear valgrind's uninitialized byte(s) warning */
	zfsfuse_cmd_t cmd = { 0 };
#else
	zfsfuse_cmd_t cmd;
#endif

	/* This should catch stray getf()s in the code.. */
	VERIFY(cur_fd >= 0);

	cmd.cmd_type = GETF_REQ;
	cmd.cmd_u.getf_req_fd = fd;

	if(write(cur_fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t))
		return NULL;

retry: ;
	/* man cmsg(3) */

	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(int))];
	int32_t *fdptr;

	struct iovec iov[1];
	char c;
	iov[0].iov_base = &c;
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	msg.msg_controllen = CMSG_LEN(sizeof(int));

	int r = recvmsg(cur_fd, &msg, 0);
	if(r == 0)
		return NULL;

	if(r == -1) {
		if(errno == EINTR)
			goto retry;
		return NULL;
	}

	if(cmsg->cmsg_len != CMSG_LEN(sizeof(int)) || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
		return NULL;

	fdptr = (int *) CMSG_DATA(cmsg);
	int new_fd = *fdptr;

	file_t *ret = kmem_alloc(sizeof(file_t), KM_SLEEP);

	if(vn_fromfd(new_fd, "file descriptor", FREAD | FWRITE, &ret->f_vnode, B_TRUE) != 0) {
		kmem_free(ret, sizeof(file_t));
		return NULL;
	}

	ret->f_client = cur_fd;
	ret->f_oldfd = fd;
	ret->f_offset = 0;

	VERIFY(pthread_mutex_lock(&fd_avl_mtx) == 0);

	avl_add(&fd_avl, ret);

	VERIFY(pthread_mutex_unlock(&fd_avl_mtx) == 0);

	return ret;
}

/*
 * Release the file descriptor allocated in getf().
 *
 * This function is declared in libsolkerncompat/include/sys/file.h
 */
void releasef(int fd)
{
	file_t f;
	f.f_client = cur_fd;
	f.f_oldfd = fd;

	VERIFY(pthread_mutex_lock(&fd_avl_mtx) == 0);

	file_t *node = avl_find(&fd_avl, &f, NULL);
	VERIFY(node != NULL);

	VOP_CLOSE(node->f_vnode, FREAD | FWRITE, 1, 0, kcred, NULL);
	VN_RELE(node->f_vnode);

	avl_remove(&fd_avl, node);

	VERIFY(pthread_mutex_unlock(&fd_avl_mtx) == 0);

	kmem_free(node, sizeof(file_t));
}
