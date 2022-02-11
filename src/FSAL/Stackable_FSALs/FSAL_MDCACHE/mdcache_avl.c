// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file mdcache_avl.c
 * @brief AVL tree for caching directory entries
 */

#include "config.h"

#include "log.h"
#include "fsal.h"
#include "mdcache_int.h"
#include "mdcache_avl.h"
#include "mdcache_lru.h"
#include "murmur3.h"
#include "city.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

void
mdcache_avl_init(mdcache_entry_t *entry)
{
	avltree_init(&entry->fsobj.fsdir.avl.t, avl_dirent_name_cmpf,
		     0 /* flags */);
	avltree_init(&entry->fsobj.fsdir.avl.ck, avl_dirent_ck_cmpf,
		     0 /* flags */);
	avltree_init(&entry->fsobj.fsdir.avl.sorted, avl_dirent_sorted_cmpf,
		     0 /* flags */);
}

static inline struct avltree_node *
avltree_inline_lookup_hk(const struct avltree_node *key,
			 const struct avltree *tree)
{
	return avltree_inline_lookup(key, tree, avl_dirent_name_cmpf);
}

void
avl_dirent_set_deleted(mdcache_entry_t *entry, mdcache_dir_entry_t *v)
{
	struct avltree_node *node;
	mdcache_dir_entry_t *next;

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Delete dir entry %p %s",
			v, v->name);

#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif
	assert(!(v->flags & DIR_ENTRY_FLAG_DELETED));

	node = avltree_inline_lookup_hk(&v->node_name,
					&entry->fsobj.fsdir.avl.t);
	assert(node);
	avltree_remove(&v->node_name, &entry->fsobj.fsdir.avl.t);

	v->flags |= DIR_ENTRY_FLAG_DELETED;
	mdcache_key_delete(&v->ckey);

	/* Do stuff if chunked... */
	if (v->chunk != NULL) {
		struct dir_chunk *chunk = v->chunk;
		mdcache_entry_t *parent = chunk->parent;

		if (v->ck == parent->fsobj.fsdir.first_ck) {
			/* This is no longer the first entry in the directory...
			 * Find the first non-deleted entry.
			 */
			next = v;
			while (next != NULL &&
			       next->flags & DIR_ENTRY_FLAG_DELETED) {
				next =  glist_next_entry(&chunk->dirents,
							 mdcache_dir_entry_t,
							 chunk_list,
							 &next->chunk_list);

				if (next != NULL) {
					/* Evaluate it in the while condition.
					 */
					continue;
				}

				/* End of the chunk... */

				/** @todo FSF This entire chunk is deleted
				 *        entries, we really should just free
				 *        the chunk...
				 */


				/* Look for the next chunk */
				if (chunk->next_ck != 0 &&
				    mdcache_avl_lookup_ck(parent,
							  chunk->next_ck,
							  &next)) {
					chunk = next->chunk;
					/* We don't need the ref, we have the
					 * content lock */
					mdcache_lru_unref_chunk(chunk);
				}
			}

			if (next != NULL) {
				/* This entry is now the first_ck. */
				parent->fsobj.fsdir.first_ck = next->ck;
			} else {
				/* There are no more cached chunks */
				parent->fsobj.fsdir.first_ck = 0;
			}
		}

		/* For now leave the entry in the ck hash so we can re-start
		 * directory from that position, this means that directory
		 * enumeration will have to skip deleted entries.
		 */
	} else {
		mdcache_avl_remove(entry, v);
	}
}

/**
 * @brief Remove a dirent from a chunk.
 *
 * @param[in] dirent    The dirent to remove
 *
 */
void unchunk_dirent(mdcache_dir_entry_t *dirent)
{
	mdcache_entry_t *parent = dirent->chunk->parent;

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Unchunking %p %s",
			dirent, dirent->name);

#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif

	/* Dirent is part of a chunk, must do additional clean
	 * up.
	 */

	/* Remove from chunk */
	glist_del(&dirent->chunk_list);

	/* Remove from FSAL cookie AVL tree */
	avltree_remove(&dirent->node_ck, &parent->fsobj.fsdir.avl.ck);

	/* Check if this was the first dirent in the directory. */
	if (parent->fsobj.fsdir.first_ck == dirent->ck) {
		/* The first dirent in the directory is no longer chunked... */
		parent->fsobj.fsdir.first_ck = 0;
	}

	/* Check if this entry was in the sorted AVL tree */
	if (dirent->flags & DIR_ENTRY_SORTED) {
		/* It was, remove it. */
		avltree_remove(&dirent->node_sorted,
			       &parent->fsobj.fsdir.avl.sorted);
	}

	/* Just make sure... */
	dirent->chunk = NULL;
}

/**
 * @brief Remove and free a dirent.
 *
 * @note parent content_lock MUST be held for write
 *
 * @param[in] parent    The directory removing from
 * @param[in] dirent    The dirent to remove
 *
 */
void mdcache_avl_remove(mdcache_entry_t *parent,
			mdcache_dir_entry_t *dirent)
{
	struct dir_chunk *chunk = dirent->chunk;

	if ((dirent->flags & DIR_ENTRY_FLAG_DELETED) == 0) {
		/* Remove from active names tree */
		avltree_remove(&dirent->node_name, &parent->fsobj.fsdir.avl.t);
	}

	if (dirent->entry) {
		/* We have a ref'd entry, drop our ref */
		mdcache_put(dirent->entry);
		dirent->entry = NULL;
	}

	if (dirent->chunk != NULL) {
		/* Dirent belongs to a chunk so remove it from the chunk. */
		unchunk_dirent(dirent);
	} else {
		/* The dirent might be a detached dirent on an LRU list */
		rmv_detached_dirent(parent, dirent);
	}

	if (dirent->ckey.kv.len)
		mdcache_key_delete(&dirent->ckey);

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Just freed dirent %p from chunk %p parent %p",
			dirent, chunk, (chunk) ? chunk->parent : NULL);

	gsh_free(dirent);
}

/**
 * @brief Insert a dirent into the lookup by FSAL cookie AVL tree.
 *
 * @param[in] entry The directory
 * @param[in] v     The dirent
 *
 * @retval 0  Success
 * @retval -1 Failure
 *
 */
int mdcache_avl_insert_ck(mdcache_entry_t *entry, mdcache_dir_entry_t *v)
{
	struct avltree_node *node;

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Insert dirent %p for %s on entry=%p FSAL cookie=%"
			PRIx64,
			v, v->name, entry, v->ck);

#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif
	node = avltree_inline_insert(&v->node_ck, &entry->fsobj.fsdir.avl.ck,
				     avl_dirent_ck_cmpf);

	if (!node) {
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "inserted dirent %p for %s on entry=%p FSAL cookie=%"
			    PRIx64,
			    v, v->name, entry, v->ck);
		return 0;
	}

	/* already inserted */
	LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
		"Already existent when inserting dirent %p for %s on entry=%p FSAL cookie=%"
		PRIx64
		", duplicated directory cookies make READDIR unreliable.",
		v, v->name, entry, v->ck);
	return -1;
}

#define MIN_COOKIE_VAL 3

/*
 * Insert into avl tree using key combination of hash of name with strcmp
 * of name to disambiguate hash collision.
 *
 * In the case of a name collision, assuming the ckey in the dirents matches,
 * and the flags are the same,  then this will be treated as a success and the
 * dirent passed in will be freed and the dirent will be set to the found one.
 *
 * If any error occurs, the passed in dirent will be freed and the dirent
 * will be set to NULL.
 *
 * @param[in] entry  The directory
 * @param[in] dirent The dirent
 *
 * @retval 0   Success
 * @retval -3  Duplicate file name but different cookie
 * @retval -4  FSAL cookie collision
 *
 **/
int
mdcache_avl_insert(mdcache_entry_t *entry, mdcache_dir_entry_t **dirent)
{
	mdcache_dir_entry_t *v = *dirent, *v2;
#if AVL_HASH_MURMUR3
	uint32_t hk[4];
#endif
	struct avltree_node *node;
	int code;

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Insert dir entry %p %s",
			v, v->name);
#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif

	/* compute hash */
#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(v->name, strlen(v->name), 67, hk);
	memcpy(&v->namehash, hk, 8);
#else
	v->namehash = CityHash64WithSeed(v->name, strlen(v->name), 67);
#endif

again:

	node = avltree_insert(&v->node_name, &entry->fsobj.fsdir.avl.t);

	if (!node) {
		/* success */
		if (v->chunk != NULL) {
			/* This directory entry is part of a chunked directory
			 * enter it into the "by FSAL cookie" avl also.
			 */
			if (mdcache_avl_insert_ck(entry, v) < 0) {
				/* We failed to insert into FSAL cookie
				 * AVL tree, remove from lookup by name
				 * AVL tree.
				 */
				avltree_remove(&v->node_name,
					       &entry->fsobj.fsdir.avl.t);
				v2 = NULL;
				code = -4;
				goto out;
			}
		}

		if (isFullDebug(COMPONENT_CACHE_INODE) ||
		    isFullDebug(COMPONENT_NFS_READDIR)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			(void) display_mdcache_key(&dspbuf, &v->ckey);

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Inserted dirent %s with ckey %s",
					v->name, str);
		}

		return 0;
	}

	/* Deal with name collision. */
	v2 = avltree_container_of(node, mdcache_dir_entry_t, node_name);

	/* Same name, probably already inserted. */
	LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
		    "Already existent when inserting new dirent on entry=%p name=%s",
		    entry, v->name);

	if (mdcache_key_cmp(&v->ckey, &v2->ckey) != 0) {
		/* The two names don't seem to have the same object
		 * handle digest. Discard the old dirent and try again.
		 */
		if (isFullDebug(COMPONENT_CACHE_INODE) ||
		    isFullDebug(COMPONENT_NFS_READDIR)) {
			char str1[LOG_BUFF_LEN / 2] = "\0";
			char str2[LOG_BUFF_LEN / 2] = "\0";
			struct display_buffer dspbuf1 = {
					sizeof(str1), str1, str1 };
			struct display_buffer dspbuf2 = {
					sizeof(str2), str2, str2 };

			(void) display_mdcache_key(&dspbuf1, &v->ckey);
			(void) display_mdcache_key(&dspbuf2, &v2->ckey);

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Keys for %s don't match v=%s v2=%s",
					v->name, str1, str2);
		}

		/* Remove the found dirent. */
		mdcache_avl_remove(entry, v2);
		v2 = NULL;
		goto again;
	}

	/* The v2 entry should NOT be deleted... */
	assert((v2->flags & DIR_ENTRY_FLAG_DELETED) == 0);

	if (v->chunk != NULL && v2->chunk == NULL) {
		/* This directory entry is part of a chunked directory enter the
		 * old dirent into the "by FSAL cookie" AVL tree also.
		 * We need to update the old dirent for the FSAL cookie
		 * bits...
		 */
		v2->chunk = v->chunk;
		v2->ck = v->ck;
		v2->eod = v->eod;

		if (mdcache_avl_insert_ck(entry, v2) < 0) {
			/* We failed to insert into FSAL cookie AVL
			 * tree, leave in lookup by name AVL tree but
			 * don't return a dirent. Also, undo the changes
			 * to the old dirent.
			 */
			v2->chunk = NULL;
			v2->ck = 0;
			v2 = NULL;
			code = -4;
		} else {
			if (isFullDebug(COMPONENT_CACHE_INODE) ||
			    isFullDebug(COMPONENT_NFS_READDIR)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
						sizeof(str), str, str };

				(void) display_mdcache_key(&dspbuf, &v2->ckey);

				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Updated dirent %p with ck=%"
						PRIx64
						" and chunk %p eod=%s ckey=%s",
						v2, v2->ck, v2->chunk,
						v2->eod ? "true" : "false",
						str);
			}

			/* Remove v2 from the detached entry cache */
			rmv_detached_dirent(entry, v2);
			code = 0;
		}
	} else if (v->chunk != NULL && v2->chunk != NULL) {
		/* Handle cases where existing entry is in a chunk as
		 * well as previous entry. Somehow an entry is showing
		 * up twice. Will prefer existing entry.
		 */
		if (v->ck == v2->ck) {
			/* completely a duplicate entry, ignore it */
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "Duplicate filename %s insert into chunk %p, existing was in chunk %p, ignoring",
				    v->name, v->chunk, v2->chunk);
			code = 0;
		} else {
			/* This is an odd case, lets treat it as an
			 * error.
			 */
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "Duplicate filename %s with different cookies ckey %"
				    PRIx64
				    " chunk %p don't match existing ckey %"
				    PRIx64" chunk %p",
				    v->name, v->ck, v->chunk,
				    v2->ck, v2->chunk);
			code = -3;
			v2 = NULL;
		}
	} else {
		/* New entry is not in a chunk, existing entry might
		 * be in a chunk, in any case, the entry already
		 * exists so we are good.
		 */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Duplicate insert of %s v->chunk=%p v2->chunk=%p",
				v->name, v->chunk, v2->chunk);
		code = 0;
	}

out:

	mdcache_key_delete(&v->ckey);
	gsh_free(v);
	*dirent = v2;

	return code;
}

/**
 * @brief Look up a dirent by FSAL cookie
 *
 * Look up a dirent by FSAL cookie.
 *
 * @note this takes a ref on the chunk containing @a dirent
 *
 * @param[in] entry	Directory to search in
 * @param[in] ck	FSAL cookie to find
 * @param[out] dirent	Returned dirent, if found, NULL otherwise
 *
 * @retval true if found
 * @retval false if not found
 */
bool mdcache_avl_lookup_ck(mdcache_entry_t *entry, uint64_t ck,
			   mdcache_dir_entry_t **dirent)
{
	struct avltree *tck = &entry->fsobj.fsdir.avl.ck;
	mdcache_dir_entry_t dirent_key[1];
	mdcache_dir_entry_t *ent;
	struct avltree_node *node;

	*dirent = NULL;
	dirent_key->ck = ck;

	node = avltree_inline_lookup(&dirent_key->node_ck, tck,
				     avl_dirent_ck_cmpf);

	if (node) {
		struct dir_chunk *chunk;
		/* This is the entry we are looking for... This function is
		 * passed the cookie of the next entry of interest in the
		 * directory.
		 */
		ent = avltree_container_of(node, mdcache_dir_entry_t, node_ck);
		chunk = ent->chunk;
		if (chunk == NULL) {
			/* This entry doesn't belong to a chunk, something
			 * is horribly wrong.
			 */
			assert(chunk);
			return false;
		}
		mdcache_lru_ref_chunk(chunk);
		*dirent = ent;
		return true;
	}

	return false;
}

mdcache_dir_entry_t *mdcache_avl_lookup(mdcache_entry_t *entry,
					const char *name)
{
	struct avltree_node *node;
	mdcache_dir_entry_t *v2;
	mdcache_dir_entry_t v;
#if AVL_HASH_MURMUR3
	uint32_t hashbuff[4];
#endif
	size_t namelen = strlen(name);

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Lookup %s", name);

#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(name, namelen, 67, hashbuff);
	/* This seems to be correct.  The avltree_lookup function looks
	   as hk.k, but does no namecmp on its own, so there's no need to
	   allocate space for or copy the name in the key. */
	memcpy(&v.namehash, hashbuff, 8);
#else
	v.namehash = CityHash64WithSeed(name, namelen, 67);
#endif
	v.name = name;

	node = avltree_lookup(&v.node_name, &entry->fsobj.fsdir.avl.t);

	if (node) {
		/* return dirent */
		v2 = avltree_container_of(node, mdcache_dir_entry_t, node_name);
		assert(!(v2->flags & DIR_ENTRY_FLAG_DELETED));
		return v2;
	}

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"entry not found %s", name);

	return NULL;
}

/**
 * @brief Remove and free all dirents from the dirent trees for a directory
 *
 * @param[in] parent    The directory removing from
 */
void mdcache_avl_clean_trees(mdcache_entry_t *parent)
{
	struct avltree_node *dirent_node;
	mdcache_dir_entry_t *dirent;

#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif

	while ((dirent_node = avltree_first(&parent->fsobj.fsdir.avl.t))) {
		dirent = avltree_container_of(dirent_node, mdcache_dir_entry_t,
					      node_name);
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Invalidate %p %s", dirent, dirent->name);

		mdcache_avl_remove(parent, dirent);
	}
}

/** @} */
