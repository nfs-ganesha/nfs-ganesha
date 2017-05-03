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
	avltree_init(&entry->fsobj.fsdir.avl.t, avl_dirent_hk_cmpf,
		     0 /* flags */);
	avltree_init(&entry->fsobj.fsdir.avl.c, avl_dirent_hk_cmpf,
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
	return avltree_inline_lookup(key, tree, avl_dirent_hk_cmpf);
}

void
avl_dirent_set_deleted(mdcache_entry_t *entry, mdcache_dir_entry_t *v)
{
	struct avltree_node *node;
	mdcache_dir_entry_t *next;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Delete dir entry %p %s",
		     v, v->name);

	assert(!(v->flags & DIR_ENTRY_FLAG_DELETED));

	node = avltree_inline_lookup_hk(&v->node_hk, &entry->fsobj.fsdir.avl.t);
	assert(node);
	avltree_remove(&v->node_hk, &entry->fsobj.fsdir.avl.t);

	v->flags |= DIR_ENTRY_FLAG_DELETED;
	mdcache_key_delete(&v->ckey);

	/* save cookie in deleted avl */
	node = avltree_insert(&v->node_hk, &entry->fsobj.fsdir.avl.c);
	assert(!node);

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

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Unchunking %p %s",
		     dirent, dirent->name);

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
 * @param[in] dirent    The dirent to remove
 * @param[in] t         The AVL tree to remove it from
 *
 */
void mdcache_avl_remove(mdcache_dir_entry_t *dirent, struct avltree *t)
{
	avltree_remove(&dirent->node_hk, t);

	if (dirent->chunk != NULL)
		unchunk_dirent(dirent);

	if (dirent->ckey.kv.len)
		mdcache_key_delete(&dirent->ckey);

	gsh_free(dirent);
}

/**
 * @brief Insert a dirent into the lookup by name AVL tree.
 *
 * @param[in] entry     The directory
 * @param[in] v         The dirent
 * @param[in] j         Part of iteration count
 * @param[in] j2        Part of iterarion count
 * @param[in,out] vout  The existing entry when there is name collision
 *
 * @retval 0  Success
 * @retval -1 Failure
 * @retval -2 Name collision
 *
 */
static inline int
mdcache_avl_insert_impl(mdcache_entry_t *entry, mdcache_dir_entry_t *v,
			int j, int j2, mdcache_dir_entry_t **vout)
{
	int code = -1;
	struct avltree_node *node;
	mdcache_dir_entry_t *v2;
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree *c = &entry->fsobj.fsdir.avl.c;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Insert dir entry %p %s j=%d j2=%d",
		     v, v->name, j, j2);

	/* first check for a previously-deleted entry */
	node = avltree_inline_lookup_hk(&v->node_hk, c);

	/* We must not allow persist-cookies to overrun resource
	 * management processes.  Note this is not a limit on
	 * directory size, but rather indirectly on the lifetime
	 * of cookie offsets of directories under mutation. */
	if ((!node) && (avltree_size(c) >
			mdcache_param.dir.avl_max_deleted)) {
		/* ie, recycle the smallest deleted entry */
		node = avltree_first(c);
	}

	if (node) {
		/* We can't really re-use slots safely since filenames can
		 * have wildly differing lengths, so remove and free the
		 * "deleted" entry.
		 */
		mdcache_avl_remove(avltree_container_of(node,
							mdcache_dir_entry_t,
							node_hk),
				   c);
		node = NULL;
	}

	node = avltree_insert(&v->node_hk, t);

	if (!node) {
		/* success, note iterations */
		v->hk.p = j + j2;
		if (entry->fsobj.fsdir.avl.collisions < v->hk.p)
			entry->fsobj.fsdir.avl.collisions = v->hk.p;

		LogDebug(COMPONENT_CACHE_INODE,
			 "inserted new dirent for %s on entry=%p cookie=%"
			 PRIu64 " collisions %d", v->name, entry, v->hk.k,
			 entry->fsobj.fsdir.avl.collisions);
		code = 0;
	} else {
		v2 = avltree_container_of(node, mdcache_dir_entry_t, node_hk);
		if (strcmp(v->name, v2->name) == 0) {
			/* Same name, probably already inserted. */
			LogDebug(COMPONENT_CACHE_INODE,
				 "Already existent when inserting new dirent on entry=%p name=%s, cookie=%"
				 PRIu64,
				 entry, v->name, v->hk.k);
			code = -2;
			if (vout != NULL)
				*vout = v2;
		} else {
			/* Hash collision, keep trying at current j, j2 */
			LogDebug(COMPONENT_CACHE_INODE,
				 "Hash collision with %s when inserting new dirent on entry=%p name=%s, cookie=%"
				 PRIu64 " this should never happen.",
				 v2->name, entry, v->name, v->hk.k);
			code = -1;
		}
	}
	return code;
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

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Insert dirent %p for %s on entry=%p FSAL cookie=%"PRIx64,
		     v, v->name, entry, v->ck);

	node = avltree_inline_insert(&v->node_ck, &entry->fsobj.fsdir.avl.ck,
				     avl_dirent_ck_cmpf);

	if (!node) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "inserted dirent %p for %s on entry=%p FSAL cookie=%"
			 PRIx64,
			 v, v->name, entry, v->ck);
		return 0;
	}

	/* already inserted */
	LogWarn(COMPONENT_CACHE_INODE,
		"Already existent when inserting dirent %p for %s on entry=%p FSAL cookie=%"
		PRIx64
		", duplicated directory cookies make READDIR unreliable.",
		v, v->name, entry, v->ck);
	return -1;
}

#define MIN_COOKIE_VAL 3

/*
 * Insert with quadatic, linear probing.  A unique k is assured for
 * any k whenever size(t) < max(uint64_t).
 *
 * First try quadratic probing, with coeff. 2 (since m = 2^n.)
 * A unique k is not assured, since the codomain is not prime.
 * If this fails, fall back to linear probing from hk.k+1.
 *
 * On return, the stored key is in v->hk.k, the iteration
 * count in v->hk.p.
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
 * @retval -1  Hash collision after 2^65 attempts
 * @retval -2  Name collision
 *
 **/
int
mdcache_avl_qp_insert(mdcache_entry_t *entry, mdcache_dir_entry_t **dirent)
{
	mdcache_dir_entry_t *v = *dirent, *v2;

#if AVL_HASH_MURMUR3
	uint32_t hk[4];
#endif
	int j, j2, code = -1;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Insert dir entry %p %s",
		     v, v->name);

	/* don't permit illegal cookies */
#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(v->name, strlen(v->name), 67, hk);
	memcpy(&v->hk.k, hk, 8);
#else
	v->hk.k = CityHash64WithSeed(v->name, strlen(v->name), 67);
#endif

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies just kill the sign bit, making
	 * cookies 63 bits... */
	v->hk.k &= ~(1ULL << 63);
#endif

	/* XXX would we really wait for UINT64_MAX?  if not, how many
	 * probes should we attempt? */

	for (j = 0; j < UINT64_MAX; j++) {
		v->hk.k = (v->hk.k + (j * 2));

		/* reject values 0, 1 and 2 */
		if (v->hk.k < MIN_COOKIE_VAL)
			continue;

again:

		code = mdcache_avl_insert_impl(entry, v, j, 0, &v2);
		if (code >= 0) {
			if (v->chunk != NULL) {
				/* This entry is part of a chunked directory
				 * enter it into the "by FSAL cookie" avl also.
				 */
				code = mdcache_avl_insert_ck(entry, v);

				if (code < 0) {
					/* We failed to insert into FSAL cookie
					 * AVL tree, remove from lookup by name
					 * AVL tree.
					 */
					avltree_remove(&v->node_hk,
						       &entry
							   ->fsobj.fsdir.avl.t);
					v2 = NULL;
					goto out;
				}
			}

			if (isFullDebug(COMPONENT_CACHE_INODE)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
							sizeof(str), str, str };

				(void) display_mdcache_key(&dspbuf,
							   &v->ckey);

				LogFullDebug(COMPONENT_CACHE_INODE,
					     "Inserted dirent %s with ckey %s",
					     v->name, str);
			}

			return code;
		} else if (code == -1) {
			/* Deal with hash collision, skip it and try next hash.
			 */
			continue;
		}

		/* Deal with name collision. */
		if (mdcache_key_cmp(&v->ckey, &v2->ckey) != 0) {
			/* The two names don't seem to have the same object
			 * handle digest. Discard the old dirent and try again.
			 */
			if (isFullDebug(COMPONENT_CACHE_INODE)) {
				char str1[LOG_BUFF_LEN / 2] = "\0";
				char str2[LOG_BUFF_LEN / 2] = "\0";
				struct display_buffer dspbuf1 = {
						sizeof(str1), str1, str1 };
				struct display_buffer dspbuf2 = {
						sizeof(str2), str2, str2 };

				(void) display_mdcache_key(&dspbuf1,
							   &v->ckey);
				(void) display_mdcache_key(&dspbuf2,
							   &v2->ckey);

				LogFullDebug(COMPONENT_CACHE_INODE,
					     "Keys for %s don't match v=%s v2=%s",
					     v->name, str1, str2);
			}

			/* Remove the found dirent. */
			mdcache_avl_remove(v2, &entry->fsobj.fsdir.avl.t);
			v2 = NULL;
			goto again;
		}

		/* The v2 entry should NOT be deleted... */
		assert((v2->flags & DIR_ENTRY_FLAG_DELETED) == 0);

		if (v->chunk != NULL && v2->chunk == NULL) {
			/* This entry is part of a chunked directory enter the
			 * old dirent into the "by FSAL cookie" AVL tree also.
			 * We need to update the old dirent for the FSAL cookie
			 * bits...
			 */
			v2->chunk = v->chunk;
			v2->ck = v->ck;
			v2->eod = v->eod;
			code = mdcache_avl_insert_ck(entry, v2);

			if (code < 0) {
				/* We failed to insert into FSAL cookie AVL
				 * tree, leave in lookup by name AVL tree but
				 * don't return a dirent. Also, undo the changes
				 * to the old dirent.
				 */
				v2->chunk = NULL;
				v2->ck = 0;
				v2 = NULL;
			} else {
				if (isFullDebug(COMPONENT_CACHE_INODE)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer dspbuf = {
							sizeof(str), str, str };

					(void) display_mdcache_key(&dspbuf,
								   &v2->ckey);

					LogFullDebug(COMPONENT_CACHE_INODE,
						     "Updated dirent %p with ck=%"
						     PRIx64
						     " and chunk %p eod=%s ckey=%s",
						     v2, v2->ck, v2->chunk,
						     v2->eod ? "true" : "false",
						     str);
				}
			}
		} else if (v->chunk != NULL && v2->chunk != NULL) {
			/* Handle cases where existing entry is in a chunk as
			 * well as previous entry. Somehow an entry is showing
			 * up twice. Will prefer existing entry.
			 */
			if (v->ck == v2->ck) {
				/* This is odd, a completely duplicate entry,
				 * ignore it.
				 */
				LogWarn(COMPONENT_CACHE_INODE,
					"Duplicate filename %s insert into chunk %p, existing was in chunk %p, ignoring",
					v->name, v->chunk, v2->chunk);
				code = 0;
			} else {
				/* This is an odd case, lets treat it as an
				 * error.
				 */
				LogWarn(COMPONENT_CACHE_INODE,
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
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Duplicate insert of %s v->chunk=%p v2->chunk=%p",
				     v->name, v->chunk, v2->chunk);
			code = 0;
		}

		goto out;
	}

	LogCrit(COMPONENT_CACHE_INODE, "could not insert at j=%d (%s)", j,
		v->name);

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies  */
	v->hk.k &= ~(1ULL << 63);
#endif
	for (j2 = 1 /* tried j=0 */; j2 < UINT64_MAX; j2 += 2) {
		v->hk.k = v->hk.k + j2;
		code = mdcache_avl_insert_impl(entry, v, j, j2, NULL);
		if (code >= 0)
			return code;
	}

	LogCrit(COMPONENT_CACHE_INODE,
		"could not insert at j=%d (%s)", j,
		v->name);

	code = -1;
	v2 = NULL;

out:

	mdcache_key_delete(&v->ckey);
	gsh_free(v);
	*dirent = v2;

	return code;
}

/**
 * @brief Look up a dirent by k-value
 *
 * Look up a dirent by k-value.  If @ref MDCACHE_FLAG_NEXT_ACTIVE is set in @a
 * flags then the dirent after the give k-value is returend (this is for
 * readdir).  If @ref MDCACHE_FLAG_ONLY_ACTIVE is set, then only the active tree
 * is searched.  Otherwise, the deleted tree is searched, and, if found, the
 * dirent after that deleted dirnet is returned.
 *
 * @param[in] entry	Directory to search in
 * @param[in] k		K-value to find
 * @param[in] flags	MDCACHE_FLAG_*
 * @param[out] dirent	Returned dirent, if found, NULL otherwise
 * @return MDCACHE_AVL_NO_ERROR if found; MDCACHE_AVL_NOT_FOUND if not found;
 * MDCACHE_AVL_LAST if next requested and found was last; and
 * MDCACHE_AVL_DELETED if all subsequent dirents are deleted.
 */
enum mdcache_avl_err
mdcache_avl_lookup_k(mdcache_entry_t *entry, uint64_t k, uint32_t flags,
		     mdcache_dir_entry_t **dirent)
{
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree *c = &entry->fsobj.fsdir.avl.c;
	mdcache_dir_entry_t dirent_key[1];
	struct avltree_node *node, *node2;

	*dirent = NULL;
	dirent_key->hk.k = k;

	node = avltree_inline_lookup_hk(&dirent_key->node_hk, t);
	if (node) {
		if (flags & MDCACHE_FLAG_NEXT_ACTIVE)
			/* client wants the cookie -after- the last we sent, and
			 * the Linux 3.0 and 3.1.0-rc7 clients misbehave if we
			 * resend the last one */
			node = avltree_next(node);
		if (!node) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "seek to cookie=%" PRIu64
				     " fail (no next entry)", k);
			return MDCACHE_AVL_LAST;
		}
	}

	/* only the forward AVL is valid for conflict checking */
	if (flags & MDCACHE_FLAG_ONLY_ACTIVE)
		goto done;

	/* Try the deleted AVL.  If a node with hk.k == v->hk.k is found,
	 * return its least upper bound in -t-, if any. */
	if (!node) {
		node2 = avltree_inline_lookup_hk(&dirent_key->node_hk, c);
		if (node2) {
			node = avltree_sup(&dirent_key->node_hk, t);
			if (!node)
				return MDCACHE_AVL_DELETED;
		}
		LogDebug(COMPONENT_NFS_READDIR,
			 "node %p found deleted supremum %p", node2, node);
	}

done:
	if (node) {
		*dirent =
		    avltree_container_of(node, mdcache_dir_entry_t, node_hk);
		return MDCACHE_AVL_NO_ERROR;
	}

	return MDCACHE_AVL_NOT_FOUND;
}

/**
 * @brief Look up a dirent by FSAL cookie
 *
 * Look up a dirent by FSAL cookie.
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
			assert(!chunk);
			return false;
		}
		*dirent = ent;
		return true;
	}

	return false;
}

mdcache_dir_entry_t *
mdcache_avl_qp_lookup_s(mdcache_entry_t *entry, const char *name, int maxj)
{
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree_node *node;
	mdcache_dir_entry_t *v2;
#if AVL_HASH_MURMUR3
	uint32_t hashbuff[4];
#endif
	int j;
	size_t namelen = strlen(name);
	mdcache_dir_entry_t v;

	LogFullDebug(COMPONENT_CACHE_INODE, "Lookup %s", name);

#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(name, namelen, 67, hashbuff);
	/* This seems to be correct.  The avltree_lookup function looks
	   as hk.k, but does no namecmp on its own, so there's no need to
	   allocate space for or copy the name in the key. */
	memcpy(&v.hk.k, hashbuff, 8);
#else
	v.hk.k = CityHash64WithSeed(name, namelen, 67);
#endif

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies */
	v.hk.k &= ~(1ULL << 63);
#endif

	for (j = 0; j < maxj; j++) {
		v.hk.k = (v.hk.k + (j * 2));
		node = avltree_lookup(&v.node_hk, t);
		if (node) {
			/* ensure that node is related to v */
			v2 = avltree_container_of(node, mdcache_dir_entry_t,
						  node_hk);
			if (strcmp(name, v2->name) == 0) {
				assert(!(v2->flags & DIR_ENTRY_FLAG_DELETED));
				return v2;
			}
		}
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "entry not found at j=%d (%s)", j,
		     name);

	return NULL;
}

/**
 * @brief Remove and free all dirents from a dirent tree
 *
 * @param[in] tree	Tree to remove from
 */
void mdcache_avl_clean_tree(struct avltree *tree)
{
	struct avltree_node *dirent_node = NULL;
	mdcache_dir_entry_t *dirent = NULL;

	while ((dirent_node = avltree_first(tree))) {
		dirent = avltree_container_of(dirent_node, mdcache_dir_entry_t,
					      node_hk);
		LogFullDebug(COMPONENT_CACHE_INODE, "Invalidate %p %s",
			     dirent, dirent->name);

		mdcache_avl_remove(dirent, tree);
	}
}

/** @} */
