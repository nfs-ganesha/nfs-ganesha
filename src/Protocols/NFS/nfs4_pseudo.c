// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @file    nfs4_pseudo.c
 * @brief   Routines used for managing the NFS4 pseudo file system.
 *
 * Routines used for managing the NFS4 pseudo file system.
 */
#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_proto_tools.h"
#include "nfs_exports.h"
#include "fsal.h"
#include "export_mgr.h"

/**
 * @brief Find the node for this path component
 *
 * If not found, create it.  Called from token_to_proc() interator
 *
 * @param token [IN] path name component
 * @param arg   [IN] callback state
 *
 * @return status as bool. false terminates foreach
 */

struct pseudofs_state {
	struct gsh_export *export;
	struct fsal_obj_handle *obj;
	struct gsh_refstr *ref_pseudopath;
	const char *st_pseudopath;
	struct gsh_refstr *ref_fullpath;
	const char *st_fullpath;
};

/**
 * @brief Check to see if an export is PSEUDO
 *
 * Can be PSEUDO, or MDCACHE on PSEUDO
 *
 * @param[in] export	Export to check
 * @return true if PSEUDO, false otherwise
 */
static bool is_export_pseudo(struct gsh_export *export)
{
	/* If it's PSEUDO, it's PSEUDO */
	if (strcmp(export->fsal_export->fsal->name, "PSEUDO") == 0)
		return true;

	/* If it's !MDCACHE, it's !PSEUDO */
	if (strcmp(export->fsal_export->fsal->name, "MDCACHE") != 0)
		return false;

	/* If it's MDCACHE stacked on PSEUDO, it's PSEUDO */
	if (strcmp(export->fsal_export->sub_export->fsal->name, "PSEUDO") == 0)
		return true;

	return false;
}

/**
 * @brief Delete the unecessary directories from pseudo FS
 *
 * @param pseudo_path [IN] full path of the node
 * @param entry [IN] cache entry for the last directory in the path
 *
 * If this entry is present is pseudo FSAL, and is unnecessary, then remove it.
 * Check recursively if the parent entry is needed.
 *
 * The pseudo_path is deconstructed in place to create the subsequently shorter
 * pseudo paths.
 *
 * When called the first time, entry is the mount point of an export that has
 * been unmounted from the PseudoFS. By definition, it is NOT the root of a
 * PseudoFS. Also, the PseudoFS root filesystem is NOT mounted and thus this
 * function will not be called for it. The req_op_context references the
 * export for the PseudoFS entry is within. Note that the caller is
 * responsible for checking if it is an FSAL_PSEUDO export (we only clean up
 * directories in FSAL_PSEUDO filesystems).
 */
void cleanup_pseudofs_node(char *pseudo_path,
			   struct fsal_obj_handle *obj)
{
	struct fsal_obj_handle *parent_obj;
	char *pos = pseudo_path + strlen(pseudo_path) - 1;
	char *name;
	fsal_status_t fsal_status;

	/* Strip trailing / from pseudo_path */
	while (*pos == '/')
		pos--;

	/* Replace first trailing / if any with NUL */
	pos[1] = '\0';

	/* Find the previous slash.
	 * We will NEVER back up PAST the root, so no need to check
	 * for walking off the beginning of the string.
	 */
	while (*pos != '/')
		pos--;

	/* Remember the element name for remove */
	name = pos + 1;

	LogDebug(COMPONENT_EXPORT,
		 "Checking if pseudo node %s is needed from path %s",
		 name, pseudo_path);

	fsal_status = fsal_lookupp(obj, &parent_obj, NULL);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Truncate the pseudo_path to be the path to the parent */
		*pos = '\0';
		LogCrit(COMPONENT_EXPORT,
			"Could not find cache entry for parent directory %s",
			pseudo_path);
		return;
	}

	fsal_status = fsal_remove(parent_obj, name);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Bailout if we get directory not empty error */
		if (fsal_status.major == ERR_FSAL_NOTEMPTY) {
			LogDebug(COMPONENT_EXPORT,
				 "PseudoFS parent directory %s is not empty",
				 pseudo_path);
		} else {
			LogCrit(COMPONENT_EXPORT,
				"Removing pseudo node %s failed with %s",
				pseudo_path, msg_fsal_err(fsal_status.major));
		}
		goto out;
	}

	/* Before recursing the check the parent, get export lock for looking at
	 * exp_root_obj so we can check if we have reached the root of
	 * the mounted on export.
	 */
	PTHREAD_RWLOCK_rdlock(&op_ctx->ctx_export->lock);

	if (parent_obj == op_ctx->ctx_export->exp_root_obj) {
		LogDebug(COMPONENT_EXPORT,
			 "Reached root of PseudoFS %s",
			 CTX_PSEUDOPATH(op_ctx));

		PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
		goto out;
	}

	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	/* Truncate the pseudo_path to be the path to the parent */
	*pos = '\0';

	/* check if the parent directory is needed */
	cleanup_pseudofs_node(pseudo_path, parent_obj);

out:
	parent_obj->obj_ops->put_ref(parent_obj);
}

bool make_pseudofs_node(char *name, struct pseudofs_state *state)
{
	struct fsal_obj_handle *new_node = NULL;
	fsal_status_t fsal_status;
	bool retried = false;
	struct fsal_attrlist sattr;
	char const *fsal_name;

retry:

	/* First, try to lookup the entry */
	fsal_status = fsal_lookup(state->obj, name, &new_node, NULL);

	if (!FSAL_IS_ERROR(fsal_status)) {
		/* Make sure new node is a directory */
		if (new_node->type != DIRECTORY) {
			LogCrit(COMPONENT_EXPORT,
				"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s is not a directory",
				state->export->export_id,
				state->st_fullpath,
				state->st_pseudopath,
				name);
			/* Release the reference on the new node */
			new_node->obj_ops->put_ref(new_node);
			return false;
		}

		LogDebug(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Parent %p entry %p %s FSAL %s already exists",
			 state->obj, new_node, name,
			 new_node->fsal->name);

		state->obj->obj_ops->put_ref(state->obj);
		/* Make new node the current node */
		state->obj = new_node;
		return true;
	}

	/* Now check the FSAL, if it's not FSAL_PSEUDO, any error in the lookup
	 * is a complete failure.
	 */
	fsal_name = op_ctx->ctx_export->fsal_export->exp_ops.get_name(
				op_ctx->ctx_export->fsal_export);

	/* fsal_name should be "PSEUDO" or "PSEUDO/<stacked-fsal-name>" */
	if (strncmp(fsal_name, "PSEUDO", 6) != 0 ||
	    (fsal_name[6] != '/' && fsal_name[6] != '\0')) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s failed with %s%s",
			state->export->export_id,
			state->st_fullpath,
			state->st_pseudopath,
			name,
			msg_fsal_err(fsal_status.major),
			fsal_status.major == ERR_FSAL_NOENT
				? " (can't create directory on non-PSEUDO FSAL)"
				: ""
			);
		return false;
	}

	/* The only failure that FSAL_PSEUDO lookup can
	 * have is the entry doesn't exist, however, when an export update is
	 * in progress (which of course it is right now...) it will return
	 * ERR_FSAL_DELAY instead of ERR_FSAL_NOENT. Since this is the ONLY
	 * error condition, if it's FSAL_PSEUDO we just ignore the actual
	 * error.
	 *
	 * Now create the missing node.
	 *
	 */
	fsal_prepare_attrs(&sattr, ATTR_MODE);
	sattr.mode = 0755;

	fsal_status = fsal_create(state->obj, name, DIRECTORY, &sattr, NULL,
				  &new_node, NULL);

	/* Release the attributes (may release an inherited ACL - which
	 * FSAL_PSEUDO doesn't have...) */
	fsal_release_attrs(&sattr);

	if (fsal_status.major == ERR_FSAL_EXIST && !retried) {
		/* This is ALMOST dead code... Since we now gate and only a
		 * single export update can be in progress, no one should be
		 * modifying the PseudoFS - EXCEPT - The PseudoFS COULD be
		 * exported read/write and thus a client COULD have created the
		 * directory we are looking for... Yea, not really going to
		 * happen, but since we have the retry code to handle it, might
		 * as well keep the code...
		 */
		LogDebug(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Parent %p Node %p %s seems to already exist, try LOOKUP again",
			 state->obj, new_node, name);
		retried = true;
		goto retry;
	}

	if (FSAL_IS_ERROR(fsal_status)) {
		/* An error occurred - this actually is technically impossible
		 * for FSAL_PSEUDO unless the PseudoFS export is read/write and
		 * a client manages to change the parent directory to a regular
		 * file...
		 */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s failed with %s",
			state->export->export_id,
			state->st_fullpath,
			state->st_pseudopath,
			name,
			msg_fsal_err(fsal_status.major));
		return false;
	}

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s obj %p state %p succeeded",
		 state->export->export_id,
		 state->st_fullpath,
		 state->st_pseudopath,
		 name,
		 new_node, new_node->state_hdl);

	/* Release reference to the old node */
	state->obj->obj_ops->put_ref(state->obj);

	/* Make new node the current node */
	state->obj = new_node;
	return true;
}

/**
 * @brief  Mount an export in the new Pseudo FS.
 *
 * @param exp     [IN] export in question
 *
 * @return status as bool.
 */
bool pseudo_mount_export(struct gsh_export *export)
{
	struct pseudofs_state state;
	char *tmp_pseudopath;
	char *last_slash;
	char *p;
	char *rest;
	fsal_status_t fsal_status;
	char *tok;
	char *saveptr = NULL;
	int rc;
	bool result = false;

	/* skip exports that aren't for NFS v4
	 * Also, nothing to actually do for Pseudo Root
	 * (defer checking pseudopath for Pseudo Root until we have refstr.
	 */
	if ((export->export_perms.options & EXPORT_OPTION_NFSV4) == 0
	    || export->pseudopath == NULL
	    || export->export_id == 0)
		return true;

	/* Initialize state and it's op_context.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	state.export = export;

	rcu_read_lock();

	state.ref_pseudopath =
			gsh_refstr_get(rcu_dereference(export->pseudopath));

	state.ref_fullpath = gsh_refstr_get(rcu_dereference(export->fullpath));

	rcu_read_unlock();

	if (state.ref_pseudopath == NULL)
		LogFatal(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Export_Id %d missing pseudopath",
			 export->export_id);

	state.st_pseudopath = state.ref_pseudopath->gr_val;

	if (state.ref_fullpath == NULL)
		LogFatal(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Export_Id %d missing fullpath",
			 export->export_id);

	state.st_fullpath = state.ref_fullpath->gr_val;

	if (state.st_pseudopath[1] == '\0') {
		/* Nothing to do for pseudo root */
		result = true;
		goto out_no_context;
	}

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s",
		 export->export_id, state.st_fullpath, state.st_pseudopath);

	/* Make a copy of the path */
	tmp_pseudopath = gsh_strdupa(state.st_pseudopath);

	/* Find last '/' in path */
	p = tmp_pseudopath;
	last_slash = tmp_pseudopath;
	while (*p != '\0') {
		if (*p == '/')
			last_slash = p;
		p++;
	}

	/* Terminate path leading to junction. */
	*last_slash = '\0';

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Looking for export for %s",
		 tmp_pseudopath);

	/* Now find the export we are mounted on */
	set_op_context_export(get_gsh_export_by_pseudo(tmp_pseudopath, false));

	if (op_ctx->ctx_export == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Could not find mounted on export for %s, tmp=%s",
			 state.st_pseudopath, tmp_pseudopath);
	}

	/* Put the slash back in */
	*last_slash = '/';

	/* Point to the portion of this export's pseudo path that is beyond the
	 * mounted on export's pseudo path.
	 */
	if (CTX_PSEUDOPATH(op_ctx)[1] == '\0')
		rest = tmp_pseudopath + 1;
	else
		rest = tmp_pseudopath +
		       strlen(CTX_PSEUDOPATH(op_ctx)) + 1;

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s Rest %s Mounted on ID %d Path %s",
		 export->export_id, state.st_fullpath,
		 state.st_pseudopath, rest,
		 op_ctx->ctx_export->export_id, CTX_PSEUDOPATH(op_ctx));

	/* Get the root inode of the mounted on export */
	fsal_status = nfs_export_get_root_entry(op_ctx->ctx_export, &state.obj);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Could not get root entry for Export_Id %d Path %s Pseudo Path %s",
			export->export_id, state.st_fullpath,
			state.st_pseudopath);

		/* Goto out to release the reference on the mounted on export.
		 */
		goto out;
	}

	/* Now we need to process the rest of the path, creating directories
	 * if necessary.
	 */
	for (tok = strtok_r(rest, "/", &saveptr);
	     tok;
	     tok = strtok_r(NULL, "/", &saveptr)) {
		rc = make_pseudofs_node(tok, &state);
		if (!rc) {
			/* Release reference on mount point inode and goto out
			 * to release the reference on the mounted on export
			 */
			state.obj->obj_ops->put_ref(state.obj);
			goto out;
		}
	}

	/* Now that all entries are added to pseudofs tree, and we are pointing
	 * to the final node, make it a proper junction.
	 */
	PTHREAD_RWLOCK_wrlock(&state.obj->state_hdl->jct_lock);
	state.obj->state_hdl->dir.junction_export = export;

	rcu_read_lock();

	state.obj->state_hdl->dir.jct_pseudopath =
			gsh_refstr_get(rcu_dereference(export->pseudopath));

	rcu_read_unlock();

	PTHREAD_RWLOCK_unlock(&state.obj->state_hdl->jct_lock);

	/* And fill in the mounted on information for the export. */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	export->exp_mounted_on_file_id = state.obj->fileid;
	/* Pass object ref off to export */
	export->exp_junction_obj = state.obj;
	export_root_object_get(export->exp_junction_obj);

	/* Take an export ref for the parent export */
	export->exp_parent_exp = op_ctx->ctx_export;
	get_gsh_export_ref(export->exp_parent_exp);

	/* Add ourselves to the list of exports mounted on parent */
	PTHREAD_RWLOCK_wrlock(&export->exp_parent_exp->lock);
	glist_add_tail(&export->exp_parent_exp->mounted_exports_list,
		       &export->mounted_exports_node);
	PTHREAD_RWLOCK_unlock(&export->exp_parent_exp->lock);

	PTHREAD_RWLOCK_unlock(&export->lock);

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s junction %p",
		 export->export_id,
		 state.st_fullpath,
		 state.st_pseudopath,
		 state.obj->state_hdl->dir.junction_export);

	result = true;

out:

	/* And we're done with the various references */
	clear_op_context_export();

out_no_context:

	gsh_refstr_put(state.ref_pseudopath);
	gsh_refstr_put(state.ref_fullpath);

	return result;
}

/**
 * @brief Build a pseudo fs from an exportlist
 *
 * foreach through the exports to create pseudofs entries.
 *
 * @return status as errno (0 == SUCCESS).
 */

void create_pseudofs(void)
{
	struct req_op_context op_context;
	struct gsh_export *export;

	/* Initialize a root context */
	init_op_context(&op_context, NULL, NULL, NULL,
			NFS_V4, 0, NFS_REQUEST);

	while (true) {
		export = export_take_mount_work();
		if (export == NULL)
			break;
		if (!pseudo_mount_export(export))
			LogFatal(COMPONENT_EXPORT,
				 "Could not complete creating PseudoFS");
	}
	release_op_context();
}

/**
 * @brief  Unmount an export from the Pseudo FS.
 *
 * @param exp     [IN] export in question
 */
void pseudo_unmount_export(struct gsh_export *export)
{
	struct gsh_export *mounted_on_export;
	struct fsal_obj_handle *junction_inode;
	struct req_op_context op_context;
	struct gsh_refstr *ref_pseudopath;

	/* Take the export write lock to get the junction inode.
	 * We take write lock because if there is no junction inode,
	 * we jump straight to cleaning up our presence in parent
	 * export.
	 */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	junction_inode = export->exp_junction_obj;
	mounted_on_export = export->exp_parent_exp;

	if (junction_inode == NULL || mounted_on_export == NULL) {
		/* This must be the Pseudo Root or a non-NFSv4 export, nothing
		 * to do then. Both better actually be NULL.
		 */
		assert(junction_inode == NULL && mounted_on_export == NULL);

		LogDebug(COMPONENT_EXPORT,
			 "Unmount of export %d unnecessary it should be pseudo root",
			 export->export_id);

		PTHREAD_RWLOCK_unlock(&export->lock);
		return;
	}

	/* Take over the reference to the junction pseudopath - it has the
	 * correct path. export->pseudopath may have been changed by update.
	 */
	ref_pseudopath = junction_inode->state_hdl->dir.jct_pseudopath;

	if (ref_pseudopath == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Unmount of Export Id %d failed no pseudopath",
			 export->export_id);
	}

	LogDebug(COMPONENT_EXPORT,
		 "Unmount %s",
		 ref_pseudopath->gr_val);

	/* Clean up the junction inode */
	LogDebug(COMPONENT_EXPORT,
		 "Cleanup junction inode %p pseudopath %s",
		 junction_inode, ref_pseudopath->gr_val);

	/* Make the node not accessible from the junction node. */
	PTHREAD_RWLOCK_wrlock(&junction_inode->state_hdl->jct_lock);
	junction_inode->state_hdl->dir.jct_pseudopath = NULL;
	junction_inode->state_hdl->dir.junction_export = NULL;
	PTHREAD_RWLOCK_unlock(&junction_inode->state_hdl->jct_lock);

	/* Detach the export from the inode */
	export_root_object_put(export->exp_junction_obj);
	export->exp_junction_obj = NULL;

	/* Detach the export from the export it's mounted on */
	LogDebug(COMPONENT_EXPORT,
		 "Remove from mounted on export %d pseudopath %s",
		 mounted_on_export->export_id,
		 mounted_on_export->pseudopath->gr_val);

	export->exp_parent_exp = NULL;

	/* Remove ourselves from the list of exports mounted on parent */
	PTHREAD_RWLOCK_wrlock(&mounted_on_export->lock);
	glist_del(&export->mounted_exports_node);
	PTHREAD_RWLOCK_unlock(&mounted_on_export->lock);

	/* Release the export lock */
	PTHREAD_RWLOCK_unlock(&export->lock);

	if (is_export_pseudo(mounted_on_export) && junction_inode != NULL) {
		char *pseudo_path = gsh_strdup(ref_pseudopath->gr_val);

		/* Get a ref to the mounted_on_export and initialize op_context
		 */
		get_gsh_export_ref(mounted_on_export);
		init_op_context(&op_context, mounted_on_export,
				mounted_on_export->fsal_export, NULL,
				NFS_V4, 0, NFS_REQUEST);

		/* Remove the unused PseudoFS nodes */
		cleanup_pseudofs_node(pseudo_path, junction_inode);

		gsh_free(pseudo_path);
		release_op_context();
	} else {
		/* Get a ref to the mounted_on_export and initialize op_context
		 */
		get_gsh_export_ref(mounted_on_export);
		init_op_context(&op_context, mounted_on_export,
				mounted_on_export->fsal_export, NULL,
				NFS_V4, 0, NFS_REQUEST);

		/* Properly unmount at the FSAL layer - primarily this will
		 * allow mdcache to unmap the junction inode from the parent
		 * export.
		 */
		mounted_on_export->fsal_export->exp_ops.unmount(
			mounted_on_export->fsal_export, junction_inode);

		release_op_context();
	}

	/* Release our reference to the export we are mounted on. */
	put_gsh_export(mounted_on_export);

	/* Release the LRU reference */
	junction_inode->obj_ops->put_ref(junction_inode);

	LogFullDebug(COMPONENT_EXPORT,
		     "Finish unexport %s", ref_pseudopath->gr_val);

	gsh_refstr_put(ref_pseudopath);
}

/**
 * @brief  Unmount an export and its descendants from the Pseudo FS.
 *
 * @param exp     [IN] export in question
 */
void pseudo_unmount_export_tree(struct gsh_export *export)
{
	/* Unmount any exports mounted on us */
	while (true) {
		struct gsh_export *sub_mounted_export;

		PTHREAD_RWLOCK_rdlock(&export->lock);
		/* Find a sub_mounted export */
		sub_mounted_export =
			glist_first_entry(&export->mounted_exports_list,
					  struct gsh_export,
					  mounted_exports_node);
		if (sub_mounted_export == NULL) {
			/* If none, break out of the loop */
			PTHREAD_RWLOCK_unlock(&export->lock);
			break;
		}

		/* Take a reference to that export. Export may be dead
		 * already, but we should see if we can speed along its
		 * unmounting.
		 */
		get_gsh_export_ref(sub_mounted_export);

		/* Drop the lock */
		PTHREAD_RWLOCK_unlock(&export->lock);

		/* And unmount it */
		pseudo_unmount_export_tree(sub_mounted_export);

		/* And put the reference */
		put_gsh_export(sub_mounted_export);
	}

	pseudo_unmount_export(export);
}

/**
 * @brief  Do a depth first search of an export and its descendants seeking any
 *         defunct descendants, and unmounting them and any descendants of those
 *         exports.
 *
 * Special inputs of generation 0 and ancestor_is_defunct true causes the
 * export and all descendants to be unmounted and added to the list to be
 * remounted. Note that if this was because the export in question is no longer
 * an NFS v4 export, it will not be remounted but any descendants that are still
 * NFS v4 exports will be remounted. Thus accomplishing the intended task.
 *
 * @param export              [IN] export in question, if NULL, prune from root
 * @param generation          [IN] generation of config
 * @param ancestor_is_defunct [IN] flag indicating an ancestor is defunct
 */
void prune_pseudofs_subtree(struct gsh_export *export,
			    uint64_t generation,
			    bool ancestor_is_defunct)
{
	struct gsh_export *child_export;
	struct glist_head *glist, *glistn;
	bool defunct, need_put = false;
	struct gsh_refstr *ref_pseudopath;

	if (export == NULL) {
		/* Get a reference to the PseudoFS Root Export */
		export = get_gsh_export_by_pseudo("/", true);
		if (export == NULL) {
			/* No pseudo root? */
			return;
		}

		need_put = true;
	}

	rcu_read_lock();

	ref_pseudopath = gsh_refstr_get(rcu_dereference(export->pseudopath));

	rcu_read_unlock();

	if (ref_pseudopath == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Unmount of Export Id %d failed no pseudopath",
			 export->export_id);
	}

	defunct = ancestor_is_defunct || export->config_gen < generation ||
		  export->update_prune_unmount;

	LogDebug(COMPONENT_EXPORT,
		 "Exxport %d pseudo %s export gen %"PRIu64
		 " current gen %"PRIu64
		 " prune unmount %s ancestor_is_defunct %s",
		 export->export_id, ref_pseudopath->gr_val,
		 export->config_gen, generation,
		 export->update_prune_unmount ? "yes" : "no",
		 ancestor_is_defunct ? "yes" : "no");

	/* Prune any exports mounted on us. Note that the list WILL change as
	 * child exports are pruned, and note that we drop the lock, however,
	 * this is safe because we hold the export_admin_mutex and that mutex is
	 * held by any thread that will be modifying the PseudoFS structure.
	 * Therefor, glistn will be safe because while the prune may eventually
	 * unmount child_export, it is impossible for any other child exports to
	 * be unmounted during this time, so glistn continues to be valid.
	 */
	PTHREAD_RWLOCK_rdlock(&export->lock);
	glist_for_each_safe(glist, glistn, &export->mounted_exports_list) {
		/* Find a sub_mounted export */
		child_export = glist_entry(glist,
					   struct gsh_export,
					   mounted_exports_node);

		/* Take a reference to that export. Export may be dead
		 * already, but we should see if we can speed along its
		 * unmounting.
		 */
		get_gsh_export_ref(child_export);

		/* Drop the lock */
		PTHREAD_RWLOCK_unlock(&export->lock);
		/* And prune this child */
		prune_pseudofs_subtree(child_export, generation, defunct);

		/* And put the reference */
		put_gsh_export(child_export);

		/* And take the lock again. */
		PTHREAD_RWLOCK_rdlock(&export->lock);
	}

	/* Drop the lock */
	PTHREAD_RWLOCK_unlock(&export->lock);

	if (defunct) {
		LogDebug(COMPONENT_EXPORT,
			 "Exxport %d pseudo %s unmounted because %s",
			 export->export_id, ref_pseudopath->gr_val,
			 export->config_gen < generation
				? "it is defunct"
				: export->update_prune_unmount
					? "update indicates unmount"
					: ancestor_is_defunct
						? "ancestor is defunct"
						: "????");

		pseudo_unmount_export(export);

		if (export->config_gen >= generation &&
		    (export->export_perms.options & EXPORT_OPTION_NFSV4) != 0 &&
		    export->export_id != 0 &&
		    ref_pseudopath->gr_val[1] != '\0') {
			export->update_remount = true;
		}
	} else {
		LogDebug(COMPONENT_EXPORT,
			 "Export %d Pseudo %s not unmounted",
			 export->export_id, ref_pseudopath->gr_val);
	}

	if (export->update_remount) {
		LogDebug(COMPONENT_EXPORT,
			 "Export %d Pseudo %s is to be remounted",
			 export->export_id, ref_pseudopath->gr_val);

		/* Add to mount work */
		export_add_to_mount_work(export);
	}

	/* Clear flags */
	export->update_prune_unmount = false;
	export->update_remount = false;

	if (need_put) {
		/* Put the pseudo root export we found above */
		put_gsh_export(export);
	}

	gsh_refstr_put(ref_pseudopath);
}
