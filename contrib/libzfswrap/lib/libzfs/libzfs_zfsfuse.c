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
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

#include <sys/mntent.h>

#include "libzfs_impl.h"

int aok=0;
extern __thread int cur_fd;
extern int xcopyout(const void *src, void *dest, size_t size);
extern int copyinstr(const char *from, char *to, size_t max, size_t *len);
extern int xcopyin(const void *src, void *dest, size_t size);


#if 0 //libzfswrap

int zfsfuse_open(const char *pathname, int flags)
{
	struct sockaddr_un name;

	int sock;
	size_t size;

	/* Create the socket. */
	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if(sock == -1) {
		perror("socket");
		return -1;
	}

	/* Bind a name to the socket. */
	name.sun_family = AF_LOCAL;
	strncpy(name.sun_path, pathname, sizeof(name.sun_path));

	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	size = SUN_LEN(&name);

	if(connect(sock, (struct sockaddr *) &name, size) == -1) {
		int error = errno;
		perror("connect");
		if(error == ENOENT || error == ECONNREFUSED)
			fprintf(stderr, "Please make sure that the zfs-fuse daemon is running.\n");
		return -1;
	}

	return sock;
}

#endif //libzfswrap
/*
 * This function is repeated in zfs-fuse/zfsfuse_socket.c
 * and in zfs-fuse/fuse_listener.c
 */
int zfsfuse_ioctl_read_loop(int fd, void *buf, int bytes)
{
	int read_bytes = 0;
	int left_bytes = bytes;

	while(left_bytes > 0) {
		int ret = recvfrom(fd, ((char *) buf) + read_bytes, left_bytes, 0, NULL, NULL);
		if(ret == 0) {
			fprintf(stderr, "zfsfuse_ioctl_read_loop(): file descriptor closed\n");
			errno = EIO;
			return -1;
		}
		if(ret == -1) {
			if(errno == EINTR)
				continue;
// 			perror("recvfrom");
			return -1;
		}
		read_bytes += ret;
		left_bytes -= ret;
	}

	return 0;
}

/*
 * Send a file descriptor to zfs-fuse.
 * The file descriptor is passed through the UNIX socket.
 */
int zfsfuse_sendfd(int sock, int fd)
{
	/* man cmsg(3) */

	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(int))];
	int *fdptr;

	/* Kernel requires we send something... */
	struct iovec iov[1];
	iov[0].iov_base = "";
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	fdptr = (int *) CMSG_DATA(cmsg);
	*fdptr = fd;

	msg.msg_controllen = cmsg->cmsg_len;

	return sendmsg(sock, &msg, 0) < 0 ? -1 : 0;
}

int zfsfuse_ioctl(int fd, int32_t request, void *arg)
{
printf("zfsfuse_ioctl => gonna crash sooner or later !!!\n");
abort();
	zfsfuse_cmd_t cmd;
	int ret;

	cmd.cmd_type = IOCTL_REQ;
	cmd.cmd_u.ioctl_req.cmd = request;
	cmd.cmd_u.ioctl_req.arg = (uint64_t)(uintptr_t) arg;
	cmd.uid = getuid();
	cmd.gid = getgid();

	if((ret=write(fd, &cmd, sizeof(zfsfuse_cmd_t)) != sizeof(zfsfuse_cmd_t)))
		return -1;

	for(;;) {
		if(zfsfuse_ioctl_read_loop(fd, &cmd, sizeof(zfsfuse_cmd_t)) != 0)
			return -1;

		switch(cmd.cmd_type) {
			case IOCTL_ANS:
				errno = cmd.cmd_u.ioctl_ans_ret;
				return errno;
			case COPYIN_REQ:
				if((ret=write(fd, (void *)(uintptr_t) cmd.cmd_u.copy_req.ptr, cmd.cmd_u.copy_req.size) != cmd.cmd_u.copy_req.size))
					return -1;
				break;
			case COPYINSTR_REQ: ;
				zfsfuse_cmd_t ans = { 0 };
				ans.cmd_type = COPYINSTR_ANS;

				size_t length = strlen((char *)(uintptr_t) cmd.cmd_u.copy_req.ptr);
				if(length >= cmd.cmd_u.copy_req.size) {
					ans.cmd_u.copy_ans.ret = ENAMETOOLONG;
					ans.cmd_u.copy_ans.lencopied = cmd.cmd_u.copy_req.size - 1;
				} else
					ans.cmd_u.copy_ans.lencopied = length;

				if(((ret=write(fd, &ans, sizeof(zfsfuse_cmd_t))) != sizeof(zfsfuse_cmd_t)))
					return -1;

				if((ret=write(fd, (void *)(uintptr_t) cmd.cmd_u.copy_req.ptr, ans.cmd_u.copy_ans.lencopied)) != ans.cmd_u.copy_ans.lencopied)
					return -1;

				break;
			case COPYOUT_REQ:
				if(zfsfuse_ioctl_read_loop(fd, (void *)(uintptr_t) cmd.cmd_u.copy_req.ptr, cmd.cmd_u.copy_req.size) != 0)
					return -1;
				break;
			case GETF_REQ:
				if(zfsfuse_sendfd(fd, cmd.cmd_u.getf_req_fd) != 0)
					return -1;
				break;
			default:
				abort();
				break;
		}
	}
}

#if 0 //libzfswrap
/* If you change this, check _sol_mount in lib/libsolcompat/include/sys/mount.h */
int zfsfuse_mount(libzfs_handle_t *hdl, const char *spec, const char *dir, int mflag, char *fstype, char *dataptr, int datalen, char *optptr, int optlen)
{
	assert(dataptr == NULL);
	assert(datalen == 0);
	assert(mflag == 0);
	assert(strcmp(fstype, MNTTYPE_ZFS) == 0);

	zfsfuse_cmd_t cmd;

	uint32_t speclen = strlen(spec);
	uint32_t dirlen = strlen(dir);
	int ret;

	cmd.cmd_type = MOUNT_REQ;
	cmd.cmd_u.mount_req.speclen = speclen;
	cmd.cmd_u.mount_req.dirlen = dirlen;
	cmd.cmd_u.mount_req.mflag = mflag;
	cmd.cmd_u.mount_req.optlen = optlen;

	if((ret=write(hdl->libzfs_fd, &cmd, sizeof(zfsfuse_cmd_t))) != sizeof(zfsfuse_cmd_t))
		return -1;

	if((ret=write(hdl->libzfs_fd, spec, speclen)) != speclen)
		return -1;

	if((ret=write(hdl->libzfs_fd, dir, dirlen)) != dirlen)
		return -1;

	if((ret=write(hdl->libzfs_fd, optptr, optlen)) != optlen)
		return -1;

	uint32_t error;

	if(zfsfuse_ioctl_read_loop(hdl->libzfs_fd, &error, sizeof(uint32_t)) != 0)
		return -1;

	if(error == 0)
		return error;

	errno = error;
	return -1;
}

/*
 * This function is repeated in lib/libzfs/libzfs_zfsfuse.c
 * and in zfs-fuse/fuse_listener.c
 */
static int zfsfuse_socket_read_loop(int fd, void *buf, int bytes)
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
#endif
