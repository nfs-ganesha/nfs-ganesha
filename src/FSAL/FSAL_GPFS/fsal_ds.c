/*
 * Copyright © 2012 CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @file   ds.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul 30 12:29:22 2012
 *
 * @brief pNFS DS operations for GPFS
 *
 * This file implements the read, write, commit, and dispose
 * operations for GPFS data-server handles.  The functionality to
 * create a data server handle is in the export.c file, as it is part
 * of the export object's interface.
 */

#include "config.h"

#include <fcntl.h>
#include "fsal.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include "gpfs_methods.h"
#include "pnfs_utils.h"

#define min(a, b)			\
	({ typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a < _b ? _a : _b; })

/**
 * @brief Release an object
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in] obj_pub The object to release
 *
 * @return FSAL status codes.
 */
static void release(struct fsal_ds_handle *const ds_pub)
{
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);

	fsal_ds_handle_uninit(&ds->ds);

	gsh_free(ds);
}

/**
 * @brief Read from a data-server handle.
 *
 * NFSv4.1 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_pub           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  requested_length Length of read requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[out] supplied_length  Length of data read
 * @param[out] eof              True on end of file
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_read(struct fsal_ds_handle *const ds_pub,
			struct req_op_context *const req_ctx,
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			count4 * const supplied_length,
			bool * const end_of_file)
{
	struct gpfs_file_handle *gpfs_handle;
	/* The amount actually read */
	int amount_read = 0;
	struct dsread_arg rarg;
	unsigned int *fh;
	int errsv = 0;

	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	gpfs_handle = &ds->wire;

	fh = (int *)&(gpfs_handle->f_handle);

	rarg.mountdirfd = ds->gpfs_fs->root_fd;
	rarg.handle = gpfs_handle;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = requested_length;
	rarg.options = 0;

	LogDebug(COMPONENT_PNFS,
		 "fh len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_handle->handle_size, gpfs_handle->handle_type,
		 gpfs_handle->handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	amount_read = gpfs_ganesha(OPENHANDLE_DS_READ, &rarg);
	errsv = errno;
	if (amount_read < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return posix2nfs4_error(-amount_read);
	}

	*supplied_length = amount_read;

	if (amount_read == 0 || amount_read < requested_length)
		*end_of_file = TRUE;

	return NFS4_OK;
}

/**
 * @brief Read plus from a data-server handle.
 *
 * NFSv4.2 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_pub           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  requested_length Length of read requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[out] supplied_length  Length of data read
 * @param[out] eof              True on end of file
 * @param[out] info             IO info
 *
 * @return An NFSv4.2 status code.
 */
static nfsstat4 ds_read_plus(struct fsal_ds_handle *const ds_pub,
			struct req_op_context *const req_ctx,
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			const count4 supplied_length,
			bool * const end_of_file,
			struct io_info *info)
{
	struct gpfs_file_handle *gpfs_handle;
	/* The amount actually read */
	int amount_read = 0;
	struct dsread_arg rarg;
	unsigned int *fh;
	int errsv = 0;

	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	gpfs_handle = &ds->wire;

	fh = (int *)&(gpfs_handle->f_handle);

	rarg.mountdirfd = ds->gpfs_fs->root_fd;
	rarg.handle = gpfs_handle;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = requested_length;
	rarg.options = IO_SKIP_HOLE;

	LogDebug(COMPONENT_PNFS,
		 "fh len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_handle->handle_size, gpfs_handle->handle_type,
		 gpfs_handle->handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	amount_read = gpfs_ganesha(OPENHANDLE_DS_READ, &rarg);
	errsv = errno;
	if (amount_read < 0 && errsv != ENODATA) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return posix2nfs4_error(errsv);
	}

	if (errsv == ENODATA) {
		info->io_content.what = NFS4_CONTENT_HOLE;
		info->io_content.hole.di_offset = offset;     /*offset of hole*/
		info->io_content.hole.di_length = requested_length;/*hole len*/
		info->io_content.hole.di_allocated = FALSE;
	} else {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + amount_read;
		info->io_content.data.d_allocated = TRUE;
		info->io_content.data.d_data.data_len = amount_read;
		info->io_content.data.d_data.data_val = buffer;
	}
	if (amount_read == 0 || amount_read < requested_length)
		*end_of_file = TRUE;

	return NFS4_OK;
}

/**
 *
 * @brief Write to a data-server handle.
 *
 * This performs a DS write not going through the data server unless
 * FILE_SYNC4 is specified, in which case it connects the filehandle
 * and performs an MDS write.
 *
 * @param[in]  ds_pub           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  write_length     Length of write requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[in]  stability wanted Stability of write
 * @param[out] written_length   Length of data written
 * @param[out] writeverf        Write verifier
 * @param[out] stability_got    Stability used for write (must be as
 *                              or more stable than request)
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_write(struct fsal_ds_handle *const ds_pub,
			 struct req_op_context *const req_ctx,
			 const stateid4 *stateid, const offset4 offset,
			 const count4 write_length, const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 * const written_length,
			 verifier4 * const writeverf,
			 stable_how4 * const stability_got)
{
	/* The amount actually read */
	int32_t amount_written = 0;
	struct dswrite_arg warg;
	unsigned int *fh;
	struct gpfs_file_handle *gpfs_handle;
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	gpfs_handle = &ds->wire;
	struct gsh_buffdesc key;
	int errsv = 0;

	fh = (int *)&(gpfs_handle->f_handle);

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	warg.mountdirfd = ds->gpfs_fs->root_fd;
	warg.handle = gpfs_handle;
	warg.bufP = (char *)buffer;
	warg.offset = offset;
	warg.length = write_length;
	warg.stability_wanted = stability_wanted;
	warg.stability_got = stability_got;
	warg.verifier4 = (int32_t *) writeverf;
	warg.options = 0;

	LogDebug(COMPONENT_PNFS,
		 "fh len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_handle->handle_size, gpfs_handle->handle_type,
		 gpfs_handle->handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	amount_written = gpfs_ganesha(OPENHANDLE_DS_WRITE, &warg);
	errsv = errno;
	if (amount_written < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return posix2nfs4_error(-amount_written);
	}

	LogDebug(COMPONENT_PNFS, "write verifier %d-%d\n", warg.verifier4[0],
		 warg.verifier4[1]);

	key.addr = gpfs_handle;
	key.len = gpfs_handle->handle_key_size;
	fsal_invalidate(req_ctx->fsal_export->fsal, &key,
			CACHE_INODE_INVALIDATE_ATTRS |
			CACHE_INODE_INVALIDATE_CONTENT);

	set_gpfs_verifier(writeverf);

	*written_length = amount_written;

	return NFS4_OK;
}

/**
 *
 * @brief Write plus to a data-server handle.
 *
 * This performs a DS write not going through the data server unless
 * FILE_SYNC4 is specified, in which case it connects the filehandle
 * and performs an MDS write.
 *
 * @param[in]  ds_pub           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  write_length     Length of write requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[in]  stability wanted Stability of write
 * @param[out] written_length   Length of data written
 * @param[out] writeverf        Write verifier
 * @param[out] stability_got    Stability used for write (must be as
 *                              or more stable than request)
 * @param[in/out] info          IO info
 *
 * @return An NFSv4.2 status code.
 */
static nfsstat4 ds_write_plus(struct fsal_ds_handle *const ds_pub,
			 struct req_op_context *const req_ctx,
			 const stateid4 *stateid, const offset4 offset,
			 const count4 write_length, const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 * const written_length,
			 verifier4 * const writeverf,
			 stable_how4 * const stability_got,
			 struct io_info *info)
{
	/* The amount actually read */
	int32_t amount_written = 0;
	struct dswrite_arg warg;
	unsigned int *fh;
	struct gpfs_file_handle *gpfs_handle;
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	gpfs_handle = &ds->wire;
	struct gsh_buffdesc key;
	int errsv = 0;

	fh = (int *)&(gpfs_handle->f_handle);

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	warg.mountdirfd = ds->gpfs_fs->root_fd;
	warg.handle = gpfs_handle;
	warg.bufP = (char *)buffer;
	warg.offset = offset;
	warg.length = write_length;
	warg.stability_wanted = stability_wanted;
	warg.stability_got = stability_got;
	warg.verifier4 = (int32_t *) writeverf;
	warg.options = 0;

	if (info->io_content.what == NFS4_CONTENT_HOLE)
		warg.options = IO_SKIP_HOLE;

	LogDebug(COMPONENT_PNFS,
		 "fh len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_handle->handle_size, gpfs_handle->handle_type,
		 gpfs_handle->handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	if (info->io_content.what == NFS4_CONTENT_APP_DATA_HOLE)
		return NFS4ERR_UNION_NOTSUPP;

	amount_written = gpfs_ganesha(OPENHANDLE_DS_WRITE, &warg);
	errsv = errno;
	if (amount_written < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return posix2nfs4_error(-amount_written);
	}

	LogDebug(COMPONENT_PNFS, "write verifier %d-%d\n",
				warg.verifier4[0], warg.verifier4[1]);

	key.addr = gpfs_handle;
	key.len = gpfs_handle->handle_key_size;
	fsal_invalidate(req_ctx->fsal_export->fsal, &key,
			CACHE_INODE_INVALIDATE_ATTRS |
			CACHE_INODE_INVALIDATE_CONTENT);

	set_gpfs_verifier(writeverf);

	*written_length = amount_written;

	return NFS4_OK;
}

/**
 * @brief Commit a byte range to a DS handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_pub    FSAL DS handle
 * @param[in]  req_ctx   Credentials
 * @param[in]  offset    Start of commit window
 * @param[in]  count     Length of commit window
 * @param[out] writeverf Write verifier
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_commit(struct fsal_ds_handle *const ds_pub,
			  struct req_op_context *const req_ctx,
			  const offset4 offset, const count4 count,
			  verifier4 * const writeverf)
{
	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	LogCrit(COMPONENT_PNFS, "Commits should go to MDS\n");
	/* GPFS asked for COMMIT to go to the MDS */
	return NFS4ERR_INVAL;
}

void ds_ops_init(struct fsal_ds_ops *ops)
{
	ops->release = release;
	ops->read = ds_read;
	ops->read_plus = ds_read_plus;
	ops->write = ds_write;
	ops->write_plus = ds_write_plus;
	ops->commit = ds_commit;
};
