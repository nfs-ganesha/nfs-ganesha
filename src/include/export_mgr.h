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

#include "ganesha_list.h"
#include "cache_inode.h"

#ifndef EXPORT_MGR_H
#define EXPORT_MGR_H

typedef enum export_state {
	EXPORT_INIT = 0,	/*< still being initialized */
	EXPORT_READY,		/*< searchable, usable */
	EXPORT_BLOCKED,		/*< not available for search */
	EXPORT_RELEASE		/*< No references, ready for reaping */
} export_state_t;

/**
 * @brief Represents an export.
 *
 */

struct gsh_export {
	/** List of all exports */
	struct glist_head exp_list;
	/** gsh_exports are kept in an AVL tree by export_id */
	struct avltree_node node_k;
	/** The list of cache inode entries belonging to this export */
	struct glist_head entry_list;
	/** Entry for the root of this export, protected by lock */
	cache_entry_t *exp_root_cache_inode;
	/** Entry for the junction of this export.  Protected by lock */
	cache_entry_t *exp_junction_inode;
	/** The export this export sits on. Protected by lock */
	struct gsh_export *exp_parent_exp;
	/** Pointer to the fsal_export associated with this export */
	struct fsal_export *fsal_export;
	/** Exported path */
	char *fullpath;
	/** PseudoFS path for export */
	char *pseudopath;
	/** Tag for direct NFS v3 mounting of export */
	char *FS_tag;
	/** Node id this is mounted on. Protected by lock */
	uint64_t exp_mounted_on_file_id;
	/** Read/Write lock protecting export */
	pthread_rwlock_t lock;
	/** References to this export */
	int64_t refcnt;
	/** The NFS server definition of the export */
	exportlist_t export;
	/** The last time the export stats were updated */
	nsecs_elapsed_t last_update;
	/** The condition the export is in */
	export_state_t state;
	/** Export_Id for this export */
	uint32_t export_id;
};

static inline void export_readlock(struct exportlist *export)
{
	struct gsh_export *exp;

	exp = container_of(export, struct gsh_export, export);
	pthread_rwlock_rdlock(&exp->lock);
}

static inline void export_writelock(struct exportlist *export)
{
	struct gsh_export *exp;

	exp = container_of(export, struct gsh_export, export);
	pthread_rwlock_wrlock(&exp->lock);
}

static inline void export_rwunlock(struct exportlist *export)
{
	struct gsh_export *exp;

	exp = container_of(export, struct gsh_export, export);
	pthread_rwlock_unlock(&exp->lock);
}

void export_pkginit(void);
#ifdef USE_DBUS
void dbus_export_init(void);
#endif
struct gsh_export *alloc_export(void);
void free_export(struct gsh_export *export);
bool insert_gsh_export(struct gsh_export *export);
struct gsh_export *get_gsh_export(int export_id);
struct gsh_export *get_gsh_export_by_path(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_path_locked(char *path,
						 bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo_locked(char *path,
						   bool exact_match);
struct gsh_export *get_gsh_export_by_tag(char *tag);
bool mount_gsh_export(struct gsh_export *exp);
void set_gsh_export_state(struct gsh_export *export, export_state_t state);
void put_gsh_export(struct gsh_export *export);
void remove_gsh_export(int export_id);
bool foreach_gsh_export(bool(*cb) (struct gsh_export *exp, void *state),
			void *state);

static inline void get_gsh_export_ref(struct gsh_export *exp)
{
	atomic_inc_int64_t(&exp->refcnt);
}

extern struct config_block add_export_param;

#endif				/* !EXPORT_MGR_H */
/** @} */
