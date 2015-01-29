/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file fsal_api.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief The object-oriented FSAL API
 */

#ifndef FSAL_API
#define FSAL_API

#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "config_parsing.h"
#include "avltree.h"
#include "abstract_atomic.h"

/**
** Forward declarations to resolve circular dependency conflicts
*/
struct gsh_client;
struct gsh_export;
struct fsal_up_vector;		/* From fsal_up.h */

/**
 * @page newapi New FSAL API
 *
 * @section structs Public and Private Data Structures
 *
 * Shared FSAL data structures have two definitions, one that is
 * global and passed around by the core, the other private which
 * included the global definition within it.
 *
 * All these data structures are passed back to the core with the
 * global pointer and dereferenced with container_of within the FSAL
 * itself like so:
 *
 * @code{.c}
 *
 * struct private_obj_handle
 * {
 *         [private stuff]
 *         struct fsal_obj_handle *pub;
 * }
 *
 * fsal_getattr(struct fsal_obj_handle handle_pub)
 * {
 *         struct private_obj_handle *handle;
 *
 *         handle = container_of(handle_pub,
 *                               struct private_obj_handle, pub);
 *         [ do stuff ]
 * }
 * @endcode
 *
 * The @c container_of macro takes the public pointer/handle @c
 * handle_pub which is indicated as the element @c pub of structure
 * type @c private_obj_handle.  Throughout the function, where private
 * elements are dereferenced, the @c handle pointer is used.  The @c
 * handle_pub pointer is used in the public case.
 *
 * @section usage Object usage
 *
 * Mutex locks and reference counts are used to manage both concurrent
 * usage and state.  The reference counts are use to determine when
 * the object is "free".  Current use is for managing ref counts and
 * lists.  This will be expanded, though many cases are already
 * handled by the locks in Cache inode.
 *
 * Since we cannot create objects out of thin air, there is an order
 * based on one object being the "context" in which the other is
 * created.  In other words, a @c fsal_export is created from the @c
 * fsal_module that connects it to the backing store (filesystem). The
 * same applies to a @c fsal_obj_handle that only makes sense for a
 * specific 'fsal_export'.
 *
 * When an object is created, it is returned with a reference already
 * taken.  The callee of the creating method must then either keep a
 * persistent reference to it or @c put it back.  For example, a @c
 * fsal_export gets created for each export in the configuration.  A
 * pointer to it gets saved in @c gsh_export and it has a reference
 * to reflect this.  It is now safe to use it to do a @c lookup which
 * will return a @c fsal_obj_handle which can then be kept in a cache
 * inode entry.  If we had done a @c put on the export, it could be
 * freed at any point and make a @c lookup using it unsafe.
 *
 * In addition to a reference count, object that create other objects
 * have a list of all the objects they create.  This serves two
 * purposes. The obvious case is to keep the object "busy" until all
 * of its children are freed.  Second, it provides a means to visit
 * all of the objects it creates.
 *
 * Every object has a pointer to its parent.  This is used for such
 * things as managing the object list and for calling methods on the
 * parent.
 *
 * @section versioning Versioning
 *
 * One intent in this API is to be able to support fsals that are built
 * out-of-tree and possibly out of synch with the core of Ganesha.  This
 * is managed by version numbers in this file that are validated at load
 * time for the fsal.  There are major and minor version numbers which are
 * monotonically increasing numbers ( V1 < V2 means V2 is newer).
 *
 * API guarantee:
 *
 * * If major version numbers differ, the fsal will not be loaded because
 *   the api has changed enough to make it unsafe.
 *
 * * If the major versions are equal, the minor version determines loadability.
 *
 *   - A fsal that is older than the Ganesha core can safely load and run.
 *
 *   - A fsal that is newer than the Ganesha core is not safe and will not
 *     be loaded.
 *
 *
 * @section vector Operation Vectors
 *
 * Each structure carries with it an @c ops pointer.  Default
 * operation vectors are created at FSAL moduel initialziation time,
 * and may be overridden there.  Individual exports or handles may
 * have different operations vectors, but they should all be derived
 * from the module operations vector.
 *
 *	This vector is used to access methods e.g.:
 *
 * @code{.c}
 * exp_hdl->ops->lookup(exp_hdl, name, ...);
 * @endcode
 *
 * Note that exp_hdl is used to dereference the method and it is also
 * *always* the first argument to the method/function.  Think of it as
 * the 'this' argument.
 *
 * @section Operation Context
 *
 * Protocol operations have lots of state such as user creds, the
 * export currently in use etc.  Rather than pass all this down the
 * stack we take advantage of the design decision that a protocol
 * operation runs to completion in the thread that dequeued the
 * request from the RPC.  All of the operation state (other than
 * some intermediate results passed as function args) are pointed
 * to by the thread local 'op_ctx'.  This will always point to a
 * valid and initialized 'struct req_op_context'.
 *
 *	Method code can reference through 'op_ctx' e.g.
 *
 * @code{.c}
 * if (op_ctx->req_type == 9P) { ... }
 * @endcode
 *
 */

/**
 * @page handles File-Handles and You
 *
 * Overview
 * ========
 *
 * In the FSAL, file handles can take three forms.  There is the full,
 * internal handle structure, compose of the @c fsal_obj_handle and
 * the FSAL-private structure that contains it.
 *
 * There is the wire-handle, the FSAL-generated portion of the
 * file handles exchanged between Ganesha and its clients through the
 * FS protocol.  The wire-handle should contain everything necessary
 * to find and use the file even if the file has been completely
 * purged from cache or Ganesha has restarted from nothing.  There may
 * be multiple wire-handles per @c fsal_obj_handle.  The wire-handle
 * is produced by the @c handle_digest method on @c fsal_obj_handle.
 * The @c create_handle on @c fsal_export produces a new
 * @c fsal_obj_handle from a wire-handle.
 *
 * There is the handle-key, the portion of the handle that contains
 * all and only information that uniquely identifies the handle within
 * the entire FSAL (it is insufficient if it only identifies it within
 * the export or within a filesystem.)  There are two functions that
 * generate a handle-key, one is the @c extract_handle method on @c
 * fsal_export.  It is used to get the key from a wire-handle so that
 * it can be looked up in the cache.  The other is @c handle_to_key on
 * @c fsal_obj_handle.  This is used after lookup or some other
 * operation that produces a @c fsal_obj_handle so that it can be
 * stored or looked up in the cache.
 *
 * The invariant to be maintained is that given an @c fsal_obj_handle,
 * fh, extract_handle(digest_handle(fh)) = handle_to_key(fh).
 *
 * History and Details
 * ===================
 *
 * The terminology is confusing here.  The old function names were
 * kept (up to a point), but the semantics differ in ways both subtle
 * and catastrophic. Making matters worse, that the first FSAL written
 * was VFS, where the internal @c file_handle for the syscalls is the
 * whole of the key, opaque, and syscall arg.  This does not imply any
 * equivalence.
 *
 * In the old regime, the only place available to store _anything_ was
 * the handle array in @c cache_entry_t.  People overloaded it with
 * all kinds of rubbish as a result, and the wire-handle, the
 * handle-key, and other stuff get mushed together.  To sort things
 * out,
 *
 * 1. The wire-handle opaque _must_ be enough to re-acquire the cache
 *    entry and its associated @c fsal_obj_handle.  Other than that,
 *    it doesn't matter a whit. The client treats the whole protocol
 *    handle (including what is in the opaque) as an opaque token.
 *
 * 2. The purpose of the @c export_id in the protocol "handle" is to
 *    locate the FSAL that knows what is inside the opaque.  The @c
 *    extract_handle is an export method for that purpose.  It should
 *    be able to take the protocol handle opaque and translate it into
 *    a handle-key that @c cache_inode_get can use to find an entry.
 *
 * 3. cache_inode_get takes an fh_desc argument which is not a
 *    handle but a _key_.  It is used to generate the hash and to do
 *    the secondary key compares.  That is all it is used for.  The
 *    end result _must_ be a cache entry and its associated
 *    @c fsal_obj_handle. See how @c cache_inode_get transitions to
 *    cache_inode_new to see how this works.
 *
 * 4. The @c handle_to_key method, a @c fsal_obj_handle method,
 *    generates a key for the cache inode hash table from the contents
 *    of the @c fsal_obj_handle.  It is an analogue of extract_handle.
 *    Note where it is called to see why it is there.
 *
 * 5. The digest method is similar in scope but it is the inverse of
 *    @c extract_handle.  It's job is to fill in the opaque part of a
 *    protocol handle.  Note that it gets passed a @c gsh_buffdesc
 *    that describes the full opaque storage in whatever protocol
 *    specific structure is used.  It's job is to put whatever it
 *    takes into the opaque so the second and third items in this list
 *    work.
 *
 * 6. Unlike the old API, a @c fsal_obj_handle is part of a FSAL
 *    private structure for the object.  Note that there is no handle
 *    member of this public structure.  The bits necessary to both
 *    create a wire handle and use a filesystem handle go into this
 *    private structure. You can put whatever you is required into the
 *    private part.  Since both @c fsal_export and @c fsal_obj_handle
 *    have private object storage, you could even do things like have
 *    a container anchored in the export object that maps the
 *    FSAL-external handle to the filesystem data needed to talk to
 *    the filesystem.  If you need more info to deal with handles
 *    differing due to hard-links, this is where you would put
 *    it.  You would also have some other context in this private data
 *    to do the right thing.  Just make sure there is a way to
 *    disambiguate the multiple cases.  We do have to observe UNIX
 *    semantics here.
 *
 * The upper layers don't care about the private handle data.  All
 * they want is to be able to get something out from the object
 * (result of a lookup) so it can find the object again later.  The
 * obvious case is what you describe in @c nfs[34]_FhandleToCache.  These
 * various methods make that happen.
 *
 * The linkage between a @c cache_entry_t and a @c fsal_obj_handle is
 * 1-to-1 so we should really think of them as one, single object.  In
 * fact, there should never be a cache_entry without its associated @c
 * fsal_obj_handle.  The @c cache_entry_t is the cache inode part
 * where things like locks and object type stuff (the AVL tree for
 * dirs) are kept.  The @c fsal_obj_handle part that it points to
 * holds the FSAL specific part where the FD (or its backend's equiv),
 * open state, and anything needed for talking to the system or
 * libraries.
 */

/**
 * @brief Major Version
 *
 * Increment this whenever any part of the existing API is changed,
 * e.g.  the argument list changed or a method is removed.
 */

#define FSAL_MAJOR_VERSION 2

/**
 * @brief Minor Version
 *
 * Increment this whenever a new method is appended to the ops vector.
 * The remainder of the API is unchanged.
 *
 * If the major version is incremented, reset the minor to 0 (zero).
 *
 * If new members are appended to struct req_op_context (following its own
 * rules), increment the minor version
 */

#define FSAL_MINOR_VERSION 0

/* Forward references for object methods */

struct fsal_export;
struct export_ops;
struct fsal_obj_handle;
struct fsal_obj_ops;
struct fsal_filesystem;
struct fsal_ds_handle;
struct fsal_ds_ops;

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif
#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE 4
#endif

struct io_info {
	contents io_content;
	uint32_t io_advise;
	bool_t   io_eof;
};

struct io_hints {
	offset4  offset;
	length4  count;
	uint32_t hints;
};

/**
 * @brief request op context
 *
 * This is created early in the operation with the context of the
 * operation.  The difference between "context" and request parameters
 * or arguments is that the context is derived information such as
 * the resolved credentials, socket (network and client host) data
 * and other bits of environment associated with the request.  It gets
 * passed down the call chain only as far as it needs to go for the op
 * i.e. don't put it in the function/method proto "just because".
 *
 * The lifetime of this structure and all the data it points to is the
 * operation for V2,3 and the compound for V4+.  All elements and what
 * they point to are invariant for the lifetime.
 *
 * NOTE: This is an across-the-api shared structure.  It must survive with
 *       older consumers of its contents.  Future development can change
 *       this struct so long as it follows the rules:
 *
 *       1. New elements are appended at the end, never inserted in the middle.
 *
 *       2. This structure _only_ contains pointers and simple scalar values.
 *
 *       3. Changing an already defined struct pointer is strictly not allowed.
 *
 *       4. This struct is always passed by reference, never by value.
 *
 *       5. This struct is never copied/saved.
 *
 *       6. Code changes are first introduced in the core.  Assume the fsal
 *          module does not know and the code will still do the right thing.
 */

struct req_op_context {
	struct user_cred *creds;	/*< resolved user creds from request */
	struct user_cred original_creds;	/*< Saved creds */
	struct group_data *caller_gdata;
	gid_t *caller_garray_copy;	/*< Copied garray from AUTH_SYS */
	gid_t *managed_garray_copy;	/*< Copied garray from managed gids */
	int	cred_flags;		/* Various cred flags */
	sockaddr_t *caller_addr;	/*< IP connection info */
	const uint64_t *clientid;	/*< Client ID of caller, NULL if
					   unknown/not applicable. */
	uint32_t nfs_vers;	/*< NFS protocol version of request */
	uint32_t nfs_minorvers;	/*< NFSv4 minor version */
	uint32_t req_type;	/*< request_type NFS | 9P */
	struct gsh_client *client;	/*< client host info including stats */
	struct gsh_export *export;	/*< current export */
	struct fsal_export *fsal_export;	/*< current fsal export */
	struct export_perms *export_perms;	/*< Effective export perms */
	nsecs_elapsed_t start_time;	/*< start time of this op/request */
	nsecs_elapsed_t queue_wait;	/*< time in wait queue */
	void *fsal_private;		/*< private for FSAL use */
	struct fsal_module *fsal_module;	/*< current fsal module */
	/* add new context members here */
};

/**
 * @brief FSAL object definition
 *
 * This structure is the base FSAL instance definition, providing the
 * public face to a single, loaded FSAL.
 */

struct fsal_module {
	struct glist_head fsals;	/*< link in list of loaded fsals */
	pthread_rwlock_t lock;		/*< Lock to be held when
					    manipulating the list of exports. */
	int32_t refcount;		/*< Reference count */
	struct glist_head exports;	/*< Head of list of exports from
					   this FSAL */
	struct glist_head handles;	/*< Head of list of object handles */
	struct glist_head ds_handles;	/*< Head of list of DS handles */
	char *name;		/*< Name set from .so and/or config */
	char *path;		/*< Path to .so file */
	void *dl_handle;	/*< Handle to the dlopen()d shared
				   library. NULL if statically linked */
	struct fsal_ops *ops;	/*< FSAL module methods vector */
};

/**
 * @brief FSAL module methods
 */

struct fsal_ops {
/**@{*/
/**
 * Base methods for loading and lifetime.
 */

/**
 * @brief Unload a module
 *
 * This function unloads the FSL module.  It should not be overridden.
 *
 * @param[in] fsal_hdl The module to unload.
 *
 * @retval 0     On success.
 * @retval EBUSY If there are outstanding references or exports.
 */
	int (*unload) (struct fsal_module *fsal_hdl);

/**@}*/

/**@{*/
/**
 * Subclass/instance methods in each fsal
 */

/**
 * @brief Initialize the configuration
 *
 * Given the root of the Ganesha configuration structure, initialize
 * the FSAL parameters.
 *
 * @param[in] fsal_hdl      The FSAL module
 * @param[in] config_struct Parsed ganesha configuration file
 *
 * @return FSAL status.
 */
	 fsal_status_t(*init_config) (struct fsal_module *fsal_hdl,
				      config_file_t config_struct);
/**
 * @brief Dump configuration
 *
 * This function dumps a human readable representation of the FSAL
 * configuration.
 *
 * @param[in] fsal_hdl The FSAL module.
 * @param[in] log_fd   File descriptor to which to output the dump
 */
	void (*dump_config) (struct fsal_module *fsal_hdl, int log_fd);

/**
 * @brief Create a new export
 *
 * This function creates a new export in the FSAL using the supplied
 * path and options.  The function is expected to allocate its own
 * export (the full, private structure).  It must then initialize the
 * public portion like so:
 *
 * @code{.c}
 *         fsal_export_init(&private_export_handle->pub);
 * @endcode
 *
 * After doing other private initialization, it must attach the export
 * to the module, like so:
 *
 *
 * @code{.c}
 *         fsal_attach_export(fsal_hdl,
 *                            &private_export->pub.exports);
 *
 * @endcode
 *
 * And create the parent link with:
 *
 * @code{.c}
 * private_export->pub.fsal = fsal_hdl;
 * @endcode
 *
 * @note This seems like something that fsal_attach_export should
 * do. -- ACE.
 *
 * @param[in]     fsal_hdl    FSAL module
 * @param[in]     parse_node  opaque pointer to parse tree node for
 *                            export options to be passed to
 *                            load_config_from_node
 * @param[in]     up_ops      Upcall ops
 *
 * @return FSAL status.
 */
	 fsal_status_t(*create_export) (struct fsal_module *fsal_hdl,
					void *parse_node,
					const struct fsal_up_vector *up_ops);

/**
 * @brief Minimal emergency cleanup on error
 *
 * This method is called only in the event of a catastrophic
 * failure. Currently, it will be called if some detail of the orderly
 * shutdown fails, so that FSALs will have the opportunity to leave
 * their underlying filesystems in a consistent state. It may at some
 * later time be called in the event of a crash. The majority of FSALs
 * will have no need to implement this call and should not do so.
 *
 * This function should, if implemented:
 *
 * 1. Do the bare minimum necessary to allow access to the each
 * underlying filesystem it serves. (the equivalent of a clean
 * unmount, so that a future instance of Ganesha or other tool can
 * mount the filesystem without difficulty.) How the FSAL defines
 * 'underlying filesystem' is FSAL specific. The FSAL handle itself
 * has a list of attached exports and that can be traversed if
 * suitable.
 *
 * 2. It /must not/ take any mutices, reader-writer locks, spinlocks,
 * sleep on any condition variables, or similar. Since other threads
 * may have crashed or been cancelled, locks may be left held,
 * overwritten with random garbage, or be similarly awful. The point
 * is to shut down cleanly, and you can't shut down cleanly if you're
 * hung. This does not create a race condition, since other threads in
 * Ganesha will have been cancelled by this point.
 *
 * 3. If it is at all possible to avoid, do not allocate memory on the
 * heap or use other services that require the user space to be in a
 * consistent state. If this is called from a crash handler, the Arena
 * may be corrupt. If you know that your FSAL *will* require memory,
 * you should either allocate it statically, or dynamically at
 * initialization time.
 */
	void (*emergency_cleanup) (void);

/**
 * pNFS functions
 */

/**
 * @brief Get information about a pNFS device
 *
 * When this function is called, the FSAL should write device
 * information to the @c da_addr_body stream.
 *
 * @param[in]  exp_hdl      Export handle
 * @param[out] da_addr_body An XDR stream to which the FSAL is to
 *                          write the layout type-specific information
 *                          corresponding to the deviceid.
 * @param[in]  type         The type of layout that specified the
 *                          device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */
	 nfsstat4(*getdeviceinfo) (struct fsal_module *fsal_hdl,
				   XDR * da_addr_body,
				   const layouttype4 type,
				   const struct pnfs_deviceid *deviceid);

/**
 * @brief Max Size of the buffer needed for da_addr_body in getdeviceinfo
 *
 * This function sets policy for XDR buffer allocation in getdeviceinfo.
 * If FSAL has a const size, just return it here. If it is dependent on
 * what the client can take return ~0UL. In any case the buffer allocated will
 * not be bigger than client's requested maximum.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Max size of the buffer needed for a da_addr_body
 */
	 size_t(*fs_da_addr_size) (struct fsal_module *fsal_hdl);

/**@}*/
};

/**
 * @brief Relinquish a reference to the module
 *
 * This function relinquishes one reference to the FSAL.  After the
 * reference count falls to zero, the FSAL may be freed and unloaded.
 *
 * @param[in] fsal_hdl FSAL on which to release reference.
 */

static inline void fsal_put(struct fsal_module *fsal_hdl)
{
	int32_t refcount;

	refcount = atomic_dec_int32_t(&fsal_hdl->refcount);

	assert(refcount >= 0);

	if (refcount == 0) {
		LogInfo(COMPONENT_FSAL,
			"FSAL %s now unused",
			fsal_hdl->name);
	}
}

/**
 * @brief Export object
 *
 * This structure is created by the @c create_export method on the
 * FSAL module.  It is stored as part of the export list and is used
 * to manage individual exports, interrogate properties of the
 * filesystem, and create individual file handle objects.
 */

struct fsal_export {
	struct fsal_module *fsal;	/*< Link back to the FSAL module */
	struct glist_head exports;	/*< Link in list of exports from
					   the same FSAL. */
	struct export_ops *ops;	/*< Vector of operations */
	struct fsal_obj_ops *obj_ops;	/*< Shared handle methods vector */
	struct fsal_ds_ops *ds_ops;	/*< Shared handle methods vector */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
};

/**
 * @brief Export operations
 */

struct export_ops {
/**@{*/

/**
* Export lifecycle management.
*/

/**
 * @brief Finalize an export
 *
 * This function is called as part of cleanup when the last reference to
 * an export is released and it is no longer part of the list.  It
 * should clean up all private resources and destroy the object.
 *
 * @param[in] exp_hdl The export to release.
 *
 * @return FSAL status.
 */
	 void (*release) (struct fsal_export *exp_hdl);
/**@}*/

/**@{*/
/**
 * Create an object handles within this export
 */

/**
 * @brief Look up a path
 *
 * This function looks up a path within the export, it is typically
 * used to get a handle for the root directory of the export.
 *
 * @param[in]  exp_hdl The export in which to look up
 * @param[in]  path    The path to look up
 * @param[out] handle  The object found
 *
 * @return FSAL status.
 */
	 fsal_status_t(*lookup_path) (struct fsal_export *exp_hdl,
				      const char *path,
				      struct fsal_obj_handle **handle);

/**
 * @brief Look up a junction
 *
 * This function returns a handle for the directory behind a junction
 * object.
 *
 * @deprecated This function is not implemented by any FSAL nor is it
 * called.  It exists here as a placeholder for implementation in 2.1
 * as part of the PseudoFSAL work.  Its argument structure will almost
 * certainly change.
 *
 * @param[in]  exp_hdl  Export in which to look up
 * @param[in]  junction The junction object
 * @param[out] handle   The underlying directory handle
 *
 * @return FSAL status.
 */
	 fsal_status_t(*lookup_junction) (struct fsal_export *exp_hdl,
					  struct fsal_obj_handle *junction,
					  struct fsal_obj_handle **handle);
/**
 * @brief Extract an opaque handle
 *
 * This function extracts a "key" handle from a "wire" handle.  That
 * is, when given a handle as passed to a client, this method will
 * extract the unique bits used to index the inode cache.
 *
 * @param[in]     exp_hdl Export in which to look up handle
 * @param[in]     in_type Protocol through which buffer was received.  One
 *                        special case, FSAL_DIGEST_SIZEOF, simply
 *                        requests that fh_desc.len be set to the proper
 *                        size of a wire handle.
 * @param[in,out] fh_desc Buffer descriptor.  The address of the
 *                        buffer is given in @c fh_desc->buf and must
 *                        not be changed.  @c fh_desc->len is the
 *                        length of the data contained in the buffer,
 *                        and @c fh_desc->maxlen is the total size of
 *                        the buffer, should the FSAL wish to write a
 *                        longer handle.  @c fh_desc->len must be
 *                        updated to the correct size.
 *
 * @return FSAL type.
 */
	 fsal_status_t(*extract_handle) (struct fsal_export *exp_hdl,
					 fsal_digesttype_t in_type,
					 struct gsh_buffdesc *fh_desc);
/**
 * @brief Create a FSAL object handle from a wire handle
 *
 * This function creates a FSAL object handle from a client supplied
 * "wire" handle (when an object is no longer in cache but the client
 * still remembers the nandle).
 *
 * @param[in]  exp_hdl  The export in which to create the handle
 * @param[in]  hdl_desc Buffer descriptor for the "wire" handle
 * @param[out] handle   FSAL object handle
 *
 * @return FSAL status.
 */
	 fsal_status_t(*create_handle) (struct fsal_export *exp_hdl,
					struct gsh_buffdesc *hdl_desc,
					struct fsal_obj_handle **handle);

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.
 *
 * @param[in]  exp_hdl  The export in which to create the handle
 * @param[in]  hdl_desc Buffer from which to creat the file
 * @param[out] handle   FSAL object handle
 *
 * @return NFSv4.1 error codes.
 */
	 nfsstat4(*create_ds_handle) (struct fsal_export *const exp_hdl,
				      const struct gsh_buffdesc *
				      const hdl_desc,
				      struct fsal_ds_handle **const handle);
/**@}*/

/**@{*/
/**
 * Statistics and configuration for this filesystem
 */

/**
 * @brief Get filesystem statistics
 *
 * This function gets information on inodes and space in use and free
 * for a filesystem.  See @c fsal_dynamicinfo_t for details of what to
 * fill out.
 *
 * @param[in]  exp_hdl Export handle to interrogate
 * @param[in]  obj_hdl Directory
 * @param[out] info    Buffer to fill with information
 *
 * @retval FSAL status.
 */
	 fsal_status_t(*get_fs_dynamic_info) (struct fsal_export *exp_hdl,
					      struct fsal_obj_handle *obj_hdl,
					      fsal_dynamicfsinfo_t *info);
/**
 * @brief Export feature test
 *
 * This function checks whether a feature is supported on this
 * filesystem.  The features that can be interrogated are given in the
 * @c fsal_fsinfo_options_t enumeration.
 *
 * @param[in] exp_hdl The export to interrogate
 * @param[in] option  The feature to query
 *
 * @retval true if the feature is supported.
 * @retval false if the feature is unsupported or unknown.
 */
	 bool(*fs_supports) (struct fsal_export *exp_hdl,
			     fsal_fsinfo_options_t option);
/**
 * @brief Get the greatest file size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest file size supported.
 */
	 uint64_t(*fs_maxfilesize) (struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest read size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest read size supported.
 */
	 uint32_t(*fs_maxread) (struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest write size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest write size supported.
 */
	 uint32_t(*fs_maxwrite) (struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest link count supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest link count supported.
 */
	 uint32_t(*fs_maxlink) (struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest name length supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest name length supported.
 */
	 uint32_t(*fs_maxnamelen) (struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest path length supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest path length supported.
 */
	 uint32_t(*fs_maxpathlen) (struct fsal_export *exp_hdl);

/**
 * @brief Get the lease time for this filesystem
 *
 * @note Currently this value has no effect, with lease time being
 * configured globally for all filesystems at once.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Lease time.
 */
	struct timespec (*fs_lease_time) (struct fsal_export *exp_hdl);

/**
 * @brief Get supported ACL types
 *
 * This function returns a bitmask indicating whether it supports
 * ALLOW, DENY, neither, or both types of ACL.
 *
 * @note Could someone with more ACL support tell me if this is sane?
 * Is it legitimate for an FSAL supporting ACLs to support just ALLOW
 * or just DENY without supporting the other?  It seems fishy to
 * me. -- ACE
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return supported ACL types.
 */
	 fsal_aclsupp_t(*fs_acl_support) (struct fsal_export *exp_hdl);

/**
 * @brief Get supported attributes
 *
 * This function returns a list of all attributes that this FSAL will
 * support.  Be aware that this is specifically the attributes in
 * struct attrlist, other NFS attributes (fileid and so forth) are
 * supported through other means.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return supported attributes.
 */
	 attrmask_t(*fs_supported_attrs) (struct fsal_export *exp_hdl);

/**
 * @brief Get umask applied to created files
 *
 * @note This seems fishy to me.  Is this actually supported properly?
 * And is it something we want the FSAL being involved in?  We already
 * have the functions in Protocol/NFS specifying a default mode. -- ACE
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return creation umask.
 */
	 uint32_t(*fs_umask) (struct fsal_export *exp_hdl);

/**
 * @brief Get permissions applied to names attributes
 *
 * @note This doesn't make sense to me as an export-level parameter.
 * Permissions on named attributes could reasonably vary with
 * permission and ownership of the associated file, and some
 * attributes may be read/write while others are read-only. -- ACE
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return permissions on named attributes.
 */
	 uint32_t(*fs_xattr_access_rights) (struct fsal_export *exp_hdl);
/**@}*/

/**@{*/

/**
 * Quotas are managed at the file system (export) level.  Someone who
 * uses quotas, please look over these comments to check/expand them.
 */

/**
 * @brief Check if quotas allow an operation
 *
 * This function checks to see if a user has overrun a quota and
 * should be disallowed from performing an operation that would
 * consume blocks or inodes.
 *
 * @param[in] exp_hdl    The export to interrogate
 * @param[in] filepath   The path within the export to check
 * @param[in] quota_type Whether we are checking inodes or blocks
 *
 * @return FSAL types.
 */
	 fsal_status_t(*check_quota) (struct fsal_export *exp_hdl,
				      const char *filepath, int quota_type);

/**
 * @brief Get a user's quota
 *
 * This function retrieves a given user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[out] quota      The user's quota
 *
 * @return FSAL types.
 */
	 fsal_status_t(*get_quota) (struct fsal_export *exp_hdl,
				    const char *filepath, int quota_type,
				    fsal_quota_t *quota);

/**
 * @brief Set a user's quota
 *
 * This function sets a user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[in]  quota      The values to set for the quota
 * @param[out] resquota   New values set (optional)
 *
 * @return FSAL types.
 */
	 fsal_status_t(*set_quota) (struct fsal_export *exp_hdl,
				    const char *filepath, int quota_type,
				    fsal_quota_t *quota,
				    fsal_quota_t *resquota);
/**@}*/

/**@{*/
/**
 * pNFS functions
 */

/**
 * @brief Get list of available devices
 *
 * This function should populate calls @c cb @c values representing the
 * low quad of deviceids it wishes to make the available to the
 * caller.  it should continue calling @c cb until @c cb returns false
 * or it runs out of deviceids to make available.  If @c cb returns
 * false, it should assume that @c cb has not stored the most recent
 * deviceid and set @c res->cookie to a value that will begin witht he
 * most recently provided.
 *
 * If it wishes to return no deviceids, it may set @c res->eof to true
 * without calling @c cb at all.
 *
 * @param[in]     exp_hdl Export handle
 * @param[in]     type    Type of layout to get devices for
 * @param[in]     cb      Function taking device ID halves
 * @param[in,out] res     In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 365-6.
 */
	 nfsstat4(*getdevicelist) (struct fsal_export *exp_hdl,
				   layouttype4 type, void *opaque,
				   bool(*cb) (void *opaque, const uint64_t id),
				   struct fsal_getdevicelist_res *res);

/**
 * @brief Get layout types supported by export
 *
 * This function is the handler of the NFS4.1 FATTR4_FS_LAYOUT_TYPES file
 * attribute. (See RFC)
 *
 * @param[in]  exp_hdl Filesystem to interrogate
 * @param[out] count   Number of layout types in array
 * @param[out] types   Static array of layout types that must not be
 *                     freed or modified and must not be dereferenced
 *                     after export reference is relinquished
 */
	void (*fs_layouttypes) (struct fsal_export *exp_hdl, int32_t *count,
				const layouttype4 **types);

/**
 * @brief Get layout block size for export
 *
 * This function is the handler of the NFS4.1 FATTR4_LAYOUT_BLKSIZE f-attribute.
 *
 * This is the preferred read/write block size.  Clients are requested
 * (but don't have to) read and write in multiples.
 *
 * NOTE: The linux client only asks for this in blocks-layout, where this is the
 * filesystem wide block-size. (Minimum write size and alignment)
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return The preferred layout block size.
 */
	 uint32_t(*fs_layout_blocksize) (struct fsal_export *exp_hdl);

/**
 * @brief Maximum number of segments we will use
 *
 * This function returns the maximum number of segments that will be
 * used to construct the response to any single layoutget call.  Bear
 * in mind that current clients only support 1 segment.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return The Maximum number of layout segments in a campound layoutget.
 */
	 uint32_t(*fs_maximum_segments) (struct fsal_export *exp_hdl);

/**
 * @brief Size of the buffer needed for loc_body at layoutget
 *
 * This function sets policy for XDR buffer allocation in layoutget vector
 * below. If FSAL has a const size, just return it here. If it is dependent on
 * what the client can take return ~0UL. In any case the buffer allocated will
 * not be bigger than client's requested maximum.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Max size of the buffer needed for a loc_body
 */
	 size_t(*fs_loc_body_size) (struct fsal_export *exp_hdl);

/**
 * @brief Get write verifier
 *
 * This function is called by write and commit to match the commit verifier
 * with the one returned on  write.
 *
 * @param[in,out] verf_desc Address and length of verifier
 */
	void (*get_write_verifier) (struct gsh_buffdesc *verf_desc);

/**@}*/
};

/**
 * @brief Public structure for filesystem objects
 *
 * This structure is used for files of all types including directories
 * and anything else that can be operated on via NFS.
 *
 * All functions that create a a new object handle should allocate
 * memory for the complete (public and private) handle and perform any
 * private initialization.  They should fill the
 * @c fsal_obj_handle::attributes structure.  They should also call the
 * @c fsal_obj_handle_init function with the public object handle,
 * object handle operations vector, public export, and file type.
 *
 * @note Do we actually need a lock and ref count on the fsal object
 * handle, since cache_inode is managing life cycle and concurrency?
 * That is, do we expect fsal_obj_handle to have a reference count
 * that would be separate from that managed by cache_inode_lru?
 */

struct fsal_obj_handle {
	pthread_rwlock_t lock;		/*< Lock on handle */
	struct glist_head handles;	/*< Link in list of handles under
					   an fsal */
	object_file_type_t type;	/*< Object file type */
	struct fsal_module *fsal;	/*< Link back to fsal module */
	struct fsal_filesystem *fs;	/*< Owning filesystem */
	struct attrlist attributes;	/*< Cached attributes */
	struct fsal_obj_ops *ops;	/*< Operations vector */
};

/**
 * @brief Public structure for filesystem descriptions
 *
 * This stucture is provided along with a general interface to support those
 * FSALs that map into a traditional file system model. Note that
 * fsal_obj_handles do not link to an fsal_filesystem, that linkage is reserved
 * for and FSAL's private obj handle if appropriate.
 *
 */

typedef int (*claim_filesystem_cb)(struct fsal_filesystem *fs,
				   struct fsal_export *exp);

typedef void (*unclaim_filesystem_cb)(struct fsal_filesystem *fs);

enum fsid_type {
	FSID_NO_TYPE,
	FSID_ONE_UINT64,
	FSID_MAJOR_64,
	FSID_TWO_UINT64,
	FSID_TWO_UINT32,
	FSID_DEVICE
};

static inline uint64_t squash_fsid(const struct fsal_fsid__ *fsid)
{
	return fsid->major ^ (fsid->minor << 32 | fsid->minor >> 32);
}

static inline int sizeof_fsid(enum fsid_type type)
{
	switch (type) {
	case FSID_NO_TYPE:
		return 0;
	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		return sizeof(uint64_t);
	case FSID_TWO_UINT64:
		return 2 * sizeof(uint64_t);
	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		return 2 * sizeof(uint32_t);
	}

	return -1;
}

struct fsal_filesystem {
	struct fsal_module *fsal;	/*< Link back to fsal module */
	struct glist_head filesystems;	/*< List of file systems */
	unclaim_filesystem_cb unclaim;  /*< Call back to unclaim this fs */
	struct fsal_filesystem *parent;	/*< Parent file system */
	struct glist_head children;	/*< Child file systems */
	struct glist_head siblings;	/*< Entry in list of parent's child
					    file systems */
	bool exported;			/*< true if explicitly exported */
	bool in_fsid_avl;		/*< true if inserted in fsid avl */
	bool in_dev_avl;		/*< true if inserted in dev avl */
	fsal_dev_t dev;			/*< device filesystem is on */
	enum fsid_type fsid_type;	/*< type of fsid present */
	struct fsal_fsid__ fsid;	/*< file system id */
	struct avltree_node avl_fsid;	/*< AVL indexed by fsid */
	struct avltree_node avl_dev;	/*< AVL indexed by dev */
	void *private;			/*< Private data for owning FSAL */
	char *path;			/*< Path to root of this file system */
	char *device;			/*< Path to block device */
	char *type;			/*< fs type */
	uint32_t pathlen;		/*< Length of path */
	uint32_t namelen;		/*< Name length from statfs */
};

/**
 * @brief Directory cookie
 */

typedef uint64_t fsal_cookie_t;

typedef bool(*fsal_readdir_cb) (const char *name, void *dir_state,
				fsal_cookie_t cookie);
/**
 * @brief FSAL objectoperations vector
 */

struct fsal_obj_ops {
/**@{*/

/**
 * Lifecycle management
 */

/**
 * @brief Clean up a filehandle
 *
 * This function cleans up private resources associated with a
 * filehandle and deallocates it.  Implement this method or you will
 * leak.
 *
 * @param[in] obj_hdl Handle to release
 *
 * @return FSAL status.
 */
	 void (*release) (struct fsal_obj_handle *obj_hdl);
/**@}*/

/**@{*/

/**
 * Directory operations
 */

/**
 * @brief Look up a filename
 *
 * This function looks up the given name in the supplied directory.
 *
 * @note The old version of the FSAL had a special case for this
 * function, such that if the directory handle and path were both
 * NULL, a handle to the root of the export was returned.  This
 * special case is no longer supported and should not be implemented.
 *
 * @param[in]  dir_hdl Directory to search
 * @param[in]  path    Name to look up
 * @param[out] handle  Object found
 *
 * @return FSAL status.
 */
	 fsal_status_t(*lookup) (struct fsal_obj_handle *dir_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle);

/**
 * @brief Read a directory
 *
 * This function reads directory entries from the FSAL and supplies
 * them to a callback.
 *
 * @param[in]  dir_hdl   Directory to read
 * @param[in]  whence    Point at which to start reading.  NULL to
 *                       start at beginning.
 * @param[in]  dir_state Opaque pointer to be passed to callback
 * @param[in]  cb        Callback to receive names
 * @param[out] eof       true if the last entry was reached
 *
 * @retval true if more entries are required
 * @retval false if no more entries are required (and the current one
 *               has not been consumed)
 */
	 fsal_status_t(*readdir) (struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
				  bool *eof);
/**@}*/

/**@{*/

/**
 * Creation operations
 */

/**
 * @brief Create a regular file
 *
 * This function creates a new regular file.
 *
 * @param[in]     dir_hdl Directory in which to create the file
 * @param[in]     name    Name of file to create
 * @param[in,out] attrib  Attributes to set on newly created
 *                        object/attributes you actually got.
 * @param[out]    new_obj Newly created object
 *
 * @return FSAL status.
 */
	 fsal_status_t(*create) (struct fsal_obj_handle *dir_hdl,
				 const char *name, struct attrlist *attrib,
				 struct fsal_obj_handle **new_obj);

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * @param[in]     dir_hdl Directory in which to create the directory
 * @param[in]     name    Name of directory to create
 * @param[in,out] attrib  Attributes to set on newly created
 *                        object/attributes you actually got.
 * @param[out]    new_obj Newly created object
 *
 * @return FSAL status.
 */
	 fsal_status_t(*mkdir) (struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrib,
				struct fsal_obj_handle **new_obj);

/**
 * @brief Create a special file
 *
 * This function creates a new special file.
 *
 * @param[in]     dir_hdl  Directory in which to create the object
 * @param[in]     name     Name of object to create
 * @param[in]     nodetype Type of special file to create
 * @param[in]     dev      Major and minor device numbers for block or
 *                         character special
 * @param[in,out] attrib   Attributes to set on newly created
 *                         object/attributes you actually got.
 * @param[out]    new_obj  Newly created object
 *
 * @return FSAL status.
 */
	 fsal_status_t(*mknode) (struct fsal_obj_handle *dir_hdl,
				 const char *name,
				 object_file_type_t nodetype,
				 fsal_dev_t *dev,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **new_obj);

/**
 * @brief Create a symbolic link
 *
 * This function creates a new symbolic link.
 *
 * @param[in]     dir_hdl   Directory in which to create the object
 * @param[in]     name      Name of object to create
 * @param[in]     link_path Content of symbolic link
 * @param[in,out] attrib    Attributes to set on newly created
 *                          object/attributes you actually got.
 * @param[out] new_obj      Newly created object
 *
 * @return FSAL status.
 */
	 fsal_status_t(*symlink) (struct fsal_obj_handle *dir_hdl,
				  const char *name,
				  const char *link_path,
				  struct attrlist *attrib,
				  struct fsal_obj_handle **new_obj);
/**@}*/

/**@{*/

/**
 * File object operations
 */

/**
 * @brief Read the content of a link
 *
 * This function reads the content of a symbolic link.  The FSAL will
 * allocate a buffer and store its address and the link length in the
 * link_content gsh_buffdesc.  The caller *must* free this buffer with
 * gsh_free.
 *
 * The symlink content passed back *must* be null terminated and the
 * length indicated in the buffer description *must* include the
 * terminator.
 *
 * @param[in]  obj_hdl      Link to read
 * @param[out] link_content Buffdesc to which the FSAL will store
 *                          the address of the buffer holding the
 *                          link and the link length.
 * @param[out] refresh      true if the content are to be retrieved
 *                          from the underlying filesystem rather
 *                          than cache
 *
 * @return FSAL status.
 */
	 fsal_status_t(*readlink) (struct fsal_obj_handle *obj_hdl,
				   struct gsh_buffdesc *link_content,
				   bool refresh);

/**
 * @brief Check access for a given user against a given object
 *
 * This function checks whether a given user is allowed to perform the
 * specified operations against the supplied file.  The goal is to
 * allow filesystem specific semantics to be applied to cached
 * metadata.
 *
 * @param[in] obj_hdl     Handle to check
 * @param[in] access_type Access requested
 * @param[out] allowed    Returned access that could be granted
 * @param[out] denied     Returned access that would be granted
 *
 * @return FSAL status.
 */
	 fsal_status_t(*test_access) (struct fsal_obj_handle *obj_hdl,
				      fsal_accessflags_t access_type,
				      fsal_accessflags_t *allowed,
				      fsal_accessflags_t *denied);

/**
 * @brief Get attributes
 *
 * This function freshens the cached attributes stored on the handle.
 * Since the caller can take the attribute lock and read them off the
 * public filehandle, they are not copied out.
 *
 * @param[in]  obj_hdl  Object to query
 *
 * @return FSAL status.
 */
	 fsal_status_t(*getattrs) (struct fsal_obj_handle *obj_hdl);

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by @c attrib_set->mask.
 *
 * @param[in] obj_hdl    The object to modify
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
	 fsal_status_t(*setattrs) (struct fsal_obj_handle *obj_hdl,
				   struct attrlist *attrib_set);

/**
 * @brief Create a new link
 *
 * This function creates a new name for an existing object.
 *
 * @param[in] obj_hdl     Object to be linked to
 * @param[in] destdir_hdl Directory in which to create the link
 * @param[in] name        Name for link
 *
 * @return FSAL status
 */
	 fsal_status_t(*link) (struct fsal_obj_handle *obj_hdl,
			       struct fsal_obj_handle *destdir_hdl,
			       const char *name);

/**
 * @brief Rename a file
 *
 * This function renames a file (technically it changes the name of
 * one link, which may be the only link to the file.)
 *
 * @param[in] olddir_hdl Old parent directory
 * @param[in] old_name   Old name
 * @param[in] newdir_hdl New parent directory
 * @param[in] new_name   New name
 *
 * @return FSAL status
 */
	 fsal_status_t(*rename) (struct fsal_obj_handle *olddir_hdl,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_hdl,
				 const char *new_name);
/**
 * @brief Remove a name from a directory
 *
 * This function removes a name from a directory and possibly deletes
 * the file so named.
 *
 * @param[in] obj_hdl The directory from which to remove the name
 * @param[in] name    The name to remove
 *
 * @return FSAL status.
 */
	 fsal_status_t(*unlink) (struct fsal_obj_handle *obj_hdl,
				 const char *name);

/**@}*/

/**@{*/
/**
 * I/O management
 */

/**
 * @brief Open a file for read or write
 *
 * This function opens a file for read or write.  The file should not
 * already be opened when this function is called.  The thread calling
 * this function will have hold the Cache inode content lock
 * exclusively and the FSAL may assume whatever private state it uses
 * to manage open/close status is protected.
 *
 * @param[in] obj_hdl   File to open
 * @param[in] openflags Mode for open
 *
 * @return FSAL status.
 */
	 fsal_status_t(*open) (struct fsal_obj_handle *obj_hdl,
			       fsal_openflags_t openflags);

/**
 * @brief Re-open a file that may be already opened
 *
 * This function reopens the file with the given open flags. You can
 * atomically go from read only flag to readwrite or vice versa.
 * This is used to reopen a file for readwrite, if the file is already
 * opened for readonly. This will not lose any file locks that are
 * already placed. May not be supported by all FSALs.
 */
	 fsal_status_t(*reopen) (struct fsal_obj_handle *obj_hdl,
				 fsal_openflags_t openflags);

/**
 * @brief Return open status
 *
 * This function returns open flags representing the current open
 * status.
 *
 * @param[in] obj_hdl File to interrogate
 *
 * @retval Flags representing current open status
 */
	 fsal_openflags_t(*status) (struct fsal_obj_handle *obj_hdl);

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file.
 *
 * @note We probably want to keep end_of_file.  There may be reasons
 * other than end of file while less data are returned than requested
 * (FSAL_PROXY, for example, might do this depending on the will of
 * the remote server.) -- ACE
 *
 * @param[in]  obj_hdl     File to read
 * @param[in]  offset      Position from which to read
 * @param[in]  buffer_size Amount of data to read
 * @param[out] buffer      Buffer to which data are to be copied
 * @param[out] read_amount Amount of data read
 * @param[out] end_of_file true if the end of file has been reached
 *
 * @return FSAL status.
 */
	 fsal_status_t(*read) (struct fsal_obj_handle *obj_hdl,
			       uint64_t offset,
			       size_t buffer_size,
			       void *buffer,
			       size_t *read_amount,
			       bool *end_of_file);	/* needed? */

/**
 * @brief Read data from a file plus
 *
 * This function reads data from the given file.
 *
 * @note We probably want to keep end_of_file.  There may be reasons
 * other than end of file while less data are returned than requested
 * (FSAL_PROXY, for example, might do this depending on the will of
 * the remote server.) -- ACE
 *
 * @param[in]  obj_hdl     File to read
 * @param[in]  offset      Position from which to read
 * @param[in]  buffer_size Amount of data to read
 * @param[out] buffer      Buffer to which data are to be copied
 * @param[out] read_amount Amount of data read
 * @param[out] end_of_file true if the end of file has been reached
 * @param[in,out] info     more information about the data
 *
 * @return FSAL status.
 */
	fsal_status_t(*read_plus) (struct fsal_obj_handle *obj_hdl,
				   uint64_t offset,
				   size_t buffer_size,
				   void *buffer,
				   size_t *read_amount,
				   bool *end_of_file,
				   struct io_info *info);

/**
 * @brief Write data to a file
 *
 * This function writes data to a file.
 *
 * @note Should buffer be const? -- ACE
 *
 * @param[in]  obj_hdl      File to be written
 * @param[in]  offset       Position at which to write
 * @param[in]  buffer       Data to be written
 * @param[in,out] fsal_stable In, if on, the fsal is requested to write data
 *                            to stable store. Out, the fsal reports what
 *                            it did.
 *
 * @return FSAL status.
 */
	 fsal_status_t(*write) (struct fsal_obj_handle *obj_hdl,
				uint64_t offset,
				size_t buffer_size,
				void *buffer,
				size_t *wrote_amount,
				bool *fsal_stable);
/**
 * @brief Write data to a file plus
 *
 * This function writes data to a file.
 *
 * @note Should buffer be const? -- ACE
 *
 * @param[in]  obj_hdl      File to be written
 * @param[in]  offset       Position at which to write
 * @param[in]  buffer       Data to be written
 * @param[in,out] fsal_stable In, if on, the fsal is requested to write data
 *                            to stable store. Out, the fsal reports what
 *                            it did.
 * @param[in,out] info     more information about the data
 *
 * @return FSAL status.
 */
	 fsal_status_t(*write_plus) (struct fsal_obj_handle *obj_hdl,
				uint64_t offset,
				size_t buffer_size,
				void *buffer,
				size_t *wrote_amount,
				bool *fsal_stable,
				struct io_info *info);
/**
 * @brief Seek to data or hole
 *
 * This function seek to data or hole in a file.
 *
 * @param[in]  obj_hdl      File to be written
 * @param[in,out] info      Information about the data
 *
 * @return FSAL status.
 */
	 fsal_status_t(*seek) (struct fsal_obj_handle *obj_hdl,
				struct io_info *info);
/**
 * @brief IO Advise
 *
 * This function give hints to fs.
 *
 * @param[in]  obj_hdl      File to be written
 * @param[in,out] info      Information about the data
 *
 * @return FSAL status.
 */
	 fsal_status_t(*io_advise) (struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints);
/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file.
 *
 * @param[in] obj_hdl File to commit
 * @param[in] offset  Start of range to commit
 * @param[in] len     Length of range to commit
 *
 * @return FSAL status.
 */
	 fsal_status_t(*commit) (struct fsal_obj_handle *obj_hdl,  /* sync */
				 off_t offset, size_t len);

/**
 * @brief Perform a lock operation
 *
 * This function performs a lock operation (lock, unlock, test) on a
 * file.
 *
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  owner            Lock owner (Not yet implemented)
 * @param[in]  lock_op          Operation to perform
 * @param[in]  request_lock     Lock to take/release/test
 * @param[out] conflicting_lock Conflicting lock
 *
 * @return FSAL status.
 */
	 fsal_status_t(*lock_op) (struct fsal_obj_handle *obj_hdl,
				  void *owner,
				  fsal_lock_op_t lock_op,
				  fsal_lock_param_t *request_lock,
				  fsal_lock_param_t *conflicting_lock);

/**
 * @brief Handle share reservations
 *
 * This function handles acquiring and releasing Microsoft share
 * reservations.
 *
 * @param[in] obj_hdl       Handle on which to operate
 * @param[in] owner         Share owner
 * @param[in] request_share Share reservation requested
 *
 * @return FSAL status.
 */
	 fsal_status_t(*share_op) (struct fsal_obj_handle *obj_hdl,
				   void *owner,
				   fsal_share_param_t request_share);
/**
 * @brief Close a file
 *
 * This function closes a file.  It is protected by the Cache inode
 * content lock.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */
	 fsal_status_t(*close) (struct fsal_obj_handle *obj_hdl);
/**@}*/

/**@{*/

/**
 * Extended attribute management
 */

/**
 * @brief List extended attributes on a file
 *
 * This function gets a list of attributes on a given file.
 *
 * @param[in]  obj_hdl        File to interrogate
 * @param[in]  cookie         Attribute at which to start
 * @param[out] xattrs_tab     Array to which to write attributes
 * @param[in]  xattrs_tabsize Size of array
 * @param[out] nb_returned    Number of entries returned
 *
 * @return FSAL status.
 */
	 fsal_status_t(*list_ext_attrs) (struct fsal_obj_handle *obj_hdl,
					 unsigned int cookie,
					 struct fsal_xattrent *xattrs_tab,
					 unsigned int xattrs_tabsize,
					 unsigned int *nb_returned,
					 int *end_of_list);

/**
 * @brief Get a number for an attribute name
 *
 * This function returns an index for a given attribute specified by
 * name.
 *
 * @param[in]  obj_hdl  File to look up
 * @param[in]  name     Name to look up
 * @param[out] xattr_id Number uniquely identifying the attribute
 *                      within the scope of the file
 *
 * @return FSAL status.
 */
	 fsal_status_t(*getextattr_id_by_name) (struct fsal_obj_handle *obj_hdl,
						const char *xattr_name,
						unsigned int *xattr_id);
/**
 * @brief Get content of an attribute by name
 *
 * This function returns the value of an extended attribute as
 * specified by name.
 *
 * @param[in]  obj_hdl     File to interrogate
 * @param[in]  xattr_name  Name of attribute
 * @param[out] buffer_addr Buffer to store content
 * @param[in]  buffer_size Buffer size
 * @param[out] output_size Size of content
 *
 * @return FSAL status.
 */
	 fsal_status_t(*getextattr_value_by_name) (struct fsal_obj_handle *
						   obj_hdl,
						   const char *xattr_name,
						   caddr_t buffer_addr,
						   size_t buffer_size,
						   size_t *output_size);

/**
 * @brief Get content of an attribute by id
 *
 * This function returns the value of an extended attribute as
 * specified by id.
 *
 * @param[in]  obj_hdl     File to interrogate
 * @param[in]  xattr_id    ID of attribute
 * @param[out] buffer_addr Buffer to store content
 * @param[in]  buffer_size Buffer size
 * @param[out] output_size Size of content
 *
 * @return FSAL status.
 */
	 fsal_status_t(*getextattr_value_by_id) (struct fsal_obj_handle *
						 obj_hdl,
						 unsigned int xattr_id,
						 caddr_t buffer_addr,
						 size_t buffer_size,
						 size_t *output_size);

/**
 * @brief Set content of an attribute
 *
 * This function sets the value of an extended attribute.
 *
 * @param[in] obj_hdl     File to modify
 * @param[in] xattr_name  Name of attribute
 * @param[in] buffer_addr Content to set
 * @param[in] buffer_size Content size
 * @param[in] create      true if attribute is to be created
 *
 * @return FSAL status.
 */
	 fsal_status_t(*setextattr_value) (struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size, int create);

/**
 * @brief Set content of an attribute by id
 *
 * This function sets the value of an extended attribute by id.
 *
 * @param[in] obj_hdl     File to modify
 * @param[in] xattr_id    ID of attribute
 * @param[in] buffer_addr Content to set
 * @param[in] buffer_size Content size
 *
 * @return FSAL status.
 */
	 fsal_status_t(*setextattr_value_by_id) (struct fsal_obj_handle *
						 obj_hdl,
						 unsigned int xattr_id,
						 caddr_t buffer_addr,
						 size_t buffer_size);

/**
 * @brief Get attributes on a named attribute
 *
 * This function gets the attributes on a named attribute.
 *
 * @param[in]  obj_hdl    File to interrogate
 * @param[in]  xattr_id   ID of attribute
 * @param[out] attributes Attributes on named attribute
 *
 * @return FSAL status.
 */
	 fsal_status_t(*getextattr_attrs) (struct fsal_obj_handle *obj_hdl,
					   unsigned int xattr_id,
					   struct attrlist *attrs);

/**
 * @brief Remove an extended attribute by id
 *
 * This function removes an extended attribute as specified by ID.
 *
 * @param[in] obj_hdl     File to modify
 * @param[in] xattr_id    ID of attribute
 *
 * @return FSAL status.
 */
	 fsal_status_t(*remove_extattr_by_id) (struct fsal_obj_handle *obj_hdl,
					       unsigned int xattr_id);

/**
 * @brief Remove an extended attribute by name
 *
 * This function removes an extended attribute as specified by name.
 *
 * @param[in] obj_hdl     File to modify
 * @param[in] xattr_name  Name of attribute to remove
 *
 * @return FSAL status.
 */
	 fsal_status_t(*remove_extattr_by_name) (struct fsal_obj_handle *
						 obj_hdl,
						 const char *xattr_name);
/**@}*/

/**@{*/
/**
 * Handle operations
 */

/**
 * @brief Test handle type
 *
 * This function tests that a handle is of the specified type.
 *
 * @retval true if it is.
 * @retval false if it isn't.
 */
	 bool(*handle_is) (struct fsal_obj_handle *obj_hdl,
			   object_file_type_t type);

/**
 * @brief Perform cleanup as requested by the LRU
 *
 * This function performs cleanup tasks as requested by the LRU
 * thread, specifically to close file handles or free memory
 * associated with a file.
 *
 * @param[in] obj_hdl  File to clean up
 * @param[in] requests Things to clean up about file
 *
 * @return FSAL status
 */
	 fsal_status_t(*lru_cleanup) (struct fsal_obj_handle *obj_hdl,
				      lru_actions_t requests);

/**
 * @brief Write wire handle
 *
 * This function writes a "wire" handle or file ID to the given
 * buffer.
 *
 * @param[in]     obj_hdl     The handle to digest
 * @param[in]     output_type The type of digest to write
 * @param[in,out] fh_desc     Buffer descriptor to which to write
 *                            digest.  Set fh_desc->len to final
 *                            output length.
 *
 * @return FSAL status
 */
	 fsal_status_t(*handle_digest) (const struct fsal_obj_handle *obj_hdl,
					fsal_digesttype_t output_type,
					struct gsh_buffdesc *fh_desc);
/**
 * @brief Get key for handle
 *
 * Indicate the unique part of the handle that should be used for
 * hashing.
 *
 * @param[in]  obj_hdl Handle whose key is to be got
 * @param[out] fh_desc Address and length giving sub-region of handle
 *                     to be used as key
 */
	void (*handle_to_key) (struct fsal_obj_handle *obj_hdl,
			       struct gsh_buffdesc *fh_desc);
/**@}*/

/**@{*/

/**
 * pNFS functions
 */

/**
 * @brief Grant a layout segment.
 *
 * This function is called by nfs41_op_layoutget.  It may be called
 * multiple times, to satisfy a request with multiple segments.  The
 * FSAL may track state (what portion of the request has been or
 * remains to be satisfied or any other information it wishes) in the
 * bookkeeper member of res.  Each segment may have FSAL-specific
 * information associated with it its segid.  This segid will be
 * supplied to the FSAL when the segment is committed or returned.
 * When the granting the last segment it intends to grant, the FSAL
 * must set the last_segment flag in res.
 *
 * @param[in]     obj_hdl  The handle of the file on which the layout is
 *                         requested.
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */
	 nfsstat4(*layoutget) (struct fsal_obj_handle *obj_hdl,
			       struct req_op_context *req_ctx,
			       XDR * loc_body,
			       const struct fsal_layoutget_arg *arg,
			       struct fsal_layoutget_res *res);

/**
 * @brief Potentially return one layout segment
 *
 * This function is called once on each segment matching the IO mode
 * and intersecting the range specified in a LAYOUTRETURN operation or
 * for all layouts corresponding to a given stateid on last close,
 * leas expiry, or a layoutreturn with a return-type of FSID or ALL.
 * Whther it is called in the former or latter case is indicated by
 * the synthetic flag in the arg structure, with synthetic being true
 * in the case of last-close or lease expiry.
 *
 * If arg->dispose is true, all resources associated with the
 * layout must be freed.
 *
 * @param[in] obj_hdl  The object on which a segment is to be returned
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */
	 nfsstat4(*layoutreturn) (struct fsal_obj_handle *obj_hdl,
				  struct req_op_context *req_ctx,
				  XDR * lrf_body,
				  const struct fsal_layoutreturn_arg *arg);

/**
 * @brief Commit a segment of a layout
 *
 * This function is called once on every segment of a layout.  The
 * FSAL may avoid being called again after it has finished all tasks
 * necessary for the commit by setting res->commit_done to true.
 *
 * The calling function does not inspect or act on the value of
 * size_supplied or new_size until after the last call to
 * FSAL_layoutcommit.
 *
 * @param[in]     obj_hdl  The object on which to commit
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
	 nfsstat4(*layoutcommit) (struct fsal_obj_handle *obj_hdl,
				  struct req_op_context *req_ctx,
				  XDR * lou_body,
				  const struct fsal_layoutcommit_arg *arg,
				  struct fsal_layoutcommit_res *res);
/**@}*/
};

/**
 * @brief Public structure for DS file handles
 *
 * This structure is used for files of all types including directories
 * and anything else that can be operated on via NFS.  Having an
 * independent reference count and lock here makes sense, since there
 * is no caching infrastructure overlaying this system.
 *
 */

struct fsal_ds_handle {
	struct glist_head ds_handles;	/*< Link in list of DS handles under
					   an fsal */
	int32_t refcount;		/*< Reference count */
	struct fsal_module *fsal;	/*< Link back to fsal module */
	struct fsal_ds_ops *ops;	/*< Operations vector */
};

struct fsal_ds_ops {
/**@{*/

/**
 * Lifecycle management.
 */

/**
 * @brief Clean up a DS handle
 *
 * This function cleans up private resources associated with a
 * filehandle and deallocates it.  Implement this method or you will
 * leak.  This function should not be called directly.
 *
 * @param[in] ds_hdl Handle to release
 *
 * @return NFSv4.1 status codes.
 */
	 void (*release) (struct fsal_ds_handle *const ds_hdl);
/**@}*/

/**@{*/

/**
 * I/O Functions
 */

/**
 * @brief Read from a data-server handle.
 *
 * NFSv4.1 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  requested_length Length of read requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[out] supplied_length  Length of data read
 * @param[out] eof              true on end of file
 *
 * @return An NFSv4.1 status code.
 */
	 nfsstat4(*read) (struct fsal_ds_handle *const ds_hdl,
			  struct req_op_context *const req_ctx,
			  const stateid4 * stateid,
			  const offset4 offset,
			  const count4 requested_length,
			  void *const buffer,
			  count4 * const supplied_length,
			  bool *const end_of_file);

/**
 * @brief Read plus from a data-server handle.
 *
 * NFSv4.2 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  requested_length Length of read requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[out] supplied_length  Length of data read
 * @param[out] eof              true on end of file
 * @param[out] info             IO info
 *
 * @return An NFSv4.2 status code.
 */
	 nfsstat4(*read_plus) (struct fsal_ds_handle *const ds_hdl,
			  struct req_op_context *const req_ctx,
			  const stateid4 * stateid,
			  const offset4 offset,
			  const count4 requested_length,
			  void *const buffer,
			  const count4 supplied_length,
			  bool *const end_of_file,
			  struct io_info *info);

/**
 *
 * @brief Write to a data-server handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  write_length     Length of write requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[in]  stability wanted Stability of write
 * @param[out] written_length   Length of data written
 * @param[out] writeverf        Write verifier
 * @param[out] stability_got    Stability used for write (must be as
 *                              or more stable than request)
 *
 * @return An NFSv4.1 status code.
 */
	 nfsstat4(*write) (struct fsal_ds_handle *const ds_hdl,
			   struct req_op_context *const req_ctx,
			   const stateid4 * stateid,
			   const offset4 offset,
			   const count4 write_length,
			   const void *buffer,
			   const stable_how4 stability_wanted,
			   count4 * const written_length,
			   verifier4 * const writeverf,
			   stable_how4 * const stability_got);

/**
 *
 * @brief Write plus to a data-server handle.
 *
 * NFSv4.2 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  write_length     Length of write requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[in]  stability wanted Stability of write
 * @param[out] written_length   Length of data written
 * @param[out] writeverf        Write verifier
 * @param[out] stability_got    Stability used for write (must be as
 *                              or more stable than request)
 * @param[in/out] info          IO info
 *
 * @return An NFSv4.2 status code.
 */
	 nfsstat4(*write_plus) (struct fsal_ds_handle *const ds_hdl,
			   struct req_op_context *const req_ctx,
			   const stateid4 * stateid,
			   const offset4 offset,
			   const count4 write_length,
			   const void *buffer,
			   const stable_how4 stability_wanted,
			   count4 * const written_length,
			   verifier4 * const writeverf,
			   stable_how4 * const stability_got,
			   struct io_info *info);

/**
 * @brief Commit a byte range to a DS handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into cache_inode or processed the
 * normal way.
 *
 * @param[in]  ds_hdl    FSAL DS handle
 * @param[in]  req_ctx   Credentials
 * @param[in]  offset    Start of commit window
 * @param[in]  count     Length of commit window
 * @param[out] writeverf Write verifier
 *
 * @return An NFSv4.1 status code.
 */
	 nfsstat4(*commit) (struct fsal_ds_handle *const ds_hdl,
			    struct req_op_context *const req_ctx,
			    const offset4 offset,
			    const count4 count,
			    verifier4 * const writeverf);
};


/**
 * @brief Get a reference on a handle
 *
 * This function increments the reference count on a handle.
 *
 * @param[in] ds_hdl The handle to reference
 */

static inline void ds_get(struct fsal_ds_handle *const ds_hdl)
{
	atomic_inc_int32_t(&ds_hdl->refcount);
}

/**
 * @brief Release a reference on a handle
 *
 * This function releases a reference to a handle.  Once a caller's
 * reference is released they should make no attempt to access the
 * handle or even dereference a pointer to it.
 *
 * @param[in] ds_hdl The handle to relinquish
 */

static inline void ds_put(struct fsal_ds_handle *const ds_hdl)
{
	int32_t refcount;

	refcount = atomic_dec_int32_t(&ds_hdl->refcount);

	assert(refcount >= 0);

	if (refcount == 0)
		ds_hdl->ops->release(ds_hdl);
}

/**
** Resolve forward declarations
*/
#include "client_mgr.h"
#include "export_mgr.h"
#include "fsal_up.h"

#endif				/* !FSAL_API */
/** @} */
