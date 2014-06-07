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
};

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
			   cache_entry_t *entry)
{
	cache_entry_t *parent_entry;
	char *pos = pseudopath + strlen(pseudopath) - 1;
	char *name;
	cache_inode_status_t cache_status;

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

	cache_status = cache_inode_lookupp(entry, &parent_entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		/* Truncate the pseudopath to be the path to the parent */
		*pos = '\0';
		LogCrit(COMPONENT_EXPORT,
			"Could not find cache entry for parent directory %s",
			pseudopath);
		return;
	}

	cache_status = cache_inode_remove(parent_entry, name);

	if (cache_status == CACHE_INODE_DIR_NOT_EMPTY) {
		LogDebug(COMPONENT_EXPORT,
			 "PseudoFS parent directory %s is not empty",
			 pseudopath);
		goto out;
	} else if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"Removing pseudo node %s failed with %s",
			pseudopath, cache_inode_err_str(cache_status));
		goto out;
	}

	/* Before recursing the check the parent, get export lock for looking at
	 * exp_root_cache_inode so we can check if we have reached the root of
	 * the mounted on export.
	 */
	PTHREAD_RWLOCK_rdlock(&op_ctx->export->lock);

	if (parent_entry == op_ctx->export->exp_root_cache_inode) {
		LogDebug(COMPONENT_EXPORT,
			 "Reached root of PseudoFS %s",
			 op_ctx->export->pseudopath);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
		goto out;
	}

	PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	/* Truncate the pseudopath to be the path to the parent */
	*pos = '\0';

	/* check if the parent directory is needed */
	cleanup_pseudofs_node(pseudopath, parent_entry);

out:

	cache_inode_put(parent_entry);
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

	if (strcmp(op_ctx->export->fsal_export->fsal->name,
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
	    cache_inode_lock_trust_attrs(state->dirent, true);

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
	op_ctx->export =
	    get_gsh_export_by_pseudo(tmp_pseudopath, false);

	if (op_ctx->export == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "Could not find mounted on export for %s, tmp=%s",
			 export->pseudopath, tmp_pseudopath);
	}

	op_ctx->fsal_export = op_ctx->export->fsal_export;

	/* Put the slash back in */
	*last_slash = '/';

	/* Point to the portion of this export's pseudo path that is beyond the
	 * mounted on export's pseudo path.
	 */
	if (op_ctx->export->pseudopath[1] == '\0')
		rest = tmp_pseudopath + 1;
	else
		rest = tmp_pseudopath +
		       strlen(op_ctx->export->pseudopath) + 1;

	LogDebug(COMPONENT_EXPORT,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s Rest %s",
		 export->export_id, export->fullpath,
		 export->pseudopath, rest);

	/* Get the root inode of the mounted on export */
	cache_status = nfs_export_get_root_entry(op_ctx->export,
						 &state.dirent);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_EXPORT,
			"BUILDING PSEUDOFS: Could not get root entry for Export_Id %d Path %s Pseudo Path %s",
			export->export_id, export->fullpath,
			export->pseudopath);

		/* Release the reference on the mounted on export. */
		put_gsh_export(op_ctx->export);
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
			put_gsh_export(op_ctx->export);
			return false;
		}
	}

	/* Lock (and refresh if necessary) the attributes */
	cache_status =
	    cache_inode_lock_trust_attrs(state.dirent, true);

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
		put_gsh_export(op_ctx->export);
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
	export->exp_parent_exp = op_ctx->export;

	/* Add ourselves to the list of exports mounted on parent */
	PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
	glist_add_tail(&op_ctx->export->mounted_exports_list,
		       &export->mounted_exports_node);
	PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	PTHREAD_RWLOCK_unlock(&export->lock);

	PTHREAD_RWLOCK_unlock(&state.dirent->attr_lock);

	/* Release the LRU reference and return success. */
	cache_inode_put(state.dirent);

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
	cache_entry_t *junction_inode;
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

		/* Take a reference to that export */
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

	junction_inode = export->exp_junction_inode;

	if (junction_inode != NULL) {
		/* Clean up the junction inode */

		/* Take an LRU reference */
		cache_inode_lru_ref(junction_inode, LRU_FLAG_NONE);

		/* Release the export lock so we can take attr_lock */
		PTHREAD_RWLOCK_unlock(&export->lock);

		/* Take the attr_lock so we can check if the export is still
		 * connected to the junction_inode.
		 */
		PTHREAD_RWLOCK_wrlock(&junction_inode->attr_lock);

		/* Take the export write lock to clean out the junction
		 * information.
		 */
		PTHREAD_RWLOCK_wrlock(&export->lock);

		/* Make the node not accessible from the junction node. */
		junction_inode->object.dir.junction_export = NULL;

		/* Detach the export from the inode */
		export->exp_junction_inode = NULL;

		/* Release the attr_lock */
		PTHREAD_RWLOCK_unlock(&junction_inode->attr_lock);
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
		if (strcmp(mounted_on_export->fsal_export->fsal->name,
		    "PSEUDO") == 0) {
			char *pseudopath = gsh_strdup(export->pseudopath);
			if (pseudopath != NULL) {
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
			} else {
				LogCrit(COMPONENT_EXPORT,
					"Could not clean up PseudoFS for %s, out of memory",
					export->pseudopath);
			}
		}

		/* Release our reference to the export we are mounted on. */
		put_gsh_export(mounted_on_export);
	}

	if (junction_inode != NULL) {
		/* Release the pin reference */
		cache_inode_dec_pin_ref(junction_inode, false);

		/* Release the LRU reference */
		cache_inode_put(junction_inode);
	}
}
