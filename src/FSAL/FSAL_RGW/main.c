/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat, 2015
 * Author: Orit Wasserman <owasserm@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include <stdlib.h>
#include <assert.h>
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "mdcache.h"

static const char *module_name = "RGW";

/* filesystem info for RGW */
static struct fsal_staticfsinfo_t default_rgw_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = false,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = false,
	.symlink_support = false,
	.lock_support = false,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true, /* XXX */
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = false,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = rgw_supported_attributes,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
};

static struct config_item rgw_items[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
		rgw_fsal_module, conf_path),
	CONF_ITEM_STR("name", 1, MAXPATHLEN, NULL,
		rgw_fsal_module, name),
	CONF_ITEM_STR("cluster", 1, MAXPATHLEN, NULL,
		rgw_fsal_module, cluster),
	CONF_ITEM_STR("init_args", 1, MAXPATHLEN, NULL,
		rgw_fsal_module, init_args),
	CONF_ITEM_MODE("umask", 0,
			rgw_fsal_module, fs_info.umask),
	CONF_ITEM_MODE("xattr_access_rights", 0,
			rgw_fsal_module, fs_info.xattr_access_rights),
	CONFIG_EOL
};

struct config_block rgw_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.rgw",
	.blk_desc.name = "RGW",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = rgw_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static pthread_mutex_t init_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *module_in,
				config_file_t config_struct,
				struct config_error_type *err_type)
{
	struct rgw_fsal_module *myself =
	    container_of(module_in, struct rgw_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL,
		 "RGW module setup.");

	myself->fs_info = default_rgw_info;
	(void) load_config_from_parse(config_struct,
				      &rgw_block,
				      myself,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

 /**
 * @brief Indicate support for extended operations.
 *
 * @retval true if extended operations are supported.
 */

bool support_ex(struct fsal_obj_handle *obj)
{
	return true;
}

/**
 * @brief Create a new export under this FSAL
 *
 * This function creates a new export object for the RGW FSAL.
 *
 * @todo ACE: We do not handle re-exports of the same cluster in a
 * sane way.  Currently we create multiple handles and cache objects
 * pointing to the same one.  This is not necessarily wrong, but it is
 * inefficient.  It may also not be something we expect to use enough
 * to care about.
 *
 * @param[in]     module_in  The supplied module handle
 * @param[in]     path       The path to export
 * @param[in]     options    Export specific options for the FSAL
 * @param[in,out] list_entry Our entry in the export list
 * @param[in]     next_fsal  Next stacked FSAL
 * @param[out]    pub_export Newly created FSAL export object
 *
 * @return FSAL status.
 */

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_STR("user_id", 0, MAXUIDLEN, NULL,
		      rgw_export, rgw_user_id),
	CONF_ITEM_STR("access_key_id", 0, MAXKEYLEN, NULL,
		      rgw_export, rgw_access_key_id),
	CONF_ITEM_STR("secret_access_key", 0, MAXSECRETLEN, NULL,
		      rgw_export, rgw_secret_access_key),
	CONFIG_EOL
};

static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.rgw-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static fsal_status_t create_export(struct fsal_module *module_in,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The internal export object */
	struct rgw_export *export = NULL;
	/* The 'private' root handle */
	struct rgw_handle *handle = NULL;
	/* Stat for root */
	struct stat st;
	/* Return code */
	int rc;
	/* Return code from RGW calls */
	int rgw_status;
	/* True if we have called fsal_export_init */
	bool initialized = false;

	/* once */
	if (!RGWFSM.rgw) {
		PTHREAD_MUTEX_lock(&init_mtx);
		if (!RGWFSM.rgw) {
			char *conf_path = NULL;
			char *inst_name = NULL;
			char *cluster = NULL;

			int argc = 1;
			char *argv[5] = { "nfs-ganesha",
					  NULL, NULL, NULL, NULL };
			int clen;

			if (RGWFSM.conf_path) {
				clen = strlen(RGWFSM.conf_path) + 8;
				conf_path = (char *) gsh_malloc(clen);
				sprintf(conf_path, "--conf=%s",
					RGWFSM.conf_path);
				argv[argc] = conf_path;
				++argc;
			}

			if (RGWFSM.name) {
				clen = strlen(RGWFSM.name) + 8;
				inst_name = (char *) gsh_malloc(clen);
				sprintf(inst_name, "--name=%s", RGWFSM.name);
				argv[argc] = inst_name;
				++argc;
			}

			if (RGWFSM.cluster) {
				clen = strlen(RGWFSM.cluster) + 8;
				cluster = (char *) gsh_malloc(clen);
				sprintf(cluster, "--cluster=%s",
					RGWFSM.cluster);
				argv[argc] = cluster;
				++argc;
			}

			if (RGWFSM.init_args) {
				argv[argc] = RGWFSM.init_args;
				++argc;
			}

			rc = librgw_create(&RGWFSM.rgw, argc, argv);
			if (rc != 0) {
				LogCrit(COMPONENT_FSAL,
					"RGW module: librgw init failed (%d)",
					rc);
			}

			if (conf_path)
				gsh_free(conf_path);
			if (inst_name)
				gsh_free(inst_name);
		}
		PTHREAD_MUTEX_unlock(&init_mtx);
	}

	if (rc != 0) {
		status.major = ERR_FSAL_BAD_INIT;
		goto error;
	}

	export = gsh_calloc(1, sizeof(struct rgw_export));
	if (export == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export object for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	fsal_export_init(&export->export);
	export_ops_init(&export->export.exp_ops);

	/* get params for this export, if any */
	if (parse_node) {
		rc = load_config_from_node(parse_node,
					   &export_param_block,
					   export,
					   true,
					   err_type);
		if (rc != 0) {
			gsh_free(export);
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	initialized = true;

	rgw_status = rgw_mount(RGWFSM.rgw,
			export->rgw_user_id,
			export->rgw_access_key_id,
			export->rgw_secret_access_key,
			&export->rgw_fs,
			RGW_MOUNT_FLAG_NONE);
	if (rgw_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to mount RGW cluster for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	if (fsal_attach_export(module_in, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to attach export for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	export->export.fsal = module_in;

	LogDebug(COMPONENT_FSAL,
		 "RGW module export %s.",
		 op_ctx->export->fullpath);

	rc = rgw_getattr(export->rgw_fs, export->rgw_fs->root_fh, &st,
			RGW_GETATTR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(export, export->rgw_fs->root_fh, &st, &handle);
	if (rc < 0) {
		status = rgw2fsal_error(rc);
		goto error;
	}

	op_ctx->fsal_export = &export->export;

	/* Stack MDCACHE on top */
	status = mdcache_export_init(up_ops, &export->export.up_ops);
	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "MDCACHE creation failed for RGW");
		goto error;
	}

	return status;

 error:
	if (export) {
		gsh_free(export);
	}

	if (initialized)
		initialized = false;

	return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a RGW cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.
 */

MODULE_INIT void init(void)
{
	struct fsal_module *myself = &RGWFSM.fsal;

	LogDebug(COMPONENT_FSAL,
		 "RGW module registering.");

	/* register_fsal seems to expect zeroed memory. */
	memset(myself, 0, sizeof(*myself));

	if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_RGW) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		LogCrit(COMPONENT_FSAL,
			"RGW module failed to register.");
	}

	/* Set up module operations */
	myself->m_ops.create_export = create_export;
	myself->m_ops.init_config = init_config;
	myself->m_ops.support_ex = support_ex;
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.  The
 * FSAL also has an open instance of the rgw library, so we also need to
 * release that.
 */

MODULE_FINI void finish(void)
{
	int ret;

	LogDebug(COMPONENT_FSAL,
		 "RGW module finishing.");

	ret = unregister_fsal(&RGWFSM.fsal);
	if (ret != 0) {
		LogCrit(COMPONENT_FSAL,
			"RGW: unregister_fsal failed (%d)", ret);
	}

	/* release the library */
	if (RGWFSM.rgw) {
		librgw_shutdown(RGWFSM.rgw);
	}
}
