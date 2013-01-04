/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file cache_inode.h
 * @brief Cache inode main interface.
 *
 * Definitions and structures for public interface to Cache inode.
 */

#ifndef CACHE_INODE_H
#define CACHE_INODE_H

#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include "abstract_mem.h"
#include "HashTable.h"
#include "avltree.h"
#include "generic_weakref.h"
#include "fsal.h"
#include "log.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nlm4.h"
#include "nlm_list.h"
#include "nfs4_acls.h"

/*< Global hash table for servicing lookups by FSAL handle key */

extern hash_table_t *fh_to_cache_entry_ht;

/* Forward references */
typedef struct cache_entry_t cache_entry_t;

/** Maximum size of NFSv2 handle */
static const size_t FILEHANDLE_MAX_LEN_V2 = 32;
/** Maximum size of NFSv3 handle */
static const size_t FILEHANDLE_MAX_LEN_V3 = 64;
/** Maximum size of NFSv4 handle */
static const size_t FILEHANDLE_MAX_LEN_V4 = 128;

/** Size for Ganesha unstable write buffers*/
static const size_t CACHE_INODE_UNSTABLE_BUFFERSIZE = 100*1024*1024;

/**
 * Constants to determine whether inode data, such as attributes,
 * expire.
 */

typedef enum cache_inode_expire_type__ {
	CACHE_INODE_EXPIRE = 0, /*< Data expire when they have been
				    refreshed less recently than grace
				    period for their type allows. */
	CACHE_INODE_EXPIRE_NEVER = 1, /*< Data never expire based on
					  time. */
	CACHE_INODE_EXPIRE_IMMEDIATE = 2 /*< Data are always treated
					     as expired. */
} cache_inode_expire_type_t;

/**
 * Values to control write stability
 */

typedef enum cache_inode_stability__ {
	CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER = 0,
	CACHE_INODE_SAFE_WRITE_TO_FS = 1,
	CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER = 2
} cache_inode_stability_t;


/**
 * Data for tracking a cache entry's position the LRU.
 */

typedef struct cache_inode_lru__ {
	struct glist_head q; /*< Link in the physical deque
			         impelmenting a portion of the logical
			         LRU. */
	pthread_mutex_t mtx; /*< Mutex protecting this entry with
			         regard to LRU operations. */
	int64_t refcount; /*< Reference count.  This is signed to make
			      mistakes easy to see. */
	uint32_t pin_refcnt; /*< Unpin it only if this goes down to zero */
	uint32_t flags; /*< Flags for details of this entry's status, such
			    as whether it is pinned and whetehr it's in L1
			    or L2. */
	uint32_t lane; /*< The lane in which an entry currently resides, so
			   we can lock the deque and decrement the correct
			   counter when moving or deleting the entry. */
} cache_inode_lru_t;

/**
 * Structure to hold cache_inode paramaters
 */

typedef struct cache_inode_parameter__ {
	hash_parameter_t hparam; /*< Parameter used for hashtable
				     initialization */
	hash_parameter_t cookie_param; /*< Parameters used for lock
					   cookie hash table
					   initialization */
	cache_inode_expire_type_t expire_type_attr; /*< Cache inode
                                                        expiration
                                                        type for
                                                        attributes */
	cache_inode_expire_type_t expire_type_link; /*< Cache inode
						        expiration
						        type for
						        symbolic
						        links */
	cache_inode_expire_type_t expire_type_dirent; /*< Cache inode
							  expiration
							  type for
							  directory
							  entries */
	time_t grace_period_attr; /*< Cached attributes grace
				      period */
	time_t grace_period_link; /*< Cached link grace period */
	time_t grace_period_dirent; /*< Cached dirent grace period */
	bool getattr_dir_invalidation; /*< Use getattr as for directory
					   invalidation */
	bool use_fsal_hash; /*< Do we rely on FSAL to hash handle or not? */
} cache_inode_parameter_t;

extern cache_inode_parameter_t cache_inode_params;

/**
 * Indicate whether this is a read or write operation, for
 * cache_inode_rdwr.
 */

typedef enum io_direction__ {
	CACHE_INODE_READ = 1, /*< Reading */
	CACHE_INODE_WRITE = 2 /*< Writing */
} cache_inode_io_direction_t;

/**
 * Passed to cache_inode_operate_cached_dirent to indicate the
 * operation being requested.
 */

typedef enum cache_inode_dirent_op__ {
	CACHE_INODE_DIRENT_OP_LOOKUP = 1, /*< Look up a name */
	CACHE_INODE_DIRENT_OP_REMOVE = 2, /*< Remove a name */
	CACHE_INODE_DIRENT_OP_RENAME = 3 /*< Rename node */
} cache_inode_dirent_op_t;

typedef enum cache_inode_avl_which__ {
	CACHE_INODE_AVL_NAMES = 1,
	CACHE_INODE_AVL_COOKIES = 2,
	CACHE_INODE_AVL_BOTH = 3
} cache_inode_avl_which_t;

/* Flags set on cache_entry_t::flags*/

/** Trust stored attributes */
static const uint32_t CACHE_INODE_TRUST_ATTRS = 0x00000001;
/** Trust inode content (for the moment, directory and symlink) */
static const uint32_t CACHE_INODE_TRUST_CONTENT = 0x00000002;
/** The directory has been populated (negative lookups are meaningful) */
static const uint32_t CACHE_INODE_DIR_POPULATED = 0x00000004;

/**
 * Bookkeeping information for unstably written data held in Ganesha's
 * write buffer.
 */

typedef struct cache_inode_unstable_data__ {
	caddr_t buffer; /*< Pointer in memory */
	uint64_t offset; /*< Offset (relative to the start of the file) */
	uint32_t length; /*< Length */
} cache_inode_unstable_data_t;

/**
 * @brief The ref counted share reservation state.
 *
 * Each field represents the count of instances of that flag being present
 * in a v3 or v4 share reservation.
 *
 * There is a separate count of v4 deny write flags so that they can be
 * enforced against v3 writes (v3 deny writes can not be enforced against
 * v3 writes because there is no connection between the share reservation
 * and the write operation). v3 reads will always be allowed.
 */
typedef struct cache_inode_share__ {
	unsigned int share_access_read;
	unsigned int share_access_write;
	unsigned int share_deny_read;
	unsigned int share_deny_write;
	unsigned int share_deny_write_v4; /**< Count of v4 share deny write */
} cache_inode_share_t;

/**
 * @brief Represents a cached directory entry
 *
 * This is a cached directory entry that associates a name and cookie
 * with a cache entry.
 */

#define DIR_ENTRY_FLAG_NONE     0x0000
#define DIR_ENTRY_FLAG_DELETED  0x0001

typedef struct cache_inode_dir_entry__ {
	struct avltree_node node_hk; /*< AVL node in tree */
	struct {
		uint64_t k; /*< Integer cookie */
		uint32_t p; /*< Number of probes, an efficiency metric */
	} hk;
	gweakref_t entry; /*< Weak reference pointing to the cache entry */
	uint64_t fsal_cookie; /*< The cookie returned by the FSAL. */
	uint32_t flags; /*< Flags */
	char name[]; /*< The NUL-terminated filename */
} cache_inode_dir_entry_t;

/**
 * @brief Represents a cached inode
 *
 * Information representing a cached file (inode) including metadata,
 * and for directories and symlinks, pointers to cached content.  This
 * is also the anchor for state held on a file.
 *
 * Regarding the locking discipline:
 *
 * (1) The attributes field is protected by attr_lock.
 *
 * (2) content_lock must be held for WRITE when modifying the AVL tree
 *     of a directory or any dirent contained therein.  It must be
 *     held for READ when accessing any of this information.
 *
 * (3) content_lock must be held for WRITE when caching or disposing
 *     of a file descriptor and when writing data into the Ganesha
 *     data cache.  It must be held for READ when accessing the cached
 *     descriptor or reading data out of the data cache.
 *
 * (4) content_lock must be held for WRITE when updating the cached
 *     content of a symlink or when NULLing the object.symlink pointer
 *     preparatory to freeing the link structure.  It must be held for
 *     READ when dereferencing the object.symlink pointer or reading
 *     cached content.
 *
 * (5) state_lock must be held for WRITE when modifying state_list or
 *     lock_list.  It must be held for READ when traversing or
 *     examining the state_list or lock_list.  Operations like LRU
 *     pinning must hold the state lock for read through the operation
 *     of moving the entry from one queue to another.
 *
 * The handle, weakref, and type fields are unprotected, as they are
 * considered to be immutable throughout the life of the object.
 *
 * The flags field is unprotected, however it should be modified only
 * through the functions atomic_set_uint32_t_bits and
 * atomic_clear_uint32_t_bits.
 *
 * The change_time and attr_time fields are unprotected and must only
 * be used for simple comparisons or servicing requests returning
 * change_info4.
 *
 * The lru field as its own mutex to protect it.
 *
 * The attributes and symlink contents are stored in the handle for
 * api simplicity but these locks apply around their access methods.
 *
 * @note As part of the transition to the new api, the handle was
 * moved out of the union and made a pointer to a separately allocated
 * object.  However, locking applies everywhere except handle object
 * creation time (nobody knows about it yet).  The symlink content
 * cache has moved into the handle as, well.  The cache_entry and
 * fsal_obj_handle are two parts of the same thing, a cached inode.
 * cache_entry holds the cache stuff and fsal_obj_handle holds the
 * stuff the the fsal has to manage, i.e. filesystem bits.
 */

struct cache_entry_t {
	/** Reader-writer lock for attributes */
	pthread_rwlock_t attr_lock;
	/** The FSAL Handle     */
	struct fsal_obj_handle *obj_handle;
	/** A weakref for this entry (pointer and generation number.)
	    The generation number is the only interesting part, but
	    this way the weakref can be easily stashed somewhere. */
	gweakref_t weakref;
	/** The type of the entry */
	object_file_type_t type;
	/** Flags for this entry */
	uint32_t flags;
	/** The time of the last operation ganesha knows about.  We
	    can ue this for change_info4, but atomic MUST be set to
	    false.  Don't use it for anything else (servicing getattr,
	    etc.) */
	time_t change_time;
	/** Time at which we last refreshed attributes. */
	time_t attr_time;
	/** New style LRU link */
	cache_inode_lru_t lru;
	/** This is separated out from the content lock, since there
	    are state oerations that don't affect anything guarded by
	    content (for example, a layout return or request has no
	    effect on a cached file handle) and the content lock may
	    be released and reacquired several times in an operation
	    that should not see changes in state. */
	pthread_rwlock_t state_lock;
	/** States on this cache entry */
	struct glist_head state_list;
	/** Layout recalls on this entry */
	struct glist_head layoutrecall_list;
	/** Lock on type-specific cached content.  See locking
	    discipline for details. */
	pthread_rwlock_t content_lock;
	/** Filetype specific data, discriminated by the type field.
	    Note that data for special files is in
	    attributes.rawdev */
	union cache_inode_fsobj {
		struct cache_inode_file {
			/** Pointers for lock list */
			struct glist_head lock_list;
			/** Pointers for NLM share list */
			struct glist_head nlm_share_list;
			/** Unstable data, for use with WRITE/COMMIT */
			cache_inode_unstable_data_t unstable_data;
			/** Share reservation state for this file. */
			cache_inode_share_t share_state;
		} file; /*< REGULAR_FILE data */

		struct {
			/** Marks this as the root directory of an export */
			bool root;
			/** Number of known active children */
			uint32_t nbactive;
			/** NULL is not a referral.  If not, this a
			    'referral string' */
			char *referral;
			/** The parent of this directory ('..') */
			gweakref_t parent;
			struct {
				/** Children */
				struct avltree t;
				/** Persist cookies */
				struct avltree c;
				/** Heuristic. Expect 0. */
				uint32_t collisions;
			} avl;
		} dir; /*< DIRECTORY data */
	} object;
};

typedef struct cache_inode_file cache_inode_file_t;
typedef union cache_inode_fsobj cache_inode_fsobj_t;

/**
 * Data to be used as the key into the cache_entry hash table.
 */

typedef struct cache_inode_fsal_data {
	struct fsal_export *export; /*< export owning this handle */
	struct gsh_buffdesc fh_desc; /*< FSAL handle descriptor  */
} cache_inode_fsal_data_t;

/**
 * Global memory pools for cached data
 */

/** Cache entries pool */
extern pool_t *cache_inode_entry_pool;
/** Pool for SYMLINK data */
extern pool_t *cache_inode_symlink_pool;

/**
 * Configuration parameters for garbage collection/LRU policy
 */

typedef struct cache_inode_gc_policy {
	uint32_t entries_hwmark; /*< High water mark for cache entries */
	uint32_t entries_lwmark; /*< Low water mark for cache_entries */
	uint32_t lru_run_interval;  /*< Interval in seconds between
				        runs of the LRU cleaner
					thread */
	bool use_fd_cache; /*< Do we cache fd or not? */
	uint32_t fd_limit_percent; /*< The percentage of the
				       system-imposed maximum of file
				       descriptors at which Ganesha
				       will deny requests. */
	uint32_t fd_hwmark_percent; /*< The percentage of the
				         system-imposed maximum of
				         file descriptors above which
				         Ganesha will make greater
				         efforts at reaping. */
	uint32_t fd_lwmark_percent; /*< The percentage of the
				         system-imposed maximum of
				         file descriptors below which
				         Ganesha will not reap file
				         descriptonot reap file
				         descriptorsrs. */
	uint32_t reaper_work; /*< Roughly, the amount of work to do on
				  each pass through the thread under
				  normal conditions.  (Ideally, a
				  multipel of the number of lanes.) */
	uint32_t biggest_window; /*< The largest window (as a
				     percentage of the system-imposed
				     limit on FDs) of work that we
				     will do in extremis. */
	uint32_t required_progress; /*< Percentage of progress toward
				        the high water mark required
				        in in a pass through the
				        thread when in extremis. */
	uint32_t futility_count; /*< Number of failures to approach the high
				     watermark before we disable caching,
				     when in extremis. */
} cache_inode_gc_policy_t;

extern cache_inode_gc_policy_t cache_inode_gc_policy;

/**
 * Type-specific data passed to cache_inode_new_entry
 */

typedef union cache_inode_create_arg {
	fsal_dev_t  dev_spec; /*< Major/minor numbers for a device file */
	bool newly_created_dir; /*< True if this directory has just
				    been created, rather than
				    pre-existing and loaded into the
				    cache. */
	char *link_content; /*< Just stash the pointer. */
} cache_inode_create_arg_t;

/**
 * Flags
 */

/** The null flag */
static const uint32_t CACHE_INODE_FLAG_NONE = 0x00;
/** Indicate that this inode newly created, rather than just being
    loaded into the cache */
static const uint32_t CACHE_INODE_FLAG_CREATE = 0x01;
/** Instruct the called function to take a lock on the entry */
static const uint32_t CACHE_INODE_FLAG_LOCK = 0x02;
/** For a function called with the attribute lock held, do not release
    the attribute lock before returning */
static const uint32_t CACHE_INODE_FLAG_ATTR_HOLD = 0x04;
/** For a function called with the content lock held, do not release
    the content lock before returning */
static const uint32_t CACHE_INODE_FLAG_CONTENT_HOLD = 0x08;
/** For a function, indicates that the attribute lock is held */
static const uint32_t CACHE_INODE_FLAG_ATTR_HAVE = 0x10;
/** For a function, indicates that the content lock is held */
static const uint32_t CACHE_INODE_FLAG_CONTENT_HAVE = 0x20;
/** Close a file even with caching enabled */
static const uint32_t CACHE_INODE_FLAG_REALLYCLOSE = 0x80;
/** File can't be pinned, so close need not check. */
static const uint32_t CACHE_INODE_FLAG_NOT_PINNED = 0x100;

/**
 * Flags to cache_inode_invalidate
 */
static const uint32_t CACHE_INODE_INVALIDATE_ATTRS = 0x01;
static const uint32_t CACHE_INODE_INVALIDATE_CONTENT = 0x02;
static const uint32_t CACHE_INODE_INVALIDATE_CLOSE = 0x04;

/**
 * Possible errors
 */
typedef enum cache_inode_status_t {
	CACHE_INODE_SUCCESS = 0,
	CACHE_INODE_MALLOC_ERROR = 1,
	CACHE_INODE_POOL_MUTEX_INIT_ERROR = 2,
	CACHE_INODE_GET_NEW_LRU_ENTRY = 3,
	CACHE_INODE_UNAPPROPRIATED_KEY = 4,
	CACHE_INODE_INIT_ENTRY_FAILED = 5,
	CACHE_INODE_FSAL_ERROR = 6,
	CACHE_INODE_LRU_ERROR = 7,
	CACHE_INODE_HASH_SET_ERROR = 8,
	CACHE_INODE_NOT_A_DIRECTORY = 9,
	CACHE_INODE_INCONSISTENT_ENTRY = 10,
	CACHE_INODE_BAD_TYPE = 11,
	CACHE_INODE_ENTRY_EXISTS = 12,
	CACHE_INODE_DIR_NOT_EMPTY = 13,
	CACHE_INODE_NOT_FOUND = 14,
	CACHE_INODE_INVALID_ARGUMENT = 15,
	CACHE_INODE_INSERT_ERROR = 16,
	CACHE_INODE_HASH_TABLE_ERROR = 17,
	CACHE_INODE_FSAL_EACCESS = 18,
	CACHE_INODE_IS_A_DIRECTORY = 19,
	CACHE_INODE_FSAL_EPERM = 20,
	CACHE_INODE_NO_SPACE_LEFT = 21,
	CACHE_INODE_READ_ONLY_FS = 25,
	CACHE_INODE_IO_ERROR = 26,
	CACHE_INODE_FSAL_ESTALE = 27,
	CACHE_INODE_FSAL_ERR_SEC = 28,
	CACHE_INODE_STATE_CONFLICT = 29,
	CACHE_INODE_QUOTA_EXCEEDED = 30,
	CACHE_INODE_DEAD_ENTRY = 31,
	CACHE_INODE_ASYNC_POST_ERROR = 32,
	CACHE_INODE_NOT_SUPPORTED = 33,
	CACHE_INODE_STATE_ERROR = 34,
	CACHE_INODE_DELAY = 35,
	CACHE_INODE_NAME_TOO_LONG = 36,
	CACHE_INODE_BAD_COOKIE = 40,
	CACHE_INODE_FILE_BIG = 41,
	CACHE_INODE_KILLED = 42,
	CACHE_INODE_FILE_OPEN = 43,
	CACHE_INODE_FSAL_XDEV = 44,
	CACHE_INODE_FSAL_MLINK = 45,
} cache_inode_status_t;

/**
 * @brief Type of callback for cache_inode_readdir
 *
 * This callback provides the upper level protocol handling function
 * with one directory entry at a time.  It may use the opaque to keep
 * track of the structure it is filling, space used, and so forth.
 *
 * This function should return true if the entry has been added to the
 * caller's responde, or false if the structure is fulled and the
 * structure has not been added.
 */

typedef bool(*cache_inode_readdir_cb_t)(
	void *opaque,
	char *name,
	struct fsal_obj_handle *obj_handle,
	uint64_t cookie);

/**
 * @brief Type of callback for cache_inode_getattr
 *
 * This callback provides an easy way to read object attributes for
 * callers that wish to avoid the details of cache_inode's locking and
 * cache coherency.  This function is provided with a pointer to the
 * object's attributes and an opaque void.  Whatever value it returns
 * is returned as the result of the cache_inode_getattr call.
 */

typedef cache_inode_status_t(*cache_inode_getattr_cb_t)(
	void *opaque,
	const struct attrlist *attr);


const char *cache_inode_err_str(cache_inode_status_t err);

void cache_inode_clean_entry(cache_entry_t *entry);
int cache_inode_compare_key_fsal(struct gsh_buffdesc *buff1,
				 struct gsh_buffdesc *buff2);
void cache_inode_release_symlink(cache_entry_t *entry);

cache_inode_status_t cache_inode_init(cache_inode_parameter_t param,
				      hash_table_t **ht);

cache_inode_status_t cache_inode_get(cache_inode_fsal_data_t *fsdata,
				     cache_entry_t *associated,
				     const struct req_op_context *opctx,
				     cache_entry_t **entry);
void cache_inode_put(cache_entry_t *entry);

cache_inode_status_t cache_inode_access_sw(cache_entry_t *entry,
					   fsal_accessflags_t access_type,
					   struct req_op_context *req_ctx,
					   bool use_mutex);
cache_inode_status_t cache_inode_access_no_mutex(
	cache_entry_t *entry,
	fsal_accessflags_t access_type,
	struct req_op_context *req_ctx);
cache_inode_status_t cache_inode_access(cache_entry_t *entry,
					fsal_accessflags_t access_type,
					struct req_op_context
					   *req_ctx);

bool is_open(cache_entry_t *entry);
bool is_open_for_read(cache_entry_t *entry);
bool is_open_for_write(cache_entry_t *entry);

cache_inode_status_t cache_inode_open(cache_entry_t *entry,
				      fsal_openflags_t openflags,
				      struct req_op_context *req_ctx,
				      uint32_t flags);
cache_inode_status_t cache_inode_close(cache_entry_t *entry,
				       uint32_t flags);

cache_inode_status_t cache_inode_create(cache_entry_t *entry_parent,
					const char *name,
					object_file_type_t type,
					uint32_t mode,
					cache_inode_create_arg_t *create_arg,
					struct req_op_context *req_ctx,
					cache_entry_t **created);

cache_inode_status_t cache_inode_getattr(cache_entry_t *entry,
					 const struct req_op_context *req_ctx,
					 void *opaque,
					 cache_inode_getattr_cb_t cb);

cache_inode_status_t cache_inode_fileid(cache_entry_t *entry,
					const struct req_op_context *req_ctx,
					uint64_t *fileid);

cache_inode_status_t cache_inode_fsid(cache_entry_t *entry,
				      const struct req_op_context *req_ctx,
				      fsal_fsid_t *fsid);

cache_inode_status_t cache_inode_size(cache_entry_t *entry,
				      const struct req_op_context *req_ctx,
				      uint64_t *size);

bool cache_inode_create_verify(cache_entry_t *entry,
			       const struct req_op_context *req_ctx,
			       uint32_t verf_hi,
			       uint32_t verf_lo);

cache_inode_status_t cache_inode_lookup_impl(cache_entry_t *entry_parent,
					     const char *name,
					     struct req_op_context *req_ctx,
					     cache_entry_t **entry);
cache_inode_status_t cache_inode_lookup(cache_entry_t *entry_parent,
					const char *name,
					struct req_op_context *req_ctx,
					cache_entry_t **entry);

cache_inode_status_t cache_inode_lookupp_impl(cache_entry_t *entry,
					      struct req_op_context *req_ctx,
					      cache_entry_t **parent);
cache_inode_status_t cache_inode_lookupp(cache_entry_t *entry,
					 struct req_op_context *req_ctx,
					 cache_entry_t **parent);

cache_inode_status_t cache_inode_readlink(cache_entry_t *entry,
					  struct gsh_buffdesc *link_content,
					  struct req_op_context *req_ctx);

cache_inode_status_t cache_inode_link(cache_entry_t *entry_src,
				      cache_entry_t *entry_dir_dest,
				      const char *link_name,
				      struct req_op_context *req_ctx);

cache_inode_status_t cache_inode_remove(cache_entry_t *entry,
					const char *node_name,
					struct req_op_context *req_ctx);
cache_inode_status_t cache_inode_remove_impl(cache_entry_t *entry,
					     const char *name,
					     struct req_op_context *req_ctx,
					     uint32_t flags);

cache_inode_status_t cache_inode_clean_internal(
	cache_entry_t *to_remove_entry);

cache_inode_status_t cache_inode_operate_cached_dirent(
	cache_entry_t *entry_parent,
	const char *name,
	const char *newname,
	cache_inode_dirent_op_t dirent_op);

cache_inode_status_t cache_inode_remove_cached_dirent(
	cache_entry_t *entry_parent,
	const char *name);

cache_inode_status_t cache_inode_rename_cached_dirent(
	cache_entry_t *entry_parent,
	const char *oldname,
	const char *newname);

cache_inode_status_t cache_inode_rename(cache_entry_t *entry,
					const char *oldname,
					cache_entry_t *entry_dirdest,
					const char *newname,
					struct req_op_context *req_ctx);

cache_inode_status_t cache_inode_setattr(cache_entry_t *entry,
					 struct attrlist *attr,
					 struct req_op_context *req_ctx);

cache_inode_status_t
cache_inode_truncate_impl(cache_entry_t *entry,
			  uint64_t length,
			  struct req_op_context *req_ctx);
cache_inode_status_t cache_inode_truncate(
	cache_entry_t *entry,
	uint64_t length,
	struct req_op_context *req_ctx);

cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status);

cache_inode_status_t cache_inode_new_entry(struct fsal_obj_handle *new_obj,
					    uint32_t flags,
					    cache_entry_t **entry);

cache_inode_status_t cache_inode_rdwr(cache_entry_t *entry,
				      cache_inode_io_direction_t io_direction,
				      uint64_t offset,
				      size_t io_size,
				      size_t *bytes_moved,
				      void *buffer,
				      bool *eof,
				      struct req_op_context *req_ctx,
				      cache_inode_stability_t *stable);

static inline cache_inode_status_t cache_inode_read(
	cache_entry_t *entry,
	uint64_t offset,
	size_t io_size,
	size_t *bytes_moved,
	void *buffer,
	bool *eof,
	struct req_op_context *req_ctx,
	cache_inode_stability_t *stable)
{
	return cache_inode_rdwr(entry, CACHE_INODE_READ, offset, io_size,
				bytes_moved, buffer, eof, req_ctx,
				stable);
}

static inline cache_inode_status_t cache_inode_write(
	cache_entry_t *entry,
	uint64_t offset,
	size_t io_size,
	size_t *bytes_moved,
	void *buffer,
	bool *eof,
	struct req_op_context *req_ctx,
	cache_inode_stability_t *stable)
{
	return cache_inode_rdwr(entry, CACHE_INODE_WRITE, offset, io_size,
				bytes_moved, buffer, eof, req_ctx,
				stable);
}

cache_inode_status_t cache_inode_commit(cache_entry_t *entry,
					uint64_t offset,
					size_t count,
					struct req_op_context *req_ctx);

cache_inode_status_t cache_inode_readdir(cache_entry_t *directory,
					 uint64_t cookie,
					 unsigned int *nbfound,
					 bool *eod_met,
					 struct req_op_context *req_ctx,
					 cache_inode_readdir_cb_t cb,
					 void *cb_opaque);

cache_inode_status_t cache_inode_add_cached_dirent(
	cache_entry_t *parent,
	const char *name,
	cache_entry_t *entry,
	cache_inode_dir_entry_t **dir_entry);
cache_inode_status_t cache_inode_make_root(struct fsal_obj_handle *root_hdl,
					   cache_entry_t **root_entry);

cache_inode_status_t cache_inode_check_trust(cache_entry_t *entry,
					     const struct req_op_context *);

void cache_inode_print_dir(cache_entry_t *cache_entry_root);

cache_inode_status_t cache_inode_statfs(cache_entry_t *entry,
					fsal_dynamicfsinfo_t *dynamicinfo,
					const struct req_op_context *);

cache_inode_status_t cache_inode_invalidate_all_cached_dirent(
	cache_entry_t *entry);

void cache_inode_release_dirents(cache_entry_t *entry,
				 cache_inode_avl_which_t which);

void cache_inode_kill_entry(cache_entry_t *entry);

cache_inode_status_t cache_inode_invalidate(
	cache_entry_t *entry,
	uint32_t flags);

/* Parsing functions */
cache_inode_status_t cache_inode_read_conf_hash_parameter(
	config_file_t in_config,
	cache_inode_parameter_t *param);
cache_inode_status_t cache_inode_read_conf_parameter(
	config_file_t in_config,
	cache_inode_parameter_t *param);
cache_inode_status_t cache_inode_read_conf_gc_policy(
	config_file_t in_config,
	cache_inode_gc_policy_t *policy);
void cache_inode_print_conf_hash_parameter(FILE *output,
					   cache_inode_parameter_t *param);
void cache_inode_print_conf_parameter(
	FILE *output,
	cache_inode_parameter_t *param);
void cache_inode_print_conf_gc_policy(FILE *output,
				      cache_inode_gc_policy_t *gcpolicy);
void cache_inode_expire_to_str(cache_inode_expire_type_t type,
			       time_t value,
			       char *out);

inline int cache_inode_set_time_current(struct timespec *time);

/* Hash functions for hashtables and RBT */
uint32_t cache_inode_fsal_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *buff);
uint64_t cache_inode_fsal_rbt_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *buff);
int cache_inode_fsal_rbt_both(hash_parameter_t *hparam,
			      struct gsh_buffdesc *buffclef,
			      uint32_t *hashval,
			      uint64_t *rbtval);
int display_key(struct gsh_buffdesc *buff, char *str);
int display_not_implemented(struct gsh_buffdesc *buff,
			    char *str);
int display_value(struct gsh_buffdesc *buff, char *str);

/**
 * @brief Update cache_entry metadata from its attributes
 *
 * This function, to be used after a FSAL_getattr, yodates the
 * attribute trust flag and time, and stores the type and change time
 * in the main cache_entry_t.
 *
 * @param[in,out] entry The entry on which we operate.
 */

static inline void cache_inode_fixup_md(cache_entry_t *entry)
{
	/* Set the refresh time for the cache entry */
	entry->attr_time = time(NULL);
	/** @todo ACE: This should really be changed to use sub-second time
	    resolution when it's available. */
	entry->change_time = entry->obj_handle->attributes.chgtime.tv_sec;
	/* Almost certainly not necessary */
	entry->type = entry->obj_handle->attributes.type;
	/* We have just loaded the attributes from the FSAL. */
	entry->flags |= CACHE_INODE_TRUST_ATTRS;
}

/**
 * @brief Reload attributes from the FSAL.
 *
 * Load the FSAL attributes as specified in the configuration into
 * this entry, mark them as trustable and update the entry metadata.
 * Note that the caller must hold the write lock on the attributes.
 *
 * @param[in,out] entry   The entry to be refreshed
 * @param[in]     opctx   Request context (user creds, client address etc)
 */
/** @TODO possibly not really necessary?
 */
static inline cache_inode_status_t cache_inode_refresh_attrs(
	cache_entry_t *entry,
	const struct req_op_context *opctx)
{
	fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

	if (entry->obj_handle->attributes.acl) {
		fsal_acl_status_t acl_status = 0;

		nfs4_acl_release_entry(entry->obj_handle->attributes.acl,
				       &acl_status);
		if (acl_status != NFS_V4_ACL_SUCCESS) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "Failed to release old acl, status=%d",
				 acl_status);
		}
		entry->obj_handle->attributes.acl = NULL;
	}

	fsal_status = entry->obj_handle->ops->getattrs(entry->obj_handle,
						       opctx);
	if (FSAL_IS_ERROR(fsal_status)) {
		cache_inode_kill_entry(entry);
		cache_status
			= cache_inode_error_convert(fsal_status);
		goto out;
	}

	cache_inode_fixup_md(entry);

	cache_status = CACHE_INODE_SUCCESS;

out:
	return cache_status;
}

/**
 * @brief Return a changeid4 for this entry.
 *
 * This function returns a changeid4 for the supplied entry.  It
 * should ONLY be used for populating change_info4 structures.
 *
 * @param[in] entry The entry to query.
 *
 * @return A changeid4 indicating the last modification of the entry.
 */

static inline changeid4 cache_inode_get_changeid4(cache_entry_t *entry)
{
	return (changeid4) entry->change_time;
}

/**
 * @brief Lock attributes and check they are trustworthy
 *
 * This function acquires a read lock.  If the CACHE_INODE_TRUST_ATTRS
 * bit is not set, it drops the read lock, acquires a write lock, and,
 * if the bit is STILL not set, refreshes the attributes.  On success
 * this function will return with the attributes either read or write
 * locked.  It should only be used when read access is desired for
 * relatively short periods of time.
 *
 * @param[in,out] entry   The entry to lock and check
 * @param[in]     context The FSAL operation context
 *
 * @return CACHE_INODE_SUCCESS if the attributes are locked and
 *         trustworthy, various cache_inode error codes otherwise.
 */

static inline cache_inode_status_t cache_inode_lock_trust_attrs(
	cache_entry_t *entry,
	const struct req_op_context *opctx)
{
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;


	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);
	/* Do we need to refresh? */
	if (!(entry->flags & CACHE_INODE_TRUST_ATTRS) ||
	    FSAL_TEST_MASK(entry->obj_handle->attributes.mask,
			   ATTR_RDATTR_ERR)) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		/* Has someone else done it for us? */
		if (!(entry->flags & CACHE_INODE_TRUST_ATTRS) ||
		    FSAL_TEST_MASK(entry->obj_handle->attributes.mask,
				   ATTR_RDATTR_ERR)) {
			/* Release the lock on error */
			cache_status = cache_inode_refresh_attrs(entry, opctx);
			if (cache_status != CACHE_INODE_SUCCESS) {
				PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			}
		}
	}

	return cache_status;
}

#endif /* CACHE_INODE_H */
/** @} */
