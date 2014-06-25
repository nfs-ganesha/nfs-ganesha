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
	cache_entry_t *exp_root_cache_inode;
	/** Allowed clients */
	struct glist_head clients;
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
	/** Max Read for this entry */
	uint64_t MaxRead;
	/** Max Write for this entry */
	uint64_t MaxWrite;
	/** Preferred Read size */
	uint64_t PrefRead;
	/** Preferred Write size */
	uint64_t PrefWrite;
	/** Preferred Readdir size */
	uint64_t PrefReaddir;
	/** Maximum Offset allowed for write */
	uint64_t MaxOffsetWrite;
	/** Maximum Offset allowed for read */
	uint64_t MaxOffsetRead;
	/** Filesystem ID for overriding fsid from FSAL*/
	fsal_fsid_t filesystem_id;
	/** References to this export */
	int64_t refcnt;
	/** Read/Write lock protecting export */
	pthread_rwlock_t lock;
	/** available mount options */
	struct export_perms export_perms;
	/** The last time the export stats were updated */
	nsecs_elapsed_t last_update;
	/** The condition the export is in */
	export_state_t state;
	/** Export non-permission options */
	uint32_t options;
	/** Export non-permission options set */
	uint32_t options_set;
	/** Expiration time interval in seconds for attributes.  Settable with
	    Attr_Expiration_Time. */
	int32_t expire_time_attr;
	/** Export_Id for this export */
	uint16_t export_id;
};

void export_pkginit(void);
#ifdef USE_DBUS
void dbus_export_init(void);
#endif
struct gsh_export *alloc_export(void);
void free_export(struct gsh_export *export);
bool insert_gsh_export(struct gsh_export *export);
struct gsh_export *get_gsh_export(uint16_t export_id);
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
void remove_gsh_export(uint16_t export_id);
bool foreach_gsh_export(bool(*cb) (struct gsh_export *exp, void *state),
			void *state);

static inline void get_gsh_export_ref(struct gsh_export *exp)
{
	atomic_inc_int64_t(&exp->refcnt);
}

void export_add_to_mount_work(struct gsh_export *export);
void export_add_to_unexport_work_locked(struct gsh_export *export);
void export_add_to_unexport_work(struct gsh_export *export);
struct gsh_export *export_take_mount_work(void);
struct gsh_export *export_take_unexport_work(void);

extern struct config_block add_export_param;

void remove_all_exports(void);

#endif				/* !EXPORT_MGR_H */
/** @} */
