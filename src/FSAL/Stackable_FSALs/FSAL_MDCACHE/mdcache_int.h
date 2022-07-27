/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2019 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @file mdcache_int.h
 * @brief MDCache main internal interface.
 *
 * Main data structures and profiles for MDCache
 */

#ifndef MDCACHE_INT_H
#define MDCACHE_INT_H

#include <stdbool.h>
#include <sys/types.h>

#include "config.h"
#include "mdcache_ext.h"
#include "sal_data.h"
#include "fsal_up.h"
#include "fsal_convert.h"
#include "display.h"

typedef struct mdcache_fsal_obj_handle mdcache_entry_t;

struct mdcache_fsal_module {
	struct fsal_module module;
	struct fsal_obj_ops handle_ops;
};

extern struct mdcache_fsal_module MDCACHE;

#define MDC_UNEXPORT 1

/**
 * @brief Reason an entry is being inserted/looked up
 */
typedef enum {
	MDC_REASON_DEFAULT,	/**< Default insertion */
	MDC_REASON_SCAN		/**< Is being inserted by a scan */
} mdc_reason_t;

typedef struct mdcache_dmap_entry__ {
	/** AVL node in tree by cookie */
	struct avltree_node node;
	/** Entry in LRU */
	struct glist_head lru_entry;
	/** Cookie */
	uint64_t ck;
	/** Name */
	char *name;
	/** Timestamp on entry */
	struct timespec timestamp;
} mdcache_dmap_entry_t;

typedef struct {
	/** Lock protecting this structure */
	pthread_mutex_t mtx;
	/** Mapping of ck -> name for whence-is-name */
	struct avltree map;
	/** LRU of dirent map entries */
	struct glist_head lru;
	/** Count of entries in LRU */
	uint32_t count;
} mdc_dirmap_t;

/*
 * MDCACHE internal export
 */
struct mdcache_fsal_export {
	struct fsal_export mfe_exp;
	char *name;
	/** My up_ops */
	struct fsal_up_vector up_ops;
	/** Higher level up_ops for ops we don't consume */
	struct fsal_up_vector super_up_ops;
	/** The list of cache entries belonging to this export */
	struct glist_head entry_list;
	/** Lock protecting entry_list */
	pthread_rwlock_t mdc_exp_lock;
	/** Flags for the export. */
	uint8_t flags;
	/** Mapping of ck -> name for whence-is-name */
	mdc_dirmap_t dirent_map;
	/** Thread for dirmap processing */
	struct fridgethr *dirmap_fridge;
};

/**
 * @brief Structure representing a cache key.
 *
 * Wraps an underlying FSAL-specific key.
 */
typedef struct mdcache_key {
	uint64_t hk;		/* hash key */
	void *fsal;		/*< sub-FSAL module */
	struct gsh_buffdesc kv;		/*< fsal handle */
} mdcache_key_t;

int display_mdcache_key(struct display_buffer *dspbuf, mdcache_key_t *key);

static inline int mdcache_key_cmp(const struct mdcache_key *k1,
				  const struct mdcache_key *k2)
{
	if (likely(k1->hk < k2->hk))
		return -1;

	if (likely(k1->hk > k2->hk))
		return 1;

	if (unlikely(k1->kv.len < k2->kv.len))
		return -1;

	if (unlikely(k1->kv.len > k2->kv.len))
		return 1;

	if (unlikely(k1->fsal < k2->fsal))
		return -1;

	if (unlikely(k1->fsal > k2->fsal))
		return 1;

	/* deep compare */
	return memcmp(k1->kv.addr,
		      k2->kv.addr,
		      k1->kv.len);
}

/**
 * Data for tracking a cache entry's position the LRU.
 */

/**
 * Valid LRU queues.
 */
enum lru_q_id {
	LRU_ENTRY_NONE = 0, /* entry not queued */
	LRU_ENTRY_L1,
	LRU_ENTRY_L2,
	LRU_ENTRY_CLEANUP
};

#define LRU_CLEANUP 0x00000001 /* Entry is on cleanup queue */
#define LRU_CLEANED 0x00000002 /* Entry has been cleaned */

typedef struct mdcache_lru__ {
	struct glist_head q;	/*< Link in the physical deque
				   implementing a portion of the logical
				   LRU. */
	enum lru_q_id qid;	/*< Queue identifier */
	int32_t refcnt;		/*< Reference count.  This is signed to make
				   mistakes easy to see. */
	uint32_t flags;		/*< Status flags; MUST use atomic ops */
	uint32_t lane;		/*< The lane in which an entry currently
				 *< resides, so we can lock the deque and
				 *< decrement the correct counter when moving
				 *< or deleting the entry. */
	uint32_t cf;		/*< Confounder */
} mdcache_lru_t;

/**
 * MDCACHE statistics.
 */
struct mdcache_stats {
	uint64_t inode_req;
	uint64_t inode_hit;
	uint64_t inode_miss;
	uint64_t inode_conf;
	uint64_t inode_added;
	uint64_t inode_mapping;
};

extern struct mdcache_stats *cache_stp;

/**
 * @brief Represents one of the many-many links between inodes and exports.
 *
 */

struct entry_export_map {
	/** The relevant cache entry */
	mdcache_entry_t *entry;
	/** The export the entry belongs to */
	struct mdcache_fsal_export *exp;
	/** List of entries per export */
	struct glist_head entry_per_export;
	/** List of exports per entry */
	struct glist_head export_per_entry;
};

/**
 * Flags
 */

/** Trust stored attributes */
#define MDCACHE_TRUST_ATTRS FSAL_UP_INVALIDATE_ATTRS
/** Trust stored ACL */
#define MDCACHE_TRUST_ACL FSAL_UP_INVALIDATE_ACL
/** Trust inode content (for the moment, directory and symlink) */
#define MDCACHE_TRUST_CONTENT FSAL_UP_INVALIDATE_CONTENT
/** The directory has been populated (negative lookups are meaningful) */
#define MDCACHE_DIR_POPULATED FSAL_UP_INVALIDATE_DIR_POPULATED
/** The directory chunks are considered valid */
#define MDCACHE_TRUST_DIR_CHUNKS FSAL_UP_INVALIDATE_DIR_CHUNKS
/** The fs_locations are considered valid */
#define MDCACHE_TRUST_FS_LOCATIONS FSAL_UP_INVALIDATE_FS_LOCATIONS
/** The sec_labels are considered valid */
#define MDCACHE_TRUST_SEC_LABEL FSAL_UP_INVALIDATE_SEC_LABEL
/** The entry has been removed, but not unhashed due to state */
static const uint32_t MDCACHE_UNREACHABLE = 0x100;


/**
 * @brief Represents a cached inode
 *
 * Information representing a cached file (inode) including metadata,
 * and for directories and symlinks, pointers to cached content.  This
 * is also the anchor for state held on a file.
 *
 * Regarding the locking discipline:
 * (1) attr_lock protects the attrs field, the export_list, and attr_time
 *
 * (2) content_lock must be held for WRITE when modifying the AVL tree
 *     of a directory or any dirent contained therein.  It must be
 *     held for READ when accessing any of this information.
 *
 * (3) content_lock must be held for WRITE when updating the cached
 *     content of a symlink or when NULLing the object.symlink pointer
 *     preparatory to freeing the link structure.  It must be held for
 *     READ when dereferencing the object.symlink pointer or reading
 *     cached content. XXX dang symlink content is in FSAL now
 *
 * The handle, cache key, and type fields are unprotected, as they are
 * considered to be immutable throughout the life of the object.
 *
 * The flags field is unprotected, however it should be modified only
 * through the functions atomic_set_uint32_t_bits and
 * atomic_clear_uint32_t_bits.
 *
 * The lru field has its own mutex to protect it.
 *
 * The attributes and symlink contents are stored in the handle for
 * api simplicity but these locks apply around their access methods.
 *
 * @note As part of the transition to the new api, the handle was
 * moved out of the union and made a pointer to a separately allocated
 * object.  However, locking applies everywhere except handle object
 * creation time (nobody knows about it yet).  The symlink content
 * cache has moved into the handle as, well.  The mdcache_entry and
 * fsal_obj_handle are two parts of the same thing, a cached inode.
 * mdcache_entry holds the cache stuff and fsal_obj_handle holds the
 * stuff the fsal has to manage, i.e. filesystem bits.
 */

struct mdcache_fsal_obj_handle {
	/** Reader-writer lock for attributes */
	pthread_rwlock_t attr_lock;
	/** MDCache FSAL Handle */
	struct fsal_obj_handle obj_handle;
	/** Sub-FSAL handle */
	struct fsal_obj_handle *sub_handle;
	/** Cached attributes */
	struct fsal_attrlist attrs;
	/** Attribute generation, increased for every write */
	uint32_t attr_generation;
	/** FH hash linkage */
	struct {
		struct avltree_node node_k;	/*< AVL node in tree */
		mdcache_key_t key;	/*< Key of this entry */
		bool inavl;
	} fh_hk;
	/** Flags for this entry */
	uint32_t mde_flags;
	/** Time at which we last refreshed attributes. */
	time_t attr_time;
	/** Time at which we last refreshed acl. */
	time_t acl_time;
	/** Time at which we last refreshed fs locations */
	time_t fs_locations_time;
	/** New style LRU link */
	mdcache_lru_t lru;
	/** Exports per entry (protected by attr_lock) */
	struct glist_head export_list;
	/** ID of the first mapped export for fast path
	 *  This is an int32_t because we need it to be -1 to indicate
	 *  no mapped export.
	 */
	int32_t first_export_id;
	/** Lock on type-specific cached content.  See locking
	    discipline for details. */
	pthread_rwlock_t content_lock;
	/** Filetype specific data, discriminated by the type field.
	    Note that data for special files is in
	    attributes.rawdev */
	union mdcache_fsobj {
		struct state_hdl hdl;
		struct {
			/** List of chunks in this directory, ordered */
			struct glist_head chunks;
			/** List of detached directory entries. */
			struct glist_head detached;
			/** Spin lock to protect the detached list. */
			pthread_spinlock_t spin;
			/** Count of detached directory entries. */
			int detached_count;
			/** @todo FSF
			 *
			 * This is somewhat fragile, however, a reorganization
			 * is possible. If state_lock was to be moved into
			 * state_file and state_dir, and the state code was
			 * made clear which it was working with, dhdl could
			 * be replaced with a state_dir which would be
			 * smaller than state_file, and then the additional
			 * members of fsdir would basically overlay
			 * the larger state_file that hdl is.
			 *
			 * Such a reorg could save memory AND make for a
			 * crisper interface.
			 */
			struct state_hdl dhdl; /**< Storage for dir state */
			/** The parent host-handle of this directory ('..') */
			struct gsh_buffdesc parent;
			/** Time at which we last refreshed parent
			  * host-handle.
			  */
			time_t parent_time;
			/** The first dirent cookie in this directory.
			 *  0 if not known.
			 */
			fsal_cookie_t first_ck;
			struct {
				/** Children by name hash */
				struct avltree t;
				/** Table of dirents by FSAL cookie */
				struct avltree ck;
				/** Table of dirents in sorted order. */
				struct avltree sorted;
				/** Heuristic. Expect 0. */
				uint32_t collisions;
			} avl;
		} fsdir;		/**< DIRECTORY data */
	} fsobj;
};

struct dir_chunk {
	/** This chunk is part of a directory */
	struct glist_head chunks;
	/** List of dirents in this chunk */
	struct glist_head dirents;
	/** Directory this chunk belongs to */
	struct mdcache_fsal_obj_handle *parent;
	/** LRU link */
	mdcache_lru_t chunk_lru;
	/** Cookie to use to reload this chunk */
	fsal_cookie_t reload_ck;
	/** Cookie of first entry in sequentially next chunk, will be set to
	 *  0 if there is no sequentially next chunk.
	 */
	fsal_cookie_t next_ck;
	/** Number of entries in chunk */
	int num_entries;
};

/**
 * @brief Represents a cached directory entry
 *
 * This is a cached directory entry that associates a name and cookie
 * with a cache entry.
 */

#define DIR_ENTRY_FLAG_NONE     0x0000
#define DIR_ENTRY_FLAG_DELETED  0x0001
#define DIR_ENTRY_SORTED        0x0004

typedef struct mdcache_dir_entry__ {
	/** This dirent is part of a chunk */
	struct glist_head chunk_list;
	/** The chunk this entry belongs to */
	struct dir_chunk *chunk;
	/** node in tree by name */
	struct avltree_node node_name;
	/** AVL node in tree by cookie */
	struct avltree_node node_ck;
	/** AVL node in tree by sorted order */
	struct avltree_node node_sorted;
	/** Cookie value from FSAL
	 *  This is the coookie that is the "key" to find THIS entry, however
	 *  a readdir with whence will be looking for the NEXT entry.
	 */
	uint64_t ck;
	/** Indicates if this dirent is the last dirent in a chunked directory.
	 */
	bool eod;
	/** Name Hash */
	uint64_t namehash;
	/** Key of cache entry */
	mdcache_key_t ckey;
	/** Flags
	 * Protected by write content_lock or atomics. */
	uint32_t flags;
	/** Temporary entry pointer
	 * Only valid while the entry is ref'd.  Must be NULL otherwise.
	 * Protected by the parent content_lock */
	mdcache_entry_t *entry;
	const char *name;
	/** The NUL-terminated filename */
	char name_buffer[];
} mdcache_dir_entry_t;

/**
 * @brief Move a detached dirent to MRU position in LRU list.
 *
 * @param[in]     parent  Parent entry
 * @param[in]     dirent  Dirent to move to MRU
 */

static inline void bump_detached_dirent(mdcache_entry_t *parent,
					mdcache_dir_entry_t *dirent)
{
	pthread_spin_lock(&parent->fsobj.fsdir.spin);
	if (glist_first_entry(&parent->fsobj.fsdir.detached,
			      mdcache_dir_entry_t, chunk_list) != dirent) {
		glist_del(&dirent->chunk_list);
		glist_add(&parent->fsobj.fsdir.detached, &dirent->chunk_list);
	}
	pthread_spin_unlock(&parent->fsobj.fsdir.spin);
}

/**
 * @brief Remove a detached dirent from the LRU list.
 *
 * @param[in]     parent  Parent entry
 * @param[in]     dirent  Dirent to remove
 */

static inline void rmv_detached_dirent(mdcache_entry_t *parent,
				       mdcache_dir_entry_t *dirent)
{
	pthread_spin_lock(&parent->fsobj.fsdir.spin);
	/* Note that the dirent might not be on the detached list if it
	 * was being reaped by another thread. All is well here...
	 */
	if (!glist_null(&dirent->chunk_list)) {
		glist_del(&dirent->chunk_list);
		parent->fsobj.fsdir.detached_count--;
	}
	pthread_spin_unlock(&parent->fsobj.fsdir.spin);
}

/* Helpers */
fsal_status_t mdcache_alloc_and_check_handle(
		struct mdcache_fsal_export *exp,
		struct fsal_obj_handle *sub_handle,
		struct fsal_obj_handle **new_obj,
		bool new_directory,
		struct fsal_attrlist *attrs_in,
		struct fsal_attrlist *attrs_out,
		const char *tag,
		mdcache_entry_t *parent,
		const char *name,
		bool *invalidate,
		struct state_t *state);

fsal_status_t mdcache_refresh_attrs(mdcache_entry_t *entry, bool need_acl,
				    bool need_fslocations, bool need_seclabel,
				    bool invalidate);

fsal_status_t mdcache_new_entry(struct mdcache_fsal_export *exp,
				struct fsal_obj_handle *sub_handle,
				struct fsal_attrlist *attrs_in,
				bool prefer_attrs_in,
				struct fsal_attrlist *attrs_out,
				bool new_directory,
				mdcache_entry_t **entry,
				struct state_t *state,
				mdc_reason_t reason);
fsal_status_t mdcache_find_keyed_reason(mdcache_key_t *key,
					mdcache_entry_t **entry,
					mdc_reason_t reason);
#define mdcache_find_keyed(key, entry) mdcache_find_keyed_reason((key), \
					(entry), MDC_REASON_DEFAULT)
fsal_status_t mdcache_locate_host(struct gsh_buffdesc *fh_desc,
				  struct mdcache_fsal_export *exp,
				  mdcache_entry_t **entry,
				  struct fsal_attrlist *attrs_out);
fsal_status_t mdc_try_get_cached(mdcache_entry_t *mdc_parent, const char *name,
				 mdcache_entry_t **entry);
fsal_status_t mdc_lookup(mdcache_entry_t *mdc_parent, const char *name,
			 bool uncached, mdcache_entry_t **new_entry,
			 struct fsal_attrlist *attrs_out);
fsal_status_t mdc_lookup_uncached(mdcache_entry_t *mdc_parent,
				  const char *name,
				  mdcache_entry_t **new_entry,
				  struct fsal_attrlist *attrs_out);
void mdcache_src_dest_lock(mdcache_entry_t *src, mdcache_entry_t *dest);
void mdcache_src_dest_unlock(mdcache_entry_t *src, mdcache_entry_t *dest);
void mdcache_dirent_remove(mdcache_entry_t *parent, const char *name);
fsal_status_t mdcache_dirent_add(mdcache_entry_t *parent,
				 const char *name,
				 mdcache_entry_t *entry,
				 bool *invalidate);

void mdcache_dirent_invalidate_all(mdcache_entry_t *entry);

fsal_status_t mdcache_readdir_uncached(mdcache_entry_t *directory, fsal_cookie_t
				       *whence, void *dir_state,
				       fsal_readdir_cb cb, attrmask_t attrmask,
				       bool *eod_met);
void mdcache_clean_dirent_chunk(struct dir_chunk *chunk);
void place_new_dirent(mdcache_entry_t *parent_dir,
		      mdcache_dir_entry_t *new_dir_entry);
fsal_status_t mdcache_readdir_chunked(mdcache_entry_t *directory,
				      fsal_cookie_t whence,
				      void *dir_state,
				      fsal_readdir_cb cb,
				      attrmask_t attrmask,
				      bool *eod_met);

fsal_status_t mdc_get_parent(struct mdcache_fsal_export *exp,
		    mdcache_entry_t *entry,
		    struct gsh_buffdesc *parent_out);

void mdc_update_attr_cache(mdcache_entry_t *entry, struct fsal_attrlist *attrs);

static inline void mdcache_free_fh(struct gsh_buffdesc *fh_desc);

/**
 * @brief Atomically test the bits in mde_flags.
 *
 * @param[in]  entry The mdcache entry to test
 * @param[in]  bits  The bits to test if set
 *
 * @returns true if all the bits are set.
 *
 */
static inline bool test_mde_flags(mdcache_entry_t *entry, uint32_t bits)
{
	return (atomic_fetch_uint32_t(&entry->mde_flags) & bits) == bits;
}

static inline struct mdcache_fsal_export *mdc_export(
					    struct fsal_export *fsal_export)
{
	return container_of(fsal_export, struct mdcache_fsal_export, mfe_exp);
}

static inline struct mdcache_fsal_export *mdc_cur_export(void)
{
	return mdc_export(op_ctx->fsal_export);
}

void mdc_clean_entry(mdcache_entry_t *entry);
fsal_status_t mdc_check_mapping(mdcache_entry_t *entry);
void _mdcache_kill_entry(mdcache_entry_t *entry,
			 char *file, int line, char *function);

#define mdcache_kill_entry(entry) \
	_mdcache_kill_entry(entry, \
			    (char *) __FILE__, __LINE__, (char *) __func__)

fsal_status_t
mdc_get_parent_handle(struct mdcache_fsal_export *exp,
		      mdcache_entry_t *entry,
		      struct fsal_obj_handle *sub_parent);



extern struct config_block mdcache_param_blk;

/* Call a sub-FSAL function using it's export, safe for use during shutdown */
#define subcall_shutdown_raw(myexp, call) do { \
	if (op_ctx) \
		op_ctx->fsal_export = (myexp)->mfe_exp.sub_export; \
	call; \
	if (op_ctx) \
		op_ctx->fsal_export = &(myexp)->mfe_exp; \
} while (0)

/* Call a sub-FSAL function using it's export */
#define subcall_raw(myexp, call) do { \
	op_ctx->fsal_export = (myexp)->mfe_exp.sub_export; \
	call; \
	op_ctx->fsal_export = &(myexp)->mfe_exp; \
} while (0)

/* Call a sub-FSAL function using it's export */
#define subcall(call) do { \
	struct mdcache_fsal_export *__export = mdc_cur_export(); \
	subcall_raw(__export, call); \
} while (0)

/* During a callback from a sub-FSAL, call using MDCACHE's export */
#define supercall_raw(myexp, call) do { \
	LogFullDebug(COMPONENT_CACHE_INODE, "supercall %s", myexp->name); \
	op_ctx->fsal_export = &(myexp)->mfe_exp; \
	call; \
	op_ctx->fsal_export = (myexp)->mfe_exp.sub_export; \
} while (0)

#define supercall(call) do { \
	struct fsal_export *save_exp = op_ctx->fsal_export; \
	op_ctx->fsal_export = save_exp->super_export; \
	call; \
	op_ctx->fsal_export = save_exp; \
} while (0)

/**
 * @brief Lock context for content lock recursion
 *
 * long description
 */
typedef struct {
	mdcache_entry_t *entry;
	bool		 iswrite;
	int		 count;
} mdc_lock_context_t;

/**
 * @brief Dup a cache key.
 *
 * Deep copies the key passed in src, to tgt.  On return, tgt->kv.addr
 * is overwritten with a new buffer of length src->kv.len, and the buffer
 * is copied.
 *
 * @param tgt [inout] Destination of copy
 * @param src [in] Source of copy
 *
 * @return 0 on success.
 */
static inline void
mdcache_key_dup(mdcache_key_t *tgt,
		    mdcache_key_t *src)
{
	tgt->kv.len = src->kv.len;
	tgt->kv.addr = gsh_malloc(src->kv.len);

	memcpy(tgt->kv.addr, src->kv.addr, src->kv.len);
	tgt->hk = src->hk;
	tgt->fsal = src->fsal;
}

/**
 * @brief Check if the parent key of an entry has expired.
 *
 * If the parent key is valid return true else return false.
 *
 * @param[in] entry     Entry whose parent key may have expired.
 * @return Return true if valid, false if invalid.
 */
static inline bool
mdcache_is_parent_valid(mdcache_entry_t *entry)
{
	time_t current_time = time(NULL);

	if (current_time > entry->fsobj.fsdir.parent_time)
		return false;
	return true;
}

/**
 * @brief Set the parent key of an entry
 *
 * If the parent key is not set, set it.  This keeps keys from being leaked.
 *
 * @param[in] entry	Entry to set
 * @return Return description
 */
static inline void
mdc_dir_add_parent(mdcache_entry_t *entry, mdcache_entry_t *mdc_parent)
{
	if (entry->fsobj.fsdir.parent.len != 0) {
		/* Already has a parent pointer */
		if (entry->fsobj.fsdir.parent_time == 0 ||
		    mdcache_is_parent_valid(entry)) {
			return;
		} else {
			/* Clean up parent key */
			mdcache_free_fh(&entry->fsobj.fsdir.parent);
		}
	}

	/* The parent key must be a host-handle so that
	 * create_handle() works in all cases.
	 */
	mdc_get_parent_handle(mdc_cur_export(), entry,
			      mdc_parent->sub_handle);
}

/**
 * @brief Delete a cache key.
 *
 * Delete a cache key. Safe to call even if key was not allocated.
 *
 * @param key [in] The key to delete
 *
 * @return void.
 */
static inline void
mdcache_key_delete(mdcache_key_t *key)
{
	key->kv.len = 0;
	gsh_free(key->kv.addr);
	key->kv.addr = NULL;
}

/* Create a copy of host-handle */
static inline void
mdcache_copy_fh(struct gsh_buffdesc *dest, struct gsh_buffdesc *src)
{
	dest->len = src->len;
	dest->addr = gsh_malloc(dest->len);
	(void)memcpy(dest->addr, src->addr, dest->len);
}

/* Delete stored parent host-handle */
static inline void
mdcache_free_fh(struct gsh_buffdesc *fh_desc)
{
	fh_desc->len = 0;
	gsh_free(fh_desc->addr);
	fh_desc->addr = NULL;
}

/**
 * @brief Update entry metadata from its attributes
 *
 * This function, to be used after a FSAL_getattr, updates the
 * attribute trust flag and time, and stores the refresh time
 * in the main mdcache_entry_t.
 *
 * @note the caller MUST hold attr_lock for write
 *
 * @param[in,out] entry The entry on which we operate.
 * @param[in]     attrs The attributes that have just been updated
 *                      (we actually only care about the masks)
 */

static inline void
mdc_fixup_md(mdcache_entry_t *entry, struct fsal_attrlist *attrs)
{
	uint32_t flags = 0;

	/* As long as the ACL was requested, and we get here, we assume no
	 * failure to fetch ACL (differentiated from no ACL to fetch), and
	 * thus we only look at the fact that ACL was requested to determine
	 * that we can trust the ACL.
	 */
	if (attrs->request_mask & ATTR_ACL)
		flags |= MDCACHE_TRUST_ACL;

	/* If the other attributes were requested, we can trust the other
	 * attributes. Note that if not all could be provided, we assumed
	 * that an error occurred.
	 */
	if (attrs->request_mask & ~(ATTR_ACL |
				    ATTR4_FS_LOCATIONS |
				    ATTR4_SEC_LABEL))
		flags |= MDCACHE_TRUST_ATTRS;

	if (attrs->valid_mask == ATTR_RDATTR_ERR) {
		/* The attribute fetch failed, mark the attributes and ACL as
		 * untrusted.
		 */
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ACL
					   | MDCACHE_TRUST_ATTRS);
		return;
	}

	if (attrs->request_mask & ATTR4_FS_LOCATIONS &&
		attrs->fs_locations != NULL) {
		flags |= MDCACHE_TRUST_FS_LOCATIONS;
	}

	if (attrs->request_mask & ATTR4_SEC_LABEL &&
		attrs->sec_label.slai_data.slai_data_val != NULL) {
		flags |= MDCACHE_TRUST_SEC_LABEL;
	}

	time_t cur_time = time(NULL);

	/* Set the refresh time for the cache entry */
	if (flags & MDCACHE_TRUST_ACL) {
		if (entry->attrs.expire_time_attr > 0)
			entry->acl_time = cur_time;
		else
			entry->acl_time = 0;
	}

	if (flags & MDCACHE_TRUST_ATTRS) {
		if (entry->attrs.expire_time_attr > 0)
			entry->attr_time = cur_time;
		else
			entry->attr_time = 0;
	}

	if (flags & MDCACHE_TRUST_FS_LOCATIONS) {
		if (entry->attrs.expire_time_attr > 0)
			entry->fs_locations_time = cur_time;
		else
			entry->fs_locations_time = 0;
	}

	/* We have just loaded the attributes from the FSAL. */
	atomic_set_uint32_t_bits(&entry->mde_flags, flags);
}

static inline bool
mdcache_test_attrs_trust(mdcache_entry_t *entry, attrmask_t mask)
{
	uint32_t flags = 0;

	/* Check what attributes were originally requested for refresh. */
	if (mask & ATTR_ACL)
		flags |= MDCACHE_TRUST_ACL;

	if (mask & ~ATTR_ACL)
		flags |= MDCACHE_TRUST_ATTRS;

	if (mask & ATTR4_FS_LOCATIONS)
		flags |= MDCACHE_TRUST_FS_LOCATIONS;

	if (mask & ATTR4_SEC_LABEL)
		flags |= MDCACHE_TRUST_SEC_LABEL;

	/* If any of the requested attributes are not valid, return. */
	if (!test_mde_flags(entry, flags))
		return false;

	if ((entry->attrs.valid_mask & mask) != (mask & ~ATTR_RDATTR_ERR))
		return false;

	return true;
}

/**
 * @brief Check if attributes are valid
 *
 * @note the caller MUST hold attr_lock for read
 *
 * @param[in] entry     The entry to check
 */

static inline bool
mdcache_is_attrs_valid(mdcache_entry_t *entry, attrmask_t mask)
{
	bool file_deleg = false;
	attrmask_t orig_mask = mask;

	if (!mdcache_test_attrs_trust(entry, mask))
		return false;

	if (entry->attrs.valid_mask == ATTR_RDATTR_ERR)
		return false;

	if (entry->obj_handle.type == DIRECTORY
	    && mdcache_param.getattr_dir_invalidation)
		return false;

	file_deleg = (entry->obj_handle.state_hdl &&
	  entry->obj_handle.state_hdl->file.fdeleg_stats.fds_curr_delegations);

	if (file_deleg) {
		/* If the file is delegated, then we can trust
		 * the attributes already fetched (i.e, which
		 * are in entry->attrs.valid_mask), unless
		 * expire_time_attr is set to '0'.
		 */
		mask = (mask & ~entry->attrs.valid_mask);
	}

	if ((orig_mask & ~ATTR_ACL) != 0 && entry->attrs.expire_time_attr == 0)
		return false;

	if ((mask & ~ATTR_ACL) != 0 && entry->attrs.expire_time_attr > 0) {
		time_t current_time = time(NULL);

		if (current_time - entry->attr_time >
		    entry->attrs.expire_time_attr)
			return false;
	}

	if ((orig_mask & ATTR_ACL) != 0 && entry->attrs.expire_time_attr == 0)
		return false;

	if ((mask & ATTR_ACL) != 0 && entry->attrs.expire_time_attr > 0) {
		time_t current_time = time(NULL);

		if (current_time - entry->acl_time >
		    entry->attrs.expire_time_attr)
			return false;
	}

	return true;
}

/**
 * @brief Remove an export <-> entry mapping
 *
 * @param[in] expmap	Mapping to remove
 *
 * @note must be called with the mdc_exp_lock and attr_lock held
 */
static inline void
mdc_remove_export_map(struct entry_export_map *expmap)
{
	glist_del(&expmap->export_per_entry);
	glist_del(&expmap->entry_per_export);
	gsh_free(expmap);
}


/**
 * @brief Check to see if an entry has state
 *
 * long description
 *
 * @param[in] entry	Entry to check
 * @return true if has state, false otherwise
 */
static inline bool
mdc_has_state(mdcache_entry_t *entry)
{
	switch (entry->obj_handle.type) {
	case REGULAR_FILE:
		if (!glist_empty(&entry->fsobj.hdl.file.list_of_states))
			return true;
		if (!glist_empty(&entry->fsobj.hdl.file.layoutrecall_list))
			return true;
		if (!glist_empty(&entry->fsobj.hdl.file.lock_list))
			return true;
		if (!glist_empty(&entry->fsobj.hdl.file.nlm_share_list))
			return true;
		return false;
	case DIRECTORY:
		if (entry->fsobj.fsdir.dhdl.dir.junction_export)
			return true;
		if (entry->fsobj.fsdir.dhdl.dir.exp_root_refcount)
			return true;
		return false;
	default:
		/* No state for these types */
		return false;
	}
}

/**
 * @brief Mark an entry as unreachable
 *
 * An entry has become unreachable.  If it has no state, kill it.  Otherwise,
 * mark it unreachable so that it can be killed when state is freed.
 *
 * @param[in] entry	Entry to mark
 */
static inline void
_mdc_unreachable(mdcache_entry_t *entry,
		 char *file, int line, char *function)
{
	if (isDebug(COMPONENT_CACHE_INODE)) {
		DisplayLogComponentLevel(COMPONENT_CACHE_INODE,
					 file, line, function, NIV_DEBUG,
					 "Unreachable %s entry %p %s state",
					 object_file_type_to_str(
							entry->obj_handle.type),
					 entry,
					 mdc_has_state(entry)
						? "has" : "doesn't have");
	}

	if (!mdc_has_state(entry)) {
		mdcache_kill_entry(entry);
		return;
	}

	atomic_set_uint32_t_bits(&entry->mde_flags, MDCACHE_UNREACHABLE);
}

#define mdc_unreachable(entry) \
	_mdc_unreachable(entry, \
			 (char *) __FILE__, __LINE__, (char *) __func__)


/* Handle methods */

/**
 * Structure used to store data for read_dirents callback.
 *
 * Before executing the upper level callback (it might be another
 * stackable fsal or the inode cache), the context has to be restored.
 */
struct mdcache_readdir_state {
	fsal_readdir_cb cb; /*< Callback to the upper layer. */
	struct mdcache_fsal_export *exp; /*< Export of the current mdcache. */
	void *dir_state; /*< State to be sent to the next callback. */
};


fsal_status_t mdcache_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out);

fsal_status_t mdcache_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle,
				   struct fsal_attrlist *attrs_out);

int mdcache_fsal_open(struct mdcache_fsal_obj_handle *, int, fsal_errors_t *);
int mdcache_fsal_readlink(struct mdcache_fsal_obj_handle *, fsal_errors_t *);

static inline bool mdcache_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

/* I/O management */
fsal_status_t mdcache_seek(struct fsal_obj_handle *obj_hdl,
			   struct io_info *info);
fsal_status_t mdcache_io_advise(struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints);
fsal_status_t mdcache_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t mdcache_open2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   enum fsal_create_mode createmode,
			   const char *name,
			   struct fsal_attrlist *attrib_set,
			   fsal_verifier_t verifier,
			   struct fsal_obj_handle **new_obj,
			   struct fsal_attrlist *attrs_out,
			   bool *caller_perm_check);
bool mdcache_check_verifier(struct fsal_obj_handle *obj_hdl,
				     fsal_verifier_t verifier);
fsal_openflags_t mdcache_status2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state);
fsal_status_t mdcache_reopen2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      fsal_openflags_t openflags);
void mdcache_read2(struct fsal_obj_handle *obj_hdl,
		   bool bypass,
		   fsal_async_cb done_cb,
		   struct fsal_io_arg *read_arg,
		   void *caller_arg);
void mdcache_write2(struct fsal_obj_handle *obj_hdl,
		    bool bypass,
		    fsal_async_cb done_cb,
		    struct fsal_io_arg *write_arg,
		    void *caller_arg);
fsal_status_t mdcache_seek2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    struct io_info *info);
fsal_status_t mdcache_io_advise2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state,
				 struct io_hints *hints);
fsal_status_t mdcache_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
			      size_t len);
fsal_status_t mdcache_lock_op2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      void *p_owner,
			      fsal_lock_op_t lock_op,
			      fsal_lock_param_t *req_lock,
			      fsal_lock_param_t *conflicting_lock);
fsal_status_t mdcache_lease_op2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state,
				void *owner,
				fsal_deleg_t deleg);
fsal_status_t mdcache_close2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state);
fsal_status_t mdcache_fallocate(struct fsal_obj_handle *obj_hdl,
				struct state_t *state, uint64_t offset,
				uint64_t length, bool allocate);

/* extended attributes management */
fsal_status_t mdcache_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list);
fsal_status_t mdcache_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id);
fsal_status_t mdcache_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      void *buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t mdcache_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t mdcache_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      void *buffer_addr,
				      size_t buffer_size,
				      int create);
fsal_status_t mdcache_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size);
fsal_status_t mdcache_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id);
fsal_status_t mdcache_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name);
fsal_status_t mdcache_getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrkey4 *name, xattrvalue4 *value);
fsal_status_t mdcache_setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_option4 option, xattrkey4 *name,
				xattrvalue4 *value);
fsal_status_t mdcache_removexattrs(struct fsal_obj_handle *obj_hdl,
				   xattrkey4 *name);
fsal_status_t mdcache_listxattrs(struct fsal_obj_handle *obj_hdl,
				 count4 len, nfs_cookie4 *cookie,
				 bool_t *eof, xattrlist4 *names);

/* Handle functions */
void mdcache_handle_ops_init(struct fsal_obj_ops *ops);

/* Export functions */
void mdcache_export_ops_init(struct export_ops *ops);

/* Upcall functions */
fsal_status_t mdcache_export_up_ops_init(struct fsal_up_vector *my_up_ops,
				 const struct fsal_up_vector *super_up_ops);

/* Debug functions */
#define MDC_LOG_KEY(key) do { \
	LogFullDebugOpaque(COMPONENT_CACHE_INODE, \
			   "FSAL key: %s", 128, (key)->kv.addr, \
			   (key)->kv.len); \
	LogFullDebug(COMPONENT_CACHE_INODE, "hash key: %lx", (key)->hk); \
} while (0)

static inline
fsal_status_t mdcache_refresh_attrs_no_invalidate(mdcache_entry_t *entry)
{
	fsal_status_t status;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	status = mdcache_refresh_attrs(entry, false, false, false, false);

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_CACHE_INODE, "Refresh attributes failed %s",
			 fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE)
			mdcache_kill_entry(entry);
	}

	return status;
}

static inline int avl_dmap_ck_cmpf(const struct avltree_node *lhs,
				     const struct avltree_node *rhs)
{
	mdcache_dmap_entry_t *lk, *rk;

	lk = avltree_container_of(lhs, mdcache_dmap_entry_t, node);
	rk = avltree_container_of(rhs, mdcache_dmap_entry_t, node);

	if (lk->ck < rk->ck)
		return -1;

	if (lk->ck == rk->ck)
		return 0;

	return 1;
}

#endif /* MDCACHE_INT_H */
