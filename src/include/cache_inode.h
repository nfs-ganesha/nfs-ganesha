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
 * @defgroup cache_inode Cache Inode
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
#include "hashtable.h"
#include "avltree.h"
#include "fsal.h"
#include "log.h"
#include "gsh_config.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nlm4.h"
#include "ganesha_list.h"
#include "nfs4_acls.h"


/**
 * @defgroup config_cache_inode Structure and defaults for Cache_Inode
 *
 * @{
 */

/**
 * @brief Structure to hold cache_inode paramaters
 */

struct cache_inode_parameter {
	/** Partitions in the Cache_Inode tree.  Defaults to 7,
	 * settable with NParts. */
	uint32_t nparts;
	/** Expiration time interval in seconds for attributes.  Settable with
	    Attr_Expiration_Time. */
	int32_t  expire_time_attr;
	/** Use getattr for directory invalidation.  Defaults to
	    false.  Settable with Use_Getattr_Directory_Invalidation. */
	bool getattr_dir_invalidation;
	/** High water mark for cache entries.  Defaults to 100000,
	    settable by Entries_HWMark. */
	uint32_t entries_hwmark;
	/** Base interval in seconds between runs of the LRU cleaner
	    thread. Defaults to 60, settable with LRU_Run_Interval. */
	time_t lru_run_interval;
	/** Whether to cache open files.  Defaults to true, settable
	    with Cache_FDs. */
	bool use_fd_cache;
	/** The percentage of the system-imposed maximum of file
	    descriptors at which Ganesha will deny requests.
	    Defaults to 99, settable with FD_Limit_Percent. */
	uint32_t fd_limit_percent;
	/** The percentage of the system-imposed maximum of file
	    descriptors above which Ganesha will make greater efforts
	    at reaping. Defaults to 90, settable with
	    FD_HWMark_Percent. */
	uint32_t fd_hwmark_percent;
	/** The percentage of the system-imposed maximum of file
	    descriptors below which Ganesha will not reap file
	    descriptonot reap file descriptors.  Defaults to 50,
	    settable with FD_LWMark_Percent. */
	uint32_t fd_lwmark_percent;
	/** Roughly, the amount of work to do on each pass through the
	    thread under normal conditions.  (Ideally, a multiple of
	    the number of lanes.)  Defaults to 1000, settable with
	    Reaper_Work. */
	uint32_t reaper_work;
	/** The largest window (as a percentage of the system-imposed
	    limit on FDs) of work that we will do in extremis.
	    Defaults to 40, settable with Biggest_Window */
	uint32_t biggest_window;
	/** Percentage of progress toward the high water mark required
	    in in a pass through the thread when in extremis.
	    Defaults to 5, settable with Required_Progress. */
	uint32_t required_progress;
	/** Number of failures to approach the high watermark before
	    we disable caching, when in extremis.  Defaults to 8,
	    settable with Futility_Count */
	uint32_t futility_count;
	/** Behavior for when readdir fails for some reason:
	    true will ask the client to retry later, false will give the
	    client a partial reply based on what we have.
	    Defaults to false, settable with Retry_Readdir */
	bool retry_readdir;
};

/** @} */

extern struct config_block cache_inode_param_blk;
extern struct cache_inode_parameter cache_param;

/* Forward references */
typedef struct cache_entry_t cache_entry_t;
struct gsh_export;

/** Maximum size of NFSv3 handle */
static const size_t FILEHANDLE_MAX_LEN_V3 = 64;
/** Maximum size of NFSv4 handle */
static const size_t FILEHANDLE_MAX_LEN_V4 = 128;

/** Size for Ganesha unstable write buffers*/
static const size_t CACHE_INODE_UNSTABLE_BUFFERSIZE = 100 * 1024 * 1024;

/**
 * Data for tracking a cache entry's position the LRU.
 */

/*
 * Valid LRU queues.
 */
enum lru_q_id {
	LRU_ENTRY_NONE = 0 /* entry not queued */ ,
	LRU_ENTRY_L1,
	LRU_ENTRY_L2,
	LRU_ENTRY_PINNED,
	LRU_ENTRY_CLEANUP
};

typedef struct cache_inode_lru__ {
	enum lru_q_id qid;	/*< Queue identifier */
	struct glist_head q;	/*< Link in the physical deque
				   impelmenting a portion of the logical
				   LRU. */
	int32_t refcnt;		/*< Reference count.  This is signed to make
				   mistakes easy to see. */
	int32_t pin_refcnt;	/*< Unpin it only if this goes down to zero */
	uint32_t flags;		/*< Flags for details of this entry's status,
				 *< such as whether it is pinned and whether
				 *< it's in L1 or L2. */
	uint32_t lane;		/*< The lane in which an entry currently
				 *< resides, so we can lock the deque and
				 *< decrement the correct counter when moving
				 *< or deleting the entry. */
	uint32_t cf;		/*< Confounder */
} cache_inode_lru_t;

/**
 * cache inode statistics.
 */
struct cache_stats {
	uint64_t inode_req;
	uint64_t inode_hit;
	uint64_t inode_miss;
	uint64_t inode_conf;
	uint64_t inode_added;
	uint64_t inode_mapping;
};

extern struct cache_stats *cache_stp;

/**
 * Indicate whether this is a read or write operation, for
 * cache_inode_rdwr.
 */

typedef enum io_direction__ {
	CACHE_INODE_READ = 1,		/*< Reading */
	CACHE_INODE_WRITE = 2,		/*< Writing */
	CACHE_INODE_READ_PLUS = 3,	/*< Reading plus */
	CACHE_INODE_WRITE_PLUS = 4	/*< Writing plus */
} cache_inode_io_direction_t;

/**
 * Passed to cache_inode_operate_cached_dirent to indicate the
 * operation being requested.
 */

typedef enum cache_inode_dirent_op__ {
	CACHE_INODE_DIRENT_OP_LOOKUP = 1,	/*< Look up a name */
	CACHE_INODE_DIRENT_OP_REMOVE = 2,	/*< Remove a name */
	CACHE_INODE_DIRENT_OP_RENAME = 3	/*< Rename node */
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
 * @brief Structure representing a cache key.
 *
 * Wraps an underlying FSAL-specific key.
 */
typedef struct cache_inode_key {
	uint64_t hk;		/* hash key */
	struct fsal_module *fsal;	/*< fsal module */
	struct gsh_buffdesc kv;		/*< fsal handle */
} cache_inode_key_t;

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
static inline int
cache_inode_key_dup(cache_inode_key_t *tgt,
		    cache_inode_key_t *src)
{
	tgt->kv.len = src->kv.len;
	tgt->kv.addr = gsh_malloc(src->kv.len);

	if (!tgt->kv.addr)
		return ENOMEM;

	memcpy(tgt->kv.addr, src->kv.addr, src->kv.len);
	tgt->hk = src->hk;
	tgt->fsal = src->fsal;

	return 0;
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
cache_inode_key_delete(cache_inode_key_t *key)
{
	key->kv.len = 0;
	gsh_free(key->kv.addr);
	key->kv.addr = (void *)0xdeaddeaddeaddead;
}

/**
 * @brief Represents a cached directory entry
 *
 * This is a cached directory entry that associates a name and cookie
 * with a cache entry.
 */

#define DIR_ENTRY_FLAG_NONE     0x0000
#define DIR_ENTRY_FLAG_DELETED  0x0001

typedef struct cache_inode_dir_entry__ {
	struct avltree_node node_hk;	/*< AVL node in tree */
	struct {
		uint64_t k;	/*< Integer cookie */
		uint32_t p;	/*< Number of probes, an efficiency metric */
	} hk;
	cache_inode_key_t ckey;	/*< Key of cache entry */
	uint32_t flags;		/*< Flags */
	char name[];		/*< The NUL-terminated filename */
} cache_inode_dir_entry_t;

/**
 * @brief Deep free a dirent.
 *
 * Deep free a dirent..
 *
 * @param dirent [in] Dirent to be freed.
 *
 * @return Pointer to node if found, else NULL.
 */
static inline void
cache_inode_free_dirent(cache_inode_dir_entry_t *dirent)
{
	if (dirent->ckey.kv.addr)
		gsh_free(dirent->ckey.kv.addr);
	gsh_free(dirent);
}

/**
 * @brief Represents one of the many-many links between inodes and exports.
 *
 */

struct entry_export_map {
	/** The relevant cache inode entry */
	cache_entry_t *entry;
	/** The export the entry belongs to */
	struct gsh_export *export;
	/** List of entries per export */
	struct glist_head entry_per_export;
	/** List of exports per entry */
	struct glist_head export_per_entry;
};

/**
 * @brief Stats for file-specific and client-file delegation heuristics
 */

struct file_deleg_stats {
	uint32_t fds_curr_delegations;    /* number of delegations on file */
	open_delegation_type4 fds_deleg_type; /* delegation type */
	uint32_t fds_delegation_count;  /* times file has been delegated */
	uint32_t fds_recall_count;      /* times file has been recalled */
	time_t fds_avg_hold;            /* avg amount of time deleg held */
	time_t fds_last_delegation;
	time_t fds_last_recall;
	uint32_t fds_num_opens;         /* total num of opens so far. */
	time_t fds_first_open;          /* time that we started recording
					   num_opens */
};

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
 * The handle, cache key, and type fields are unprotected, as they are
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
	/** The FSAL Handle */
	struct fsal_obj_handle *obj_handle;
	/** FH hash linkage */
	struct {
		struct avltree_node node_k;	/*< AVL node in tree */
		cache_inode_key_t key;	/*< Key of this entry */
		bool inavl;
	} fh_hk;
	/** The type of the entry */
	object_file_type_t type;
	/** Flags for this entry */
	uint32_t flags;
	/** refcount for number of active icreate */
	int32_t icreate_refcnt;
	/** The time of the last operation ganesha knows about.  We
	    can ue this for change_info4, but atomic MUST be set to
	    false.  Don't use it for anything else (servicing getattr,
	    etc.) */
	time_t change_time;
	/** Time at which we last refreshed attributes. */
	time_t attr_time;
	/** New style LRU link */
	cache_inode_lru_t lru;
	/** There is one export root reference counted for each export
	    for which this entry is a root for. This field is used
	    with the atomic inc/dec/fetch routines. */
	int32_t exp_root_refcount;
	/** This is separated out from the content lock, since there
	    are state oerations that don't affect anything guarded by
	    content (for example, a layout return or request has no
	    effect on a cached file handle) and the content lock may
	    be released and reacquired several times in an operation
	    that should not see changes in state. */
	pthread_rwlock_t state_lock;
	/** States on this cache entry */
	struct glist_head state_list;
	/** Exports per entry (protected by attr_lock) */
	struct glist_head export_list;
	/** Atomic pointer to the first mapped export for fast path */
	void *first_export;
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
			/** Pointers for delegation list */
			struct glist_head deleg_list;
			/** Pointers for NLM share list */
			struct glist_head nlm_share_list;
			/** Share reservation state for this file. */
			cache_inode_share_t share_state;
			bool write_delegated; /* true iff write delegated */
			/** Delegation statistics */
			struct file_deleg_stats fdeleg_stats;
			uint32_t anon_ops;   /* number of anonymous operations
					      * happening at the moment which
					      * prevents delegations from being
					      * granted */
		} file;		/*< REGULAR_FILE data */

		struct {
			/** Number of known active children */
			uint32_t nbactive;
			/** The parent of this directory ('..') */
			cache_inode_key_t parent;
			struct {
				/** Children */
				struct avltree t;
				/** Persist cookies */
				struct avltree c;
				/** Heuristic. Expect 0. */
				uint32_t collisions;
			} avl;
			/** If this is a junction, the export this node points
			    to. Protected by the attr_lock. */
			struct gsh_export *junction_export;
			/** List of exports that have this cache inode
			    as their root. Protected by the attr_lock. */
			struct glist_head export_roots;
		} dir;		/*< DIRECTORY data */
	} object;
};

/**
 * Data to be used as the key into the cache_entry hash table.
 */

typedef struct cache_inode_fsal_data {
	struct fsal_export *export;	/*< export owning this handle */
	struct gsh_buffdesc fh_desc;	/*< FSAL handle descriptor  */
} cache_inode_fsal_data_t;

/**
 * Global memory pools for cached data
 */

/** Cache entries pool */
extern pool_t *cache_inode_entry_pool;

/**
 * Type-specific data passed to cache_inode_new_entry
 */

typedef union cache_inode_create_arg {
	fsal_dev_t dev_spec;	/*< Major/minor numbers for a device file */
	char *link_content;	/*< Just stash the pointer. */
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
/** Open for reclaim. */
static const uint32_t CACHE_INODE_FLAG_RECLAIM = 0x200;

/**
 * Flags to cache_inode_invalidate
 */
static const uint32_t CACHE_INODE_INVALIDATE_ATTRS = 0x01;
static const uint32_t CACHE_INODE_INVALIDATE_CONTENT = 0x02;
static const uint32_t CACHE_INODE_INVALIDATE_CLOSE = 0x04;
static const uint32_t CACHE_INODE_INVALIDATE_GOT_LOCK = 0x08;

/**
 * Possible errors
 */
typedef enum cache_inode_status_t {
	CACHE_INODE_SUCCESS = 0,
	CACHE_INODE_MALLOC_ERROR = 1,
	CACHE_INODE_POOL_MUTEX_INIT_ERROR = 2,
	CACHE_INODE_GET_NEW_LRU_ENTRY = 3,
	CACHE_INODE_INIT_ENTRY_FAILED = 4,
	CACHE_INODE_FSAL_ERROR = 5,
	CACHE_INODE_LRU_ERROR = 6,
	CACHE_INODE_HASH_SET_ERROR = 7,
	CACHE_INODE_NOT_A_DIRECTORY = 8,
	CACHE_INODE_INCONSISTENT_ENTRY = 9,
	CACHE_INODE_BAD_TYPE = 10,
	CACHE_INODE_ENTRY_EXISTS = 11,
	CACHE_INODE_DIR_NOT_EMPTY = 12,
	CACHE_INODE_NOT_FOUND = 13,
	CACHE_INODE_INVALID_ARGUMENT = 14,
	CACHE_INODE_INSERT_ERROR = 15,
	CACHE_INODE_HASH_TABLE_ERROR = 16,
	CACHE_INODE_FSAL_EACCESS = 17,
	CACHE_INODE_IS_A_DIRECTORY = 18,
	CACHE_INODE_FSAL_EPERM = 19,
	CACHE_INODE_NO_SPACE_LEFT = 20,
	CACHE_INODE_READ_ONLY_FS = 21,
	CACHE_INODE_IO_ERROR = 22,
	CACHE_INODE_FSAL_ESTALE = 23,
	CACHE_INODE_FSAL_ERR_SEC = 24,
	CACHE_INODE_STATE_CONFLICT = 25,
	CACHE_INODE_QUOTA_EXCEEDED = 26,
	CACHE_INODE_DEAD_ENTRY = 27,
	CACHE_INODE_ASYNC_POST_ERROR = 28,
	CACHE_INODE_NOT_SUPPORTED = 29,
	CACHE_INODE_STATE_ERROR = 30,
	CACHE_INODE_DELAY = 31,
	CACHE_INODE_NAME_TOO_LONG = 32,
	CACHE_INODE_BAD_COOKIE = 33,
	CACHE_INODE_FILE_BIG = 34,
	CACHE_INODE_KILLED = 35,
	CACHE_INODE_FILE_OPEN = 36,
	CACHE_INODE_FSAL_XDEV = 37,
	CACHE_INODE_FSAL_MLINK = 38,
	CACHE_INODE_SERVERFAULT = 39,
	CACHE_INODE_TOOSMALL = 40,
	CACHE_INODE_FSAL_SHARE_DENIED = 41,
	CACHE_INODE_BADNAME = 42,
	CACHE_INODE_UNION_NOTSUPP = 43,
	CACHE_INODE_CROSS_JUNCTION = 44,
	CACHE_INODE_IN_GRACE = 45,
	CACHE_INODE_BADHANDLE = 46,
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

struct cache_inode_readdir_cb_parms {
	void *opaque;		/*< Protocol specific parms */
	const char *name;	/*< Dir entry name */
	bool attr_allowed;	/*< True if caller has perm to getattr */
	uint64_t cookie;	/*< Directory cookie for this entry */
	bool in_result;		/*< true if the entry has been added to the
				 *< caller's responde, or false if the
				 *< structure is filled and the entry has not
				 *< been added. */
};

/**
 * @brief Type of callback for cache_inode_getattr
 *
 * This callback provides an easy way to read object attributes for
 * callers that wish to avoid the details of cache_inode's locking and
 * cache coherency.  This function is provided with a pointer to the
 * object's attributes and an opaque void.  Whatever value it returns
 * is returned as the result of the cache_inode_getattr call.
 */

typedef cache_inode_status_t (*cache_inode_getattr_cb_t)
	(void *opaque,
	 cache_entry_t *entry,
	 const struct attrlist *attr,
	 uint64_t mounted_on_fileid);

const char *cache_inode_err_str(cache_inode_status_t err);

int cache_inode_compare_key_fsal(struct gsh_buffdesc *buff1,
				 struct gsh_buffdesc *buff2);

cache_inode_status_t cache_inode_init(void);

#define CIG_KEYED_FLAG_NONE         0x0000
#define CIG_KEYED_FLAG_CACHED_ONLY  0x0001

bool check_mapping(cache_entry_t *entry,
		   struct gsh_export *export);
void clean_mapping(cache_entry_t *entry);
cache_inode_status_t cache_inode_get(cache_inode_fsal_data_t *fsdata,
				     cache_entry_t **entry);
cache_entry_t *cache_inode_get_keyed(cache_inode_key_t *key,
				     uint32_t flags,
				     cache_inode_status_t *status);
cache_inode_status_t
cache_inode_get_protected(cache_entry_t **entry,
			  pthread_rwlock_t *lock,
			  cache_inode_status_t get_entry(cache_entry_t **,
							 void *),
			  void *source);
void cache_inode_put(cache_entry_t *entry);
void cache_inode_unexport(struct gsh_export *export);

cache_inode_status_t cache_inode_access_sw(cache_entry_t *entry,
					   fsal_accessflags_t access_type,
					   fsal_accessflags_t *allowed,
					   fsal_accessflags_t *denied,
					   bool use_mutex);
/**
 *
 * @brief Checks entry permissions without taking a lock
 *
 * This function checks whether the specified permissions are
 * available on the object.  This function may only be called if an
 * attribute lock is held.
 *
 * @param[in] entry       entry pointer for the fs object to be checked.
 * @param[in] access_type The kind of access to be checked
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
static inline cache_inode_status_t
cache_inode_access_no_mutex(cache_entry_t *entry,
			    fsal_accessflags_t access_type)
{
	return cache_inode_access_sw(entry, access_type, NULL, NULL, false);
}

/**
 *
 * @brief Checks permissions on an entry
 *
 * This function acquires the attribute lock on the supplied cach
 * entry then checks if the supplied credentials are sufficient to
 * gain the supplied access.
 *
 * @param[in] entry       The object to be checked
 * @param[in] access_type The kind of access to be checked
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
static inline cache_inode_status_t
cache_inode_access(cache_entry_t *entry,
		   fsal_accessflags_t access_type)
{
	return cache_inode_access_sw(entry, access_type, NULL, NULL, true);
}

cache_inode_status_t
cache_inode_check_setattr_perms(cache_entry_t *entry,
				struct attrlist *attr,
				bool is_open_write);

bool is_open(cache_entry_t *entry);
bool is_open_for_read(cache_entry_t *entry);
bool is_open_for_write(cache_entry_t *entry);

cache_inode_status_t cache_inode_open(cache_entry_t *entry,
				      fsal_openflags_t openflags,
				      uint32_t flags);
cache_inode_status_t cache_inode_close(cache_entry_t *entry, uint32_t flags);
void cache_inode_adjust_openflags(cache_entry_t *entry);

cache_inode_status_t cache_inode_create(cache_entry_t *entry_parent,
					const char *name,
					object_file_type_t type, uint32_t mode,
					cache_inode_create_arg_t *create_arg,
					cache_entry_t **created);

cache_inode_status_t cache_inode_getattr(cache_entry_t *entry,
					 void *opaque,
					 cache_inode_getattr_cb_t cb);

cache_inode_status_t cache_inode_fileid(cache_entry_t *entry,
					uint64_t *fileid);

cache_inode_status_t cache_inode_fsid(cache_entry_t *entry,
				      fsal_fsid_t *fsid);

cache_inode_status_t cache_inode_size(cache_entry_t *entry,
				      uint64_t *size);

void cache_inode_create_set_verifier(struct attrlist *sattr, uint32_t verf_hi,
				     uint32_t verf_lo);

bool cache_inode_create_verify(cache_entry_t *entry,
			       uint32_t verf_hi, uint32_t verf_lo);

cache_inode_status_t cache_inode_lookup_impl(cache_entry_t *entry_parent,
					     const char *name,
					     cache_entry_t **entry);

cache_inode_status_t cache_inode_lookup(cache_entry_t *entry_parent,
					const char *name,
					cache_entry_t **entry);

cache_inode_status_t cache_inode_lookupp_impl(cache_entry_t *entry,
					      cache_entry_t **parent);

cache_inode_status_t cache_inode_lookupp(cache_entry_t *entry,
					 cache_entry_t **parent);

cache_inode_status_t cache_inode_readlink(cache_entry_t *entry,
					  struct gsh_buffdesc *link_content);

cache_inode_status_t cache_inode_link(cache_entry_t *entry_src,
				      cache_entry_t *entry_dir_dest,
				      const char *link_name);

cache_inode_status_t cache_inode_remove(cache_entry_t *entry,
					const char *node_name);

cache_inode_status_t cache_inode_operate_cached_dirent(
	cache_entry_t *entry_parent, const char *name, const char *newname,
	cache_inode_dirent_op_t dirent_op);

cache_inode_status_t cache_inode_remove_cached_dirent(
	cache_entry_t *entry_parent, const char *name);

cache_inode_status_t cache_inode_rename_cached_dirent(
	cache_entry_t *entry_parent, const char *oldname, const char *newname);

cache_inode_status_t cache_inode_rename(cache_entry_t *entry,
					const char *oldname,
					cache_entry_t *entry_dirdest,
					const char *newname);

cache_inode_status_t cache_inode_setattr(cache_entry_t *entry,
					 struct attrlist *attr,
					 bool is_open_write);

cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status);

cache_inode_status_t cache_inode_new_entry(struct fsal_obj_handle *new_obj,
					   uint32_t flags,
					   cache_entry_t **entry);

cache_inode_status_t cache_inode_rdwr(cache_entry_t *entry,
				      cache_inode_io_direction_t io_direction,
				      uint64_t offset, size_t io_size,
				      size_t *bytes_moved, void *buffer,
				      bool *eof,
				      bool *sync);

cache_inode_status_t cache_inode_rdwr_plus(cache_entry_t *entry,
				      cache_inode_io_direction_t io_direction,
				      uint64_t offset, size_t io_size,
				      size_t *bytes_moved, void *buffer,
				      bool *eof,
				      bool *sync, struct io_info *info);

cache_inode_status_t cache_inode_commit(cache_entry_t *entry, uint64_t offset,
					size_t count);

cache_inode_status_t cache_inode_readdir(cache_entry_t *directory,
					 uint64_t cookie, unsigned int *nbfound,
					 bool *eod_met,
					 attrmask_t attrmask,
					 cache_inode_getattr_cb_t cb,
					 void *opaque);

cache_inode_status_t cache_inode_add_cached_dirent(
	cache_entry_t *parent, const char *name, cache_entry_t *entry,
	cache_inode_dir_entry_t **dir_entry);

cache_inode_status_t cache_inode_lock_trust_attrs(cache_entry_t *entry,
						  bool need_wr_lock);

void cache_inode_print_dir(cache_entry_t *cache_entry_root);

cache_inode_status_t cache_inode_statfs(cache_entry_t *entry,
					fsal_dynamicfsinfo_t *dynamicinfo);

cache_inode_status_t cache_inode_invalidate_all_cached_dirent(
	cache_entry_t *entry);

void cache_inode_release_dirents(cache_entry_t *entry,
				 cache_inode_avl_which_t which);

void cache_inode_kill_entry(cache_entry_t *entry);

cache_inode_status_t cache_inode_invalidate(cache_entry_t *entry,
					    uint32_t flags);

inline int cache_inode_set_time_current(struct timespec *time);

void cache_inode_destroyer(void);

/**
 * @brief Update cache_entry metadata from its attributes
 *
 * This function, to be used after a FSAL_getattr, updates the
 * attribute trust flag and time, and stores the type and change time
 * in the main cache_entry_t.
 *
 * @param[in,out] entry The entry on which we operate.
 */

static inline void
cache_inode_fixup_md(cache_entry_t *entry)
{
	/* Set the refresh time for the cache entry */
	if (entry->obj_handle->attributes.expire_time_attr > 0)
		entry->attr_time = time(NULL);
	else
		entry->attr_time = 0;

	/* I don't like using nsecs as a counter, it will be annoying in
	 * 500 years.  I'll fix to match MS nano-intervals later.
	 *
	 * Also, fsal attrs has a changetime.
	 * (Matt). */
	entry->change_time =
	    timespec_to_nsecs(&entry->obj_handle->attributes.chgtime);

	/* Almost certainly not necessary */
	entry->type = entry->obj_handle->attributes.type;
	/* We have just loaded the attributes from the FSAL. */
	entry->flags |= CACHE_INODE_TRUST_ATTRS;
}

/**
 * @brief Check if attributes are valid
 *
 * The caller must hold the read lock on the attributes.
 *
 * @param[in] entry     The entry to check
 */

static inline bool
cache_inode_is_attrs_valid(const cache_entry_t *entry)
{
	if (!(entry->flags & CACHE_INODE_TRUST_ATTRS))
		return false;

	if (FSAL_TEST_MASK(entry->obj_handle->attributes.mask, ATTR_RDATTR_ERR))
		return false;

	if (entry->type == DIRECTORY
	    && cache_param.getattr_dir_invalidation)
		return false;

	if (entry->obj_handle->attributes.expire_time_attr == 0)
		return false;

	if (entry->obj_handle->attributes.expire_time_attr > 0) {
		time_t current_time = time(NULL);
		if (current_time - entry->attr_time >
		    entry->obj_handle->attributes.expire_time_attr)
			return false;
	}

	return true;
}

/**
 * @brief Reload attributes from the FSAL.
 *
 * Load the FSAL attributes as specified in the configuration into
 * this entry, mark them as trustable and update the entry metadata.
 * Note that the caller must hold the write lock on the attributes.
 *
 * @todo Possibly not really necessary?
 *
 * @param[in,out] entry   The entry to be refreshed
 */

static inline cache_inode_status_t
cache_inode_refresh_attrs(cache_entry_t *entry)
{
	fsal_status_t fsal_status = { ERR_FSAL_NO_ERROR, 0 };
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

	fsal_status =
	    entry->obj_handle->ops->getattrs(entry->obj_handle);
	if (FSAL_IS_ERROR(fsal_status)) {
		cache_inode_kill_entry(entry);
		cache_status = cache_inode_error_convert(fsal_status);
		LogDebug(COMPONENT_CACHE_INODE, "Failed on entry %p %s", entry,
			 cache_inode_err_str(cache_status));
		goto out;
	}

	cache_inode_fixup_md(entry);

 out:
	return cache_status;
}

/**
 * @brief Reload attributes from the FSAL.
 *
 * Load the FSAL attributes as specified in the configuration into
 * this entry, mark them as trustable and update the entry metadata.
 *
 * @param[in,out] entry   The entry to be refreshed
 */
static inline cache_inode_status_t
cache_inode_refresh_attrs_locked(cache_entry_t *entry)
{
	cache_inode_status_t status;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	status = cache_inode_refresh_attrs(entry);

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	return status;
}

/**
 * @brief Return a changeid4 for this entry.
 *
 * This function returns a changeid4 for the supplied entry.  It
 * should ONLY be used for populating change_info4 structures.
 *
 * @param[in] entry   The entry to query.
 *
 * @return A changeid4 indicating the last modification of the entry.
 */

static inline changeid4
cache_inode_get_changeid4(cache_entry_t *entry)
{
	cache_inode_status_t status;
	changeid4 changeid;
	status = cache_inode_lock_trust_attrs(entry, false);

	changeid = (changeid4) entry->change_time;

	if (status == CACHE_INODE_SUCCESS)
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	return changeid;
}

#endif				/* CACHE_INODE_H */
/** @} */
