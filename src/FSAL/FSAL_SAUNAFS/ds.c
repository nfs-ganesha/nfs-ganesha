// SPDX-License-Identifier: LGPL-3.0-or-later
/*
   Copyright 2017 Skytechnology sp. z o.o.
   Copyright 2023 Leil Storage OÜ

   This file is part of SaunaFS.

   SaunaFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   SaunaFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SaunaFS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "fsal.h"
#include "fsal_api.h"
#include "fsal_types.h"
#include "../FSAL/fsal_private.h"
#include "nfs_exports.h"

#include "context_wrap.h"
#include "fileinfo_cache.h"
#include "saunafs_fsal_types.h"
#include "saunafs_internal.h"

/**
 * @brief Remove count expired instances from cache.
 *
 * @param[in] export     export that store the cache
 * @param[in] count      number of instances to be removed
 */
static void clearFileInfoCache(struct SaunaFSExport *export, int count)
{
	if (export == NULL)
		return;

	for (int i = 0; i < count; ++i) {
		FileInfoEntry_t *cacheHandle = NULL;

		cacheHandle = popExpiredFileInfoCache(export->cache);

		if (cacheHandle == NULL)
			break;

		fileinfo_t *fileHandle = extractFileInfo(cacheHandle);

		sau_release(export->fsInstance, fileHandle);
		fileInfoEntryFree(cacheHandle);
	}
}

/**
 * @brief Clean up a DS handle.
 *
 * DS handle Lifecycle management.
 * This function cleans up private resources associated with a filehandle
 * and deallocates it. Implement this method or you will leak. This
 * function should not be called directly.
 *
 * @param[in] dataServerHandle     Handle to release
 */
static void dsh_release(struct fsal_ds_handle *const dataServerHandle)
{
	struct SaunaFSExport *export;
	struct DataServerHandle *dataServer;

	export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
			      struct SaunaFSExport, export);

	dataServer =
		container_of(dataServerHandle, struct DataServerHandle, handle);

	assert(export->cache);

	if (dataServer->cacheHandle != NULL)
		releaseFileInfoCache(export->cache, dataServer->cacheHandle);

	gsh_free(dataServer);
	clearFileInfoCache(export, 5);
}

/**
 * @brief Open a file from DataServerHandle.
 *
 * Auxiliar function to open files in the syscalls related with Data
 * Server.
 *
 * @param[in] export         Handle to release
 * @param[in] dataServer     Data Server handle
 *
 * @returns: nfsstat4 status returned after opening the file
 */
static nfsstat4 openfile(struct SaunaFSExport *export,
			 struct DataServerHandle *dataServer)
{
	if (export == NULL)
		return NFS4ERR_IO;

	if (dataServer->cacheHandle != NULL)
		return NFS4_OK;

	clearFileInfoCache(export, 2);

	struct FileInfoEntry *entry = NULL;

	entry = acquireFileInfoCache(export->cache, dataServer->inode);

	dataServer->cacheHandle = entry;
	if (dataServer->cacheHandle == NULL)
		return NFS4ERR_IO;

	fileinfo_t *fileHandle = extractFileInfo(dataServer->cacheHandle);

	if (fileHandle != NULL)
		return NFS4_OK;

	fileHandle = saunafs_open(export->fsInstance, NULL, dataServer->inode,
				  O_RDWR);

	if (fileHandle == NULL) {
		eraseFileInfoCache(export->cache, dataServer->cacheHandle);
		dataServer->cacheHandle = NULL;
		return NFS4ERR_IO;
	}

	attachFileInfo(dataServer->cacheHandle, fileHandle);
	return NFS4_OK;
}

/**
 * @brief Read from a data-server handle.
 *
 * DS handle I/O Functions
 *
 * NFSv4.1 data server handles are disjount from normal filehandles (in
 * Ganesha, there is a ds_flag in the filehandle_v4_t structure) and do not
 * get loaded into mdcache or processed the normal way.
 *
 * @param[in] dataServerHandle      Handle to release
 * @param[in] stateid               The stateid supplied with the READ
 *                                  operation, for validation
 * @param[in] offset                The offset at which to read
 * @param[in] requestedLength       Length of read requested (and size of
 *                                  buffer)
 * @param[out] buffer               The buffer to which to store read data
 * @param[out] suppliedLength       Length of data read
 * @param[out] eof                  true on end of file
 *
 * @returns: An NFSv4.1 status code.
 */
static nfsstat4 dsh_read(struct fsal_ds_handle *const dataServerHandle,
			 const stateid4 *stateid, const offset4 offset,
			 const count4 requestedLength, void *const buffer,
			 count4 *const suppliedLength, bool *const eof)
{
	(void)stateid;

	struct SaunaFSExport *export = NULL;
	struct DataServerHandle *dataServer = NULL;
	fileinfo_t *fileHandle = NULL;

	export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
			      struct SaunaFSExport, export);

	dataServer =
		container_of(dataServerHandle, struct DataServerHandle, handle);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32,
		     export->export.export_id, dataServer->inode, offset,
		     requestedLength);

	nfsstat4 status = openfile(export, dataServer);

	if (status != NFS4_OK)
		return status;

	fileHandle = extractFileInfo(dataServer->cacheHandle);
	ssize_t bytes = saunafs_read(export->fsInstance, NULL, fileHandle,
				     offset, requestedLength, buffer);

	if (bytes < 0)
		return nfs4LastError();

	*suppliedLength = bytes;
	*eof = (bytes == 0);

	return NFS4_OK;
}

/**
 * @brief Write to a data-server handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal filehandles (in
 * Ganesha, there is a ds_flag in the filehandle_v4_t structure) and do not
 * get loaded into mdcache or processed the normal way.
 *
 * @param[in] dataServerHandle      FSAL DS handle
 * @param[in] stateid               The stateid supplied with the READ
 *                                  operation, for validation
 * @param[in] offset                The offset at which to read
 * @param[in] writeLength           Length of write requested (and size of
 *                                  buffer)
 * @param[out] buffer               The buffer to which to store read data
 * @param[in] stability             wanted Stability of write
 * @param[out] writtenLength        Length of data written
 * @param[out] writeVerifier        Write verifier
 * @param[out] stabilityGot         Stability used for write (must be as or
 *                                  more stable than request)
 *
 * @returns: An NFSv4.1 status code.
 */
static nfsstat4 dsh_write(struct fsal_ds_handle *const dataServerHandle,
			  const stateid4 *stateid, const offset4 offset,
			  const count4 writeLength, const void *buffer,
			  const stable_how4 stability,
			  count4 * const writtenLength,
			  verifier4 * const writeVerifier,
			  stable_how4 * const stabilityGot)
{
	(void)stateid;
	(void)writeVerifier;

	struct SaunaFSExport *export = NULL;
	struct DataServerHandle *dataServer = NULL;

	fileinfo_t *fileHandle = NULL;
	int status = 0;

	export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
			      struct SaunaFSExport, export);

	dataServer =
		container_of(dataServerHandle, struct DataServerHandle, handle);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32,
		     export->export.export_id, dataServer->inode, offset,
		     writeLength);

	nfsstat4 nfsStatus = openfile(export, dataServer);

	if (nfsStatus != NFS4_OK)
		return nfsStatus;

	fileHandle = extractFileInfo(dataServer->cacheHandle);
	ssize_t bytes = saunafs_write(export->fsInstance, NULL, fileHandle,
				      offset, writeLength, buffer);

	if (bytes < 0)
		return nfs4LastError();

	if (stability != UNSTABLE4)
		status = saunafs_flush(export->fsInstance, NULL, fileHandle);

	*writtenLength = bytes;
	*stabilityGot = (status < 0) ? UNSTABLE4 : stability;

	return NFS4_OK;
}

/**
 * @brief Commit a byte range to a DS handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal filehandles (in
 * Ganesha, there is a ds_flag in the filehandle_v4_t structure) and do not
 * get loaded into mdcache or processed the normal way.
 *
 * @param[in] dataServerHandle      FSAL DS handle
 * @param[in] offset                Start of commit window
 * @param[in] count                 Length of commit window
 * @param[out] writeVerifier        Write verifier
 *
 * @returns: An NFSv4.1 status code.
 */
static nfsstat4 dsh_commit(struct fsal_ds_handle *const dataServerHandle,
			   const offset4 offset, const count4 count,
			   verifier4 * const writeVerifier)
{
	struct SaunaFSExport *export = NULL;
	struct DataServerHandle *dataServer = NULL;
	fileinfo_t *fileHandle = NULL;

	memset(writeVerifier, 0, NFS4_VERIFIER_SIZE);

	export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
			      struct SaunaFSExport, export);

	dataServer =
		container_of(dataServerHandle, struct DataServerHandle, handle);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32,
		     export->export.export_id, dataServer->inode, offset,
		     count);

	nfsstat4 nfsStatus = openfile(export, dataServer);

	if (nfsStatus != NFS4_OK) {
		/* If we failed here then there is no opened SaunaFS file
		 * descriptor, which implies that we don't need to flush
		 * anything */
		return NFS4_OK;
	}

	fileHandle = extractFileInfo(dataServer->cacheHandle);
	int status = saunafs_flush(export->fsInstance, NULL, fileHandle);

	if (status < 0) {
		LogMajor(COMPONENT_PNFS, "ds_commit() failed  '%s'",
			 sau_error_string(sau_last_err()));
		return NFS4ERR_INVAL;
	}

	return NFS4_OK;
}

/**
 * @brief Read plus from a data-server handle.
 *
 * NFSv4.2 data server handles are disjount from normal filehandles (in
 * Ganesha, there is a ds_flag in the filehandle_v4_t structure) and do not
 * get loaded into mdcache or processed the normal way.
 *
 * @param[in]  dataServerHandle      FSAL DS handle
 * @param[in]  stateid               Stateid supplied with the READ
 *                                   operation, for validation
 * @param[in]  offset                The offset at which to read
 * @param[in]  requestedLength       Length of read requested (and size of
 *                                   buffer)
 * @param[out] buffer                Buffer to which to store read data
 * @param[out] suppliedLength        Length of data read
 * @param[out] eof                   true on end of file
 * @param[out] info                  IO info
 *
 * @returns: An NFSv4.2 status code.
 */
static nfsstat4 dsh_read_plus(struct fsal_ds_handle *const dataServerHandle,
			      const stateid4 *stateid, const offset4 offset,
			      const count4 requestedLength, void *const buffer,
			      const count4 suppliedLength, bool *const eof,
			      struct io_info *info)
{
	(void)dataServerHandle;
	(void)stateid;
	(void)offset;
	(void)requestedLength;
	(void)buffer;
	(void)suppliedLength;
	(void)eof;
	(void)info;

	LogCrit(COMPONENT_PNFS, "Unimplemented DS read_plus!");
	return NFS4ERR_NOTSUPP;
}

/**
 * @brief Create a FSAL data server handle from a wire handle.
 *
 * This function creates a FSAL data server handle from a client supplied
 * "wire" handle.
 *
 * @param[in]  pnfsDataServer       FSAL pNFS DS
 * @param[in]  buffer               Buffer from which to create the struct
 * @param[out] handle               FSAL DS handle
 * @param[out] flags                Flags used to create the FSAL data
 *                                  server handle
 *
 * @returns: NFSv4.1 error codes.
 */
static nfsstat4 make_ds_handle(struct fsal_pnfs_ds *const pnfsDataServer,
			       const struct gsh_buffdesc *const buffer,
			       struct fsal_ds_handle **const handle, int flags)
{
	(void)pnfsDataServer;

	struct DSWire *dataServerWire = NULL;

	dataServerWire = (struct DSWire *)buffer->addr;

	struct DataServerHandle *dataServerHandle = NULL;
	*handle = NULL;

	if (buffer->len != sizeof(struct DSWire))
		return NFS4ERR_BADHANDLE;

	if (dataServerWire->inode == 0)
		return NFS4ERR_BADHANDLE;

	dataServerHandle = gsh_calloc(1, sizeof(struct DataServerHandle));
	*handle = &dataServerHandle->handle;

	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		dataServerHandle->inode = bswap_32(dataServerWire->inode);
#else
		dataServerHandle->inode = dataServerWire->inode;
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		dataServerHandle->inode = bswap_32(dataServerWire->inode);
#else
		dataServerHandle->inode = dataServerWire->inode;
#endif
	}

	return NFS4_OK;
}

/**
 * @brief Initialize FSAL specific permissions per pNFS DS.
 *
 * @param[in] pnfsDataServer     FSAL pNFS DS
 * @param[in] request            Incoming request
 *
 * @returns: NFSv4.1 error codes: NFS4_OK, NFS4ERR_ACCESS, NFS4ERR_WRONGSEC
 */
static nfsstat4 ds_permissions(struct fsal_pnfs_ds *const pnfsDataServer,
			       struct svc_req *request)
{
	(void)pnfsDataServer;
	(void)request;

	op_ctx->export_perms.set = root_op_export_set;
	op_ctx->export_perms.options = root_op_export_options;
	return NFS4_OK;
}

/**
 * @brief Initialize FSAL specific values for pNFS Data Server.
 *
 * @param[in] ops     FSAL pNFS Data Server operations vector
 */
void pnfsDsOperationsInit(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
	ops->make_ds_handle = make_ds_handle;
	ops->dsh_release = dsh_release;
	ops->dsh_read = dsh_read;
	ops->dsh_write = dsh_write;
	ops->dsh_commit = dsh_commit;
	ops->dsh_read_plus = dsh_read_plus;
	ops->ds_permissions = ds_permissions;
}
