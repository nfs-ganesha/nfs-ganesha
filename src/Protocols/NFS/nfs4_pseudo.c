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
#include "nfs4.h"
#include "nfs_proto_tools.h"
#include "nfs_exports.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
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
	cache_entry_t *dirent;
	struct req_op_context *req_ctx;
};

/**
 * @brief Delete the unecessary directories from pseudo FS
 *
 * @param token [IN] full path of the node
 * @param token [IN] cache entry for the last directory in the path
 * If this entry is present is pseudo FSAL, and is unnecessary, then remove it.
 * Check recursively if the parent entry is needed.
 *
 * Must be called with additional ref on the entry.
 * This routin will put one ref on entry.
 */
bool cleanup_pseudofs_node(const char *pseudopath, cache_entry_t *entry)
{
	struct gsh_export *node_exp;
	cache_entry_t *parent_entry;
	char *parent_pseudopath;
	char *last_slash;
	char *p;
	char *name;
	struct root_op_context root_op_context;
	int pseudopath_len = strlen(pseudopath);
	cache_inode_status_t cache_status;
	bool retval = true;

	assert(entry->type == DIRECTORY);

	if (pseudopath_len == 0)
		goto out;

	node_exp = (struct gsh_export *)
			atomic_fetch_voidptr(&entry->first_export);
	if (strcmp(node_exp->fsal_export->fsal->name, "PSEUDO") != 0) {
		/* Junction is NOT in FSAL_PSEUDO */
		goto out;
	}

	/* Make a copy of the path */
	parent_pseudopath = alloca(pseudopath_len + 1);
	strcpy(parent_pseudopath, pseudopath);

	/* Find last '/' in path */
	p = parent_pseudopath;
	last_slash = parent_pseudopath;
	while (*p != '\0') {
		if (*p == '/')
			last_slash = p;
		p++;
	}
	/* Terminate path leading to junction. */
	*last_slash = '\0';
	name = last_slash + 1;

	LogDebug(COMPONENT_EXPORT,
		 "Checking if pseudo node is needed: %s", pseudopath);

	/* Get attr_lock for looking at junction_export */
	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

	/**
	 * entry is needed if:
	 * It has active children
	 * OR is a junction
	 * OR is pseudo root "/"
	 */
	if (entry->object.dir.nbactive > 0 ||
	    entry->object.dir.junction_export != NULL ||
	    entry == node_exp->exp_root_cache_inode) {
		/* This node is needed */
		LogInfo(COMPONENT_EXPORT,
			 "Pseudo node is needed : %s", name);

		/* Release attr_lock */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		goto out;
	}

	/* Release attr_lock */
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	/* Delete the current entry */
	LogInfo(COMPONENT_EXPORT,
		 "Removing pseudo node: %s", name);

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, node_exp,
				node_exp->fsal_export,
				NFS_V4, 0, NFS_REQUEST);

	parent_entry = cache_inode_get_keyed(&entry->object.dir.parent,
					     &root_op_context.req_ctx,
					     CIG_KEYED_FLAG_CACHED_ONLY,
					     &cache_status);

	if (cache_status != CACHE_INODE_SUCCESS) {
		retval = false;
		goto out;
	}

	cache_status = cache_inode_remove(parent_entry, name,
					  &root_op_context.req_ctx);
	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"Removing pseudo node failed for node name : %s",
			name);
		retval = false;
		cache_inode_put(parent_entry);
		goto out;
	}

	/* check if its parent is needed */
	retval = cleanup_pseudofs_node(parent_pseudopath, parent_entry);

out:
	cache_inode_put(entry);
	return retval;
}

bool make_pseudofs_node(char *name, void *arg)
{
	struct pseudofs_state *state = arg;
	cache_entry_t *new_node = NULL;
	cache_inode_status_t cache_status;
	bool retried = false;

retry:

	/* First, try to lookup the entry */
	cache_status = cache_inode_lookup(state->dirent,
					  name,
					  state->req_ctx,
					  &new_node);

	if (cache_status == CACHE_INODE_SUCCESS) {
		/* Make sure new node is a directory */
		if (new_node->type != DIRECTORY) {
			LogCrit(COMPONENT_EXPORT,
				"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s is not a directory",
				state->export->export_id,
				state->export->fullpath,
				state->export->pseudopath,
				name);
			/* Release the reference on the new node */
			cache_inode_put(new_node);
			return false;
		}

		LogDebug(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Parent %p entry %p %s FSAL %s already exists",
			 state->dirent, new_node, name,
			 new_node->obj_handle->fsal->name);
		/* Release reference to the old node */
		cache_inode_put(state->dirent);

		/* Make new node the current node */
		state->dirent = new_node;
		return true;
	}

	if (cache_status != CACHE_INODE_NOT_FOUND) {
		/* An error occurred */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s failed with %s",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			cache_inode_err_str(cache_status));
		return false;
	}

	if (strcmp(state->req_ctx->export->fsal_export->fsal->name,
		   "PSEUDO") != 0) {
		/* Only allowed to create directories on FSAL_PSEUDO */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s LOOKUP %s failed with %s (can't create directory on non-PSEUDO FSAL)",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			cache_inode_err_str(cache_status));
		return false;
	}

	/* Node was not found and no other error, must create node. */
	cache_status = cache_inode_create(state->dirent,
					  name,
					  DIRECTORY,
					  0755,
					  NULL,
					  state->req_ctx,
					  &new_node);

	if (cache_status == CACHE_INODE_ENTRY_EXISTS && !retried) {
		LogDebug(COMPONENT_EXPORT,
			 "BUILDING PSEUDOFS: Parent %p Node %p %s seems to already exist, try LOOKUP again",
			 state->dirent, new_node, name);
		retried = true;
		goto retry;
	}

	if (cache_status != CACHE_INODE_SUCCESS) {
		/* An error occurred */
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s failed with %s",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			cache_inode_err_str(cache_status));
		return false;
	}

	/* Lock (and refresh if necessary) the attributes */
	cache_status =
	    cache_inode_lock_trust_attrs(state->dirent, state->req_ctx, true);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s GETATTR %s failed with %s",
			state->export->export_id,
			state->export->fullpath,
			state->export->pseudopath,
			name,
			cache_inode_err_str(cache_status));
		return false;
	}

	PTHREAD_RWLOCK_unlock(&state->dirent->attr_lock);

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s CREATE %s succeded",
		 state->export->export_id,
		 state->export->fullpath,
		 state->export->pseudopath,
		 name);

	/* Release reference to the old node */
	cache_inode_put(state->dirent);

	/* Make new node the current node */
	state->dirent = new_node;
	return true;
}

/**
 * @brief  Mount an export in the new Pseudo FS.
 *
 * @param exp     [IN] export in question
 * @param req_ctx [IN] req_op_context to operate in
 *
 * @return status as bool.
 */
bool pseudo_mount_export(struct gsh_export *export,
			 struct req_op_context *req_ctx)
{
	struct pseudofs_state state;
	char *tmp_pseudopath;
	char *last_slash;
	char *p;
	char *rest;
	cache_inode_status_t cache_status;
	char *tok;
	char *saveptr;
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
	state.req_ctx = req_ctx;

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
	state.req_ctx->export =
	    get_gsh_export_by_pseudo_locked(tmp_pseudopath, false);

	if (state.req_ctx->export == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Could not find mounted on export for %s, tmp=%s",
			 export->pseudopath, tmp_pseudopath);
	}

	state.req_ctx->fsal_export = req_ctx->export->fsal_export;

	/* Put the slash back in */
	*last_slash = '/';

	/* Point to the portion of this export's pseudo path that is beyond the
	 * mounted on export's pseudo path.
	 */
	if (state.req_ctx->export->pseudopath[1] == '\0')
		rest = tmp_pseudopath + 1;
	else
		rest = tmp_pseudopath +
		       strlen(state.req_ctx->export->pseudopath) + 1;

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s Rest %s",
		 export->export_id, export->fullpath,
		 export->pseudopath, rest);

	/* Get the root inode of the mounted on export */
	cache_status = nfs_export_get_root_entry(state.req_ctx->export,
						 &state.dirent);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Could not get root entry for Export_Id %d Path %s Pseudo Path %s",
			export->export_id, export->fullpath,
			export->pseudopath);

		/* Release the reference on the mounted on export. */
		put_gsh_export(state.req_ctx->export);
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
			cache_inode_put(state.dirent);
			put_gsh_export(state.req_ctx->export);
			return false;
		}
	}

	/* Lock (and refresh if necessary) the attributes */
	cache_status =
	    cache_inode_lock_trust_attrs(state.dirent, state.req_ctx, true);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s final GETATTR failed with %s",
			export->export_id,
			export->fullpath,
			export->pseudopath,
			cache_inode_err_str(cache_status));

		/* Release reference on mount point inode
		 * and the mounted on export
		 */
		cache_inode_put(state.dirent);
		put_gsh_export(state.req_ctx->export);
		return false;
	}

	/* Instead of an LRU reference, we must hold a pin reference.
	 * We hold the LRU reference until we are done here to prevent
	 * premature cleanup. Once we are done here, if the entry has
	 * already gone bad, cleanup will happen on our unref and
	 * and will be correct.
	 */
	cache_status = cache_inode_inc_pin_ref(state.dirent);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_INIT,
			"BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s final pin failed with %s",
			export->export_id,
			export->fullpath,
			export->pseudopath,
			cache_inode_err_str(cache_status));

		/* Release the LRU reference and return failure. */
		cache_inode_put(state.dirent);
		return false;
	}

	/* Now that all entries are added to pseudofs tree, and we are pointing
	 * to the final node, make it a proper junction.
	 */
	state.dirent->object.dir.junction_export = export;

	/* And fill in the mounted on information for the export. */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	export->exp_mounted_on_file_id =
	    state.dirent->obj_handle->attributes.fileid;
	export->exp_junction_inode = state.dirent;
	export->exp_parent_exp = state.req_ctx->export;

	PTHREAD_RWLOCK_unlock(&export->lock);

	PTHREAD_RWLOCK_unlock(&state.dirent->attr_lock);

	/* Release the LRU reference and return success. */
	cache_inode_put(state.dirent);

	return true;
}

bool pseudo_mount_export_wrapper(struct gsh_export *export, void *arg)
{
	struct root_op_context *root_op_context = arg;
	return pseudo_mount_export(export, &root_op_context->req_ctx);
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

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     NFS_V4, 0, NFS_REQUEST);

	if (!foreach_gsh_export(pseudo_mount_export_wrapper, &root_op_context))
		LogFatal(COMPONENT_INIT,
			 "Error while initializing NFSv4 pseudo file system");
}

/**
 * @brief  Unmount an export from the Pseudo FS.
 *
 * @param exp     [IN] export in question
 * @param req_ctx [IN] req_op_context to operate in
 */
void pseudo_unmount_export(struct gsh_export *export,
			   struct req_op_context *req_ctx)
{
	struct gsh_export *mounted_on_export;
	cache_entry_t *junction_inode;

	/* Take the lock to clean out the junction information. */
	PTHREAD_RWLOCK_wrlock(&export->lock);

	mounted_on_export = export->exp_parent_exp;
	junction_inode = export->exp_junction_inode;

	if (junction_inode) {
		/* Make the node not accessible from the junction node */
		PTHREAD_RWLOCK_wrlock(&junction_inode->attr_lock);
		junction_inode->object.dir.junction_export = NULL;
		PTHREAD_RWLOCK_unlock(&junction_inode->attr_lock);
	} else {
		/* The node is not mounted in the Pseudo FS, bail out. */
		goto out;
	}

	/* Detach the export from the inode */
	export->exp_junction_inode = NULL;
	/* Detach the export from the export it's mounted on */
	export->exp_parent_exp = NULL;

	/* Remove the unused pseudofs nodes
	 * Need to take addtional ref for cleanup_pseudofs_node
	 */
	cache_inode_lru_ref(junction_inode, LRU_FLAG_NONE);
	if (!cleanup_pseudofs_node(export->pseudopath, junction_inode)) {
		LogCrit(COMPONENT_EXPORT,
			"pseudofs node cleanup failed for path: %s",
			export->pseudopath);
	}

	/* Release the pin reference */
	cache_inode_dec_pin_ref(junction_inode, false);

	/* Release our reference to the export we are mounted on. */
	if (mounted_on_export != NULL)
		put_gsh_export(mounted_on_export);

out:

	PTHREAD_RWLOCK_unlock(&export->lock);
}
