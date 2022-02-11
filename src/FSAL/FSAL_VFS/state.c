// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS Inc., 2015
 * Author: Daniel Gryniewicz dang@cohortfs.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* state.c
 * VFS state management
 */

#include "config.h"

#include <string.h>
#include <sys/types.h>
#include "gsh_types.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "avltree.h"
#include "vfs_methods.h"

struct vfs_state_entry {
	struct gsh_buffdesc	fs_key;		/**< Key for tree */
	struct avltree_node	fs_node;	/**< AVL tree node */
	struct state_hdl	ostate;		/**< Actual file state */
};

static struct avltree vfs_state_tree = {0};

/**
 * @brief VFS state comparator for AVL tree walk
 *
 */
static int vfs_state_cmpf(const struct avltree_node *lhs,
			  const struct avltree_node *rhs)
{
	struct vfs_state_entry *lk, *rk;

	lk = avltree_container_of(lhs, struct vfs_state_entry, fs_node);
	rk = avltree_container_of(rhs, struct vfs_state_entry, fs_node);
	if (lk->fs_key.len != rk->fs_key.len)
		return (lk->fs_key.len < rk->fs_key.len) ? -1 : 1;

	return memcmp(lk->fs_key.addr, rk->fs_key.addr, lk->fs_key.len);
}

static struct vfs_state_entry *vfs_state_lookup(struct gsh_buffdesc *key)
{
	struct vfs_state_entry key_entry;
	struct avltree_node *node;

	memset(&key_entry, 0, sizeof(key_entry));
	key_entry.fs_key = *key;
	node = avltree_lookup(&key_entry.fs_node, &vfs_state_tree);
	if (!node)
		return NULL;

	return avltree_container_of(node, struct vfs_state_entry, fs_node);
}

void vfs_state_init(void)
{
	if (vfs_state_tree.cmp_fn == NULL)
		avltree_init(&vfs_state_tree, vfs_state_cmpf, 0);
}

void vfs_state_release(struct gsh_buffdesc *key)
{
	struct vfs_state_entry *fs_entry;

	fs_entry = vfs_state_lookup(key);
	if (!fs_entry)
		return;

	avltree_remove(&fs_entry->fs_node, &vfs_state_tree);
	gsh_free(fs_entry);
}

struct state_hdl *vfs_state_locate(struct fsal_obj_handle *obj)
{
	struct vfs_state_entry *fs_entry;
	struct avltree_node *node;
	struct gsh_buffdesc key;

	obj->obj_ops->handle_to_key(obj, &key);

	fs_entry = vfs_state_lookup(&key);
	if (fs_entry) {
		fs_entry->ostate.file.obj = obj;
		return &fs_entry->ostate;
	}

	fs_entry = gsh_calloc(sizeof(struct vfs_state_entry), 1);
	fs_entry->fs_key = key;
	node = avltree_insert(&fs_entry->fs_node, &vfs_state_tree);
	if (unlikely(node)) {
		/* Race won */
		gsh_free(fs_entry);
		fs_entry = avltree_container_of(node, struct vfs_state_entry,
						fs_node);
	} else {
		state_hdl_init(&fs_entry->ostate, obj->type, obj);
	}

	/* Always update with current handle pointer */
	fs_entry->ostate.file.obj = obj;

	return &fs_entry->ostate;
}
