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
 * @param pseudopath [IN] full path of the node
 * @param entry [IN] cache entry for the last directory in the path
 *
 * If this entry is present is pseudo FSAL, and is unnecessary, then remove it.
 * Check recursively if the parent entry is needed.
 *
 * The pseudopath is deconstructed in place to create the subsequently shorter
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
void cleanup_pseudofs_node(char *pseudopath,
			   struct fsal_obj_handle *obj)
{
	struct fsal_obj_handle *parent_obj;
	char *pos = pseudopath + strlen(pseudopath) - 1;
	char *name;
	fsal_status_t fsal_status;

	/* Strip trailing / from pseudopath */
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
		 "Checking if pseudo node %s is needed", pseudopath);

	fsal_status = fsal_lookupp(obj, &parent_obj, NULL);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Truncate the pseudopath to be the path to the parent */
		*pos = '\0';
		LogCrit(COMPONENT_EXPORT,
			"Could not find cache entry for parent directory %s",
			pseudopath);
		return;
	}

	fsal_status = fsal_remove(parent_obj, name);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Bailout if we get directory not empty error */
		if (fsal_status.major == ERR_FSAL_NOTEMPTY) {
			LogDebug(COMPONENT_EXPORT,
				 "PseudoFS parent directory %s is not empty",
				 pseudopath);
		} else {
			LogCrit(COMPONENT_EXPORT,
				"Removing pseudo node %s failed with %s",
				pseudopath, msg_fsal_err(fsal_status.major));
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
			 op_ctx->ctx_export->pseudopath);

		PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
		goto out;
	}

	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	/* Truncate the pseudopath to be the path to the parent */
	*pos = '\0';

	/* check if the parent directory is needed */
	cleanup_pseudofs_node(pseudopath, parent_obj);

out:
	parent_obj->obj_ops->put_ref(parent_obj);
}

bool make_pseudofs_node(char *name, struct pseudofs_state *state)
{
	struct fsal_obj_handle *new_node = NULL;
	fsal_status_t fsal_status;
	bool retried = false;
	struct attrlist sattr;
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
				state->export->fullpath,
				state->export->pseudopath,
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

	if (fsal_status.major != ERR_FSAL_NOENT) {
		/* An error occurred */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s failed with %s",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			msg_fsal_err(fsal_status.major));
		return false;
	}

	fsal_name = op_ctx->ctx_export->fsal_export->exp_ops.get_name(
				op_ctx->ctx_export->fsal_export);
	/* fsal_name should be "PSEUDO" or "PSEUDO/<stacked-fsal-name>" */
	if (strncmp(fsal_name, "PSEUDO", 6) != 0 ||
	    (fsal_name[6] != '/' && fsal_name[6] != '\0')) {
		/* Only allowed to create directories on FSAL_PSEUDO */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s failed with %s (can't create directory on non-PSEUDO FSAL)",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			msg_fsal_err(fsal_status.major));
		return false;
	}

	/* Node was not found and no other error, must create node. */
	fsal_prepare_attrs(&sattr, ATTR_MODE);
	sattr.mode = 0755;

	fsal_status = fsal_create(state->obj, name, DIRECTORY, &sattr, NULL,
				  &new_node, NULL);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (fsal_status.major == ERR_FSAL_EXIST && !retried) {
		LogDebug(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Parent %p Node %p %s seems to already exist, try LOOKUP again",
			 state->obj, new_node, name);
		retried = true;
		goto retry;
	}

	if (FSAL_IS_ERROR(fsal_status)) {
		/* An error occurred */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s failed with %s",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			msg_fsal_err(fsal_status.major));
		return false;
	}

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s obj %p state %p succeeded",
		 state->export->export_id,
		 state->export->fullpath,
		 state->export->pseudopath,
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

	/* skip exports that aren't for NFS v4
	 * Also, nothing to actually do for Pseudo Root
	 */
	if ((export->export_perms.options & EXPORT_OPTION_NFSV4) == 0
	    || export->pseudopath == NULL
	    || export->export_id == 0
	    || export->pseudopath[1] == '\0')
		return true;

	/* Initialize state and it's req_ctx.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	state.export = export;

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s",
		 export->export_id, export->fullpath, export->pseudopath);

	/* Make a copy of the path */
	tmp_pseudopath = alloca(strlen(export->pseudopath) + 1);
	strcpy(tmp_pseudopath, export->pseudopath);

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
	op_ctx->ctx_export = get_gsh_export_by_pseudo(tmp_pseudopath, false);

	if (op_ctx->ctx_export == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Could not find mounted on export for %s, tmp=%s",
			 export->pseudopath, tmp_pseudopath);
	}

	op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;

	/* Put the slash back in */
	*last_slash = '/';

	/* Point to the portion of this export's pseudo path that is beyond the
	 * mounted on export's pseudo path.
	 */
	if (op_ctx->ctx_export->pseudopath[1] == '\0')
		rest = tmp_pseudopath + 1;
	else
		rest = tmp_pseudopath +
		       strlen(op_ctx->ctx_export->pseudopath) + 1;

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s Rest %s",
		 export->export_id, export->fullpath,
		 export->pseudopath, rest);

	/* Get the root inode of the mounted on export */
	fsal_status = nfs_export_get_root_entry(op_ctx->ctx_export, &state.obj);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Could not get root entry for Export_Id %d Path %s Pseudo Path %s",
			export->export_id, export->fullpath,
			export->pseudopath);

		/* Release the reference on the mounted on export. */
		put_gsh_export(op_ctx->ctx_export);
		return false;
	}

	/* Now we need to process the rest of the path, creating directories
	 * if necessary.
	 */
	for (tok = strtok_r(rest, "/", &saveptr);
	     tok;
	     tok = strtok_r(NULL, "/", &saveptr)) {
		rc = make_pseudofs_node(tok, &state);
		if (!rc) {
			/* Release reference on mount point inode
			 * and the mounted on export
			 */
			state.obj->obj_ops->put_ref(state.obj);
			put_gsh_export(op_ctx->ctx_export);
			return false;
		}
	}

	/* Now that all entries are added to pseudofs tree, and we are pointing
	 * to the final node, make it a proper junction.
	 */
	PTHREAD_RWLOCK_wrlock(&state.obj->state_hdl->state_lock);
	state.obj->state_hdl->dir.junction_export = export;
	PTHREAD_RWLOCK_unlock(&state.obj->state_hdl->state_lock);

	/* And fill in the mounted on information for the export. */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	export->exp_mounted_on_file_id = state.obj->fileid;
	/* Pass ref off to export */
	export->exp_junction_obj = state.obj;
	export->exp_parent_exp = op_ctx->ctx_export;

	/* Add ourselves to the list of exports mounted on parent */
	PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);
	glist_add_tail(&op_ctx->ctx_export->mounted_exports_list,
		       &export->mounted_exports_node);
	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	PTHREAD_RWLOCK_unlock(&export->lock);

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s junction %p",
		 state.export->export_id,
		 state.export->fullpath,
		 state.export->pseudopath,
		 state.obj->state_hdl->dir.junction_export);

	return true;
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
	struct root_op_context root_op_context;
	struct gsh_export *export;

	/* Initialize a root context */
	init_root_op_context(&root_op_context, NULL, NULL,
			     NFS_V4, 0, NFS_REQUEST);

	while (true) {
		export = export_take_mount_work();
		if (export == NULL)
			break;
		if (!pseudo_mount_export(export))
			LogFatal(COMPONENT_EXPORT,
				 "Could not complete creating PseudoFS");
	}
	release_root_op_context();
}

/**
 * @brief  Unmount an export from the Pseudo FS.
 *
 * @param exp     [IN] export in question
 */
void pseudo_unmount_export(struct gsh_export *export)
{
	struct gsh_export *mounted_on_export;
	struct gsh_export *sub_mounted_export;
	struct fsal_obj_handle *junction_inode;
	struct root_op_context root_op_context;

	/* Unmount any exports mounted on us */
	while (true) {
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
		pseudo_unmount_export(sub_mounted_export);

		/* And put the reference */
		put_gsh_export(sub_mounted_export);
	}

	LogDebug(COMPONENT_EXPORT,
		 "Unmount %s",
		 export->pseudopath);

	/* Take the export write lock to get the junction inode.
	 * We take write lock because if there is no junction inode,
	 * we jump straight to cleaning up our presence in parent
	 * export.
	 */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	junction_inode = export->exp_junction_obj;

	if (junction_inode != NULL) {
		/* Clean up the junction inode */

		/* Don't take a reference; there is a sentinal one */

		/* Release the export lock so we can take take it write */
		PTHREAD_RWLOCK_unlock(&export->lock);

		/* Make the node not accessible from the junction node. */
		PTHREAD_RWLOCK_wrlock(&junction_inode->state_hdl->state_lock);
		junction_inode->state_hdl->dir.junction_export = NULL;
		PTHREAD_RWLOCK_unlock(&junction_inode->state_hdl->state_lock);

		/* Detach the export from the inode */
		PTHREAD_RWLOCK_wrlock(&export->lock);
		export->exp_junction_obj = NULL;
	}

	/* Detach the export from the export it's mounted on */
	mounted_on_export = export->exp_parent_exp;

	if (mounted_on_export != NULL) {
		export->exp_parent_exp = NULL;
		/* Remove ourselves from the list of exports mounted on
		 * parent
		 */
		PTHREAD_RWLOCK_wrlock(&mounted_on_export->lock);
		glist_del(&export->mounted_exports_node);
		PTHREAD_RWLOCK_unlock(&mounted_on_export->lock);
	}

	/* Release the export lock */
	PTHREAD_RWLOCK_unlock(&export->lock);

	if (mounted_on_export != NULL) {
		if (is_export_pseudo(mounted_on_export)
		    && junction_inode != NULL) {
			char *pseudopath = gsh_strdup(export->pseudopath);

			/* Initialize req_ctx */
			init_root_op_context(
				&root_op_context,
				mounted_on_export,
				mounted_on_export->fsal_export,
				NFS_V4, 0, NFS_REQUEST);

			/* Remove the unused PseudoFS nodes */
			cleanup_pseudofs_node(pseudopath,
					      junction_inode);

			gsh_free(pseudopath);
			release_root_op_context();
		}

		/* Release our reference to the export we are mounted on. */
		put_gsh_export(mounted_on_export);
	}

	if (junction_inode != NULL) {
		/* Release the LRU reference */
		junction_inode->obj_ops->put_ref(junction_inode);
	}
}

bool export_is_defunct(struct gsh_export *export, uint64_t generation)
{
	bool ok;
	struct glist_head *cur;

	if (export->config_gen >= generation) {
		LogDebug(COMPONENT_EXPORT,
			 "%s can't be unmounted (conf=%lu gen=%lu)",
			 export->pseudopath, export->config_gen, generation);
		return false;
	}

	ok = strcmp(export->pseudopath, "/");
	if (!ok) {
		LogDebug(COMPONENT_EXPORT, "Refusing to unmount /");
		return false;
	}

	PTHREAD_RWLOCK_rdlock(&export->lock);
	glist_for_each(cur, &export->mounted_exports_list) {
		struct gsh_export *sub = container_of(cur, struct gsh_export,
							mounted_exports_node);

		/* Test each submount */
		ok = export_is_defunct(sub, generation);
		if (!ok) {
			LogCrit(COMPONENT_EXPORT,
				"%s can't be unmounted (child export remains)",
				export->pseudopath);
			break;
		}
	}
	PTHREAD_RWLOCK_unlock(&export->lock);
	return ok;
}
