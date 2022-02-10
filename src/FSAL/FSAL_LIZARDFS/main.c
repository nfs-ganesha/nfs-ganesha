/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <limits.h>

#include "fsal.h"
#include "fsal_api.h"
#include "fsal_types.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_init.h"
#include "pnfs_utils.h"

#include "context_wrap.h"
#include "lzfs_internal.h"

static struct lzfs_fsal_module gLizardFSM;
static const char *gModuleName = "LizardFS";

static fsal_staticfsinfo_t default_lizardfs_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = MFS_NAME_MAX,
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
	.acl_support = FSAL_ACLSUPPORT_ALLOW | FSAL_ACLSUPPORT_DENY,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = LZFS_SUPPORTED_ATTRS,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.pnfs_mds = false,
	.pnfs_ds = false,
	.fsal_trace = false,
	.fsal_grace = false,
	.link_supports_permission_checks = true,
};

static struct config_item lzfs_fsal_items[] = {
	CONF_ITEM_MODE("umask", 0, fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("link_support", true, fsal_staticfsinfo_t,
			link_support),
	CONF_ITEM_BOOL("symlink_support", true, fsal_staticfsinfo_t,
			symlink_support),
	CONF_ITEM_BOOL("cansettime", true, fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_BOOL("auth_xdev_export", false, fsal_staticfsinfo_t,
			auth_exportpath_xdev),
	CONF_ITEM_BOOL("PNFS_MDS", false, fsal_staticfsinfo_t,
			pnfs_mds),
	CONF_ITEM_BOOL("PNFS_DS", false, fsal_staticfsinfo_t, pnfs_ds),
	CONF_ITEM_BOOL("fsal_trace", true, fsal_staticfsinfo_t, fsal_trace),
	CONF_ITEM_BOOL("fsal_grace", false, fsal_staticfsinfo_t, fsal_grace),
	CONFIG_EOL
};

static struct config_block lzfs_fsal_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.lizardfs",
	.blk_desc.name = "LizardFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = lzfs_fsal_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static struct config_item lzfs_fsal_export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_MAND_STR("hostname", 1, MAXPATHLEN, NULL, lzfs_fsal_export,
		      lzfs_params.host),
	CONF_ITEM_STR("port", 1, MAXPATHLEN, "9421", lzfs_fsal_export,
		      lzfs_params.port),
	CONF_ITEM_STR("mountpoint", 1, MAXPATHLEN, "nfs-ganesha",
		      lzfs_fsal_export, lzfs_params.mountpoint),
	CONF_ITEM_STR("subfolder", 1, MAXPATHLEN, "/", lzfs_fsal_export,
		      lzfs_params.subfolder),
	CONF_ITEM_BOOL("delayed_init", false, lzfs_fsal_export,
			lzfs_params.delayed_init),
	CONF_ITEM_UI32("io_retries", 0, 1024, 30, lzfs_fsal_export,
			lzfs_params.io_retries),
	CONF_ITEM_UI32("chunkserver_round_time_ms", 0, 65536, 200,
			lzfs_fsal_export,
			lzfs_params.chunkserver_round_time_ms),
	CONF_ITEM_UI32("chunkserver_connect_timeout_ms", 0, 65536, 2000,
			lzfs_fsal_export,
			lzfs_params.chunkserver_connect_timeout_ms),
	CONF_ITEM_UI32("chunkserver_wave_read_timeout_ms", 0, 65536, 500,
			lzfs_fsal_export,
			lzfs_params.chunkserver_wave_read_timeout_ms),
	CONF_ITEM_UI32("total_read_timeout_ms", 0, 65536, 2000,
			lzfs_fsal_export, lzfs_params.total_read_timeout_ms),
	CONF_ITEM_UI32("cache_expiration_time_ms", 0, 65536, 1000,
			lzfs_fsal_export,
			lzfs_params.cache_expiration_time_ms),
	CONF_ITEM_UI32("readahead_max_window_size_kB", 0, 65536, 16384,
			lzfs_fsal_export,
			lzfs_params.readahead_max_window_size_kB),
	CONF_ITEM_UI32("write_cache_size", 0, 1024, 64, lzfs_fsal_export,
			lzfs_params.write_cache_size),
	CONF_ITEM_UI32("write_workers", 0, 32, 10, lzfs_fsal_export,
			lzfs_params.write_workers),
	CONF_ITEM_UI32("write_window_size", 0, 256, 32, lzfs_fsal_export,
			lzfs_params.write_window_size),
	CONF_ITEM_UI32("chunkserver_write_timeout_ms", 0, 60000, 5000,
			lzfs_fsal_export,
			lzfs_params.chunkserver_write_timeout_ms),
	CONF_ITEM_UI32("cache_per_inode_percentage", 0, 80, 25,
			lzfs_fsal_export,
			lzfs_params.cache_per_inode_percentage),
	CONF_ITEM_UI32("symlink_cache_timeout_s", 0, 60000, 3600,
			lzfs_fsal_export, lzfs_params.symlink_cache_timeout_s),
	CONF_ITEM_BOOL("debug_mode", false, lzfs_fsal_export,
			lzfs_params.debug_mode),
	CONF_ITEM_I32("keep_cache", 0, 2, 0, lzfs_fsal_export,
			lzfs_params.keep_cache),
	CONF_ITEM_BOOL("verbose", false, lzfs_fsal_export,
			lzfs_params.verbose),
	CONF_ITEM_UI32("fileinfo_cache_timeout", 1, 3600, 60, lzfs_fsal_export,
			fileinfo_cache_timeout),
	CONF_ITEM_UI32("fileinfo_cache_max_size", 100, 1000000, 1000,
			lzfs_fsal_export, fileinfo_cache_max_size),
	CONF_ITEM_STR("password", 1, 128, NULL, lzfs_fsal_export,
			lzfs_params.password),
	CONF_ITEM_STR("md5_pass", 32, 32, NULL, lzfs_fsal_export,
			lzfs_params.md5_pass),
	CONFIG_EOL
};

static struct config_block lzfs_fsal_export_param_block = {
	.dbus_interface_name =
			"org.ganesha.nfsd.config.fsal.lizardfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = lzfs_fsal_export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static fsal_status_t lzfs_fsal_create_export(
					struct fsal_module *fsal_hdl,
					void *parse_node,
					struct config_error_type *err_type,
					const struct fsal_up_vector *up_ops)
{
	struct lzfs_fsal_export *lzfs_export;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	int rc;
	struct fsal_pnfs_ds *pds = NULL;

	lzfs_export = gsh_calloc(1, sizeof(struct lzfs_fsal_export));

	fsal_export_init(&lzfs_export->export);
	lzfs_fsal_export_ops_init(&lzfs_export->export.exp_ops);

	// parse params for this export
	liz_set_default_init_params(&lzfs_export->lzfs_params, "", "", "");
	if (parse_node) {
		rc = load_config_from_node(parse_node,
					   &lzfs_fsal_export_param_block,
					   lzfs_export,
					   true,
					   err_type);
		if (rc != 0) {
			LogCrit(COMPONENT_FSAL,
				"Failed to parse export configuration for %s",
				CTX_FULLPATH(op_ctx));

			status = fsalstat(ERR_FSAL_INVAL, 0);
			goto error;
		}
	}

	lzfs_export->lzfs_params.subfolder = gsh_strdup(CTX_FULLPATH(op_ctx));
	lzfs_export->lzfs_instance = liz_init_with_params(
						&lzfs_export->lzfs_params);

	if (lzfs_export->lzfs_instance == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to mount LizardFS cluster for %s.",
			CTX_FULLPATH(op_ctx));
		status = fsalstat(ERR_FSAL_SERVERFAULT, 0);
		goto error;
	}

	if (fsal_attach_export(fsal_hdl, &lzfs_export->export.exports) != 0) {
		LogCrit(COMPONENT_FSAL, "Unable to attach export for %s.",
			CTX_FULLPATH(op_ctx));
		status = fsalstat(ERR_FSAL_SERVERFAULT, 0);
		goto error;
	}

	lzfs_export->export.fsal = fsal_hdl;
	lzfs_export->export.up_ops = up_ops;

	lzfs_export->pnfs_ds_enabled =
		lzfs_export->export.exp_ops.fs_supports(&lzfs_export->export,
							fso_pnfs_ds_supported);
	if (lzfs_export->pnfs_ds_enabled) {
		lzfs_export->fileinfo_cache = liz_create_fileinfo_cache(
			lzfs_export->fileinfo_cache_max_size,
			lzfs_export->fileinfo_cache_timeout * 1000);
		if (lzfs_export->fileinfo_cache == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Unable to create fileinfo cache for %s.",
				CTX_FULLPATH(op_ctx));
			status = fsalstat(ERR_FSAL_SERVERFAULT, 0);
			goto error;
		}

		status = fsal_hdl->m_ops.create_fsal_pnfs_ds(fsal_hdl,
							     parse_node,
							     &pds);
		if (status.major != ERR_FSAL_NO_ERROR) {
			goto error;
		}

		/* special case: server_id matches export_id */
		pds->id_servers = op_ctx->ctx_export->export_id;
		pds->mds_export = op_ctx->ctx_export;
		pds->mds_fsal_export = &lzfs_export->export;

		if (!pnfs_ds_insert(pds)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pds->id_servers);
			status.major = ERR_FSAL_EXIST;

			/* Return the ref taken by create_fsal_pnfs_ds */
			pnfs_ds_put(pds);
			goto error;
		}

		LogDebug(COMPONENT_PNFS, "pnfs ds was enabled for [%s]",
			 CTX_FULLPATH(op_ctx));
	}

	lzfs_export->pnfs_mds_enabled =
		lzfs_export->export.exp_ops.fs_supports(
				&lzfs_export->export, fso_pnfs_mds_supported);
	if (lzfs_export->pnfs_mds_enabled) {
		LogDebug(COMPONENT_PNFS,
			 "pnfs mds was enabled for [%s]",
			 CTX_FULLPATH(op_ctx));
		lzfs_fsal_export_ops_pnfs(&lzfs_export->export.exp_ops);
	}

	// get attributes for root inode
	liz_attr_reply_t ret;

	rc = liz_cred_getattr(lzfs_export->lzfs_instance,
			      &op_ctx->creds,
			      SPECIAL_INODE_ROOT,
			      &ret);
	if (rc < 0) {
		status = lzfs_fsal_last_err();

		if (pds != NULL) {
			/* Remove and destroy the fsal_pnfs_ds */
			pnfs_ds_remove(pds->id_servers);
		}
		goto error_pds;
	}

	lzfs_export->root = lzfs_fsal_new_handle(&ret.attr, lzfs_export);
	op_ctx->fsal_export = &lzfs_export->export;

	LogDebug(COMPONENT_FSAL,
		 "LizardFS module export %s.",
		 CTX_FULLPATH(op_ctx));

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

error_pds:
	if (pds != NULL)
		/* Return the ref taken by create_fsal_pnfs_ds */
		pnfs_ds_put(pds);

error:
	if (lzfs_export) {
		if (lzfs_export->lzfs_instance) {
			liz_destroy(lzfs_export->lzfs_instance);
		}
		if (lzfs_export->fileinfo_cache) {
			liz_destroy_fileinfo_cache(
						lzfs_export->fileinfo_cache);
		}
		gsh_free(lzfs_export);
	}

	return status;
}

static fsal_status_t lzfs_fsal_init_config(struct fsal_module *module_in,
					   config_file_t config_struct,
					   struct config_error_type *err_type)
{
	struct lzfs_fsal_module *lzfs_module;

	lzfs_module = container_of(module_in, struct lzfs_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL, "LizardFS module setup.");

	lzfs_module->fs_info = default_lizardfs_info;
	(void)load_config_from_parse(config_struct, &lzfs_fsal_param_block,
				     &lzfs_module->fs_info, true, err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&lzfs_module->fsal);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

MODULE_INIT void init(void)
{
	struct fsal_module *lzfs_module = &gLizardFSM.fsal;

	LogDebug(COMPONENT_FSAL, "LizardFS module registering.");

	memset(lzfs_module, 0, sizeof(*lzfs_module));
	if (register_fsal(lzfs_module, gModuleName, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_LIZARDFS)) {
		LogCrit(COMPONENT_FSAL, "LizardFS module failed to register.");
	}

	lzfs_module->m_ops.fsal_pnfs_ds_ops = lzfs_fsal_ds_handle_ops_init;
	lzfs_module->m_ops.create_export = lzfs_fsal_create_export;
	lzfs_module->m_ops.init_config = lzfs_fsal_init_config;
	lzfs_fsal_ops_pnfs(&lzfs_module->m_ops);
}

MODULE_FINI void finish(void)
{
	LogDebug(COMPONENT_FSAL, "LizardFS module finishing.");

	if (unregister_fsal(&gLizardFSM.fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload LizardFS FSAL. Dying with extreme prejudice.");
		abort();
	}
}
