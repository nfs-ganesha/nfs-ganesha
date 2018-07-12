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
	/** CFG Allowed clients - update protected by lock */
	struct glist_head clients;
	/** Entry for the junction of this export.  Protected by lock */
	struct fsal_obj_handle *exp_junction_obj;
	/** The export this export sits on. Protected by lock */
	struct gsh_export *exp_parent_exp;
	/** Pointer to the fsal_export associated with this export */
	struct fsal_export *fsal_export;
	/** CFG: Exported path - static option */
	char *fullpath;
	/** CFG: PseudoFS path for export - static option */
	char *pseudopath;
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
	nsecs_elapsed_t last_update;
	/** CFG: Export non-permission options - atomic changeable option */
	uint32_t options;
	/** CFG: Export non-permission options set - atomic changeable option */
	uint32_t options_set;
	/** CFG: Export_Id for this export - static option */
	uint16_t export_id;

	uint8_t export_status;		/*< current condition */
	bool has_pnfs_ds;		/*< id_servers matches export_id */
};

/* Use macro to define this to get around include file order. */
#define export_path(export) \
	((nfs_param.core_param.mount_path_pseudo) \
		? ((export)->pseudopath) \
		: ((export)->fullpath))

/* If op_ctx request is NFS_V4 always use pseudopath, otherwise use fullpath
 * for export.
 */
#define op_ctx_export_path(export) \
	((op_ctx->nfs_vers == NFS_V4) || \
	 (nfs_param.core_param.mount_path_pseudo) \
		? ((export)->pseudopath) \
		: ((export)->fullpath))

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
void free_export(struct gsh_export *a_export);
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

void _put_gsh_export(struct gsh_export *a_export,
		     char *file, int line, char *function);

#define put_gsh_export(a_export) \
	_put_gsh_export(a_export, \
	(char *) __FILE__, __LINE__, (char *) __func__)

void export_cleanup(struct gsh_export *a_export);
void export_revert(struct gsh_export *a_export);
void export_add_to_mount_work(struct gsh_export *a_export);
void export_add_to_unexport_work_locked(struct gsh_export *a_export);
void export_add_to_unexport_work(struct gsh_export *a_export);
struct gsh_export *export_take_mount_work(void);
struct gsh_export *export_take_unexport_work(void);

extern struct config_block add_export_param;
extern struct config_block update_export_param;

void remove_all_exports(void);

extern struct timespec nfs_stats_time;
#endif				/* !EXPORT_MGR_H */
/** @} */
