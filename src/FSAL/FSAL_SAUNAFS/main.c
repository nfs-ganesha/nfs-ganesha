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

#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "pnfs_utils.h"

#include "context_wrap.h"
#include "saunafs_fsal_types.h"
#include "saunafs_internal.h"

/* FSAL name determines name of shared library: libfsalsaunafs.so */
static const char *const module = "SaunaFS";

static const int millisecondsInOneSecond = 1000;

/**
 * my module private storage
 */

struct SaunaFSModule
	SaunaFS = { .fsal = {
			    .fs_info = {
				    .maxfilesize = UINT64_MAX,
				    .maxlink = _POSIX_LINK_MAX,
				    .maxnamelen = SFS_NAME_MAX,
				    .maxpathlen = MAXPATHLEN,
				    .no_trunc = true,
				    .chown_restricted = false,
				    .case_insensitive = false,
				    .case_preserving = true,
				    .link_support = true,
				    .symlink_support = true,
				    .lock_support = true,
				    .lock_support_async_block = false,
				    .named_attr = true,
				    .unique_handles = true,
#ifdef ENABLE_NFS_ACL_SUPPORT
				    .acl_support =
					   (unsigned int)FSAL_ACLSUPPORT_ALLOW |
					   (unsigned int)FSAL_ACLSUPPORT_DENY,
#else
				    .acl_support = 0,
#endif
				    .cansettime = true,
				    .homogenous = true,
				    .supported_attrs = SAUNAFS_SUPPORTED_ATTRS,
				    .maxread = (uint64_t)FSAL_MAXIOSIZE,
				    .maxwrite = (uint64_t)FSAL_MAXIOSIZE,
				    .umask = 0,
				    .auth_exportpath_xdev = false,
				    .pnfs_mds = true,
				    .pnfs_ds = true,
				    .fsal_trace = false,
				    .fsal_grace = false,
				    .link_supports_permission_checks = true,
				    .xattr_support = true,
			    } } };

static struct config_item export_params[] = {
	CONF_ITEM_MODE("umask", 0, fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("link_support", true, fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true, fsal_staticfsinfo_t,
		       symlink_support),
	CONF_ITEM_BOOL("cansettime", true, fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_BOOL("auth_xdev_export", false, fsal_staticfsinfo_t,
		       auth_exportpath_xdev),
	CONF_ITEM_UI64("maxread", 512, (uint64_t)FSAL_MAXIOSIZE,
		       (uint64_t)FSAL_MAXIOSIZE, fsal_staticfsinfo_t, maxread),
	CONF_ITEM_UI64("maxwrite", 512, (uint64_t)FSAL_MAXIOSIZE,
		       (uint64_t)FSAL_MAXIOSIZE, fsal_staticfsinfo_t, maxwrite),
	CONF_ITEM_BOOL("PNFS_MDS", false, fsal_staticfsinfo_t, pnfs_mds),
	CONF_ITEM_BOOL("PNFS_DS", false, fsal_staticfsinfo_t, pnfs_ds),
	CONF_ITEM_BOOL("fsal_trace", true, fsal_staticfsinfo_t, fsal_trace),
	CONF_ITEM_BOOL("fsal_grace", false, fsal_staticfsinfo_t, fsal_grace),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.saunafs",
	.blk_desc.name = "SaunaFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE, /* too risky to have more */
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static struct config_item fsal_export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_MAND_STR("hostname", 1, MAXPATHLEN, NULL, SaunaFSExport,
		      parameters.host),
	CONF_ITEM_STR("port", 1, MAXPATHLEN, "9421", SaunaFSExport,
		      parameters.port),
	CONF_ITEM_STR("mountpoint", 1, MAXPATHLEN, "nfs-ganesha", SaunaFSExport,
		      parameters.mountpoint),
	CONF_ITEM_STR("subfolder", 1, MAXPATHLEN, "/", SaunaFSExport,
		      parameters.subfolder),
	CONF_ITEM_BOOL("delayed_init", false, SaunaFSExport,
		       parameters.delayed_init),
	CONF_ITEM_UI32("io_retries", 0, 1024, 30, SaunaFSExport,
		       parameters.io_retries),
	CONF_ITEM_UI32("chunkserver_round_time_ms", 0, 65536, 200,
		       SaunaFSExport, parameters.chunkserver_round_time_ms),
	CONF_ITEM_UI32("chunkserver_connect_timeout_ms", 0, 65536, 2000,
		       SaunaFSExport,
		       parameters.chunkserver_connect_timeout_ms),
	CONF_ITEM_UI32("chunkserver_wave_read_timeout_ms", 0, 65536, 500,
		       SaunaFSExport,
		       parameters.chunkserver_wave_read_timeout_ms),
	CONF_ITEM_UI32("total_read_timeout_ms", 0, 65536, 2000, SaunaFSExport,
		       parameters.total_read_timeout_ms),
	CONF_ITEM_UI32("cache_expiration_time_ms", 0, 65536, 1000,
		       SaunaFSExport, parameters.cache_expiration_time_ms),
	CONF_ITEM_UI32("readahead_max_window_size_kB", 0, 65536, 16384,
		       SaunaFSExport, parameters.readahead_max_window_size_kB),
	CONF_ITEM_UI32("write_cache_size", 0, 1024, 64, SaunaFSExport,
		       parameters.write_cache_size),
	CONF_ITEM_UI32("write_workers", 0, 32, 10, SaunaFSExport,
		       parameters.write_workers),
	CONF_ITEM_UI32("write_window_size", 0, 256, 32, SaunaFSExport,
		       parameters.write_window_size),
	CONF_ITEM_UI32("chunkserver_write_timeout_ms", 0, 60000, 5000,
		       SaunaFSExport, parameters.chunkserver_write_timeout_ms),
	CONF_ITEM_UI32("cache_per_inode_percentage", 0, 80, 25, SaunaFSExport,
		       parameters.cache_per_inode_percentage),
	CONF_ITEM_UI32("symlink_cache_timeout_s", 0, 60000, 3600, SaunaFSExport,
		       parameters.symlink_cache_timeout_s),
	CONF_ITEM_BOOL("debug_mode", false, SaunaFSExport,
		       parameters.debug_mode),
	CONF_ITEM_I32("keep_cache", 0, 2, 0, SaunaFSExport,
		      parameters.keep_cache),
	CONF_ITEM_BOOL("verbose", false, SaunaFSExport, parameters.verbose),
	CONF_ITEM_UI32("fileinfo_cache_timeout", 1, 3600, 60, SaunaFSExport,
		       cacheTimeout),
	CONF_ITEM_UI32("fileinfo_cache_max_size", 100, 1000000, 1000,
		       SaunaFSExport, cacheMaximumSize),
	CONF_ITEM_STR("password", 1, 128, NULL, SaunaFSExport,
		      parameters.password),
	CONF_ITEM_STR("md5_pass", 32, 32, NULL, SaunaFSExport,
		      parameters.md5_pass),
	CONFIG_EOL
};

static struct config_block fsal_export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.saunafs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = fsal_export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/**
 * @brief Release an export
 *
 * @param[in] export SaunaFS export
 */
static inline void releaseExport(struct SaunaFSExport *export)
{
	if (export) {
		if (export->fsInstance)
			sau_destroy(export->fsInstance);

		if (export->cache)
			destroyFileInfoCache(export->cache);

		gsh_free(export);
	}
}

/**
 * @brief Create a new export.
 *
 * This function creates a new export in the FSAL using the supplied path and
 * options. The function is expected to allocate its own export (the full,
 * private structure).
 *
 * @param [in]  module               FSAL module.
 * @param [in]  parseNode            Opaque pointer to parse tree node for
 *                                   export options to be passed to
 *                                   load_config_from_node.
 * @param [out] errorType            Config processing error reporting.
 * @param [in]  operations           Upcall operations.
 *
 * \see fsal_api.h for more information
 *
 * @returns: FSAL status
 */
static fsal_status_t createExport(struct fsal_module *module, void *parseNode,
				  struct config_error_type *errorType,
				  const struct fsal_up_vector *operations)
{
	fsal_status_t status;
	struct fsal_pnfs_ds *pnfsDs = NULL;
	int retvalue = 0;

	struct SaunaFSExport *export =
		gsh_calloc(1, sizeof(struct SaunaFSExport));

	fsal_export_init(&export->export);
	exportOperationsInit(&export->export.exp_ops);

	/* parse parameters for this export */
	sau_set_default_init_params(&export->parameters, "", "", "");

	if (parseNode) {
		retvalue = load_config_from_node(parseNode,
						 &fsal_export_param_block,
						 export, true, errorType);

		if (retvalue != 0) {
			LogCrit(COMPONENT_FSAL,
				"Failed to parse export configuration for %s",
				CTX_FULLPATH(op_ctx));

			releaseExport(export);
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	export->parameters.subfolder = gsh_strdup(CTX_FULLPATH(op_ctx));
	export->fsInstance = sau_init_with_params(&export->parameters);

	if (export->fsInstance == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to mount SaunaFS cluster for %s.",
			CTX_FULLPATH(op_ctx));

		releaseExport(export);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (fsal_attach_export(module, &export->export.exports) != 0) {
		LogCrit(COMPONENT_FSAL, "Unable to attach export for %s.",
			CTX_FULLPATH(op_ctx));

		releaseExport(export);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	export->export.fsal = module;
	export->export.up_ops = operations;

	export->pnfsDsEnabled = export->export.exp_ops.fs_supports(
		&export->export, fso_pnfs_ds_supported);

	if (export->pnfsDsEnabled) {
		export->cache = createFileInfoCache(
			export->cacheMaximumSize,
			(int)export->cacheTimeout * millisecondsInOneSecond);

		if (export->cache == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Unable to create fileinfo cache for %s.",
				CTX_FULLPATH(op_ctx));

			releaseExport(export);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}

		status = module->m_ops.create_fsal_pnfs_ds(module, parseNode,
							   &pnfsDs);

		if (status.major != ERR_FSAL_NO_ERROR) {
			releaseExport(export);
			return status;
		}

		/* special case: server_id matches export_id */
		pnfsDs->id_servers = op_ctx->ctx_export->export_id;
		pnfsDs->mds_export = op_ctx->ctx_export;
		pnfsDs->mds_fsal_export = &export->export;

		if (!pnfs_ds_insert(pnfsDs)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pnfsDs->id_servers);

			status.major = ERR_FSAL_EXIST;

			/* Return the ref taken by create_fsal_pnfs_ds */
			pnfs_ds_put(pnfsDs);

			releaseExport(export);
			return status;
		}

		LogDebug(COMPONENT_PNFS, "pnfs ds was enabled for [%s]",
			 CTX_FULLPATH(op_ctx));
	}

	export->pnfsMdsEnabled = export->export.exp_ops.fs_supports(
		&export->export, fso_pnfs_mds_supported);

	if (export->pnfsMdsEnabled) {
		LogDebug(COMPONENT_PNFS, "pnfs mds was enabled for [%s]",
			 CTX_FULLPATH(op_ctx));

		exportOperationsPnfs(&export->export.exp_ops);
	}

	/* get attributes for root inode */
	sau_attr_reply_t reply;

	retvalue = saunafs_getattr(export->fsInstance, &op_ctx->creds,
				   SPECIAL_INODE_ROOT, &reply);

	if (retvalue < 0) {
		status = fsalLastError();

		if (pnfsDs != NULL) {
			/* Remove and destroy the fsal_pnfs_ds */
			pnfs_ds_remove(pnfsDs->id_servers);
		}

		if (pnfsDs != NULL) {
			/* Return the ref taken by create_fsal_pnfs_ds */
			pnfs_ds_put(pnfsDs);
		}

		releaseExport(export);
		return status;
	}

	export->root = allocateHandle(&reply.attr, export);
	op_ctx->fsal_export = &export->export;

	LogDebug(COMPONENT_FSAL, "SaunaFS module export %s.",
		 CTX_FULLPATH(op_ctx));

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Initialize the configuration.
 *
 * @param [in]  module         FSAL module.
 * @param [in]  configFile     Parsed ganesha configuration file.
 * @param [out] errorType      config processing error reporting.
 *
 * @returns: FSAL status
 */
static fsal_status_t initialize(struct fsal_module *module,
				config_file_t configFile,
				struct config_error_type *errorType)
{
	struct SaunaFSModule *myself = NULL;

	myself = container_of(module, struct SaunaFSModule, fsal);

	(void)load_config_from_parse(configFile, &export_param,
				     &myself->filesystemInfo, true, errorType);

	if (!config_error_is_harmless(errorType)) {
		LogDebug(COMPONENT_FSAL, "config_error_is_harmless failed.");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	display_fsinfo(&myself->fsal);

	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 myself->fsal.fs_info.supported_attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Initialize and register SaunaFS FSAL
 *
 * Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */
MODULE_INIT void initializeSaunaFS(void)
{
	struct fsal_module *myself = &SaunaFS.fsal;

	int retval = register_fsal(myself, module, FSAL_MAJOR_VERSION,
				   FSAL_MINOR_VERSION, FSAL_ID_SAUNAFS);

	if (retval) {
		LogCrit(COMPONENT_FSAL, "SaunaFS module failed to register.");
		return;
	}

	/* Set up module operations */
	myself->m_ops.create_export = createExport;
	myself->m_ops.init_config = initialize;
	myself->m_ops.fsal_pnfs_ds_ops = pnfsDsOperationsInit;

	pnfsMdsOperationsInit(&myself->m_ops);

	/* Initialize fsal_obj_handle ops for FSAL SaunaFS */
	handleOperationsInit(&SaunaFS.handleOperations);
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.
 */
MODULE_FINI void finish(void)
{
	int retval = unregister_fsal(&SaunaFS.fsal);

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload SaunaFS FSAL. Dying with extreme prejudice.");
		abort();
	}
}
