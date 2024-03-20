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

#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_convert.h"
#include "nfs_exports.h"

#include "saunafs_fsal_types.h"
#include "context_wrap.h"
#include "saunafs_internal.h"

/* Flags to determine if ACLs are supported */
#define NFSv4_ACL_SUPPORT (!op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL))

/**
 * @brief Finalize an export
 *
 * This function is called as part of cleanup when the last reference to an
 * export is released and it is no longer part of the list.
 *
 * It should clean up all private resources and destroy the object.
 *
 * @param[in] exportHandle     The export to release
 */
static void release(struct fsal_export *exportHandle)
{
	struct SaunaFSExport *export = NULL;

	export = container_of(exportHandle, struct SaunaFSExport, export);

	deleteHandle(export->root);
	export->root = NULL;

	fsal_detach_export(export->export.fsal, &export->export.exports);
	free_export_ops(&export->export);

	if (export->cache) {
		resetFileInfoCacheParameters(export->cache, 0, 0);

		while (1) {
			FileInfoEntry_t *cacheHandle = NULL;
			fileinfo_t *fileHandle = NULL;

			cacheHandle = popExpiredFileInfoCache(export->cache);
			if (!cacheHandle)
				break;

			fileHandle = extractFileInfo(cacheHandle);
			sau_release(export->fsInstance, fileHandle);
			fileInfoEntryFree(cacheHandle);
		}

		destroyFileInfoCache(export->cache);
		export->cache = NULL;
	}

	sau_destroy(export->fsInstance);
	export->fsInstance = NULL;

	gsh_free((char *)export->parameters.subfolder);
	gsh_free(export);
}

/**
 * @brief Look up a path.
 *
 * Create an object handles within this export.
 *
 * This function looks up a path within the export, it is now exclusively
 * used to get a handle for the root directory of the export.
 *
 * @param [in]     exportHandle     The export in which to look up
 * @param [in]     path             The path to look up
 * @param [out]    handle           The object found
 * @param [in,out] attributes       Optional attributes for newly created object
 *
 * \see fsal_api.h for more information
 *
 * @returns: FSAL status
 */
fsal_status_t lookup_path(struct fsal_export *exportHandle, const char *path,
			  struct fsal_obj_handle **handle,
			  struct fsal_attrlist *attributes)
{
	static const char *rootDirPath = "/";

	struct SaunaFSExport *export = NULL;
	struct SaunaFSHandle *objectHandle = NULL;
	const char *realPath = NULL;

	LogFullDebug(COMPONENT_FSAL, "export_id=%" PRIu16 " path=%s",
		     exportHandle->export_id, path);

	export = container_of(exportHandle, struct SaunaFSExport, export);

	*handle = NULL;

	/* set the real path to the path without the prefix from
	 * ctx_export->fullpath */
	if (*path != '/') {
		realPath = strchr(path, ':');
		if (realPath == NULL)
			return fsalstat(ERR_FSAL_INVAL, 0);

		++realPath;
		if (*realPath != '/')
			return fsalstat(ERR_FSAL_INVAL, 0);
	} else {
		realPath = path;
	}

	if (strstr(realPath, CTX_FULLPATH(op_ctx)) != realPath)
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);

	realPath += strlen(CTX_FULLPATH(op_ctx));
	if (*realPath == '\0')
		realPath = rootDirPath;

	LogFullDebug(COMPONENT_FSAL, "real path = %s", realPath);

	/* special case the root */
	if (strcmp(realPath, "/") == 0) {
		assert(export->root);
		*handle = &export->root->handle;

		if (attributes == NULL)
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	sau_entry_t entry;
	int status = saunafs_lookup(export->fsInstance, &op_ctx->creds,
				    SPECIAL_INODE_ROOT, realPath, &entry);

	if (status < 0)
		return fsalLastError();

	if (attributes != NULL)
		posix2fsal_attributes_all(&entry.attr, attributes);

	if (*handle == NULL) {
		objectHandle = allocateHandle(&entry.attr, export);
		*handle = &objectHandle->handle;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get dynamic filesystem statistics and configuration for this
 * filesystem.
 *
 * This function gets information on inodes and space in use and free for a
 * filesystem. See fsal_dynamicfsinfo_t for details of what to fill out.
 *
 * @param[in]  exportHandle     Export handle to interrogate
 * @param[in]  objectHandle     Directory
 * @param[out] info             Buffer to fill with information
 *
 * @returns: FSAL status.
 */
static fsal_status_t get_dynamic_info(struct fsal_export *exportHandle,
				      struct fsal_obj_handle *objectHandle,
				      fsal_dynamicfsinfo_t *info)
{
	(void)objectHandle;

	struct SaunaFSExport *export = NULL;

	export = container_of(exportHandle, struct SaunaFSExport, export);

	sau_stat_t statfsEntry;
	int status = sau_statfs(export->fsInstance, &statfsEntry);

	if (status < 0)
		return fsalLastError();

	memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
	info->total_bytes = statfsEntry.total_space;
	info->free_bytes = statfsEntry.avail_space;
	info->avail_bytes = statfsEntry.avail_space;

	info->total_files = MAX_REGULAR_INODE;
	info->free_files = MAX_REGULAR_INODE - statfsEntry.inodes;
	info->avail_files = MAX_REGULAR_INODE - statfsEntry.inodes;

	info->time_delta.tv_sec = 0;
	info->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Free a state_t structure.
 *
 * @param[in] state      state_t structure to free
 */
void fs_free_state(struct state_t *state)
{
	struct SaunaFSFd *fd = NULL;

	fd = &container_of(state, struct SaunaFSStateFd, state)->saunafsFd;

	destroy_fsal_fd(&fd->fsalFd);
	gsh_free(state);
}

/**
 * @brief Allocate a state_t structure.
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param [in] export           Export state_t will be associated with
 * @param [in] stateType        Type of state to allocate
 * @param [in] relatedState     Related state if appropriate
 *
 * @returns: a state structure.
 */
struct state_t *allocate_state(struct fsal_export *export,
			       enum state_type stateType,
			       struct state_t *relatedState)
{
	(void)export; /* Not used */
	struct state_t *state = NULL;
	struct SaunaFSFd *fileDescriptor = NULL;

	state = init_state(gsh_calloc(1, sizeof(struct SaunaFSStateFd)),
			   fs_free_state, stateType, relatedState);

	fileDescriptor =
		&container_of(state, struct SaunaFSStateFd, state)->saunafsFd;

	init_fsal_fd(&fileDescriptor->fsalFd, FSAL_FD_STATE,
		     op_ctx->fsal_export);
	fileDescriptor->fd = NULL;

	return state;
}

/**
 * @brief Convert a wire handle to a host handle.
 *
 * This function extracts a host handle from a wire handle. That is, when
 * given a handle as passed to a client, this method will extract the handle
 * to create objects.
 *
 * @param[in]     export             Export handle.
 * @param[in]     protocol           Protocol through which buffer was received.
 * @param[in]     flags              Flags to describe the wire handle. Example,
 *                                   if the handle is a big endian handle.
 * @param[in,out] buffer             Buffer descriptor. The address of the
 *                                   buffer is given in bufferDescriptor->buf
 *                                   and must not be changed.
 *                                   bufferDescriptor->len is the length of the
 *                                   data contained in the buffer,
 *                                   bufferDescriptor->len must be updated to
 *                                   the correct host handle size.
 *
 * @returns: FSAL type.
 */
static fsal_status_t wire_to_host(struct fsal_export *export,
				  fsal_digesttype_t protocol,
				  struct gsh_buffdesc *buffer, int flags)
{
	(void)protocol;
	(void)export;

	if (!buffer || !buffer->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	sau_inode_t *inode = (sau_inode_t *)buffer->addr;

	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		static_assert(sizeof(sau_inode_t) == 4, "");
		*inode = bswap_32(*inode);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		assert(sizeof(sau_inode_t) == 4);
		*inode = bswap_32(*inode);
#endif
	}

	if (buffer->len != sizeof(sau_inode_t)) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle. Should be %zu, got %zu",
			 sizeof(sau_inode_t), buffer->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Extract "key" from a host handle
 *
 * This function extracts a "key" from a host handle. That is, when given a
 * handle that is extracted from wire_to_host() above, this method will extract
 * the unique bits used to index the inode cache.
 *
 * @param[in]     export      Export handle
 * @param[in,out] buffer      Buffer descriptor. The address of the buffer is
 *                            given in @c buffer->addr and must not be changed.
 *                            @c buffer->len is the length of the data
 *                            contained in the buffer, @c buffer->len must be
 *                            updated to the correct size. In other words, the
 *                            key has to be placed at the beginning of the
 *                            buffer.
 */
fsal_status_t host_to_key(struct fsal_export *export,
			  struct gsh_buffdesc *buffer)
{
	(void)export;
	struct SaunaFSHandleKey *key = buffer->addr;
	/*
	 * Ganesha automatically mixes the export_id in with the actual wire
	 * filehandle and strips that out before transforming it to a host
	 * handle.
	 * This method is called on a host handle which doesn't have the
	 * export_id.
	 */
	key->exportId = op_ctx->ctx_export->export_id;
	buffer->len = sizeof(*key);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a FSAL object handle from a host handle.
 *
 * This function creates a FSAL object handle from a host handle (when an
 * object is no longer in cache but the client still remembers the handle).
 *
 * @param[in]     exportHandle     The export in which to create the handle.
 * @param[in]     buffer           Buffer descriptor for the host handle.
 * @param[in]     publicHandle     FSAL object handle.
 * @param[in,out] attributes       Optional attributes for newly created object.
 *
 * \see fsal_api.h for more information
 *
 * @returns: FSAL status
 */
fsal_status_t create_handle(struct fsal_export *exportHandle,
			    struct gsh_buffdesc *buffer,
			    struct fsal_obj_handle **publicHandle,
			    struct fsal_attrlist *attributes)
{
	struct SaunaFSExport *export = NULL;
	struct SaunaFSHandle *handle = NULL;
	sau_inode_t *inode = NULL;

	export = container_of(exportHandle, struct SaunaFSExport, export);
	inode = (sau_inode_t *)buffer->addr;

	*publicHandle = NULL;
	if (buffer->len != sizeof(sau_inode_t))
		return fsalstat(ERR_FSAL_INVAL, 0);

	sau_attr_reply_t result;
	int status = saunafs_getattr(export->fsInstance, &op_ctx->creds, *inode,
				     &result);

	if (status < 0)
		return fsalLastError();

	handle = allocateHandle(&result.attr, export);

	if (attributes != NULL)
		posix2fsal_attributes_all(&result.attr, attributes);

	*publicHandle = &handle->handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get supported ACL types.
 *
 * This function returns a bitmask indicating whether it supports
 * ALLOW, DENY, neither, or both types of ACL.
 *
 * @param[in] export       Filesystem to interrogate.
 *
 * @returns: supported ACL types.
 */
static fsal_aclsupp_t fs_acl_support(struct fsal_export *export)
{
	return fsal_acl_support(&export->fsal->fs_info);
}

/**
 * @brief Get supported attributes.
 *
 * This function returns a list of all attributes that this FSAL will support.
 * Be aware that this is specifically the attributes in struct fsal_attrlist,
 * other NFS attributes (fileid and so forth) are supported through other means.
 *
 * @param[in] export       Filesystem to interrogate.
 *
 * @returns: supported attributes.
 */
static attrmask_t fs_supported_attrs(struct fsal_export *export)
{
	attrmask_t supported_mask = 0;

	supported_mask = fsal_supported_attrs(&export->fsal->fs_info);

	/* Fixup supported_mask to indicate if ACL is supported for
	 * this export */
	if (NFSv4_ACL_SUPPORT)
		supported_mask |= (attrmask_t)ATTR_ACL;
	else
		supported_mask &= ~(attrmask_t)ATTR_ACL;

	return supported_mask;
}

/**
 * @brief Function to get the fsal_obj_handle that has fsal_fd as its global fd.
 *
 * @param[in]     export    The export in which the handle exists
 * @param[in]     fd        File descriptor in question
 * @param[out]    handle    FSAL object handle
 *
 * @return the fsal_obj_handle.
 */
void get_fsal_obj_hdl(struct fsal_export *export, struct fsal_fd *fd,
		      struct fsal_obj_handle **handle)
{
	(void)export; /* Not used */
	struct SaunaFSFd *saunafsFd = NULL;
	struct SaunaFSHandle *myself = NULL;

	saunafsFd = container_of(fd, struct SaunaFSFd, fsalFd);
	myself = container_of(saunafsFd, struct SaunaFSHandle, fd);

	*handle = &myself->handle;
}

/**
 * @brief Set operations for exports
 *
 * This function overrides operations that we've implemented, leaving the rest
 * for the default.
 *
 * @param[in,out] ops     Operations vector
 */
void exportOperationsInit(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = lookup_path;
	ops->wire_to_host = wire_to_host;
	ops->host_to_key = host_to_key;
	ops->create_handle = create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_acl_support = fs_acl_support;
	ops->alloc_state = allocate_state;
	ops->get_fsal_obj_hdl = get_fsal_obj_hdl;
	exportOperationsPnfs(ops);
}
