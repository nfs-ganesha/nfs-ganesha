/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2016 Red Hat, Inc. and/or its affiliates.
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

typedef struct mdcache_fsal_obj_handle mdcache_entry_t;

/*
 * MDCACHE internal export
 */
struct mdcache_fsal_export {
	struct fsal_export export;
	char *name;
	/** My up_ops */
	struct fsal_up_vector up_ops;
	/** Higher level up_ops for ops we don't consume */
	struct fsal_up_vector super_up_ops;
	/** The list of cache inode entries belonging to this export */
	struct glist_head entry_list;
	/** Lock protecting entry_list */
	pthread_rwlock_t mdc_exp_lock;
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
	LRU_ENTRY_NOSCAN,
	LRU_ENTRY_CLEANUP
};

#define LRU_CLEANUP 0x00000001 /* Entry is on cleanup queue */
#define LRU_CLEANED 0x00000002 /* Entry has been cleaned */

typedef struct mdcache_lru__ {
	struct glist_head q;	/*< Link in the physical deque
				   impelmenting a portion of the logical
				   LRU. */
	enum lru_q_id qid;	/*< Queue identifier */
	int32_t refcnt;		/*< Reference count.  This is signed to make
				   mistakes easy to see. */
	int32_t noscan_refcnt;	/*< Count of times marked noscan */
	uint32_t flags;		/*< Status flags; MUST use atomic ops */
	uint32_t lane;		/*< The lane in which an entry currently
				 *< resides, so we can lock the deque and
				 *< decrement the correct counter when moving
				 *< or deleting the entry. */
	uint32_t cf;		/*< Confounder */
} mdcache_lru_t;

/**
 * cache inode statistics.
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
	/** The relevant cache inode entry */
	mdcache_entry_t *entry;
	/** The export the entry belongs to */
	struct mdcache_fsal_export *export;
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
/** The entry has been removed, but not unhashed due to state */
static const uint32_t MDCACHE_UNREACHABLE = 0x00000008;


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
 * stuff the the fsal has to manage, i.e. filesystem bits.
 */

struct mdcache_fsal_obj_handle {
	/** Reader-writer lock for attributes */
	pthread_rwlock_t attr_lock;
	/** MDCache FSAL Handle */
	struct fsal_obj_handle obj_handle;
	/** Sub-FSAL handle */
	struct fsal_obj_handle *sub_handle;
	/** Cached attributes */
	struct attrlist attrs;
	/** FH hash linkage */
	struct {
		struct avltree_node node_k;	/*< AVL node in tree */
		mdcache_key_t key;	/*< Key of this entry */
		bool inavl;
	} fh_hk;
	/** Flags for this entry */
	uint32_t mde_flags;
	/** refcount for number of active icreate */
	int32_t icreate_refcnt;
	/** Time at which we last refreshed attributes. */
	time_t attr_time;
	/** Time at which we last refreshed acl. */
	time_t acl_time;
	/** New style LRU link */
	mdcache_lru_t lru;
	/** Exports per entry (protected by attr_lock) */
	struct glist_head export_list;
	/** Atomic pointer to the first mapped export for fast path */
	void *first_export;
	/** Lock on type-specific cached content.  See locking
	    discipline for details. */
	pthread_rwlock_t content_lock;
	/** Filetype specific data, discriminated by the type field.
	    Note that data for special files is in
	    attributes.rawdev */
	union mdcache_fsobj {
		struct state_hdl hdl;
		struct {
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
			/** Number of known active children */
			uint32_t nbactive;
			/** The parent of this directory ('..') */
			mdcache_key_t parent;
			struct {
				/** Children */
				struct avltree t;
				/** Persist cookies */
				struct avltree c;
				/** Heuristic. Expect 0. */
				uint32_t collisions;
			} avl;
		} fsdir;		/**< DIRECTORY data */
	} fsobj;
};

/**
 * @brief Represents a cached directory entry
 *
 * This is a cached directory entry that associates a name and cookie
 * with a cache entry.
 */

#define DIR_ENTRY_FLAG_NONE     0x0000
#define DIR_ENTRY_FLAG_DELETED  0x0001

typedef struct mdcache_dir_entry__ {
	struct avltree_node node_hk;	/*< AVL node in tree */
	struct {
		uint64_t k;	/*< Integer cookie */
		uint32_t p;	/*< Number of probes, an efficiency metric */
	} hk;
	mdcache_key_t ckey;	/*< Key of cache entry */
	uint32_t flags;		/*< Flags */
	char name[];		/*< The NUL-terminated filename */
} mdcache_dir_entry_t;

/* Helpers */
fsal_status_t mdcache_alloc_and_check_handle(
		struct mdcache_fsal_export *export,
		struct fsal_obj_handle *sub_handle,
		struct fsal_obj_handle **new_obj,
		bool new_directory,
		struct attrlist *attrs_in,
		struct attrlist *attrs_out,
		const char *tag,
		mdcache_entry_t *parent,
		const char *name,
		bool invalidate,
		struct state_t *state);

fsal_status_t get_optional_attrs(struct fsal_obj_handle *obj_hdl,
				 struct attrlist *attrs_out);
fsal_status_t mdcache_new_entry(struct mdcache_fsal_export *export,
				struct fsal_obj_handle *sub_handle,
				struct attrlist *attrs_in,
				struct attrlist *attrs_out,
				bool new_directory,
				mdcache_entry_t **entry,
				struct state_t *state);
fsal_status_t mdcache_find_keyed(mdcache_key_t *key, mdcache_entry_t **entry);
fsal_status_t mdcache_locate_keyed(mdcache_key_t *key,
				   struct mdcache_fsal_export *export,
				   mdcache_entry_t **entry,
				   struct attrlist *attrs_out);
fsal_status_t mdc_try_get_cached(mdcache_entry_t *mdc_parent, const char *name,
				 mdcache_entry_t **entry);
fsal_status_t mdc_lookup(mdcache_entry_t *mdc_parent, const char *name,
			 bool uncached, mdcache_entry_t **new_entry,
			 struct attrlist *attrs_out);
fsal_status_t mdc_lookup_uncached(mdcache_entry_t *mdc_parent,
				  const char *name,
				  mdcache_entry_t **new_entry,
				  struct attrlist *attrs_out);
void mdcache_src_dest_lock(mdcache_entry_t *src, mdcache_entry_t *dest);
void mdcache_src_dest_unlock(mdcache_entry_t *src, mdcache_entry_t *dest);
fsal_status_t mdcache_dirent_remove(mdcache_entry_t *parent, const char *name);
fsal_status_t mdcache_dirent_add(mdcache_entry_t *parent,
					const char *name,
					mdcache_entry_t *entry);
fsal_status_t mdcache_dirent_rename(mdcache_entry_t *parent,
				    const char *oldname,
				    const char *newname);

void mdcache_dirent_invalidate_all(mdcache_entry_t *entry);

fsal_status_t mdcache_dirent_populate(mdcache_entry_t *dir);

static inline bool mdc_dircache_trusted(mdcache_entry_t *dir)
{
	if (!(dir->obj_handle.type == DIRECTORY))
		return false;
	return ((dir->mde_flags & MDCACHE_TRUST_CONTENT) &&
		(dir->mde_flags & MDCACHE_DIR_POPULATED));
}

static inline struct mdcache_fsal_export *mdc_export(
					    struct fsal_export *fsal_export)
{
	return container_of(fsal_export, struct mdcache_fsal_export, export);
}

static inline struct mdcache_fsal_export *mdc_cur_export(void)
{
	return mdc_export(op_ctx->fsal_export);
}

void mdc_clean_entry(mdcache_entry_t *entry);
void _mdcache_kill_entry(mdcache_entry_t *entry,
			 char *file, int line, char *function);

#define mdcache_kill_entry(entry) \
	_mdcache_kill_entry(entry, \
			    (char *) __FILE__, __LINE__, (char *) __func__)


extern struct config_block mdcache_param_blk;

/* Call a sub-FSAL function using it's export, safe for use during shutdown */
#define subcall_shutdown_raw(myexp, call) do { \
	if (op_ctx) \
		op_ctx->fsal_export = (myexp)->export.sub_export; \
	call; \
	if (op_ctx) \
		op_ctx->fsal_export = &(myexp)->export; \
} while (0)

/* Call a sub-FSAL function using it's export */
#define subcall_raw(myexp, call) do { \
	op_ctx->fsal_export = (myexp)->export.sub_export; \
	call; \
	op_ctx->fsal_export = &(myexp)->export; \
} while (0)

/* Call a sub-FSAL function using it's export */
#define subcall(call) do { \
	struct mdcache_fsal_export *__export = mdc_cur_export(); \
	subcall_raw(__export, call); \
} while (0)

/* During a callback from a sub-FSAL, call using MDCACHE's export */
#define supercall_raw(myexp, call) do { \
	LogFullDebug(COMPONENT_CACHE_INODE, "supercall %s", myexp->name); \
	op_ctx->fsal_export = &(myexp)->export; \
	call; \
	op_ctx->fsal_export = (myexp)->export.sub_export; \
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
	if (entry->fsobj.fsdir.parent.kv.len == 0)
		mdcache_key_dup(&entry->fsobj.fsdir.parent,
				&mdc_parent->fh_hk.key);
}

/**
 * @brief Delete a cache key.
 *
 * Delete a cache key.
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

/**
 * @brief Update entry metadata from its attributes
 *
 * This function, to be used after a FSAL_getattr, updates the
 * attribute trust flag and time, and stores the change time
 * in the main mdcache_entry_t.
 *
 * @note the caller MUST hold attr_lock for write
 *
 * @param[in,out] entry The entry on which we operate.
 */

static inline void
mdc_fixup_md(mdcache_entry_t *entry, attrmask_t mask)
{
	uint32_t flags = 0;

	/* Check what attributes were originally requested for refresh. */
	if (mask & ATTR_ACL)
		flags |= MDCACHE_TRUST_ACL;

	if (mask & ~ATTR_ACL)
		flags |= MDCACHE_TRUST_ATTRS;

	if (entry->attrs.mask == ATTR_RDATTR_ERR) {
		/* The attribute fetch failed, mark the attributes and ACL as
		 * untrusted.
		 */
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ACL
					   | MDCACHE_TRUST_ATTRS);
		return;
	}

	/* Set the refresh time for the cache entry */
	if (mask & ATTR_ACL) {
		if (entry->attrs.expire_time_attr > 0)
			entry->acl_time = time(NULL);
		else
			entry->acl_time = 0;
	}

	if (mask & ~ATTR_ACL) {
		if (entry->attrs.expire_time_attr > 0)
			entry->attr_time = time(NULL);
		else
			entry->attr_time = 0;
	}

	/* We have just loaded the attributes from the FSAL. */
	atomic_set_uint32_t_bits(&entry->mde_flags, flags);
}

/**
 * @brief Check if attributes are valid
 *
 * @note the caller MUST hold attr_lock for read
 *
 * @param[in] entry     The entry to check
 */

static inline bool
mdcache_is_attrs_valid(const mdcache_entry_t *entry, attrmask_t mask)
{
	uint32_t flags = 0;

	/* Check what attributes were originally requested for refresh. */
	if (mask & ATTR_ACL)
		flags |= MDCACHE_TRUST_ACL;

	if (mask & ~ATTR_ACL)
		flags |= MDCACHE_TRUST_ATTRS;

	/* If any of the requested attributes are not valid, return. */
	if ((entry->mde_flags & flags) != flags)
		return false;

	if (FSAL_TEST_MASK(entry->attrs.mask, ATTR_RDATTR_ERR))
		return false;

	if (entry->obj_handle.type == DIRECTORY
	    && mdcache_param.getattr_dir_invalidation)
		return false;

	if ((mask & ~ATTR_ACL) != 0 && entry->attrs.expire_time_attr == 0)
		return false;

	if ((mask & ~ATTR_ACL) != 0 && entry->attrs.expire_time_attr > 0) {
		time_t current_time = time(NULL);

		if (current_time - entry->attr_time >
		    entry->attrs.expire_time_attr)
			return false;
	}

	if ((mask & ATTR_ACL) != 0 && entry->attrs.expire_time_attr == 0)
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
						? "has" : "does't have");
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
				 struct attrlist *attrs_out);

fsal_status_t mdcache_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle,
				   struct attrlist *attrs_out);

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
fsal_status_t mdcache_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags);
fsal_status_t mdcache_reopen(struct fsal_obj_handle *obj_hdl,
			     fsal_openflags_t openflags);
fsal_openflags_t mdcache_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t mdcache_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *eof);
fsal_status_t mdcache_read_plus(struct fsal_obj_handle *obj_hdl,
				uint64_t offset, size_t buf_size,
				void *buffer, size_t *read_amount,
				bool *eof, struct io_info *info);
fsal_status_t mdcache_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable);
fsal_status_t mdcache_write_plus(struct fsal_obj_handle *obj_hdl,
				 uint64_t offset, size_t buf_size,
				 void *buffer, size_t *write_amount,
				 bool *fsal_stable, struct io_info *info);
fsal_status_t mdcache_seek(struct fsal_obj_handle *obj_hdl,
			   struct io_info *info);
fsal_status_t mdcache_io_advise(struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints);
fsal_status_t mdcache_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len);
fsal_status_t mdcache_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock);
fsal_status_t mdcache_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			      fsal_share_param_t param);
fsal_status_t mdcache_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t mdcache_open2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   enum fsal_create_mode createmode,
			   const char *name,
			   struct attrlist *attrib_set,
			   fsal_verifier_t verifier,
			   struct fsal_obj_handle **new_obj,
			   struct attrlist *attrs_out,
			   bool *caller_perm_check);
bool mdcache_check_verifier(struct fsal_obj_handle *obj_hdl,
				     fsal_verifier_t verifier);
fsal_openflags_t mdcache_status2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state);
fsal_status_t mdcache_reopen2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      fsal_openflags_t openflags);
fsal_status_t mdcache_read2(struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   uint64_t offset,
			   size_t buf_size,
			   void *buffer,
			   size_t *read_amount,
			   bool *eof,
			   struct io_info *info);
fsal_status_t mdcache_write2(struct fsal_obj_handle *obj_hdl,
			     bool bypass,
			     struct state_t *state,
			     uint64_t offset,
			     size_t buf_size,
			     void *buffer,
			     size_t *write_amount,
			     bool *fsal_stable,
			     struct io_info *info);
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
fsal_status_t mdcache_close2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state);

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
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t mdcache_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t mdcache_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create);
fsal_status_t mdcache_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size);
fsal_status_t mdcache_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id);
fsal_status_t mdcache_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name);
fsal_status_t mdcache_getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrname4 *name, xattrvalue4 *value);
fsal_status_t mdcache_setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_type4 type, xattrname4 *name,
				xattrvalue4 *value);
fsal_status_t mdcache_removexattrs(struct fsal_obj_handle *obj_hdl,
				   xattrname4 *name);
fsal_status_t mdcache_listxattrs(struct fsal_obj_handle *obj_hdl,
				 count4 len, nfs_cookie4 *cookie,
				 verifier4 *verf, bool_t *eof,
				 xattrlist4 *names);

/* Handle functions */
void mdcache_handle_ops_init(struct fsal_obj_ops *ops);

/* Export functions */
fsal_status_t mdc_init_export(struct fsal_module *fsal_hdl,
			      const struct fsal_up_vector *mdc_up_ops,
			      const struct fsal_up_vector *super_up_ops);

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

#endif /* MDCACHE_INT_H */
