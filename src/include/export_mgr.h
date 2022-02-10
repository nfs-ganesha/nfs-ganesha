/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup Filesystem export management
 * @{
 */

/**
 * @file export_mgr.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Export manager
 */

#include "gsh_list.h"
#include "avltree.h"
#include "abstract_atomic.h"
#include "fsal.h"

#ifndef EXPORT_MGR_H
#define EXPORT_MGR_H

extern pthread_mutex_t export_admin_mutex;

/* The following counter is used to implement a seqlock protecting code that
 * needs to look at exports that are being changed up by an in progress update.
 * Such code should generally return an error causing the client to retry since
 * an export update may take way too much time to retry in line.
 *
 * Any code that modifies exports must increment this counter after taking the
 * above lock and again before releasing the above lock.
 */
extern uint64_t export_admin_counter;

static inline void EXPORT_ADMIN_LOCK(void)
{
	PTHREAD_MUTEX_lock(&export_admin_mutex);
	export_admin_counter++;
}

static inline void EXPORT_ADMIN_UNLOCK(void)
{
	export_admin_counter++;
	PTHREAD_MUTEX_unlock(&export_admin_mutex);
}

static inline int EXPORT_ADMIN_TRYLOCK(void)
{
	int rc = PTHREAD_MUTEX_trylock(&export_admin_mutex);

	if (rc == 0)
		export_admin_counter++;

	return rc;
}

/**
 * @brief Implement seqlock verification
 *
 * To use the export_admin_counter, a process that might get bad results due
 * to an in progress export update should save the export_admin_counter as
 * start_export_admin_counter before executing the code that could be confused.
 * The after the code is complete, the code can call
 * is_export_admin_counter_valid(start_export_admin_counter) to determine if
 * and export update might have upended things.
 *
 * Depending on how the code functions, it may only need to perform this check
 * if an unexpected result occurred. On the other hand the check is cheap, while
 * a false negative is possible, that still requires the code have been
 * executing parallel with an export update which are expected to be extremely
 * rare so even if the code catches a half-updated counter (due to NOT using
 * atomics) it just results in a false negative.
 */
static inline
bool is_export_admin_counter_valid(uint64_t start_export_admin_counter)
{
	return (start_export_admin_counter % 2) == 0 &&
		start_export_admin_counter == export_admin_counter;
}

/**
 * @brief Simple check if an export update is in progress
 *
 * If code uses locks in a way that guarantee that an export update can not
 * upset their world while the code is executing then a simple check after
 * failure that an update is in progress (seqlock value is odd) is sufficient.
 * For example, code implementing a lookup in a pseudo fs where lookup holds a
 * lock that prevents the update from changing the pseudo fs while the lookup
 * in progress means that any update that will upset this lookups apple cart
 * can not start AND end while the lookup is in progress.
 */
static inline bool is_export_update_in_progress(void)
{
	return (export_admin_counter % 2) != 0;
}

enum export_status {
	EXPORT_READY,		/*< searchable, usable */
	EXPORT_STALE,		/*< export is no longer valid */
};

/**
 * @brief Represents an export.
 *
 * CFG: Identifies those fields that are associated with configuration.
 *
 */

struct gsh_export {
	/** List of all exports */
	struct glist_head exp_list;
	/** gsh_exports are kept in an AVL tree by export_id */
	struct avltree_node node_k;
	/** List of NFS v4 state belonging to this export */
	struct glist_head exp_state_list;
	/** List of locks belonging to this export */
	struct glist_head exp_lock_list;
	/** List of NLM shares belonging to this export */
	struct glist_head exp_nlm_share_list;
	/** List of exports rooted on the same inode */
	struct glist_head exp_root_list;
	/** List of exports to be mounted or cleaned up */
	struct glist_head exp_work;
	/** List of exports mounted on this export */
	struct glist_head mounted_exports_list;
	/** This export is a node in the list of mounted_exports */
	struct glist_head mounted_exports_node;
	/** Entry for the root of this export, protected by lock */
	struct fsal_obj_handle *exp_root_obj;
	/** CFG config_generation that last touched this export */
	uint64_t config_gen;
	/** CFG Allowed clients - update protected by lock */
	struct glist_head clients;
	/** Entry for the junction of this export.  Protected by lock */
	struct fsal_obj_handle *exp_junction_obj;
	/** The export this export sits on. Protected by lock */
	struct gsh_export *exp_parent_exp;
	/** Pointer to the fsal_export associated with this export */
	struct fsal_export *fsal_export;
	/** CFG: Exported path - static option */
	struct gsh_refstr *fullpath;
	/** CFG: PseudoFS path for export - static option */
	struct gsh_refstr *pseudopath;
	/** CFG: The following two strings are ONLY used during configuration
	 *       where they are guaranteed not to change. They can only be
	 *       changed while updating an export which can only happen while
	 *       the export_admin_mutex is held. Note that when doing an update,
	 *       the existing export is fetched, and it is safe to use these
	 *       strings from that export also. They will be safely updated as
	 *       part of the update.
	 */
	char *cfg_fullpath;
	char *cfg_pseudopath;
	/** CFG: Tag for direct NFS v3 mounting of export - static option */
	char *FS_tag;
	/** Node id this is mounted on. Protected by lock */
	uint64_t exp_mounted_on_file_id;
	/** CFG: Max Read for this entry - atomic changeable option */
	uint64_t MaxRead;
	/** CFG: Max Write for this entry - atomic changeable option */
	uint64_t MaxWrite;
	/** CFG: Preferred Read size - atomic changeable option */
	uint64_t PrefRead;
	/** CFG: Preferred Write size - atomic changeable option */
	uint64_t PrefWrite;
	/** CFG: Preferred Readdir size - atomic changeable option */
	uint64_t PrefReaddir;
	/** CFG: Maximum Offset allowed for write - atomic changeable option */
	uint64_t MaxOffsetWrite;
	/** CFG: Maximum Offset allowed for read - atomic changeable option */
	uint64_t MaxOffsetRead;
	/** CFG: Filesystem ID for overriding fsid from FSAL - ????? */
	fsal_fsid_t filesystem_id;
	/** References to this export */
	int64_t refcnt;
	/** Read/Write lock protecting export */
	pthread_rwlock_t lock;
	/** CFG: available mount options - update protected by lock */
	struct export_perms export_perms;
	/** The last time the export stats were updated */
	struct timespec last_update;
	/** CFG: Export non-permission options - atomic changeable option */
	uint32_t options;
	/** CFG: Export non-permission options set - atomic changeable option */
	uint32_t options_set;
	/** CFG: Export_Id for this export - static option */
	uint16_t export_id;

	uint8_t export_status;		/*< current condition */
	bool has_pnfs_ds;		/*< id_servers matches export_id */
	/* Due to an update, during the prune phase, this export must be
	 * unmounted. It will then be added to the mount work done during the
	 * remount phase. This flag WILL be cleared during prune.
	 */
	bool update_prune_unmount;
	/* Due to an update, this export will need to be remounted. */
	bool update_remount;
};

/* Use macro to define this to get around include file order. */
#define ctx_export_path(ctx) \
	((nfs_param.core_param.mount_path_pseudo) \
		? CTX_PSEUDOPATH(ctx) \
		: CTX_FULLPATH(ctx))

/* If op_ctx request is NFS_V4 always use pseudopath, otherwise use fullpath
 * for export.
 */
#define op_ctx_export_path(ctx) \
	((ctx->nfs_vers == NFS_V4) || \
	 (nfs_param.core_param.mount_path_pseudo) \
		? CTX_PSEUDOPATH(ctx) \
		: CTX_FULLPATH(ctx))

/**
 * @brief Structure to make it easier to access the fullpath and pseudopath for
 *        an export that isn't op_ctx->ctx_export or where an op context may not
 *        be available.
 *
 * NOTE: This structure is not intended to be re-used and it is expected to
 *       only be used for an actual export.
 */
struct tmp_export_paths {
	struct gsh_refstr *tmp_fullpath;
	struct gsh_refstr *tmp_pseudopath;
};

#define TMP_PSEUDOPATH(tmp) ((tmp)->tmp_pseudopath->gr_val)

#define TMP_FULLPATH(tmp) ((tmp)->tmp_fullpath->gr_val)

#define tmp_export_path(tmp) \
	((nfs_param.core_param.mount_path_pseudo) \
		? TMP_PSEUDOPATH(tmp) \
		: TMP_FULLPATH(tmp))

#define op_ctx_tmp_export_path(ctx, tmp) \
	((ctx->nfs_vers == NFS_V4) || \
	 (nfs_param.core_param.mount_path_pseudo) \
		? TMP_PSEUDOPATH(tmp) \
		: TMP_FULLPATH(tmp))

static inline void tmp_get_exp_paths(struct tmp_export_paths *tmp,
				     struct gsh_export *exp)
{
	struct gsh_refstr *gr;

	rcu_read_lock();

	gr = rcu_dereference(exp->fullpath);

	if (gr != NULL)
		tmp->tmp_fullpath = gsh_refstr_get(gr);
	else
		tmp->tmp_fullpath = gsh_refstr_dup(exp->cfg_fullpath);

	gr = rcu_dereference(exp->pseudopath);

	if (gr != NULL)
		tmp->tmp_pseudopath = gsh_refstr_get(gr);
	else if (exp->cfg_pseudopath != NULL)
		tmp->tmp_pseudopath = gsh_refstr_dup(exp->cfg_pseudopath);
	else
		tmp->tmp_pseudopath = gsh_refstr_get(no_export);

	rcu_read_unlock();
}

static inline void tmp_put_exp_paths(struct tmp_export_paths *tmp)
{
	gsh_refstr_put(tmp->tmp_fullpath);
	gsh_refstr_put(tmp->tmp_pseudopath);
}

static inline bool op_ctx_export_has_option(uint32_t option)
{
	return atomic_fetch_uint32_t(&op_ctx->ctx_export->options) & option;
}

static inline bool op_ctx_export_has_option_set(uint32_t option)
{
	return atomic_fetch_uint32_t(&op_ctx->ctx_export->options_set) & option;
}

void export_pkginit(void);
#ifdef USE_DBUS
void dbus_export_init(void);
#endif
struct gsh_export *alloc_export(void);
bool insert_gsh_export(struct gsh_export *a_export);
struct gsh_export *get_gsh_export(uint16_t export_id);
struct gsh_export *get_gsh_export_by_path(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_path_locked(char *path,
						 bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo_locked(char *path,
						   bool exact_match);
struct gsh_export *get_gsh_export_by_tag(char *tag);
bool mount_gsh_export(struct gsh_export *exp);
void unmount_gsh_export(struct gsh_export *exp);
void remove_gsh_export(uint16_t export_id);
bool foreach_gsh_export(bool(*cb) (struct gsh_export *exp, void *state),
			bool wrlock, void *state);

/**
 * @brief Advisory check of export readiness.
 *
 * This function does not guarantee the export is reachable at the point of
 * the test, it is just used to allow a function to take a shortcut if the
 * export has gone stale, usually when the function is about to take an
 * additional reference based on some object having a pointer and reference
 * to the export.
 *
 * @param[in] export The export to test for readiness.
 *
 * @retval true if the export is ready
 */
static inline bool export_ready(struct gsh_export *a_export)
{
	return a_export->export_status == EXPORT_READY;
}

void _get_gsh_export_ref(struct gsh_export *a_export,
			 char *file, int line, char *function);

#define get_gsh_export_ref(a_export) \
	_get_gsh_export_ref(a_export, \
	(char *) __FILE__, __LINE__, (char *) __func__)

void _put_gsh_export(struct gsh_export *a_export, bool config,
		     char *file, int line, char *function);

#define put_gsh_export(a_export) \
	_put_gsh_export(a_export, false, \
	(char *) __FILE__, __LINE__, (char *) __func__)

#define put_gsh_export_config(a_export) \
	_put_gsh_export(a_export, true, \
	(char *) __FILE__, __LINE__, (char *) __func__)

void export_revert(struct gsh_export *a_export);
void export_add_to_mount_work(struct gsh_export *a_export);
void export_add_to_unexport_work_locked(struct gsh_export *a_export);
void export_add_to_unexport_work(struct gsh_export *a_export);
struct gsh_export *export_take_mount_work(void);

extern struct config_block add_export_param;
extern struct config_block update_export_param;

void prune_defunct_exports(uint64_t generation);
void remove_all_exports(void);

extern struct timespec nfs_stats_time;
void nfs_init_stats_time(void);
#endif				/* !EXPORT_MGR_H */
/** @} */
