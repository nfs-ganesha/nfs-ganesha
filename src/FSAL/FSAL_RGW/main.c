// SPDX-License-Identifier: LGPL-3.0-or-later
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

static const char *module_name = "RGW";

#if ((LIBRGW_FILE_VER_MAJOR > 1) || \
	((LIBRGW_FILE_VER_MAJOR == 1) && \
	 ((LIBRGW_FILE_VER_MINOR > 1) || \
	  ((LIBRGW_FILE_VER_MINOR == 1) && (LIBRGW_FILE_VER_EXTRA >= 4)))))
#define HAVE_DIRENT_OFFSETOF 1
#else
#define HAVE_DIRENT_OFFSETOF 0
#endif

/**
 * RGW global module object.
 */
struct rgw_fsal_module RGWFSM = {
	.fsal = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
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
			.lock_support_async_block = false,
			.named_attr = true, /* XXX */
			.unique_handles = true,
			.acl_support = 0,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = RGW_SUPPORTED_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.rename_changes_key = true,
			#if HAVE_DIRENT_OFFSETOF
				.compute_readdir_cookie = true,
			#endif
			.whence_is_name = true,
			.expire_time_parent = -1,
		}
	}
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
			rgw_fsal_module, fsal.fs_info.umask),
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

	(void) load_config_from_parse(config_struct,
				      &rgw_block,
				      myself,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&myself->fsal);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
	CONF_MAND_STR("user_id", 0, MAXUIDLEN, NULL,
		      rgw_export, rgw_user_id),
	CONF_MAND_STR("access_key_id", 0, MAXKEYLEN, NULL,
		      rgw_export, rgw_access_key_id),
	CONF_MAND_STR("secret_access_key", 0, MAXSECRETLEN, NULL,
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
	int rc = 0;
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
				if (access(RGWFSM.conf_path, F_OK)) {
					LogCrit(COMPONENT_FSAL,
					"ceph.conf path does not exist");
				}
				clen = strlen(RGWFSM.conf_path) + 8;
				conf_path = (char *) gsh_malloc(clen);
				(void) sprintf(conf_path, "--conf=%s",
					       RGWFSM.conf_path);
				argv[argc] = conf_path;
				++argc;
			}

			if (RGWFSM.name) {
				clen = strlen(RGWFSM.name) + 8;
				inst_name = (char *) gsh_malloc(clen);
				(void) sprintf(inst_name, "--name=%s",
					       RGWFSM.name);
				argv[argc] = inst_name;
				++argc;
			}

			if (RGWFSM.cluster) {
				clen = strlen(RGWFSM.cluster) + 8;
				cluster = (char *) gsh_malloc(clen);
				(void) sprintf(cluster, "--cluster=%s",
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

			gsh_free(conf_path);
			gsh_free(inst_name);
			gsh_free(cluster);
		}
		PTHREAD_MUTEX_unlock(&init_mtx);
	}

	if (rc != 0) {
		status.major = ERR_FSAL_BAD_INIT;
		goto error;
	}

	export = gsh_calloc(1, sizeof(struct rgw_export));
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

#ifndef USE_FSAL_RGW_MOUNT2
	rgw_status = rgw_mount(RGWFSM.rgw,
			       export->rgw_user_id,
			       export->rgw_access_key_id,
			       export->rgw_secret_access_key,
			       &export->rgw_fs,
			       RGW_MOUNT_FLAG_NONE);
#else
	const char *rgw_fullpath = CTX_FULLPATH(op_ctx);

	if (strcmp(rgw_fullpath, "/") && strchr(rgw_fullpath, '/') &&
		(strchr(rgw_fullpath, '/') - rgw_fullpath) > 1) {
		/* case : "bucket_name/dir" */
		rgw_status = rgw_mount(RGWFSM.rgw,
					export->rgw_user_id,
					export->rgw_access_key_id,
					export->rgw_secret_access_key,
					&export->rgw_fs,
					RGW_MOUNT_FLAG_NONE);
	} else {
		/* case : "/" of "bucket_name" or "/bucket_name" */
		if (strcmp(rgw_fullpath, "/") && strchr(rgw_fullpath, '/') &&
			(strchr(rgw_fullpath, '/') - rgw_fullpath) == 0) {
			rgw_fullpath = rgw_fullpath + 1;
		}
	  rgw_status = rgw_mount2(RGWFSM.rgw,
					export->rgw_user_id,
					export->rgw_access_key_id,
					export->rgw_secret_access_key,
					rgw_fullpath,
					&export->rgw_fs,
					RGW_MOUNT_FLAG_NONE);
	}
#endif
	if (rgw_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to mount RGW cluster for %s.",
			CTX_FULLPATH(op_ctx));
		if (rgw_status == -EINVAL) {
			LogCrit(COMPONENT_FSAL,
			"Authorization Failed for user %s ",
			export->rgw_user_id);
		}
		goto error;
	}

	if (fsal_attach_export(module_in, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to attach export for %s.",
			CTX_FULLPATH(op_ctx));
		goto error;
	}

	if (rgw_register_invalidate(export->rgw_fs, rgw_fs_invalidate,
					up_ops->up_fsal_export,
					RGW_REG_INVALIDATE_FLAG_NONE) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to register invalidates for %s.",
			CTX_FULLPATH(op_ctx));
		goto error;
	}

	export->export.fsal = module_in;

	LogDebug(COMPONENT_FSAL,
		 "RGW module export %s.",
		 CTX_FULLPATH(op_ctx));

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

	export->root = handle;
	export->export.up_ops = up_ops;

	return status;

 error:
	gsh_free(export);

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

	/* Initialize the fsal_obj_handle ops for FSAL RGW */
	handle_ops_init(&RGWFSM.handle_ops);
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
