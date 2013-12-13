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

#include "nlm_list.h"

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
	/** gsh_exports are kept in an AVL tree by export_id */
	struct avltree_node node_k;
	/** The list of cache inode entries belonging to this export */
	struct glist_head entry_list;
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
};

void export_pkginit(void);
#ifdef USE_DBUS_STATS
void dbus_export_init(void);
#endif
struct gsh_export *get_gsh_export(int export_id, bool lookup_only);
struct gsh_export *get_gsh_export_by_path(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_path_locked(char *path,
						 bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo(char *path, bool exact_match);
struct gsh_export *get_gsh_export_by_pseudo_locked(char *path,
						   bool exact_match);
struct gsh_export *get_gsh_export_by_tag(char *tag);
void set_gsh_export_state(struct gsh_export *export, export_state_t state);
void put_gsh_export(struct gsh_export *export);
bool remove_gsh_export(int export_id);
bool foreach_gsh_export(bool(*cb) (struct gsh_export *exp, void *state),
			void *state);

static inline void get_gsh_export_ref(struct gsh_export *exp)
{
	atomic_inc_int64_t(&exp->refcnt);
}

#endif				/* !EXPORT_MGR_H */
/** @} */
