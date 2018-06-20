/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2014
 * Author: Jiffin Tony Thottan jthottan@redhat.com
 *       : Anand Subramanian   anands@redhat.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */
#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_up.h"
#include "gluster_internal.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"
#include "pnfs_utils.h"
#include "nfs_exports.h"
#include <arpa/inet.h>
#include "../fsal_private.h"

/**
 * @brief Release a DS object
 *
 * @param[in] obj_pub The object to release
 *
 * @return NFS Status codes.
 */
static void release(struct fsal_ds_handle *const ds_pub)
{
	int    rc                 = 0;
	struct glfs_ds_handle *ds =
		container_of(ds_pub, struct glfs_ds_handle, ds);

	fsal_ds_handle_fini(&ds->ds);
	if (ds->glhandle) {
		rc = glfs_h_close(ds->glhandle);
		if (rc) {
			LogMajor(COMPONENT_PNFS,
				 "glfs_h_close returned error %s(%d)",
				 strerror(errno), errno);
		}
	}
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
	/* The private DS handle */
	struct glfs_ds_handle *ds =
		container_of(ds_pub, struct glfs_ds_handle, ds);
	int    rc = 0;
	struct glusterfs_export *glfs_export =
	container_of(ds_pub->pds->mds_fsal_export,
		     struct glusterfs_export, export);

	if (ds->glhandle == NULL)
		LogDebug(COMPONENT_PNFS, "glhandle NULL");

	rc = glfs_h_anonymous_read(glfs_export->gl_fs->fs, ds->glhandle,
				   buffer, requested_length, offset);
	if (rc < 0) {
		rc = errno;
		LogMajor(COMPONENT_PNFS, "Read failed on DS");
		return posix2nfs4_error(rc);
	}

	*supplied_length = rc;
	if (rc == 0 || rc < requested_length)
		*end_of_file = true;


	return NFS4_OK;
}

/**
 *
 * @brief Write to a data-server handle.
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
	struct glfs_ds_handle *ds =
		container_of(ds_pub, struct glfs_ds_handle, ds);
	struct glusterfs_export *glfs_export =
	container_of(ds_pub->pds->mds_fsal_export,
		     struct glusterfs_export, export);
	int    rc = 0;

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	if (ds->glhandle == NULL)
		LogDebug(COMPONENT_PNFS, "glhandle NULL");

	rc = glfs_h_anonymous_write(glfs_export->gl_fs->fs, ds->glhandle,
				    buffer, write_length, offset);
	if (rc < 0) {
		rc = errno;
		LogMajor(COMPONENT_PNFS, "status after write %d", rc);
		return posix2nfs4_error(rc);
	}

	/** @todo:Here DS is performing the write operation, so the MDS is not
	 *      aware of the change.We should inform MDS through upcalls about
	 *      change in file attributes such as size and time.
	 */
	*written_length = rc;

	*stability_got = stability_wanted;
	ds->stability_got = stability_wanted;

	/* Incase of MDS being DS, there shall not be upcalls sent from
	 * backend. Hence invalidate the entry here */
	(void)upcall_inode_invalidate(glfs_export->gl_fs, ds->glhandle);

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
	struct glfs_ds_handle *ds =
		container_of(ds_pub, struct glfs_ds_handle, ds);
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	if (ds->stability_got == FILE_SYNC4) {
		struct glusterfs_export *glfs_export =
			container_of(ds_pub->pds->mds_fsal_export,
				     struct glusterfs_export, export);
		struct glfs_fd *glfd = NULL;

		SET_GLUSTER_CREDS(glfs_export, &op_ctx->creds->caller_uid,
				  &op_ctx->creds->caller_gid,
				  op_ctx->creds->caller_glen,
				  op_ctx->creds->caller_garray);

		glfd = glfs_h_open(glfs_export->gl_fs->fs, ds->glhandle,
				   O_RDWR);
		if (glfd == NULL) {
			LogDebug(COMPONENT_PNFS, "glfd in ds_handle is NULL");
			SET_GLUSTER_CREDS(glfs_export, NULL, NULL, 0, NULL);
			return NFS4ERR_SERVERFAULT;
		}
#ifdef USE_GLUSTER_STAT_FETCH_API
		rc = glfs_fsync(glfd, NULL, NULL);
#else
		rc = glfs_fsync(glfd);
#endif
		if (rc != 0)
			LogMajor(COMPONENT_PNFS,
				 "glfs_fsync failed %d", errno);
		rc = glfs_close(glfd);
		if (rc != 0)
			LogDebug(COMPONENT_PNFS,
				 "status after close %d", errno);

		SET_GLUSTER_CREDS(glfs_export, NULL, NULL, 0, NULL);
	}

	if ((rc != 0) || (status.major != ERR_FSAL_NO_ERROR))
		return NFS4ERR_INVAL;

	return NFS4_OK;
}

/* Initialise DS operations */
void dsh_ops_init(struct fsal_dsh_ops *ops)
{
	ops->release = release;
	ops->read = ds_read;
	ops->write = ds_write;
	ops->commit = ds_commit;
};

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  export_pub The export in which to create the handle
 * @param[in]  desc       Buffer from which to create the file
 * @param[out] ds_pub     FSAL data server handle
 *
 * @return NFSv4.1 error codes.
 */
static nfsstat4 make_ds_handle(struct fsal_pnfs_ds *const pds,
			       const struct gsh_buffdesc *const hdl_desc,
			       struct fsal_ds_handle **const handle,
			       int flags)
{

	/* Handle to be created for DS */
	struct glfs_ds_handle *ds                   = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
	struct stat sb;
	struct glusterfs_export *glfs_export =
		container_of(pds->mds_fsal_export,
			     struct glusterfs_export, export);

	*handle = NULL;

	if (hdl_desc->len != sizeof(struct glfs_ds_wire))
		return NFS4ERR_BADHANDLE;

	ds = gsh_calloc(1, sizeof(struct glfs_ds_handle));

	*handle = &ds->ds;
	fsal_ds_handle_init(*handle, pds);

	memcpy(globjhdl, hdl_desc->addr, GFAPI_HANDLE_LENGTH);

	/* Create glfs_object for the DS handle */
	ds->glhandle =	glfs_h_create_from_handle(glfs_export->gl_fs->fs,
						  globjhdl,
						  GFAPI_HANDLE_LENGTH, &sb);
	if (ds->glhandle == NULL) {
		LogDebug(COMPONENT_PNFS,
			 "glhandle in ds_handle is NULL");
		return NFS4ERR_SERVERFAULT;
	}

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	   here. */

	ds->connected = false;

	return NFS4_OK;
}

void pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
	ops->make_ds_handle = make_ds_handle;
	ops->fsal_dsh_ops = dsh_ops_init;
}
