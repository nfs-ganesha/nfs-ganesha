// SPDX-License-Identifier: LGPL-2.1-or-later
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
 * @file FSAL/FSAL_VFS/os/linux/handle_syscalls.c
 * @brief System calls for the Linux handle calls
 *
 */

#include "fsal.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "../../vfs_methods.h"

/* We can at most support 40 byte handles, which are the largest known.
 * We also expect handles to be at least 4 bytes.
 */
#define VFS_MAX_HANDLE 48
#define VFS_MIN_HANDLE_SIZE 4

/* Visual handle format
 *
 * 1 byte		handle_len (doesn't go on wire)
 * 1 byte		flags
 *				00xxxxxx fsid_type
 *				01000000 handle_type fits in one byte
 *				10000000 handle_type fits in two bytes
 *				11000000 handle_type fits in four bytes
 */
#define HANDLE_TYPE_8 0x40
#define HANDLE_TYPE_16 0x80
#define HANDLE_TYPE_32 0xC0
#define HANDLE_TYPE_MASK 0xC0
#define HANDLE_DUMMY 0x20
#define HANDLE_FSID_MASK (~(HANDLE_TYPE_MASK | HANDLE_DUMMY))
 /*
 * 0, 8, or 16 bytes	fsid type
 * 1,2 or 4 bytes	handle type
 * 12 to 40 bytes	opaque kernel handle
 *
 * NOTE: if handle type doesn't fit in 2 bytes or less, 40 byte handles
 *	 (BTRFS and GPFS are known file systems that use 40 bytes) are
 *	 incompatible with 16 byte uuid form fsids.
 *	 If VMware clients are concerned, the limit is 8 bytes less...
 */

int display_vfs_handle(struct display_buffer *dspbuf,
		       struct vfs_file_handle *fh)
{
	int16_t i16;
	int32_t i32;
	uint32_t u32[2];
	uint64_t u64[2];
	uint8_t handle_cursor = 1;
	int b_left;

	b_left = display_printf(dspbuf, "Handle len %hhu 0x%02hhx: ",
				fh->handle_len, fh->handle_data[0]);

	if (b_left <= 0)
		return b_left;

	switch ((enum fsid_type) fh->handle_data[0] & HANDLE_FSID_MASK) {
	case FSID_NO_TYPE:
		b_left = display_cat(dspbuf, "no fsid");
		break;

	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		memcpy(u64,
		       fh->handle_data + handle_cursor,
		       sizeof(u64[0]));
		handle_cursor += sizeof(u64[0]);
		b_left = display_printf(dspbuf,
					"fsid=0x%016"PRIx64
					".0x0000000000000000",
					u64[0]);
		break;

	case FSID_TWO_UINT64:
		memcpy(u64,
		       fh->handle_data + handle_cursor,
		       sizeof(u64));
		handle_cursor += sizeof(u64);
		b_left = display_printf(dspbuf,
					"fsid=0x%016"PRIx64".0x%016"PRIx64,
					u64[0], u64[1]);
		break;

	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		memcpy(u32,
		       fh->handle_data + handle_cursor,
		       sizeof(u32));
		handle_cursor += sizeof(u32);
		b_left = display_printf(dspbuf,
					"fsid=0x%016"PRIx64".0x%016"PRIx64,
					u32[0], u32[1]);
		break;
	}

	if (b_left <= 0)
		return b_left;

	if ((fh->handle_data[0] & HANDLE_DUMMY) != 0)
		return display_cat(dspbuf, ", DUMMY");

	switch (fh->handle_data[0] & HANDLE_TYPE_MASK) {
	case 0:
		b_left = display_cat(dspbuf, ", invalid type");
		break;

	case HANDLE_TYPE_8:
		b_left = display_printf(dspbuf, ", type 0x%02hhx",
					fh->handle_data[handle_cursor]);
		handle_cursor++;
		break;
	case HANDLE_TYPE_16:
		memcpy(&i16,
		       fh->handle_data + handle_cursor,
		       sizeof(i16));
		handle_cursor += sizeof(i16);
		b_left = display_printf(dspbuf, ", type 0x%04h"PRIx16, i16);
		break;
	case HANDLE_TYPE_32:
		memcpy(&i32,
		       fh->handle_data + handle_cursor,
		       sizeof(i32));
		handle_cursor += sizeof(i32);
		b_left = display_printf(dspbuf, ", type 0x%04"PRIx32, i32);
		break;
	}

	if (b_left <= 0)
		return b_left;

	b_left = display_cat(dspbuf, ", opaque: ");

	if (b_left <= 0)
		return b_left;

	return display_opaque_value(dspbuf,
				    fh->handle_data + handle_cursor,
				    fh->handle_len - handle_cursor);
}

#define LogVFSHandle(fh)						\
	do {								\
		if (isMidDebug(COMPONENT_FSAL)) {			\
			char buf[256] = "\0";				\
			struct display_buffer dspbuf =			\
					{sizeof(buf), buf, buf};	\
									\
			display_vfs_handle(&dspbuf, fh);		\
									\
			LogMidDebug(COMPONENT_FSAL, "%s", buf);		\
		}							\
	} while (0)

int vfs_map_name_to_handle_at(int fd,
			      struct fsal_filesystem *fs,
			      const char *path,
			      vfs_file_handle_t *fh,
			      int flags)
{
	struct file_handle *kernel_fh;
	int32_t i32;
	int rc;
	int mnt_id;

	kernel_fh = alloca(sizeof(struct file_handle) + VFS_MAX_HANDLE);

	kernel_fh->handle_bytes = VFS_MAX_HANDLE;

	rc = name_to_handle_at(fd, path, kernel_fh, &mnt_id, flags);

	if (rc < 0) {
		int err = errno;

		LogDebug(COMPONENT_FSAL,
			 "Error %s (%d) bytes = %d",
			 strerror(err), err, (int) kernel_fh->handle_bytes);
		errno = err;
		return rc;
	}

	/* Init flags with fsid type */
	fh->handle_data[0] = fs->fsid_type;
	fh->handle_len = 1;

	/* Pack fsid into wire handle */
	rc = encode_fsid(fh->handle_data + 1,
			 sizeof_fsid(fs->fsid_type),
			 &fs->fsid,
			 fs->fsid_type);

	if (rc < 0) {
		errno = EINVAL;
		return rc;
	}

	fh->handle_len += rc;

	/* Pack handle type into wire handle */
	if (kernel_fh->handle_type <= UINT8_MAX) {
		/* Copy one byte in and advance cursor */
		fh->handle_data[fh->handle_len] =
					kernel_fh->handle_type;
		fh->handle_len++;
		fh->handle_data[0] |= HANDLE_TYPE_8;
	} else if (kernel_fh->handle_type <= INT16_MAX &&
		   kernel_fh->handle_type >= INT16_MIN) {
		/* Type fits in 16 bits */
		int16_t handle_type_16 = kernel_fh->handle_type;

		memcpy(fh->handle_data + fh->handle_len,
		       &handle_type_16,
		       sizeof(handle_type_16));
		fh->handle_len += sizeof(handle_type_16);
		fh->handle_data[0] |= HANDLE_TYPE_16;
	} else  {
		/* Type needs whole 32 bits */
		i32 = kernel_fh->handle_type;
		memcpy(fh->handle_data + fh->handle_len,
		       &i32,
		       sizeof(i32));
		fh->handle_len += sizeof(i32);
		fh->handle_data[0] |= HANDLE_TYPE_32;
	}

	/* Pack opaque handle into wire handle */
	if (fh->handle_len + kernel_fh->handle_bytes > VFS_HANDLE_LEN) {
		/* We just can't fit this handle... */
		errno = EOVERFLOW;
		return -1;
	} else {
		memcpy(fh->handle_data + fh->handle_len,
		       kernel_fh->f_handle,
		       kernel_fh->handle_bytes);
		fh->handle_len += kernel_fh->handle_bytes;
	}

	LogVFSHandle(fh);

	return 0;
}

int vfs_open_by_handle(struct fsal_filesystem *fs,
		       vfs_file_handle_t *fh, int openflags,
		       fsal_errors_t *fsal_error)
{
	struct file_handle *kernel_fh;
	uint8_t handle_cursor = sizeof_fsid(fs->fsid_type) + 1;
	int16_t i16;
	int32_t i32;
	int fd;

	LogFullDebug(COMPONENT_FSAL,
		     "vfs_fs = %s root_fd = %d",
		     fs->path, root_fd(fs));

	LogVFSHandle(fh);

	kernel_fh = alloca(sizeof(struct file_handle) + VFS_MAX_HANDLE);

	switch (fh->handle_data[0] & HANDLE_TYPE_MASK) {
	case 0:
		LogDebug(COMPONENT_FSAL,
			 "Invaliid handle type = 0");
		errno = EINVAL;
		fd = -1;
		goto out;
	case HANDLE_TYPE_8:
		kernel_fh->handle_type = fh->handle_data[handle_cursor];
		handle_cursor++;
		break;
	case HANDLE_TYPE_16:
		memcpy(&i16,
		       fh->handle_data + handle_cursor,
		       sizeof(i16));
		handle_cursor += sizeof(i16);
		kernel_fh->handle_type = i16;
		break;
	case HANDLE_TYPE_32:
		memcpy(&i32,
		       fh->handle_data + handle_cursor,
		       sizeof(i32));
		handle_cursor += sizeof(i32);
		kernel_fh->handle_type = i32;
		break;
	}

	kernel_fh->handle_bytes = fh->handle_len - handle_cursor;
	memcpy(kernel_fh->f_handle,
	       fh->handle_data + handle_cursor,
	       kernel_fh->handle_bytes);

	fd = open_by_handle_at(root_fd(fs), kernel_fh, openflags);

out:
	if (fd < 0) {
		fd = -errno;
		if (fd == -ENOENT)
			fd = -ESTALE;
		*fsal_error = posix2fsal_error(-fd);
		LogDebug(COMPONENT_FSAL, "Failed with %s openflags 0x%08x",
			 strerror(-fd), openflags);
	} else {
		LogFullDebug(COMPONENT_FSAL, "Opened fd %d", fd);
	}

	return fd;
}

int vfs_fd_to_handle(int fd, struct fsal_filesystem *fs,
		     vfs_file_handle_t *fh)
{
	return vfs_map_name_to_handle_at(fd, fs, "", fh, AT_EMPTY_PATH);
}

int vfs_name_to_handle(int atfd,
		       struct fsal_filesystem *fs,
		       const char *name,
		       vfs_file_handle_t *fh)
{
	return vfs_map_name_to_handle_at(atfd, fs, name, fh, 0);
}

int vfs_extract_fsid(vfs_file_handle_t *fh,
		     enum fsid_type *fsid_type,
		     struct fsal_fsid__ *fsid)
{
	LogVFSHandle(fh);

	*fsid_type = (enum fsid_type) fh->handle_data[0] & HANDLE_FSID_MASK;

	if (decode_fsid(fh->handle_data + 1,
			fh->handle_len - 1,
			fsid,
			*fsid_type) < 0)
		return ESTALE;
	else
		return 0;
}

int vfs_encode_dummy_handle(vfs_file_handle_t *fh,
			    struct fsal_filesystem *fs)
{
	int rc;

	/* Init flags with fsid type */
	fh->handle_data[0] = fs->fsid_type | HANDLE_DUMMY;
	fh->handle_len = 1;

	/* Pack fsid into wire handle */
	rc = encode_fsid(fh->handle_data + 1,
			 sizeof_fsid(fs->fsid_type),
			 &fs->fsid,
			 fs->fsid_type);

	if (rc < 0) {
		errno = EINVAL;
		return rc;
	}

	fh->handle_len += rc;

	LogVFSHandle(fh);

	return 0;
}

bool vfs_is_dummy_handle(vfs_file_handle_t *fh)
{
	return (fh->handle_data[0] & HANDLE_DUMMY) != 0;
}

bool vfs_valid_handle(struct gsh_buffdesc *desc)
{
	uint8_t handle0;
	int len = 1; /* handle_type */
	bool fsid_type_ok = false;
	bool ok;

	if (desc->addr == NULL) {
		LogDebug(COMPONENT_FSAL,
			 "desc->addr == NULL");
		return false;
	}

	if (desc->len > VFS_HANDLE_LEN) {
		LogDebug(COMPONENT_FSAL,
			 "desc->len %d > VFS_HANDLE_LEN",
			 (int) desc->len);
		return false;
	}

	handle0 = *((uint8_t *) (desc->addr));

	switch ((enum fsid_type) handle0 & HANDLE_FSID_MASK) {
	case FSID_NO_TYPE:
	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
	case FSID_TWO_UINT64:
	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		fsid_type_ok = true;
		len += sizeof_fsid((enum fsid_type) handle0 &
						    HANDLE_FSID_MASK);
		break;
	}

	if (!fsid_type_ok) {
		LogDebug(COMPONENT_FSAL,
			 "FSID Type %02hhx invalid",
			 (uint8_t) (handle0 & HANDLE_FSID_MASK));
		return false;
	}

	if ((handle0 & HANDLE_DUMMY) != 0) {
		ok = len == desc->len;
		if (!ok) {
			LogDebug(COMPONENT_FSAL,
				 "Len %d != desc->len %d for DUMMY handle",
				 len, (int) desc->len);
		}

		return ok;
	}

	/* min kernel handle size */
	len += sizeof(uint32_t);

	switch (handle0 & HANDLE_TYPE_MASK) {
	case HANDLE_TYPE_8:
		len += sizeof(uint8_t);
		break;
	case HANDLE_TYPE_16:
		len += sizeof(int16_t);
		break;
	case HANDLE_TYPE_32:
		len += sizeof(int32_t);
		break;
	default:
		LogDebug(COMPONENT_FSAL,
			 "Handle Type %02hhx invalid",
			 (uint8_t) (handle0 & HANDLE_TYPE_MASK));
		return false;
	}

	ok = (len + VFS_MIN_HANDLE_SIZE) <= desc->len;
	if (!ok) {
		LogDebug(COMPONENT_FSAL,
			 "Len %d + VFS_MIN_HANDLE_SIZE %d > desc->len %d",
			 len, len + VFS_MIN_HANDLE_SIZE, (int) desc->len);
		return false;
	}
	ok = (len + VFS_MAX_HANDLE) >= desc->len;
	if (!ok) {
		LogDebug(COMPONENT_FSAL,
			 "Len %d + VFS_MAX_HANDLE %d < desc->len %d",
			 len, len + VFS_MAX_HANDLE, (int) desc->len);
	}
	return true;
}

int vfs_re_index(struct fsal_filesystem *fs,
		 struct vfs_fsal_export *exp)
{
	return 0;
}

/** @} */
