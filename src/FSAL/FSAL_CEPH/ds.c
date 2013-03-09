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
 * @brief pNFS DS operations for Ceph
 *
 * This file implements the read, write, commit, and dispose
 * operations for Ceph data-server handles.  The functionality to
 * create a data server handle is in the export.c file, as it is part
 * of the export object's interface.
 */

#include "config.h"

#include <cephfs/libcephfs.h>
#include <fcntl.h>
#include "fsal.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "internal.h"
#include "pnfs_utils.h"

#define min(a,b)				\
	({ typeof (a) _a = (a);			\
	     typeof (b) _b = (b);		\
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
static nfsstat4 release(struct fsal_ds_handle *const ds_pub)
{
	/* The private 'full' DS handle */
	struct ds *ds
		= container_of(ds_pub, struct ds, ds);
	if (fsal_ds_handle_uninit(&ds->ds)) {
		return EINVAL;
	}
	gsh_free(ds);
	return 0;
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
			const stateid4 *stateid,
			const offset4 offset,
			const count4 requested_length,
			void *const buffer,
			count4 *const supplied_length,
			bool *const end_of_file)
{
	/* The private 'full' export */
	struct export *export
		= container_of(ds_pub->export, struct export, export);
	/* The private 'full' DS handle */
	struct ds *ds
		= container_of(ds_pub, struct ds, ds);
	/* The OSD number for this machine */
	int local_OSD = 0;
	/* Width of a stripe in the file */
	uint32_t stripe_width = 0;
	/* Beginning of a block */
	uint64_t block_start = 0;
	/* Number of the stripe being read */
	uint32_t stripe = 0;
	/* Internal offset within the stripe*/
	uint32_t internal_offset = 0;
	/* The amount actually read */
	int amount_read = 0;

	/* Find out what my OSD ID is, so we can avoid talking to
           other OSDs. */

	local_OSD = ceph_get_local_osd(export->cmount);
	if (local_OSD < 0) {
		return posix2nfs4_error(-local_OSD);
	}

	/* Find out what stripe we're writing to and where within the
           stripe. */

	stripe_width = ds->wire.layout.fl_stripe_unit;
	stripe = offset / stripe_width;
	block_start = stripe * stripe_width;
	internal_offset = offset - block_start;

	if (local_OSD
	    != ceph_ll_get_stripe_osd(export->cmount,
				      ds->wire.wire.vi,
				      stripe,
				      &(ds->wire.layout))) {
		return NFS4ERR_PNFS_IO_HOLE;
	}

	amount_read = ceph_ll_read_block(
		export->cmount,
		ds->wire.wire.vi,
		stripe,
		buffer,
		internal_offset,
		min((stripe_width -
		     internal_offset),
		    requested_length),
		&(ds->wire.layout));
	if (amount_read < 0) {
		return posix2nfs4_error(-amount_read);
	}

	*supplied_length = amount_read;

	*end_of_file = false;

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
			 const stateid4 *stateid,
			 const offset4 offset,
			 const count4 write_length,
			 const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 *const written_length,
			 verifier4 *const writeverf,
			 stable_how4 *const stability_got)
{
	/* The private 'full' export */
	struct export *export
		= container_of(ds_pub->export, struct export, export);
	/* The private 'full' DS handle */
	struct ds *ds
		= container_of(ds_pub, struct ds, ds);
	/* The OSD number for this host */
	int local_OSD = 0;
	/* Width of a stripe in the file */
	uint32_t stripe_width = 0;
	/* Beginning of a block */
	uint64_t block_start = 0;
	/* Number of the stripe being written */
	uint32_t stripe = 0;
	/* Internal offset within the stripe*/
	uint32_t internal_offset = 0;
	/* The amount actually written */
	int32_t amount_written = 0;
	/* The adjusted write length, confined to one object */
	uint32_t adjusted_write = 0;
	/* Return code from ceph calls */
	int ceph_status = 0;

	memset(*writeverf, 0, NFS4_VERIFIER_SIZE);

	/* Find out what my OSD ID is, so we can avoid talking to
           other OSDs. */

	local_OSD = ceph_get_local_osd(export->cmount);

	/* Find out what stripe we're writing to and where within the
           stripe. */

	stripe_width = ds->wire.layout.fl_stripe_unit;
	stripe = offset / stripe_width;
	block_start = stripe * stripe_width;
	internal_offset = offset - block_start;

	if (local_OSD
	    != ceph_ll_get_stripe_osd(export->cmount,
				      ds->wire.wire.vi,
				      stripe,
				      &(ds->wire.layout))) {
		return NFS4ERR_PNFS_IO_HOLE;
	}

	adjusted_write = min((stripe_width - internal_offset),
			     write_length);

	/* If the client specifies FILE_SYNC4, then we have to connect
           the filehandle and use the MDS to update size and access
           time. */
	if (stability_wanted == FILE_SYNC4) {
		Fh* descriptor = NULL;

		if (!ds->connected) {
			if ((ceph_status
			     = ceph_ll_connectable_m(
				     export->cmount,
				     &ds->wire.wire.vi,
				     ds->wire.wire.parent_ino,
				     ds->wire.wire.parent_hash)) != 0) {
				LogMajor(COMPONENT_PNFS,
					 "Filehandle connection failed"
					 " with: %d\n",
					 ceph_status);
				return posix2nfs4_error(-ceph_status);
			}
			ds->connected = true;
		}
		if ((ceph_status
		     = ceph_ll_open(export->cmount,
				    ds->wire.wire.vi,
				    O_WRONLY,
				    &descriptor,
				    0,
				    0)) != 0) {
			printf("Open failed with: %d\n", ceph_status);
			return posix2nfs4_error(-ceph_status);
		}

		amount_written
			= ceph_ll_write(export->cmount,
					descriptor,
					offset,
					adjusted_write,
					buffer);

		if (amount_written < 0) {
			printf("Write failed with: %d\n", amount_written);
			ceph_ll_close(export->cmount, descriptor);
			return posix2nfs4_error(-amount_written);
		}

		if ((ceph_status
		     = ceph_ll_fsync(export->cmount, descriptor, 0)) < 0) {
			printf("fsync failed with: %d\n", ceph_status);
			ceph_ll_close(export->cmount, descriptor);
			return posix2nfs4_error(-ceph_status);
		}

		if ((ceph_status = ceph_ll_close(export->cmount,
						 descriptor)) < 0) {
			printf("close failed with: %d\n", ceph_status);
			return posix2nfs4_error(-ceph_status);
		}
		*written_length = amount_written;
		*stability_got = FILE_SYNC4;
	} else {
		/* FILE_SYNC4 wasn't specified, so we don't have to
                   bother with the MDS. */

		amount_written
			= ceph_ll_write_block(export->cmount,
					      ds->wire.wire.vi,
					      stripe,
					      (char*) buffer,
					      internal_offset,
					      adjusted_write,
					      &(ds->wire.layout),
					      ds->wire.snapseq,
					      (stability_wanted
					       == DATA_SYNC4));
		if (amount_written < 0) {
			return posix2nfs4_error(-amount_written);
		}

		*written_length = amount_written;
		*stability_got = stability_wanted;
	}

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
			  const offset4 offset,
			  const count4 count,
			  verifier4 *const writeverf)
{
#ifdef COMMIT_FIX
	/* The private 'full' export */
	struct export *export
		= container_of(ds_pub->export, struct export, export);
	/* The private 'full' DS handle */
	struct ds *ds
		= container_of(ds_pub, struct ds, ds);
	/* Error return from Ceph */
	int rc = 0;

	/* Find out what stripe we're writing to and where within the
           stripe. */

	rc = ceph_ll_commit_blocks(export->cmount,
				   ds->wire.wire.vi,
				   offset,
				   (count == 0) ?
				   UINT64_MAX :
				   count);
	if (rc < 0) {
		return posix2nfs4_error(rc);
	}
#endif /* COMMIT_FIX */

	memset(*writeverf, 0, NFS4_VERIFIER_SIZE);
	return NFS4_OK;
}

void ds_ops_init(struct fsal_ds_ops *ops)
{
	ops->release = release;
	ops->read = ds_read;
	ops->write = ds_write;
	ops->commit = ds_commit;
};
