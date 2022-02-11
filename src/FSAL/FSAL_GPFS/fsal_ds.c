// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * operations for GPFS data-server handles.
 *
 * Also, creating a data server handle -- now called via the DS itself.
 */

#include "config.h"

#include <fcntl.h>
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_localfs.h"
#include "../fsal_private.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include "gpfs_methods.h"
#include "pnfs_utils.h"
#include "nfs_creds.h"

#define min(a, b)			\
	({ typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a < _b ? _a : _b; })

/**
 * @brief Release a DS handle
 *
 * @param[in] ds_pub The object to release
 */
static void ds_handle_release(struct fsal_ds_handle *const ds_pub)
{
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);

	gsh_free(ds);
}

/**
 * @brief Read from a data-server handle.
 *
 * NFSv4.1 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_pub           FSAL DS handle
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
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			count4 * const supplied_length,
			bool * const end_of_file)
{
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	struct gpfs_file_handle *gpfs_handle = &ds->wire;
	/* The amount actually read */
	int amount_read = 0;
	struct dsread_arg rarg;
	unsigned int *fh;
	int errsv = 0;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	fh = (int *)&(gpfs_handle->f_handle);

	rarg.mountdirfd = export_fd;
	rarg.handle = gpfs_handle;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = requested_length;
	rarg.options = 0;
	if (op_ctx && op_ctx->client)
		rarg.cli_ip = op_ctx->client->hostaddr_str;

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
		return posix2nfs4_error(errsv);
	}

	*supplied_length = amount_read;

	if (amount_read == 0 || amount_read < requested_length)
		*end_of_file = true;

	return NFS4_OK;
}

/**
 * @brief Read plus from a data-server handle.
 *
 * NFSv4.2 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_pub           FSAL DS handle
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
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			const count4 supplied_length,
			bool * const end_of_file,
			struct io_info *info)
{
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	struct gpfs_file_handle *gpfs_handle = &ds->wire;
	/* The amount actually read */
	int amount_read = 0;
	struct dsread_arg rarg;
	unsigned int *fh;
	uint64_t filesize;
	int errsv = 0;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	fh = (int *)&(gpfs_handle->f_handle);

	rarg.mountdirfd = export_fd;
	rarg.handle = gpfs_handle;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = requested_length;
	rarg.filesize = &filesize;
	rarg.options = IO_SKIP_HOLE;
	if (op_ctx && op_ctx->client)
		rarg.cli_ip = op_ctx->client->hostaddr_str;

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
		if (errsv != ENODATA)
			return posix2nfs4_error(errsv);

		/* errsv == ENODATA */
		info->io_content.what = NFS4_CONTENT_HOLE;
		info->io_content.hole.di_offset = offset;     /*offset of hole*/
		info->io_content.hole.di_length = requested_length;/*hole len*/

		if ((requested_length + offset) > filesize) {
			amount_read = filesize - offset;
			if (amount_read < 0) {
				amount_read = 0;
				*end_of_file = true;
			} else if (amount_read < requested_length)
				*end_of_file = true;
			info->io_content.hole.di_length = amount_read;
		}
	} else {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + amount_read;
		info->io_content.data.d_data.data_len = amount_read;
		info->io_content.data.d_data.data_val = buffer;
		if (amount_read == 0 || amount_read < requested_length)
			*end_of_file = true;
	}

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
			 const stateid4 *stateid, const offset4 offset,
			 const count4 write_length, const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 * const written_length,
			 verifier4 * const writeverf,
			 stable_how4 * const stability_got)
{
	/* The private 'full' DS handle */
	struct gpfs_ds *ds = container_of(ds_pub, struct gpfs_ds, ds);
	struct gpfs_file_handle *gpfs_handle = &ds->wire;
	/* The amount actually read */
	int32_t amount_written = 0;
	struct dswrite_arg warg;
	unsigned int *fh;
	struct gsh_buffdesc key;
	int errsv = 0;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	fh = (int *)&(gpfs_handle->f_handle);

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	warg.mountdirfd = export_fd;
	warg.handle = gpfs_handle;
	warg.bufP = (char *)buffer;
	warg.offset = offset;
	warg.length = write_length;
	warg.stability_wanted = stability_wanted;
	warg.stability_got = stability_got;
	warg.verifier4 = (int32_t *) writeverf;
	warg.options = 0;
	if (op_ctx && op_ctx->client)
		warg.cli_ip = op_ctx->client->hostaddr_str;

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
		return posix2nfs4_error(errsv);
	}

	LogDebug(COMPONENT_PNFS, "write verifier %d-%d\n", warg.verifier4[0],
		 warg.verifier4[1]);

	key.addr = gpfs_handle;
	key.len = gpfs_handle->handle_key_size;
	op_ctx->fsal_export->up_ops->invalidate(
			op_ctx->fsal_export->up_ops, &key,
			FSAL_UP_INVALIDATE_CACHE);

	*written_length = amount_written;

	return NFS4_OK;
}

/**
 * @brief Commit a byte range to a DS handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_pub    FSAL DS handle
 * @param[in]  offset    Start of commit window
 * @param[in]  count     Length of commit window
 * @param[out] writeverf Write verifier
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_commit(struct fsal_ds_handle *const ds_pub,
			  const offset4 offset, const count4 count,
			  verifier4 * const writeverf)
{
	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	LogCrit(COMPONENT_PNFS, "Commits should go to MDS\n");
	/* GPFS asked for COMMIT to go to the MDS */
	return NFS4ERR_INVAL;
}

/**
 * @brief Try to create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  desc     Buffer from which to create the file
 * @param[out] handle   FSAL DS handle
 *
 * @return NFSv4.1 error codes.
 */

static nfsstat4 make_ds_handle(struct fsal_pnfs_ds *const pds,
			       const struct gsh_buffdesc *const desc,
			       struct fsal_ds_handle **const handle,
			       int flags)
{
	struct gpfs_file_handle *fh = (struct gpfs_file_handle *)desc->addr;
	struct gpfs_ds *ds;		/* Handle to be created */
	struct fsal_filesystem *fs;
	struct fsal_fsid__ fsid;

	*handle = NULL;

	if (desc->len != sizeof(struct gpfs_file_handle))
		return NFS4ERR_BADHANDLE;

	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		fh->handle_size = bswap_16(fh->handle_size);
		fh->handle_type = bswap_16(fh->handle_type);
		fh->handle_version = bswap_16(fh->handle_version);
		fh->handle_key_size = bswap_16(fh->handle_key_size);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		fh->handle_size = bswap_16(fh->handle_size);
		fh->handle_type = bswap_16(fh->handle_type);
		fh->handle_version = bswap_16(fh->handle_version);
		fh->handle_key_size = bswap_16(fh->handle_key_size);
#endif
	}
	LogFullDebug(COMPONENT_FSAL,
	  "flags 0x%X size %d type %d ver %d key_size %d FSID 0x%X:%X",
	   flags, fh->handle_size, fh->handle_type, fh->handle_version,
	   fh->handle_key_size, fh->handle_fsid[0], fh->handle_fsid[1]);

	gpfs_extract_fsid(fh, &fsid);

	fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);
	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for fsid=0x%016"PRIx64
			".0x%016"PRIx64" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	if (fs->fsal != pds->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non GPFS filesystem fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	ds = gsh_calloc(1, sizeof(struct gpfs_ds));

	*handle = &ds->ds;

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	   here. */

	ds->connected = false;

	ds->gpfs_fs = fs->private_data;

	memcpy(&ds->wire, desc->addr, desc->len);
	return NFS4_OK;
}

static nfsstat4 pds_permissions(struct fsal_pnfs_ds *const pds,
				struct svc_req *req)
{
	/* special case: related export has been set */
	return nfs4_export_check_access(req);
}

/**
 *  @param ops FSAL pNFS ds ops
 */
void pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
	ops->ds_permissions = pds_permissions;
	ops->make_ds_handle = make_ds_handle;
	ops->dsh_release = ds_handle_release;
	ops->dsh_read = ds_read;
	ops->dsh_read_plus = ds_read_plus;
	ops->dsh_write = ds_write;
	ops->dsh_commit = ds_commit;
}
