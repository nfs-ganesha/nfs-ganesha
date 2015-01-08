/*
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**
 * @file   ds.c
 *
 * @brief pNFS DS operations for LUSTRE
 *
 * This file implements the read, write, commit, and dispose
 * operations for LUSTRE data-server handles.
 *
 * Also, creating a data server handle -- now called via the DS itself.
 */

#include "config.h"

#include <assert.h>
#include "fsal_api.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "../fsal_private.h"
#include "lustre_methods.h"
#include "pnfs_utils.h"
#include "nfs_exports.h"
#include "nfs_creds.h"

/**
 * @brief Release a DS handle
 *
 * @param[in] ds_pub The object to release
 */
static void
lustre_release(struct fsal_ds_handle *const ds_pub)
{
	/* The private 'full' DS handle */
	struct lustre_ds *ds = container_of(ds_pub,
					    struct lustre_ds,
					    ds);

	fsal_ds_handle_fini(&ds->ds);
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
static nfsstat4
lustre_ds_read(struct fsal_ds_handle *const ds_pub,
		struct req_op_context *const req_ctx,
		const stateid4 *stateid,
		const offset4 offset,
		const count4 requested_length,
		void *const buffer,
		count4 *const supplied_length,
		bool *const end_of_file)
{
	/* The private 'full' DS handle */
	struct lustre_ds *ds = container_of(ds_pub, struct lustre_ds, ds);
	struct lustre_file_handle *lustre_handle = &ds->wire;
	/* The amount actually read */
	int amount_read = 0;
	char mypath[MAXPATHLEN];
	int fd = 0;

	/* get the path of the file in Lustre */
	lustre_handle_to_path(ds->lustre_fs->fs->path,
			      lustre_handle, mypath);

	/* @todo: we could take care of parameter stability_wanted here */
	fd = open(mypath, O_RDONLY|O_NOFOLLOW|O_SYNC);
	if (fd < 0)
		return posix2nfs4_error(errno);

	/* write the data */
	amount_read = pread(fd, buffer, requested_length, offset);
	if (amount_read < 0) {
		/* ignore any potential error on close if read failed? */
		close(fd);
		return posix2nfs4_error(-amount_read);
	}

	if (close(fd) < 0)
		return posix2nfs4_error(errno);


	*supplied_length = amount_read;
	*end_of_file = amount_read == 0 ? true : false;

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
static nfsstat4
lustre_ds_write(struct fsal_ds_handle *const ds_pub,
		struct req_op_context *const req_ctx,
		const stateid4 *stateid,
		const offset4 offset,
		const count4 write_length,
		const void *buffer,
		const stable_how4 stability_wanted,
		count4 *written_length,
		verifier4 *writeverf,
		stable_how4 *stability_got)
{
	/* The private 'full' DS handle */
	struct lustre_ds *ds = container_of(ds_pub, struct lustre_ds, ds);
	struct lustre_file_handle *lustre_handle = &ds->wire;
	/* The amount actually read */
	int32_t amount_written = 0;
	char mypath[MAXPATHLEN];
	int fd = 0;

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	/** @todo Add some debug code here about the fh to be used */

	/* get the path of the file in Lustre */
	lustre_handle_to_path(ds->lustre_fs->fs->path,
			      lustre_handle, mypath);

	/* @todo: we could take care of parameter stability_wanted here */
	fd = open(mypath, O_WRONLY|O_NOFOLLOW|O_SYNC);
	if (fd < 0)
		return posix2nfs4_error(errno);

	/* write the data */
	amount_written = pwrite(fd, buffer, write_length, offset);
	if (amount_written < 0) {
		close(fd);
		return posix2nfs4_error(-amount_written);
	}

	if (close(fd) < 0)
		return posix2nfs4_error(errno);

	*written_length = amount_written;
	*stability_got = stability_wanted;

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


static nfsstat4
lustre_ds_commit(struct fsal_ds_handle *const ds_pub,
		 struct req_op_context *const req_ctx,
		 const offset4 offset,
		 const count4 count,
		 verifier4 *const writeverf)
{
	memset(writeverf, 0, NFS4_VERIFIER_SIZE);
	return NFS4_OK;
}

static void
dsh_ops_init(struct fsal_dsh_ops *ops)
{
	memcpy(ops, &def_dsh_ops, sizeof(struct fsal_dsh_ops));

	ops->release = lustre_release;
	ops->read = lustre_ds_read;
	ops->write = lustre_ds_write;
	ops->commit = lustre_ds_commit;
}

/**
 * @brief Try to create a FSAL data server handle
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  desc     Buffer from which to create the file
 * @param[out] handle   FSAL DS handle
 *
 * @return NFSv4.1 error codes.
 */

static nfsstat4 make_ds_handle(struct fsal_pnfs_ds *const pds,
			       const struct gsh_buffdesc *const desc,
			       struct fsal_ds_handle **const handle)
{
	struct lustre_file_handle *lustre_fh =
					(struct lustre_file_handle *)desc->addr;
	struct lustre_ds *ds;		/* Handle to be created */
	struct fsal_filesystem *fs;
	struct fsal_fsid__ fsid;
	enum fsid_type fsid_type;

	*handle = NULL;

	if (desc->len != sizeof(struct lustre_file_handle))
		return NFS4ERR_BADHANDLE;

	lustre_extract_fsid(lustre_fh, &fsid_type, &fsid);

	fs = lookup_fsid(&fsid, fsid_type);
	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	if (fs->fsal != pds->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non LUSTRE filesystem "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	ds = gsh_calloc(sizeof(struct lustre_ds), 1);
	if (ds == NULL)
		return NFS4ERR_SERVERFAULT;

	*handle = &ds->ds;
	fsal_ds_handle_init(*handle, pds);

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	   here. */

	ds->connected = false;

	ds->lustre_fs = fs->private;

	memcpy(&ds->wire, desc->addr, desc->len);
	return NFS4_OK;
}

static nfsstat4 pds_permissions(struct fsal_pnfs_ds *const pds,
				struct svc_req *req)
{
	/* special case: related export has been set */
	return nfs4_export_check_access(req);
}

void lustre_pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
	ops->permissions = pds_permissions;
	ops->make_ds_handle = make_ds_handle;
	ops->fsal_dsh_ops = dsh_ops_init;
}
