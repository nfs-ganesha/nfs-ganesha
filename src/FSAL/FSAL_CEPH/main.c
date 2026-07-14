// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright © 2012-2014, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
 *		  Marcus Watts <mdw@cohortfs.com>
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
 * @file FSAL_CEPH/main.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @author William Allen Simpson <bill@cohortfs.com>
 * @date Wed Oct 22 13:24:33 2014
 *
 * @brief Implementation of FSAL module founctions for Ceph
 *
 * This file implements the module functions for the Ceph FSAL, for
 * initialization, teardown, configuration, and creation of exports.
 */

#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "statx_compat.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "city.h"

/**
 * The name of this module.
 */
static const char *module_name = "Ceph";

/**
 * Ceph global module object.
 */
struct ceph_fsal_module
	CephFSM = { .fsal = {
			    .fs_info = {
#if 0
			.umask = 0,
#endif
				    /* fixed */
				    .symlink_support = true,
				    .link_support = true,
				    .cansettime = true,
				    .no_trunc = true,
				    .chown_restricted = true,
				    .case_preserving = true,
				    .maxfilesize = INT64_MAX,
				    .maxread = FSAL_MAXIOSIZE,
				    .maxwrite = FSAL_MAXIOSIZE,
				    .maxlink = 1024,
				    .maxnamelen = NAME_MAX,
				    .maxpathlen = PATH_MAX,
#ifdef CEPHFS_POSIX_ACL
				    .acl_support = FSAL_ACLSUPPORT_ALLOW,
#else /* CEPHFS_POSIX_ACL */
				    .acl_support = 0,
#endif /* CEPHFS_POSIX_ACL */
				    .supported_attrs = CEPH_SUPPORTED_ATTRS,
#ifdef USE_FSAL_CEPH_SETLK
				    .lock_support = true,
				    .lock_support_async_block = false,
#endif
				    .unique_handles = true,
				    .homogenous = true,
#ifdef USE_FSAL_CEPH_LL_DELEGATION
				    .delegations = FSAL_OPTION_FILE_DELEGATIONS,
#endif
				    .readdir_plus = true,
				    .xattr_support = true,
#ifdef USE_FSAL_CEPH_FS_ZEROCOPY_IO
				    .allocate_own_read_buffer = true,
#else
				    .allocate_own_read_buffer = false,
#endif
				    .expire_time_parent = -1,
				    .readdir_mode = FSAL_RDDIR_CHUNK_NEVER,
			    } } };

static int ceph_conf_commit(void *node, void *link_mem, void *self_struct,
			    struct config_error_type *err_type)
{
	struct ceph_fsal_module *CephFSM = self_struct;

	if (CephFSM->client_oc && CephFSM->zerocopy) {
		LogWarn(COMPONENT_FSAL,
			"client_oc and zerocopy are incompatible");
		err_type->invalid = true;
		return 1;
	}

	return 0;
}

static struct config_item ceph_items[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL, ceph_fsal_module,
		       conf_path),
	CONF_ITEM_MODE("umask", 0, ceph_fsal_module, fsal.fs_info.umask),
	CONF_ITEM_BOOL("client_oc", false, ceph_fsal_module, client_oc),
	CONF_ITEM_UI64_ZERO("client_oc_size", 0, UINT64_MAX, 209715200,
			    ceph_fsal_module, client_oc_size),
	CONF_ITEM_UI64_ZERO("client_oc_max_dirty", 0, UINT64_MAX, 104857600,
			    ceph_fsal_module, client_oc_max_dirty),
	CONF_ITEM_BOOL("async", false, ceph_fsal_module, async),
	CONF_ITEM_BOOL("zerocopy", false, ceph_fsal_module, zerocopy),
	CONF_ITEM_BOOL("use_old_uuid", false, ceph_fsal_module, use_old_uuid),
	CONF_ITEM_BOOL("register_service", false, ceph_fsal_module,
		       register_service),
	CONF_ITEM_STR("nodeid", 1, MAXPATHLEN, NULL, ceph_fsal_module, nodeid),
	CONFIG_EOL
};

static struct config_block ceph_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.ceph",
	.blk_desc.name = "Ceph",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE, /* too risky to have more */
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = ceph_items,
	.blk_desc.u.blk.commit = ceph_conf_commit,
	.mem_comp = MEM_COMP_CONFIG
};

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *module_in,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct ceph_fsal_module *myself =
		container_of(module_in, struct ceph_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL, "Ceph module setup.");

	(void)load_config_from_parse(config_struct, &ceph_block, myself, true,
				     err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&myself->fsal);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t find_cephfs_root(struct ceph_export *export, Inode **pi,
				      struct ceph_statx *stx, bool *stxr)
{
	int lmp, rc;
	struct user_cred root_creds = {};
	char *walk_path;

#ifdef USE_FSAL_CEPH_LL_LOOKUP_ROOT
	/* If no cmount_path or cmount_path is the same as CTX_FULLPATH(op_ctx)
	 * then we just want to lookup the root of the cmount.
	 */
	if (export->cmount_path == NULL ||
	    strcmp(export->cmount_path, CTX_FULLPATH(op_ctx)) == 0) {
		rc = ceph_ll_lookup_root(export->cmount, pi);
		if (rc) {
			LogWarn(COMPONENT_FSAL,
				"Root lookup failed for %s : %s",
				CTX_FULLPATH(op_ctx), strerror(-rc));
		}
		*stxr = false;
		goto out;
	}
#endif

	if (export->cmount_path != NULL) {
		/* Find the portion of CTX_FULLPATH(op_ctx) that is deeper than
		 * cmount_path. For example, if:
		 *    cmount_path = "/export"
		 * and
		 *    CTX_FULLPATH(op_ctx) = "/export/exp1"
		 * then we want to walk from the root of the cmount (at /export)
		 * to /exp1.
		 *
		 * If cmount_path is just "/", we will want the whole
		 * CTX_FULLPATH(op_ctx)
		 */
		lmp = strlen(export->cmount_path);

		if (lmp == 1) {
			/* If cmount_path is "/" we need the leading '/'. */
			lmp = 0;
		}

		walk_path = CTX_FULLPATH(op_ctx) + lmp;
	} else {
		/* No cmount_path, so we did a cmount at CTX_FULLPATH(op_ctx)
		 * and now we just need to walk to the root of the cmount.
		 */
		walk_path = "/";
	}

	LogDebug(COMPONENT_FSAL, "Cmount path %s, walk_path %s",
		 export->cmount_path, walk_path);

	/* Now walk the path */
	rc = fsal_ceph_ll_walk(export->cmount, walk_path, pi, stx, false,
			       &root_creds);

	if (rc) {
		LogWarn(COMPONENT_FSAL, "ceph_ll_walk failed for %s : %s",
			walk_path, strerror(-rc));
	}
	*stxr = true;

out:

	return ceph2fsal_error(rc);
}

static int ceph_export_commit(void *node, void *link_mem, void *self_struct,
			      struct config_error_type *err_type)
{
	struct ceph_export *export = self_struct;
	int lmp, lpath;

	/* If cmount_path is not configured, no further checks */
	if (export->cmount_path == NULL)
		return 0;

	if (export->cmount_path[0] != '/') {
		LogWarn(COMPONENT_FSAL, "cmount path not starting with / : %s",
			export->cmount_path);
		err_type->invalid = true;
		return 1;
	}

	/* Get length of cmount_path and remove trailing slash, adjusting
	 * length.
	 */
	lmp = strlen(export->cmount_path);

	while ((export->cmount_path[lmp - 1] == '/') && (lmp > 1)) {
		/* Trim a trailing '/' */
		lmp--;
	}

	export->cmount_path[lmp] = '\0';

	/* Get the length of the full path from the export */
	lpath = strlen(op_ctx->ctx_export->cfg_fullpath);

	LogDebug(COMPONENT_FSAL, "Commit %s mount path %s",
		 op_ctx->ctx_export->cfg_fullpath, export->cmount_path);

	if (lpath < lmp) {
		LogWarn(COMPONENT_FSAL,
			"cmount path is bigger than export path");
		err_type->invalid = true;
		return 1;
	}

	if (lmp > 1 &&
	    strncmp(export->cmount_path, CTX_FULLPATH(op_ctx), lmp) != 0) {
		/* path is not a sub-directory of mount_path - error */
		LogWarn(COMPONENT_FSAL,
			"Export path is not sub-directory of cmount path, cmount_path : %s, export : %s",
			export->cmount_path, op_ctx->ctx_export->cfg_fullpath);
		err_type->invalid = true;
		return 1;
	}

	return 0;
}

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_STR("user_id", 0, MAXUIDLEN, NULL, ceph_export, user_id),
	CONF_ITEM_STR("filesystem", 0, NAME_MAX, NULL, ceph_export, fs_name),
	CONF_ITEM_PATH("cmount_path", 1, MAXPATHLEN, NULL, ceph_export,
		       cmount_path),
	CONF_ITEM_STR("secret_access_key", 0, MAXSECRETLEN, NULL, ceph_export,
		      secret_key),
	CONF_ITEM_STR("sec_label_xattr", 0, 256, "security.selinux",
		      ceph_export, sec_label_xattr),
	CONFIG_EOL
};

static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.ceph-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = ceph_export_commit,
	.mem_comp = MEM_COMP_EXPORT
};

#ifdef USE_FSAL_CEPH_LL_DELEGATION
void enable_delegations(struct ceph_mount *cm, struct gsh_export *export)
{
	/* Skip setting delegation timeout if delegation is disabled
	 * in the NFSv4 stanza.
	 */
	if (!nfs_param.nfsv4_param.allow_delegations) {
		LogFullDebug(COMPONENT_FSAL, "Delegations disabled in NFSV4");
		return;
	}

	/* Skip setting delegation timeout again if it was already
	 * configured successfully for this cmount instance.
	 */
	if (cm->cm_allow_delegations) {
		LogFullDebug(COMPONENT_FSAL,
			     "Deleg timeout is already set for this cmount");
		return;
	}

	/* Skip setting delegation timeout if it previously failed,
	 * i.e., ceph_set_deleg_timeout returned an error for this cmount.
	 */
	if (cm->cm_disallow_delegations) {
		LogFullDebug(COMPONENT_FSAL,
			     "Deleg previously disabled for this cmount");
		return;
	}

	/* Check EXPORT, EXPORT_DEFAULTS, and CLIENT blocks for
	 * delegation-related settings.
	 * We only need to know if the delegation option is set in
	 * any stanza, not which specific EXPORT or CLIENT enabled it.
	 */
	uint32_t eff_options = export_check_client_options(export);

	if (eff_options & EXPORT_OPTION_DELEGATIONS) {
		/*
		 * Ganesha will time out delegations when the recall fails
		 * for two lease periods. We add just a little bit above that
		 * as a scheduling fudge-factor.
		 *
		 * The idea here is to make this long enough to give ganesha
		 * a chance to kick out a misbehaving client, but shorter
		 * than ceph cluster-wide MDS session timeout.
		 *
		 * Exceeding the MDS session timeout may result in the client
		 * (ganesha) being blacklisted in the cluster. Fixing that can
		 * require a long wait and/or administrative intervention.
		 */
		unsigned int dt = nfs_param.nfsv4_param.lease_lifetime * 2 + 5;
		int ceph_status;

		LogDebug(COMPONENT_FSAL, "Setting deleg timeout to %u", dt);

		ceph_status = ceph_set_deleg_timeout(cm->cmount, dt);

		if (ceph_status != 0) {
			cm->cm_disallow_delegations = true;

			LogWarn(COMPONENT_FSAL,
				"Unable to set delegation timeout for %s. Disabling delegation support: %s",
				CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		} else {
			cm->cm_allow_delegations = true;

			LogFullDebug(COMPONENT_FSAL,
				     "Setting cm_allow_delegations to %d",
				     cm->cm_allow_delegations);
		}
	} else {
		LogDebug(COMPONENT_FSAL, "No deleg option set in the config");
	}
}

/* Handle delegation option transition during run-time.*/
void handle_deleg_transition(struct fsal_export *orig, struct gsh_export *exp)
{
	struct ceph_export *ce =
		container_of(orig->sub_export, struct ceph_export, export);

	struct gsh_export *probe_exp = orig->owning_export;

	/* Invoke asynchronous dynamic delegation option parsing implementation
	 * Check if the delegation option was updated and recall an outstanding
	 * delegation if necessary.
	 */
	if (async_deleg_transition_handler(general_fridge, probe_exp) != 0)
		LogCrit(COMPONENT_STATE,
			"Failed to start thread to deleg transition");

	/* Invoke enable_delegations in case the delegation option was enabled
	 * at the run time.
	 */
	enable_delegations(ce->cm, exp);
}

#else /* !USE_FSAL_CEPH_LL_DELEGATION */
static inline void enable_delegations(struct ceph_mount *cm)
{
}
#endif /* USE_FSAL_CEPH_LL_DELEGATION */

#ifdef USE_FSAL_CEPH_RECLAIM_RESET
#define RECLAIM_UUID_PREFIX "ganesha-"

/* create uuid for ceph client */
void create_unique_id(struct ceph_mount *cm, char *nodeid, char **uniq_id)
{
	size_t len;

	if (CephFSM.use_old_uuid) {
		LogEvent(COMPONENT_FSAL, "Old logic of uuid for ceph");
		len = strlen(RECLAIM_UUID_PREFIX) + strlen(nodeid) + 1 + 4 + 1;
		*uniq_id = gsh_malloc(len, MEM_COMP_RECOVERY);
		(void)snprintf(*uniq_id, len, RECLAIM_UUID_PREFIX "%s-%4.4hx",
			       nodeid, cm->cm_export_id);
	} else {
		char buff[8192]; /* large buffer to accommodate lengthy path */
		uint64_t hashkey;

		LogEvent(COMPONENT_FSAL, "New logic of uuid for ceph");
		/* create string containing nodeid, userid, fs_name and mount
		 * path for hashing purpose*/
		(void)snprintf(buff, 8192, "%s%s%s%s", nodeid, cm->cm_user_id,
			       cm->cm_fs_name, cm->cm_mount_path);
		LogDebug(COMPONENT_FSAL, "ceph_mount hash data: %s", buff);
		hashkey = CityHash64(buff, strlen(buff));
		len = strlen(RECLAIM_UUID_PREFIX) + 128 + 1;
		*uniq_id = gsh_malloc(len, MEM_COMP_RECOVERY);
		/* uniq_id will be always like "ganesha-<64-bytes-hash>" */
		(void)snprintf(*uniq_id, len, RECLAIM_UUID_PREFIX "0x%" PRIx64,
			       hashkey);
	}
	LogEvent(COMPONENT_FSAL, "Unique id for ceph_mount : %s", *uniq_id);
}

static int reclaim_reset(struct ceph_mount *cm)
{
	int ceph_status;
	char *nodeid, *uuid;

	/*
	 * Set long timeout for the session to ensure that MDS doesn't lose
	 * state before server can come back and do recovery.
	 */
	ceph_set_session_timeout(cm->cmount, 300);

	/*
	 * For combination of nodeid, fs_name, userid and mount path, there is
	 * ceph client being used. The uuid should be created for this
	 * combination.
	 */
	ceph_status = nfs_recovery_get_nodeid(&nodeid);
	if (ceph_status != 0) {
		LogEvent(COMPONENT_FSAL, "couldn't get nodeid: %s",
			 strerror(errno));
		return ceph_status;
	}
	create_unique_id(cm, nodeid, &uuid);
	LogDebug(COMPONENT_FSAL, "Issuing reclaim reset for %s", uuid);
	ceph_status = ceph_start_reclaim(cm->cmount, uuid, CEPH_RECLAIM_RESET);
	if (ceph_status) {
		/* Error ENOENT indicates that most likely this is first run
		 * of this Ganesha instance, so can be ignored. Any other
		 * failure indicates problem with this ceph client, better
		 * throw the error and exit */
		LogEvent(COMPONENT_FSAL, "start_reclaim failed: (%d) %s",
			 ceph_status, strerror(-ceph_status));
		if ((-ceph_status) != ENOENT) {
			gsh_free(nodeid, MEM_COMP_RECOVERY);
			gsh_free(uuid, MEM_COMP_RECOVERY);
			return ceph_status;
		}
	}
	ceph_finish_reclaim(cm->cmount);
	ceph_set_uuid(cm->cmount, uuid);
	gsh_free(nodeid, MEM_COMP_RECOVERY);
	gsh_free(uuid, MEM_COMP_RECOVERY);
	return 0;
}

/* ceph client reclaim for takeover of failed node */
fsal_status_t node_takeover_reclaim(struct fsal_module *module_in, char *nodeid)
{
	int ceph_status = -EDOM;
	struct avltree_node *node;

	PTHREAD_RWLOCK_rdlock(&cmount_lock);
	/* Go through all cmounts and carry out reclaim operation */
	for (node = avltree_first(&avl_cmount); node != NULL;
	     node = avltree_next(node)) {
		struct ceph_mount *cm;
		char *uuid;

		cm = avltree_container_of(node, struct ceph_mount,
					  cm_avl_mount);
		create_unique_id(cm, nodeid, &uuid);

		LogDebug(COMPONENT_FSAL,
			 "Issuing reclaim reset for node %s, uuid %s", nodeid,
			 uuid);
		ceph_status = ceph_start_reclaim(cm->cmount, uuid,
						 CEPH_RECLAIM_RESET);
		gsh_free(uuid, MEM_COMP_RECOVERY);
		if (ceph_status) {
			/* Error ENOENT indicates that most likely the failed
			 * node was not running before. Ignoring this error
			 * now, but may need to revisit in future. Any other
			 * failure indicates problem with reclaim activity,
			 * better throw the error and exit */
			LogEvent(COMPONENT_FSAL,
				 "Ceph client reclaim failed (Node %s): %s",
				 nodeid, strerror(-ceph_status));
			if ((-ceph_status) != ENOENT)
				break;
		}
		ceph_finish_reclaim(cm->cmount);
	}
	PTHREAD_RWLOCK_unlock(&cmount_lock);
	if (ceph_status)
		return ceph2fsal_error(ceph_status);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#undef RECLAIM_UUID_PREFIX
#else
static inline int reclaim_reset(struct ceph_mount *cm)
{
	return 0;
}
fsal_status node_takeover_reclaim(char *nodeid)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif

#ifdef USE_FSAL_CEPH_GET_FS_CID
static int select_filesystem(struct ceph_mount *cm)
{
	int ceph_status;

	if (cm->cm_fs_name) {
		ceph_status =
			ceph_select_filesystem(cm->cmount, cm->cm_fs_name);
		if (ceph_status != 0) {
			LogCrit(COMPONENT_FSAL,
				"Unable to set filesystem to %s.",
				cm->cm_fs_name);
			return ceph_status;
		}
	}
	return 0;
}
#else /* USE_FSAL_CEPH_GET_FS_CID */
static int select_filesystem(struct ceph_mount *cm)
{
	if (cm->fs_name) {
		LogCrit(COMPONENT_FSAL,
			"This libcephfs version doesn't support named filesystems.");
		return -EINVAL;
	}
	return 0;
}
#endif /* USE_FSAL_CEPH_GET_FS_CID */

#ifdef USE_FSAL_CEPH_REGISTER_CALLBACKS
static void ino_release_cb(void *handle, vinodeno_t vino)
{
	struct ceph_mount *cm = handle;
	struct ceph_handle_key key;
	struct gsh_buffdesc fh_desc;

	LogDebug(COMPONENT_FSAL,
		 "libcephfs asking to release 0x%lx:0x%lx:0x%lx", cm->cm_fscid,
		 vino.snapid.val, vino.ino.val);
	key.hhdl.chk_ino = vino.ino.val;
	key.hhdl.chk_snap = vino.snapid.val;
	key.hhdl.chk_fscid = cm->cm_fscid;
	key.export_id = cm->cm_export_id;
	fh_desc.addr = &key;
	fh_desc.len = sizeof(key);

	PTHREAD_RWLOCK_rdlock(&cmount_lock);

	cm->cm_export->export.up_ops->try_release(cm->cm_export->export.up_ops,
						  &fh_desc, 0);

	PTHREAD_RWLOCK_unlock(&cmount_lock);
}

/* Callback for inode invalidation. This callback is triggered when ceph client
 * cache is invalidated due to file attribute change */
static void ino_invalidate_cb(void *handle, vinodeno_t vino, int64_t offset,
			      int64_t len)
{
	struct ceph_mount *cm = handle;
	struct ceph_handle_key key;
	struct gsh_buffdesc fh_desc;

	LogDebug(COMPONENT_FSAL,
		 "libcephfs asking to invalidate 0x%lx:0x%lx:0x%lx",
		 cm->cm_fscid, vino.snapid.val, vino.ino.val);
	key.hhdl.chk_ino = vino.ino.val;
	key.hhdl.chk_snap = vino.snapid.val;
	key.hhdl.chk_fscid = cm->cm_fscid;
	key.export_id = cm->cm_export_id;
	fh_desc.addr = &key;
	fh_desc.len = sizeof(key);

	PTHREAD_RWLOCK_rdlock(&cmount_lock);

	cm->cm_export->export.up_ops->invalidate(cm->cm_export->export.up_ops,
						 &fh_desc,
						 FSAL_UP_INVALIDATE_CACHE);

	PTHREAD_RWLOCK_unlock(&cmount_lock);
}

/* Callback for dentry invalidation. This callback is triggered when ceph client
 * finds that a directory entry is invalid */
static void dentry_invalidate_cb(void *handle, vinodeno_t dir_ino,
				 vinodeno_t dentry_ino, const char *dentry_name,
				 size_t dentry_len)
{
	struct ceph_mount *cm = handle;
	struct ceph_handle_key dir_key;
	struct gsh_buffdesc dir_fh_desc;
	const struct fsal_up_vector *event_func;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	LogDebug(COMPONENT_FSAL,
		 "ceph asking to invalidate content of dir 0x%lx:0x%lx:0x%lx",
		 cm->cm_fscid, dir_ino.snapid.val, dir_ino.ino.val);
	LogDebug(COMPONENT_FSAL,
		 "ceph asking to invalidate dentry 0x%lx:0x%lx:0x%lx Name:%s",
		 cm->cm_fscid, dentry_ino.snapid.val, dentry_ino.ino.val,
		 dentry_name);

	/* Try to invalidate the directory */
	dir_key.hhdl.chk_ino = dir_ino.ino.val;
	dir_key.hhdl.chk_snap = dir_ino.snapid.val;
	dir_key.hhdl.chk_fscid = cm->cm_fscid;
	dir_key.export_id = cm->cm_export_id;
	dir_fh_desc.addr = &dir_key;
	dir_fh_desc.len = sizeof(dir_key);

	/* Fetch the up vector */
	event_func = cm->cm_export->export.up_ops;
	PTHREAD_RWLOCK_rdlock(&cmount_lock);
	status = event_func->invalidate(cm->cm_export->export.up_ops,
					&dir_fh_desc,
					FSAL_UP_INVALIDATE_DIR_POPULATED |
						FSAL_UP_INVALIDATE_DIR_CHUNKS);
	PTHREAD_RWLOCK_unlock(&cmount_lock);

	if (status.major != ERR_FSAL_NO_ERROR)
		LogWarn(COMPONENT_FSAL, "Directory invalidation failed");
}

static mode_t umask_cb(void *handle)
{
	mode_t umask = CephFSM.fsal.fs_info.umask;

	LogDebug(COMPONENT_FSAL, "libcephfs set umask = %04o by umask callback",
		 umask);
	return umask;
}

static void register_callbacks(struct ceph_mount *cm)
{
	struct ceph_client_callback_args args = {
		.handle = cm,
		.ino_cb = ino_invalidate_cb,
		.ino_release_cb = ino_release_cb,
		.dentry_cb = dentry_invalidate_cb,
		.umask_cb = umask_cb
	};
	ceph_ll_register_callbacks(cm->cmount, &args);
}
#else /* USE_FSAL_CEPH_REGISTER_CALLBACKS */
static void register_callbacks(struct ceph_mount *cm)
{
	LogWarnOnce(
		COMPONENT_FSAL,
		"This libcephfs does not support registering callbacks. Ganesha will be unable to respond to MDS cache pressure.");
}
#endif /* USE_FSAL_CEPH_REGISTER_CALLBACKS */

/**
 * @brief Create a new export under this FSAL
 *
 * This function creates a new export object for the Ceph FSAL.
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

static fsal_status_t create_export(struct fsal_module *module_in,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	/* The ceph module */
	struct ceph_fsal_module *my_module =
		container_of(module_in, struct ceph_fsal_module, fsal);
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The internal export object */
	struct ceph_export *export =
		gsh_calloc(1, sizeof(struct ceph_export), MEM_COMP_EXPORT);
	/* The 'private' root handle */
	struct ceph_handle *handle = NULL;
	/* Root inode */
	struct Inode *i = NULL;
	/* Stat for root */
	struct ceph_statx stx;
	/* Return code */
	int rc;
	/* Return code from Ceph calls */
	int ceph_status;
	/* Ceph mount key */
	struct ceph_mount cm_key;
	/* Ceph mount */
	struct ceph_mount *cm;
	/* stx is filled in */
	bool stxr = false;

	fsal_export_init(&export->export);
	export_ops_init(&export->export.exp_ops);

	/* get params for this export, if any */
	if (parse_node) {
		rc = load_config_from_node(parse_node, &export_param_block,
					   export, true, err_type);
		if (rc != 0) {
			gsh_free(export, MEM_COMP_EXPORT);
			LogWarn(COMPONENT_FSAL,
				"Unable to load config for export : %s",
				CTX_FULLPATH(op_ctx));
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	memset(&cm_key, 0, sizeof(cm_key));
	cm_key.cm_fs_name = export->fs_name;
	cm_key.cm_user_id = export->user_id;
	cm_key.cm_secret_key = export->secret_key;

	/* If cmount_path is configured, use that, otherwise use
	 * CTX_FULLPATH(op_ctx). This allows an export where cmount_path
	 * was going to be the same as CTX_FULLPATH(op_ctx) to share the
	 * cmount with other exports that use the same cmount_path (but then
	 * MUST be exporting a sub-directory) and cmount_path need not be
	 * specified for the export where CTX_FULLPATH(op_ctx) is the same as
	 * that later cmount_path.
	 */
	if (export->cmount_path != NULL)
		cm_key.cm_mount_path = export->cmount_path;
	else
		cm_key.cm_mount_path = CTX_FULLPATH(op_ctx);

	PTHREAD_RWLOCK_wrlock(&cmount_lock);

	cm = ceph_mount_lookup(&cm_key.cm_avl_mount);

	if (cm != NULL) {
		cm->cm_refcnt++;
		LogDebug(COMPONENT_FSAL, "Re-using cmount %s for %s",
			 cm->cm_mount_path, CTX_FULLPATH(op_ctx));
		goto has_cmount;
	}

	cm = gsh_calloc(1, sizeof(*cm), MEM_COMP_FSAL);

	cm->cm_allow_delegations = false;
	cm->cm_disallow_delegations = false;

	LogDebug(COMPONENT_FSAL,
		 "Initialized allow_delegations %d disallow_delegations %d",
		 cm->cm_allow_delegations, cm->cm_disallow_delegations);

	cm->cm_refcnt = 1;

	if (export->fs_name)
		cm->cm_fs_name = gsh_strdup(export->fs_name, MEM_COMP_FSAL);

	if (export->cmount_path)
		cm->cm_mount_path = gsh_strdup(export->cmount_path,
					       MEM_COMP_FSAL);
	else
		cm->cm_mount_path = gsh_strdup(CTX_FULLPATH(op_ctx),
					       MEM_COMP_FSAL);

	if (export->user_id)
		cm->cm_user_id = gsh_strdup(export->user_id, MEM_COMP_FSAL);

	if (export->secret_key)
		cm->cm_secret_key = gsh_strdup(export->secret_key,
					       MEM_COMP_FSAL);

	LogDebug(COMPONENT_FSAL, "New cmount %s for %s", cm->cm_mount_path,
		 CTX_FULLPATH(op_ctx));

	cm->cm_export_id = export->export.export_id;
	cm->cm_export = export;

	glist_init(&cm->cm_exports);

	ceph_mount_insert(&cm->cm_avl_mount);

	/* allocates ceph_mount_info */
	ceph_status = ceph_create(&cm->cmount, cm->cm_user_id);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to create Ceph handle for %s : %s",
			CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		goto error;
	}

	ceph_status = ceph_conf_read_file(cm->cmount, CephFSM.conf_path);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to read Ceph configuration for %s : %s",
			CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		goto error;
	}

	if (cm->cm_secret_key) {
		ceph_status =
			ceph_conf_set(cm->cmount, "key", cm->cm_secret_key);
		if (ceph_status) {
			status.major = ERR_FSAL_INVAL;
			LogCrit(COMPONENT_FSAL,
				"Unable to set Ceph secret key for %s: %s",
				CTX_FULLPATH(op_ctx), strerror(-ceph_status));
			goto error;
		}
	}

	/*
	 * Workaround for broken libcephfs that doesn't handle the path
	 * given in ceph_mount properly. Should be harmless for fixed
	 * libcephfs as well (see http://tracker.ceph.com/issues/18254).
	 */
	ceph_status = ceph_conf_set(cm->cmount, "client_mountpoint", "/");

	if (ceph_status) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL,
			"Unable to set Ceph client_mountpoint: %s",
			strerror(-ceph_status));
		goto error;
	}

	ceph_status = ceph_conf_set(cm->cmount, "client_acl_type", "posix_acl");

	if (ceph_status < 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to set Ceph client_acl_type: %s",
			strerror(-ceph_status));
		goto error;
	}

	ceph_status = ceph_conf_set(cm->cmount, "client_oc",
				    my_module->client_oc ? "true" : "false");

	if (ceph_status) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL, "Unable to set Ceph client_oc: %d",
			ceph_status);
		goto error;
	}

	if (my_module->client_oc) {
		/* Set other client_oc related config options.
		 * ceph_conf_set accepts strings, so convert the values. */
		char str_val[1024];

		snprintf(str_val, 1024, "%" PRIu64, my_module->client_oc_size);
		ceph_status =
			ceph_conf_set(cm->cmount, "client_oc_size", str_val);
		if (ceph_status) {
			status.major = ERR_FSAL_INVAL;
			LogCrit(COMPONENT_FSAL,
				"Unable to set Ceph client_oc_size: %d",
				ceph_status);
			goto error;
		}

		snprintf(str_val, 1024, "%" PRIu64,
			 my_module->client_oc_max_dirty);
		ceph_status = ceph_conf_set(cm->cmount, "client_oc_max_dirty",
					    str_val);
		if (ceph_status) {
			status.major = ERR_FSAL_INVAL;
			LogCrit(COMPONENT_FSAL,
				"Unable to set Ceph client_oc_max_dirty: %d",
				ceph_status);
			goto error;
		}
	}

	ceph_status = ceph_init(cm->cmount);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to init Ceph handle : %s",
			strerror(-ceph_status));
		goto error;
	}

	register_callbacks(cm);

	ceph_status = select_filesystem(cm);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to select/use file system for %s : %s",
			CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		goto error;
	}

	ceph_status = reclaim_reset(cm);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to do reclaim_reset for %s : %s",
			CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		goto error;
	}

	ceph_status = ceph_mount(cm->cmount, cm->cm_mount_path);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to mount Ceph cluster for %s : %s",
			CTX_FULLPATH(op_ctx), strerror(-ceph_status));
		goto error;
	}

#ifdef USE_FSAL_CEPH_GET_FS_CID
	/* Fetch fscid for use in filehandles */
	cm->cm_fscid = ceph_get_fs_cid(cm->cmount);

	if (cm->cm_fscid < 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Error getting fscid for %s.",
			cm->cm_fs_name);
		goto error;
	}
#endif /* USE_FSAL_CEPH_GET_FS_CID */

has_cmount:
	export->cm = cm;
	export->cmount = cm->cmount;
	export->fscid = cm->cm_fscid;
	export->export.fsal = module_in;
	export->export.up_ops = up_ops;
	export->use_acl = !op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL);

	glist_add_tail(&cm->cm_exports, &export->cm_list);

	LogDebug(COMPONENT_FSAL, "Ceph module export %s.",
		 CTX_FULLPATH(op_ctx));

	status = find_cephfs_root(export, &i, &stx, &stxr);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Error finding root for %s.",
			CTX_FULLPATH(op_ctx));
		goto error;
	}

	if (!stxr) {
		rc = fsal_ceph_ll_getattr(export->cmount, i, &stx,
					  CEPH_STATX_HANDLE_MASK,
					  &op_ctx->creds);

		if (rc < 0) {
			LogCrit(COMPONENT_FSAL, "Ceph getattr failed %s : %s",
				CTX_FULLPATH(op_ctx), strerror(-rc));
			status = ceph2fsal_error(rc);
			goto error;
		}
	}

	LogDebug(COMPONENT_FSAL, "Ceph module export %s root %" PRIx64,
		 CTX_FULLPATH(op_ctx), stx.stx_ino);

	construct_handle(&stx, i, export, &handle);

	export->root = handle;
	op_ctx->fsal_export = &export->export;

	if (fsal_attach_export(module_in, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach export for %s.",
			CTX_FULLPATH(op_ctx));
		goto error;
	}

	/* Set the delegation timer if the delegation option is enabled
	 * in CLIENT, EXPORT, or EXPORT_DEFAULT stanzas
	 */
	LogFullDebug(COMPONENT_FSAL, "Export ID: %d, Cmount: %p, Refcount: %d",
		     cm->cm_export_id, cm, cm->cm_refcnt);
	enable_delegations(cm, op_ctx->ctx_export);

	PTHREAD_RWLOCK_unlock(&cmount_lock);

	return status;

error:

	if (i)
		ceph_ll_put(export->cmount, i);

	/* Detach this export from the ceph_mount */
	glist_del(&export->cm_list);

	if (--cm->cm_refcnt == 0) {
		/* This was the initial reference */

		if (cm->cmount)
			ceph_shutdown(cm->cmount);

		ceph_mount_remove(&cm->cm_avl_mount);

		gsh_free(cm->cm_fs_name, MEM_COMP_FSAL);
		gsh_free(cm->cm_mount_path, MEM_COMP_FSAL);
		gsh_free(cm->cm_user_id, MEM_COMP_FSAL);
		gsh_free(cm->cm_secret_key, MEM_COMP_FSAL);

		gsh_free(cm, MEM_COMP_FSAL);
		cm = NULL;
	}

	gsh_free(export, MEM_COMP_EXPORT);

	PTHREAD_RWLOCK_unlock(&cmount_lock);

	return status;
}

/*
 * Dynamically loads the Ceph RADOS service registration library
 * and registers this NFS-Ganesha instance as a service in Ceph.
 *
 * This is used for service monitoring and status reporting.
 * If the shared library cannot be loaded, the process exits.
 */

static void ceph_register_nfs_service(void)
{
	if (!CephFSM.register_service)
		return;

	void (*register_nfs_fun_ptr)(char *);
	void *dl = NULL;
	char *err = NULL;

#if defined(LINUX) && !defined(SANITIZE_ADDRESS)
	dl = dlopen("libganesha_rados_urls.so",
		    RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#elif defined(BSDBASED) || defined(SANITIZE_ADDRESS)
	dl = dlopen("libganesha_rados_urls.so", RTLD_NOW | RTLD_LOCAL);
#endif
	if (dl == NULL) {
		fprintf(stderr, "Failed to load libganesha_rados_urls.so\n");
		exit(1);
	}

	dlerror(); /* Clear any existing dynamic loader errors */

	/* Resolve the registration entry point from the shared library */
	register_nfs_fun_ptr = dlsym(dl, "register_service_to_ceph");

	err = dlerror(); /* check if dlsym failed */

	if (err != NULL) {
		LogDebug(
			COMPONENT_FSAL,
			"Unable to load register_service_to_ceph to register nfs service");
		return;
	}

	/* Register this NFS instance with the Ceph backend */
	register_nfs_fun_ptr(CephFSM.nodeid);
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a Ceph cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.
 */

MODULE_INIT void init(void)
{
	struct fsal_module *myself = &CephFSM.fsal;

	LogDebug(COMPONENT_FSAL, "Ceph module registering.");

	if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_CEPH) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		LogCrit(COMPONENT_FSAL, "Ceph module failed to register.");
	}

	ceph_mount_init();

	/* Set up module operations */
#ifdef CEPH_PNFS
	myself->m_ops.fsal_pnfs_ds_ops = pnfs_ds_ops_init;
#endif /* CEPH_PNFS */
	myself->m_ops.create_export = create_export;
	myself->m_ops.init_config = init_config;
	myself->m_ops.fsal_reclaim_client = node_takeover_reclaim;
	myself->m_ops.handle_deleg_transition = handle_deleg_transition;
	myself->m_ops.fsal_register_nfs_service = ceph_register_nfs_service;

	/* Initialize the fsal_obj_handle ops for FSAL CEPH */
	handle_ops_init(&CephFSM.handle_ops);
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.
 * The Ceph FSAL has no other resources to release on the per-FSAL
 * level.
 */

MODULE_FINI void finish(void)
{
	LogDebug(COMPONENT_FSAL, "Ceph module finishing.");

	if (unregister_fsal(&CephFSM.fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload Ceph FSAL.  Dying with extreme prejudice.");
		abort();
	}
}
