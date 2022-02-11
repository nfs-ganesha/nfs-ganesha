/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#include <urcu-bp.h>
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "sal_shared.h"
#include "config_parsing.h"
#include "avltree.h"
#include "abstract_atomic.h"
#include "gsh_refstr.h"

/**
** Forward declarations to resolve circular dependency conflicts
*/
struct gsh_client;
struct gsh_export;
struct fsal_up_vector;		/* From fsal_up.h */
struct state_t;
extern struct gsh_refstr *no_export;

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
 * handled by the locks in MDCACHE.
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
 * exp_hdl->exp_ops.lookup(exp_hdl, name, ...);
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
 * First off, there is the fsal_obj_handle which is usually extended by the
 * FSAL. This is a memory object that describes an instance of a file within
 * the Ganesha structure. It contains all the information the FSAL needs to
 * interact with the underlying file system for that file.
 *
 * Next are three forms of what are more traditionally considered a file
 * handle. The host handle, the handle-key, and the wire handle.
 *
 * The host handle is the set of components the underlying file system
 * needs to find a file object given only a handle and not path and file name.
 * This might be information such as file system id, inode number, inode
 * generation, and maybe even directory parent information.
 *
 * The wire handle is the handle form shared with NFS clients. The upper
 * protocol layers encapsulate this handle with additional information including
 * the export_id which allows the upper layer to find the FSAL that owns the
 * handle. The wire handle may be byte swapped or have other transformations
 * from the host handle.
 *
 * The final handle form is the handle-key or simply key. This is used by the
 * MDACHE stackable FSAL (which is in fact always present). This handle may have
 * less information than the wire or host handles for FSALs that have multiple
 * forms of the same handle (for example, if directory parent is part of the
 * host and wire handle, a moved or linked file might have different
 * directory parents but we need a single cache object to reference the file.
 * In this case, the key strips out that directory parent information. Other
 * FSALs may actually need to have a separate cache object per export and thus
 * ADD the export_id to the key. This does cause problems but at the moment is
 * the best solution for the CEPH FSAL. A FSAL that has a larger key than the
 * host handle MUST NOT make it larger by more than FSAL_KEY_EXTRA_BYTES.
 *
 * There are two methods that convert between these forms:
 *
 *    wire_to_host converts a wire handle to a host handle. It would handle any
 *    byte swapping.
 *
 *    host_to_key converts a host handle into a handle-key. It is allowed to
 *    return a larger key than the host handle, but it may only increase the
 *    size by FSAL_KEY_EXTRA_BYTES defined below. NOTE that callers of
 *    host_to_key MUST make room for at least this many bytes past the size of
 *    the host handle.
 *
 * Note that there are no key_to_X methods. The mdcache (almost) always has a
 * reference to a fsal_obj_handle (after all, that's what it's caching...). The
 * exception is the dirent cache which caches host handles to provide a weak
 * link to the file system object rather than a strong link to a
 * fsal_obj_handle (if the object is in cache, a simple cache lookup will
 * resolve the object, host_to_key will be used during this lookup process).
 *
 * The host_to_X transformations are handled by the methods below,
 * handle_to_wire and handle_to_key.
 *
 * There are three methods that work to interface between handles and
 * fsal_obj_handles:
 *
 *    create_handle takes a host handle and calls to the underlying file system
 *    to instantiate a fsal_obj_handle for that object.
 *
 *    handle_to_wire takes a fsal_obj_handle and produces a wire handle to be
 *    passed to the protocol layer to be encapsulated and passed to a client.
 *
 *    handle_to_key takes a fsal_obj_handle and produces a handle-key to allow
 *    the object to be placed in the mdcache. Note that in some sense this does
 *    the same conversion as host_to_key.
 *
 * The invariant to be maintained is that given an @c fsal_obj_handle,
 * obj_hdl, exp_ops.host_to_key(wire_to_host(handle_to_wire(obj_hdl)))
 * is equal to obj_ops->handle_to_key(obj_hdl).
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
 *    wire_to_host is an export method for that purpose.  It should
 *    be able to take the wire-handle opaque and translate it into
 *    a host-handle. handle-key should be derived from @c host_to_key
 *    that MDCACHE can use to find an entry.
 *
 * 3. The @c handle_to_key method, a @c fsal_obj_handle method,
 *    generates a key for the MDCACHE hash table from the contents
 *    of the @c fsal_obj_handle.  It is an analogue of fsal export
 *    @c host_to_key method. Note where it is called to see why it is
 *    there.
 *
 * 4. The @c handle_to_wire method is similar in scope but it is the
 *    inverse of @c wire_to_host.  It's job is to fill in the opaque
 *    part of a protocol handle.  Note that it gets passed a @c gsh_buffdesc
 *    that describes the full opaque storage in whatever protocol
 *    specific structure is used.  It's job is to put whatever it
 *    takes into the opaque so the second and third items in this list
 *    work.
 *
 * 5. Unlike the old API, a @c fsal_obj_handle is part of a FSAL
 *    private structure for the object.  Note that there is no handle
 *    member of this public structure.  The bits necessary to both
 *    create a wire handle and use a filesystem handle go into this
 *    private structure. You can put whatever is required into the
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
 */

/**
 * @brief Major Version
 *
 * Increment this whenever any part of the existing API is changed,
 * e.g.  the argument list changed or a method is removed.
 *
 * Technically this should also change if the libganesha_nfsd.so exported API
 * changes.
 */

#define FSAL_MAJOR_VERSION 11

/**
 * @brief Minor Version
 *
 * Increment this whenever a new method is appended to the m_ops vector.
 * The remainder of the API is unchanged.
 *
 * If the major version is incremented, reset the minor to 0 (zero).
 *
 * If new members are appended to struct req_op_context (following its own
 * rules), increment the minor version
 */

#define FSAL_MINOR_VERSION 0

/* Forward references for object methods */

struct fsal_module;
struct fsal_export;
struct fsal_obj_handle;
struct fsal_filesystem;
struct fsal_pnfs_ds;
struct fsal_pnfs_ds_ops;
struct fsal_ds_handle;
struct fsal_dsh_ops;

/* Allow extra space in handle-key for expansion beyond the size of the host
 * handle. Used by callers of host_to_key.
 */
#define FSAL_KEY_EXTRA_BYTES 8

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

enum request_type {
	UNKNOWN_REQUEST,
	NFS_CALL,
	NFS_REQUEST,
#ifdef _USE_9P
	_9P_REQUEST,
#endif				/* _USE_9P */
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
 * If an op context is active, ctx_export MAY be NULL (very rare conditions)
 * but ctx_fullpath and ctx_pseudopath must always be valid. The function
 * init_op_context() assures this and init_op_context() and resum_op_context()
 * are the only functions that can ever set op_ctx to a non-NULL value, and
 * resume_op_context() must be setting it to point to an op context initialized
 * with init_op_context().
 *
 * NOTE: This is an across-the-api shared structure.  Changing it implies a
 *       change in the FSAL API.
 *
 * NOTE: There is a set of functions in fsal.h to initialize and manage the
 *       req_op_context. Please use them...
 *
 * NOTE: Usually a gsh_export is referenced by the req_op_context, it is
 *       expected that a reference to that gsh_export will be held for the
 *       duration of the time it's attached to the req_op_context or the life
 *       of the req_op_context. To this extent, the responsibility to make the
 *       put_gsh_export is now borne by the req_op_context management functions.
 *       Specifically release_op_contextm, clear_op_context_export,
 *       set_op_context_export, set_op_context_export_fsal, and
 *       restore_op_context_export will all call put_gsh_export if there is an
 *       attached export.
 *
 *       The functions that manage a saved_export_context maintain a reference
 *       to the gsh_export and thus discard_op_context_export will clean that
 *       up since the saved req_op_context will not be restored.
 *
 * NOTE: In support of exports having fullpath and pseudopath changeable,
 *       those strings are now accessed by gsh_refstr. In order to simplify
 *       the bulk of access to those strings for op_cxt->ctx_export, whenever
 *       ctx_export is changed, proper references to those strings are taken
 *       and stored in the req_op_context. Since those references can not be
 *       changed by any other thread, they are safe to access without RCU
 *       protection. Further if for any reason, those strings are not available,
 *       particularly when no export is attached, a reference to a "No Export"
 *       string is taken, so it is ALWAYS safe to use these strings, there is
 *       ALWAYS a valid reference which is safe to use in the context of the
 *       thread owning the req_op_context. There are a set of functions that
 *       make it easy to access these strings, and to access the "best" string
 *       in the context of which string is used for NFS v3 MOUNT requests.
 */

struct req_op_context {
	struct req_op_context *saved_op_ctx; /* saved op_ctx */
	struct user_cred creds;	/*< resolved user creds from request */
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
	enum request_type req_type;	/*< request_type NFS | 9P */
	struct gsh_client *client;	/*< client host info including stats */
	struct gsh_export *ctx_export;	/*< current export, this MUST only
					    be changed by one of the functions
					    in commonlib.c. */
	struct gsh_refstr *ctx_fullpath;	/*< current fullpath */
	struct gsh_refstr *ctx_pseudopath;	/*< current pseudopath */
	struct fsal_export *fsal_export;	/*< current fsal export */
	struct export_perms export_perms;	/*< Effective export perms */
	struct timespec start_time;	/*< start time of this op/request */
	void *fsal_private;		/*< private for FSAL use */
	void *proto_private;		/*< private for protocol layer use */
	struct fsal_module *fsal_module;	/*< current fsal module */
	struct fsal_pnfs_ds *ctx_pnfs_ds;	/*< current pNFS DS */
};

/**
 * @brief Structure to save export context from op_context
 *
 * When we need to temporarily change the export in op_context, we can save
 * the context here. Use of this is cheaper than using set_op_context_export
 * to restore the op context since various references need not be retaken.
 */
struct saved_export_context {
	struct gsh_export *saved_export;
	struct gsh_refstr *saved_fullpath;	/*< saved fullpath */
	struct gsh_refstr *saved_pseudopath;	/*< saved pseudopath */
	struct fsal_export *saved_fsal_export;
	struct fsal_module *saved_fsal_module;
	struct fsal_pnfs_ds *saved_pnfs_ds;	/*< saved pNFS DS */
	struct export_perms saved_export_perms;
};

/* Anything using these expects a valid op context and a valid op context
 * always had at least a reference to the no_export string.
 */
#define CTX_PSEUDOPATH(ctx) (ctx->ctx_pseudopath->gr_val)
#define CTX_FULLPATH(ctx) (ctx->ctx_fullpath->gr_val)

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
	int (*unload)(struct fsal_module *fsal_hdl);

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
 * @param[out]err_type      config error processing state
 *
 * @return FSAL status.
 */
	 fsal_status_t (*init_config)(struct fsal_module *fsal_hdl,
				      config_file_t config_struct,
				      struct config_error_type *err_type);
/**
 * @brief Dump configuration
 *
 * This function dumps a human readable representation of the FSAL
 * configuration.
 *
 * @param[in] fsal_hdl The FSAL module.
 * @param[in] log_fd   File descriptor to which to output the dump
 */
	void (*dump_config)(struct fsal_module *fsal_hdl, int log_fd);

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
 * @param[out]    err_type    config proocessing error reporting
 * @param[in]     up_ops      Upcall ops
 *
 * @return FSAL status.
 */
	 fsal_status_t (*create_export)(struct fsal_module *fsal_hdl,
					void *parse_node,
					struct config_error_type *err_type,
					const struct fsal_up_vector *up_ops);

/**
 * @brief Update an existing export
 *
 * This will result in a temporary fsal_export being created, and built into
 * a stacked export.
 *
 * On entry, op_ctx has the original gsh_export and no fsal_export.
 *
 * The caller passes the original fsal_export, as well as the new super_export's
 * FSAL when there is a stacked export. This will allow the underlying export to
 * validate that the stacking has not changed.
 *
 * This function does not actually create a new fsal_export, the only purpose is
 * to validate and update the config.
 *
 * @param[in]     fsal_hdl         FSAL module
 * @param[in]     parse_node       opaque pointer to parse tree node for
 *                                 export options to be passed to
 *                                 load_config_from_node
 * @param[out]    err_type         config proocessing error reporting
 * @param[in]     original         The original export that is being updated
 * @param[in]     updated_super    The updated super_export's FSAL
 *
 * @return FSAL status.
 */
	 fsal_status_t (*update_export)(struct fsal_module *fsal_hdl,
					void *parse_node,
					struct config_error_type *err_type,
					struct fsal_export *original,
					struct fsal_module *updated_super);

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
	void (*emergency_cleanup)(void);

/**
 * pNFS functions
 */

/**
 * @brief Get information about a pNFS device
 *
 * When this function is called, the FSAL should write device
 * information to the @c da_addr_body stream.
 *
 * @param[in]  fsal_hdl     FSAL module
 * @param[out] da_addr_body An XDR stream to which the FSAL is to
 *                          write the layout type-specific information
 *                          corresponding to the deviceid.
 * @param[in]  type         The type of layout that specified the
 *                          device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */
	 nfsstat4(*getdeviceinfo)(struct fsal_module *fsal_hdl,
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
	 size_t (*fs_da_addr_size)(struct fsal_module *fsal_hdl);

/**
 * @brief Create a FSAL pNFS data server
 *
 * @param[in]  fsal_hdl		FSAL module
 * @param[in]  parse_node	opaque pointer to parse tree node for
 *				export options to be passed to
 *				load_config_from_node
 * @param[out] handle		FSAL pNFS DS
 *
 * @return FSAL status.
 */
	 fsal_status_t (*create_fsal_pnfs_ds)(
					struct fsal_module *const fsal_hdl,
					void *parse_node,
					struct fsal_pnfs_ds **const handle);

/**
 * @brief Initialize FSAL specific values for pNFS data server
 *
 * @param[in]  ops	FSAL pNFS Data Server operations vector
 */
	 void (*fsal_pnfs_ds_ops)(struct fsal_pnfs_ds_ops *ops);

/**
 * @brief Provides function to extract FSAL stats
 *
 * @param[in] fsal_hdl		FSAL module
 * @param[in] iter		opaque pointer to DBusMessageIter
 */
	void (*fsal_extract_stats)(struct fsal_module *const fsal_hdl,
				   void *iter);

/**
 * @brief FSAL function to reset FSAL stats
 *
 * @param[in] fsal_hdl          FSAL module
 */
	void (*fsal_reset_stats)(struct fsal_module *const fsal_hdl);

/**@}*/
};

/**
 * @brief Export operations
 */

struct export_ops {
/**@{*/

/**
* Export information
*/

/**
 * @brief Get the name of the FSAL provisioning the export
 *
 * This function is used to find the name of the ultimate FSAL providing the
 * filesystem.  If FSALs are stacked, then the super-FSAL may want to pass this
 * through to the sub-FSAL to get the name, or add the sub-FSAL's name onto it's
 * own name.
 *
 * @param[in] exp_hdl The export to query.
 * @return Name of FSAL provisioning export
 */
	 const char *(*get_name)(struct fsal_export *exp_hdl);
/**@}*/

/**@{*/

/**
* Export lifecycle management.
*/

/**
 * @brief Prepare an export to be unexported
 *
 * This function is called prior to unexporting an export. It should do any
 * preparation that the export requires prior to being removed.
 */
	 void (*prepare_unexport)(struct fsal_export *exp_hdl);

/**
 * @brief Clean up an export when it's unexported
 *
 * This function is called when the export is unexported.  It should release any
 * working data that is not necessary when unexported, but not free the export
 * itself, as there are still references to it.
 *
 * @param[in] exp_hdl	The export to unexport.
 * @param[in] root_obj	The root object of the export
 */
	 void (*unexport)(struct fsal_export *exp_hdl,
			  struct fsal_obj_handle *root_obj);

/**
 * @brief Handle the unmounting of an export.
 *
 * This function is called when the export is unmounted.  The FSAL may need
 * to clean up references to the junction_obj and parent export.
 *
 * Specifically, mdcache must remove the export mapping and possibly schedule
 * the junction node for cleanup (which may be the same node as the unmounted
 * export's root node).
 *
 * The caller is expected to hold a reference to the junction_obj.
 *
 * @param[in] parent_exp_hdl	The parent export of the mount.
 * @param[in] junction_obj	The junction object the export was mounted on
 */
	 void (*unmount)(struct fsal_export *parent_exp_hdl,
			 struct fsal_obj_handle *junction_obj);

/**
 * @brief Finalize an export
 *
 * This function is called as part of cleanup when the last reference to
 * an export is released and it is no longer part of the list.  It
 * should clean up all private resources and destroy the object.
 *
 * @param[in] exp_hdl The export to release.
 */
	 void (*release)(struct fsal_export *exp_hdl);
/**@}*/

/**@{*/
/**
 * Create an object handles within this export
 */

/**
 * @brief Look up a path
 *
 * This function looks up a path within the export, it is now exclusively
 * used to get a handle for the root directory of the export.
 *
 * NOTE: This method will eventually be replaced by a method that simply
 *       requests the root obj_handle be instantiated. The single caller
 *       doesn't request attributes (nor did the two callers that were removed
 *       in favor of calling fsal_lookup_path).
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     exp_hdl   The export in which to look up
 * @param[in]     path      The path to look up
 * @param[out]    handle    The object found
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a handle has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*lookup_path)(struct fsal_export *exp_hdl,
				      const char *path,
				      struct fsal_obj_handle **handle,
				      struct fsal_attrlist *attrs_out);

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
	 fsal_status_t (*lookup_junction)(struct fsal_export *exp_hdl,
					  struct fsal_obj_handle *junction,
					  struct fsal_obj_handle **handle);
/**
 * @brief Convert a wire handle to a host handle
 *
 * This function extracts a host handle from a wire handle.  That
 * is, when given a handle as passed to a client, this method will
 * extract the handle to create objects.
 *
 * @param[in]     exp_hdl Export handle
 * @param[in]     in_type Protocol through which buffer was received.
 * @param[in]     flags   Flags to describe the wire handle. Example, if
 *			  the handle is a big endian handle.
 * @param[in,out] fh_desc Buffer descriptor.  The address of the
 *                        buffer is given in @c fh_desc->buf and must
 *                        not be changed.  @c fh_desc->len is the
 *                        length of the data contained in the buffer,
 *                        @c fh_desc->len must be updated to the correct
 *                        host handle size.
 *
 * @return FSAL type.
 */
	 fsal_status_t (*wire_to_host)(struct fsal_export *exp_hdl,
					 fsal_digesttype_t in_type,
					 struct gsh_buffdesc *fh_desc,
					 int flags);

/**
 * @brief extract "key" from a host handle
 *
 * This function extracts a "key" from a host handle.  That is, when
 * given a handle that is extracted from wire_to_host() above, this
 * method will extract the unique bits used to index the inode cache.
 *
 * NOTE: Callers MUST make sure the passed in buffer has at least enough
 *       room to hold the host handle PLUS FSAL_KEY_EXTRA_BYTES (defined
 *       above).
 *
 * @param[in]     exp_hdl Export handle
 * @param[in,out] fh_desc Buffer descriptor.  The address of the
 *                        buffer is given in @c fh_desc->buf and must
 *                        not be changed.  @c fh_desc->len is the length
 *                        of the data contained in the buffer, @c
 *                        fh_desc->len must be updated to the correct
 *                        size. In other words, the key has to be placed
 *                        at the beginning of the buffer!
 */
	 fsal_status_t (*host_to_key)(struct fsal_export *exp_hdl,
				      struct gsh_buffdesc *fh_desc);

/**
 * @brief Create a FSAL object handle from a host handle
 *
 * This function creates a FSAL object handle from a host handle
 * (when an object is no longer in cache but the client still remembers
 * the handle).
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     exp_hdl   The export in which to create the handle
 * @param[in]     hdl_desc  Buffer descriptor for the host handle
 * @param[out]    handle    FSAL object handle
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a handle has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*create_handle)(struct fsal_export *exp_hdl,
					struct gsh_buffdesc *fh_desc,
					struct fsal_obj_handle **handle,
					struct fsal_attrlist *attrs_out);
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
	 fsal_status_t (*get_fs_dynamic_info)(struct fsal_export *exp_hdl,
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
	 bool (*fs_supports)(struct fsal_export *exp_hdl,
			     fsal_fsinfo_options_t option);
/**
 * @brief Get the greatest file size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest file size supported.
 */
	 uint64_t (*fs_maxfilesize)(struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest read size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest read size supported.
 */
	 uint32_t (*fs_maxread)(struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest write size supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest write size supported.
 */
	 uint32_t (*fs_maxwrite)(struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest link count supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest link count supported.
 */
	 uint32_t (*fs_maxlink)(struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest name length supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest name length supported.
 */
	 uint32_t (*fs_maxnamelen)(struct fsal_export *exp_hdl);

/**
 * @brief Get the greatest path length supported
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Greatest path length supported.
 */
	 uint32_t (*fs_maxpathlen)(struct fsal_export *exp_hdl);

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
	 fsal_aclsupp_t (*fs_acl_support)(struct fsal_export *exp_hdl);

/**
 * @brief Get supported attributes
 *
 * This function returns a list of all attributes that this FSAL will
 * support.  Be aware that this is specifically the attributes in
 * struct fsal_attrlist, other NFS attributes (fileid and so forth) are
 * supported through other means.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return supported attributes.
 */
	 attrmask_t (*fs_supported_attrs)(struct fsal_export *exp_hdl);

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
	 uint32_t (*fs_umask)(struct fsal_export *exp_hdl);
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
	 fsal_status_t (*check_quota)(struct fsal_export *exp_hdl,
				      const char *filepath, int quota_type);

/**
 * @brief Get a user's quota
 *
 * This function retrieves a given user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[in]  quota_id   Id for which quota is set
 * @param[out] quota      The user's quota
 *
 * @return FSAL types.
 */
	 fsal_status_t (*get_quota)(struct fsal_export *exp_hdl,
				    const char *filepath, int quota_type,
				    int quota_id,
				    fsal_quota_t *quota);

/**
 * @brief Set a user's quota
 *
 * This function sets a user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[in]  quota_id   Id for which quota is set
 * @param[in]  quota      The values to set for the quota
 * @param[out] resquota   New values set (optional)
 *
 * @return FSAL types.
 */
	 fsal_status_t (*set_quota)(struct fsal_export *exp_hdl,
				    const char *filepath, int quota_type,
				    int quota_id,
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
	 nfsstat4(*getdevicelist)(struct fsal_export *exp_hdl,
				  layouttype4 type, void *opaque,
				  bool (*cb)(void *opaque, const uint64_t id),
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
	void (*fs_layouttypes)(struct fsal_export *exp_hdl, int32_t *count,
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
	 uint32_t (*fs_layout_blocksize)(struct fsal_export *exp_hdl);

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
	 uint32_t (*fs_maximum_segments)(struct fsal_export *exp_hdl);

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
	 size_t (*fs_loc_body_size)(struct fsal_export *exp_hdl);

/**
 * @brief Get write verifier
 *
 * This function is called by write and commit to match the commit verifier
 * with the one returned on  write.
 *
 * @param[in] exp_hdl	Export to query
 * @param[in,out] verf_desc Address and length of verifier
 */
	void (*get_write_verifier)(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *verf_desc);

/**@}*/

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

	struct state_t *(*alloc_state)(struct fsal_export *exp_hdl,
				       enum state_type state_type,
				       struct state_t *related_state);

/**
 * @brief Free a state_t structure
 *
 * @param[in] exp_hdl               Export state_t is associated with
 * @param[in] state                 state_t structure to free.
 *
 * @returns NULL on failure otherwise a state structure.
 */

	void (*free_state)(struct fsal_export *exp_hdl, struct state_t *state);

/**
 * @brief Check to see if a user is superuser
 *
 * @param[in] exp_hdl               Export state_t is associated with
 * @param[in] creds                 Credentials to check for superuser
 *
 * @returns NULL on failure otherwise a state structure.
 */

	bool (*is_superuser)(struct fsal_export *exp_hdl,
			     const struct user_cred *creds);

/**
 * @brief Get the expiration time for parent handle.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Expiration time for parent handle
 */

	int32_t (*fs_expiretimeparent)(struct fsal_export *exp_hdl);
};

/**
 * @brief Filesystem operations
 */

typedef void (*fsal_async_cb)(struct fsal_obj_handle *obj, fsal_status_t ret,
			      void *obj_data, void *caller_data);

/** Types of filesystem claims, there can not be both CLAIM_ROOT and CLAIM_CHILD
 *  for the same filesystem.
 */
enum claim_type {
	/** tyoe used only for counts of all claims */
	CLAIM_ALL,
	/** Claim is the root of the file system */
	CLAIM_ROOT,
	/** Claim is a subtree of the file system */
	CLAIM_SUBTREE,
	/** Claim is due to the parent being claimed */
	CLAIM_CHILD,
	/** Temporary claim */
	CLAIM_TEMP,
	/** Number of claim types */
	CLAIM_NUM
};

/** @brief FSAL method to claim a filesystem
 *
 * @param(in)  fs            filesystem to claim
 * @param(in)  exp           export to claim for
 * @param(out) private_data  private_data
 *
 * @retval errno or 0
 */
typedef int (*claim_filesystem_cb)(struct fsal_filesystem *fs,
				   struct fsal_export *exp,
				   void **private_data);

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

/**
 * @brief Directory cookie
 */

typedef uint64_t fsal_cookie_t;

/* Cookie values 0, 1, and 2 are reserved by NFS:
 * 0 is "start from beginning"
 * 1 is the cookie associated with the "." entry
 * 2 is the cookie associated with the ".." entry
 *
 * FSALs that support compute_readdir_cookie that are for some reason unable
 * to compute the cookie for the very first entry (other than . and ..)
 * should return FIRST_COOKIE. Caching layers such as MDCACHE should treat an
 * insert of an entry with cookie 3 as inserting a new first entry, and then
 * compute a new cookie for the old first entry - they can safely assume the
 * sort order doesn't change which may allow for optimization of things like'
 * AVL trees.
 */
#define FIRST_COOKIE 3

enum fsal_dir_result {
	/** Continue readdir, call back with another dirent. */
	DIR_CONTINUE,
	/** Continue supplying entries if readahead is supported, otherwise
	 *  stop providing entries.
	 */
	DIR_READAHEAD,
	/** Terminate readdir. */
	DIR_TERMINATE,
};

const char *fsal_dir_result_str(enum fsal_dir_result result);

/**
 * @brief Callback to provide readdir caller with each directory entry
 *
 * The called function will indicate if readdir should continue, terminate,
 * terminate and mark cookie, or continue and mark cookie. In the last case,
 * the called function may also return a cookie if requested in the ret_cookie
 * parameter (which may be NULL if the caller doesn't need to mark cookies).
 * If ret_cookie is 0, the caller had no cookie to return.
 *
 * @param[in]      name         The name of the entry
 * @param[in]      obj          The fsal_obj_handle describing the entry
 * @param[in]      attrs        The requested attribues for the entry (see
 *                              readdir attrmask parameter)
 * @param[in]      dir_state    Opaque pointer to be passed to callback
 * @param[in]      cookie       An FSAL generated cookie for the entry
 *
 * @returns fsal_dir_result above
 */
typedef enum fsal_dir_result (*fsal_readdir_cb)(
				const char *name, struct fsal_obj_handle *obj,
				struct fsal_attrlist *attrs,
				void *dir_state, fsal_cookie_t cookie);

/**
 * @brief Argument for read2/write2 and their callbacks
 *
 */
struct fsal_io_arg {
	size_t io_amount;	/**< Total amount of I/O actually done */
	struct io_info *info;	/**< More info about data for read_plus */
	union {
		bool end_of_file;	/**< True if end-of-file reached */
		bool fsal_stable;	/**< requested/achieved stability */
	};
	struct state_t *state;	/**< State to use for read (or NULL) */
	uint64_t offset;	/**< Offset into file to read */
	int iov_count;		/**< Number of vectors in iov */
	struct iovec iov[];	/**< Vector of buffers to fill */
};

/**
 * @brief FSAL object operations vector
 */

struct fsal_obj_ops {
/**@{*/

/**
 * Lifecycle management
 */

/**
 * @brief Get a reference to a handle
 *
 * Refcounting is required for all FSALs. An FSAL that will have FSAL_MDCACHE
 * stacked on top need not handle this as FSAL_MDCACHE will handle it.
 *
 * @param[in] obj_hdl Handle to release
 */
	 void (*get_ref)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Put a reference to a handle
 *
 * Refcounting is required for all FSALs. An FSAL that will have FSAL_MDCACHE
 * stacked on top need not handle this as FSAL_MDCACHE will handle it.
 *
 * @param[in] obj_hdl Handle to release
 */
	 void (*put_ref)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Clean up a filehandle
 *
 * This function cleans up private resources associated with a
 * filehandle and deallocates it.  Implement this method or you will
 * leak.  Refcount (if used) should be 1
 *
 * @param[in] obj_hdl Handle to release
 */
	 void (*release)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 *
 * @return FSAL status.
 *
 */
	 fsal_status_t (*merge)(struct fsal_obj_handle *orig_hdl,
				struct fsal_obj_handle *dupe_hdl);

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
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     dir_hdl   Directory to search
 * @param[in]     path      Name to look up
 * @param[out]    handle    Object found
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a handle has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*lookup)(struct fsal_obj_handle *dir_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out);

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
 * @param[in]  attrmask  Indicate which attributes the caller is interested in
 * @param[out] eof       true if the last entry was reached
 *
 * @return FSAL status.
 */
	 fsal_status_t (*readdir)(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
				  attrmask_t attrmask,
				  bool *eof);

/**
 * @brief Compute the readdir cookie for a given filename.
 *
 * Some FSALs are able to compute the cookie for a filename deterministically
 * from the filename. They also have a defined order of entries in a directory
 * based on the name (could be strcmp sort, could be strict alpha sort, could
 * be deterministic order based on cookie - in any case, the dirent_cmp method
 * will also be provided.
 *
 * The returned cookie is the cookie that can be passed as whence to FIND that
 * directory entry. This is different than the cookie passed in the readdir
 * callback (which is the cookie of the NEXT entry).
 *
 * @param[in]  parent  Directory file name belongs to.
 * @param[in]  name    File name to produce the cookie for.
 *
 * @retval 0 if not supported.
 * @returns The cookie value.
 */
	fsal_cookie_t (*compute_readdir_cookie)(struct fsal_obj_handle *parent,
						const char *name);

/**
 * @brief Help sort dirents.
 *
 * For FSALs that are able to compute the cookie for a filename
 * deterministically from the filename, there must also be a defined order of
 * entries in a directory based on the name (could be strcmp sort, could be
 * strict alpha sort, could be deterministic order based on cookie).
 *
 * Although the cookies could be computed, the caller will already have them
 * and thus will provide them to save compute time.
 *
 * @param[in]  parent   Directory entries belong to.
 * @param[in]  name1    File name of first dirent
 * @param[in]  cookie1  Cookie of first dirent
 * @param[in]  name2    File name of second dirent
 * @param[in]  cookie2  Cookie of second dirent
 *
 * @retval < 0 if name1 sorts before name2
 * @retval == 0 if name1 sorts the same as name2
 * @retval >0 if name1 sorts after name2
 */
	int (*dirent_cmp)(struct fsal_obj_handle *parent,
			  const char *name1, fsal_cookie_t cookie1,
			  const char *name2, fsal_cookie_t cookie2);
/**@}*/

/**@{*/

/**
 * Creation operations
 */

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     dir_hdl   Directory in which to create the directory
 * @param[in]     name      Name of directory to create
 * @param[in]     attrs_in  Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*mkdir)(struct fsal_obj_handle *dir_hdl,
				const char *name,
				struct fsal_attrlist *attrs_in,
				struct fsal_obj_handle **new_obj,
				struct fsal_attrlist *attrs_out);

/**
 * @brief Create a special file
 *
 * This function creates a new special file.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * If the node type has rawdev info, then @a attrs_in MUST have the rawdev field
 * set.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     dir_hdl   Directory in which to create the object
 * @param[in]     name      Name of object to create
 * @param[in]     nodetype  Type of special file to create
 * @param[in]     attrs_in  Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*mknode)(struct fsal_obj_handle *dir_hdl,
				 const char *name,
				 object_file_type_t nodetype,
				 struct fsal_attrlist *attrs_in,
				 struct fsal_obj_handle **new_obj,
				 struct fsal_attrlist *attrs_out);

/**
 * @brief Create a symbolic link
 *
 * This function creates a new symbolic link.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     dir_hdl   Directory in which to create the object
 * @param[in]     name      Name of object to create
 * @param[in]     link_path Content of symbolic link
 * @param[in]     attrs_in  Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
	 fsal_status_t (*symlink)(struct fsal_obj_handle *dir_hdl,
				  const char *name,
				  const char *link_path,
				  struct fsal_attrlist *attrs_in,
				  struct fsal_obj_handle **new_obj,
				  struct fsal_attrlist *attrs_out);
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
	 fsal_status_t (*readlink)(struct fsal_obj_handle *obj_hdl,
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
 * This method must read attributes and/or get them from a cache.
 *
 * @param[in] obj_hdl     Handle to check
 * @param[in] access_type Access requested
 * @param[out] allowed    Returned access that could be granted
 * @param[out] denied     Returned access that would be granted
 * @param[in] owner_skip  Skip test if op_ctx->creds is owner
 *
 * @return FSAL status.
 */
	 fsal_status_t (*test_access)(struct fsal_obj_handle *obj_hdl,
				      fsal_accessflags_t access_type,
				      fsal_accessflags_t *allowed,
				      fsal_accessflags_t *denied,
				      bool owner_skip);

/**
 * @brief Get attributes
 *
 * This function fetches the attributes for the object. The attributes
 * requested in the mask are copied out (though other attributes might
 * be copied out).
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * The caller MUST call fsal_release_attrs when done with the copied
 * out attributes. This will release any attributes that might take
 * additional memory.
 *
 * @param[in]  obj_hdl    Object to query
 * @param[out] attrs_out  Attribute list for file
 *
 * @return FSAL status.
 */
	 fsal_status_t (*getattrs)(struct fsal_obj_handle *obj_hdl,
				   struct fsal_attrlist *attrs_out);

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
	 fsal_status_t (*link)(struct fsal_obj_handle *obj_hdl,
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
	 fsal_status_t (*rename)(struct fsal_obj_handle *obj_hdl,
				 struct fsal_obj_handle *olddir_hdl,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_hdl,
				 const char *new_name);
/**
 * @brief Remove a name from a directory
 *
 * This function removes a name from a directory and possibly deletes
 * the file so named.
 *
 * @param[in] dir_hdl The directory from which to remove the name
 * @param[in] obj_hdl The object being removed
 * @param[in] name    The name to remove
 *
 * @return FSAL status.
 */
	 fsal_status_t (*unlink)(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name);

/**@}*/

/**@{*/
/**
 * I/O management
 */

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
	 fsal_status_t (*seek)(struct fsal_obj_handle *obj_hdl,
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
	 fsal_status_t (*io_advise)(struct fsal_obj_handle *obj_hdl,
				    struct io_hints *hints);

/**
 * @brief Close a file
 *
 * This function closes a file.  This should return ERR_FSAL_NOT_OPENED if
 * the global FD for this obj was not open.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */
	 fsal_status_t (*close)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Reserve/Deallocate space in a region of a file
 *
 * @param[in] obj_hdl File to which bytes should be allocated
 * @param[in] state   open stateid under which to do the allocation
 * @param[in] offset  offset at which to begin the allocation
 * @param[in] length  length of the data to be allocated
 * @param[in] allocate Should space be allocated or deallocated?
 *
 * @return FSAL status.
 */
	 fsal_status_t (*fallocate)(struct fsal_obj_handle *obj_hdl,
				    struct state_t *state, uint64_t offset,
				    uint64_t length, bool allocate);
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
	 fsal_status_t (*list_ext_attrs)(struct fsal_obj_handle *obj_hdl,
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
	 fsal_status_t (*getextattr_id_by_name)(struct fsal_obj_handle *obj_hdl,
						const char *xattr_name,
						unsigned int *xattr_id);
/**
 * @brief Get content of an attribute by name
 *
 * This function returns the value of an extended attribute as
 * specified by name.
 *
 * As a special rule, because it is implemented that way in the linux
 * getxattr call, giving a buffer_size of 0 is allowed and should set
 * output_size appropriately to fit the xattr.
 *
 * Please note that the xattr could change between the query-size call
 * and that actual fetch, so this is not fail-proof.
 *
 * @param[in]  obj_hdl     File to interrogate
 * @param[in]  xattr_name  Name of attribute
 * @param[out] buffer_addr Buffer to store content
 * @param[in]  buffer_size Buffer size
 * @param[out] output_size Size of content
 *
 * @return FSAL status.
 */
	 fsal_status_t (*getextattr_value_by_name)(struct fsal_obj_handle *
						   obj_hdl,
						   const char *xattr_name,
						   void *buffer_addr,
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
	 fsal_status_t (*getextattr_value_by_id)(struct fsal_obj_handle *
						 obj_hdl,
						 unsigned int xattr_id,
						 void *buffer_addr,
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
	 fsal_status_t (*setextattr_value)(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   void *buffer_addr,
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
	 fsal_status_t (*setextattr_value_by_id)(struct fsal_obj_handle *
						 obj_hdl,
						 unsigned int xattr_id,
						 void *buffer_addr,
						 size_t buffer_size);

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
	 fsal_status_t (*remove_extattr_by_id)(struct fsal_obj_handle *obj_hdl,
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
	 fsal_status_t (*remove_extattr_by_name)(struct fsal_obj_handle *
						 obj_hdl,
						 const char *xattr_name);
/**@}*/

/**@{*/
/**
 * Handle operations
 */

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
	 fsal_status_t (*handle_to_wire)(const struct fsal_obj_handle *obj_hdl,
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
	void (*handle_to_key)(struct fsal_obj_handle *obj_hdl,
			      struct gsh_buffdesc *fh_desc);
/**
 * @brief Compare two handles
 *
 * This function compares two handles to see if they reference the same file
 *
 * @param[in]     obj_hdl1    The first handle to compare
 * @param[in]     obj_hdl2    The second handle to compare
 *
 * @return True if match, false otherwise
 */
	 bool (*handle_cmp)(struct fsal_obj_handle *obj_hdl1,
			    struct fsal_obj_handle *obj_hdl2);
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
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */
	 nfsstat4(*layoutget)(struct fsal_obj_handle *obj_hdl,
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
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */
	 nfsstat4(*layoutreturn)(struct fsal_obj_handle *obj_hdl,
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
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
	 nfsstat4(*layoutcommit)(struct fsal_obj_handle *obj_hdl,
				 XDR * lou_body,
				 const struct fsal_layoutcommit_arg *arg,
				 struct fsal_layoutcommit_res *res);

/**
 * @brief Get Extended Attribute
 *
 * This function gets an extended attribute of an object.
 *
 * @param[in]  obj_hdl  Input object to query
 * @param[in]  xa_name  Input xattr name
 * @param[out] xa_value Output xattr value
 *
 * @return FSAL status.
 */
	 fsal_status_t (*getxattrs)(struct fsal_obj_handle *obj_hdl,
				    xattrkey4 *xa_name,
				    xattrvalue4 *xa_value);

/**
 * @brief Set Extended Attribute
 *
 * This function sets an extended attribute of an object.
 *
 * @param[in]  obj_hdl  Input object to set
 * @param[in]  xa_type  Input xattr type
 * @param[in]  xa_name  Input xattr name to set
 * @param[in]  xa_value Input xattr value to set
 *
 * @return FSAL status.
 */
	 fsal_status_t (*setxattrs)(struct fsal_obj_handle *obj_hdl,
				    setxattr_option4 option,
				    xattrkey4 *xa_name,
				    xattrvalue4 *xa_value);

/**
 * @brief Remove Extended Attribute
 *
 * This function remove an extended attribute of an object.
 *
 * @param[in]  obj_hdl  Input object to set
 * @param[in]  xa_name  Input xattr name to remove
 *
 * @return FSAL status.
 */
	 fsal_status_t (*removexattrs)(struct fsal_obj_handle *obj_hdl,
				    xattrkey4 *xa_name);

/**
 * @brief List Extended Attributes
 *
 * This function list the extended attributes of an object.
 *
 * @param[in]      obj_hdl       Input object to list
 * @param[in]      la_maxcount   Input maximum number of bytes for names
 * @param[in,out]  la_cookie     In/out cookie
 * @param[out]     lr_eof        Output eof set if no more extended attributes
 * @param[out]     lr_names      Output list of extended attribute names
 *				 this buffer size is double the size of
 *				 la_maxcount to allow for component4 overhead
 *
 * @return FSAL status.
 */
	 fsal_status_t (*listxattrs)(struct fsal_obj_handle *obj_hdl,
				     count4 la_maxcount,
				     nfs_cookie4 *la_cookie,
				     bool_t *lr_eof,
				     xattrlist4 * lr_names);


/**@}*/

/**@{*/

/**
 * Extended API functions.
 *
 * With these new operations, the FSAL becomes responsible for managing
 * share reservations. The FSAL is also granted more control over the
 * state of a "file descriptor" and has more control of what a "file
 * descriptor" even is. Ultimately, it is whatever the FSAL needs in
 * order to manage the share reservations and lock state.
 *
 * The open2 method also allows atomic create/setattr/open (just like the
 * NFS v4 OPEN operation).
 *
 */

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * At least the mode attribute must be set if createmode is not FSAL_NO_CREATE.
 * Some FSALs may still have to pass a mode on a create call for exclusive,
 * and even with FSAL_NO_CREATE, and empty set of attributes MUST be passed.
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method may instantiate a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * The attributes will not be returned if this is an open by object as
 * opposed to an open by name.
 *
 * @note If the file was created, @a new_obj has been ref'd
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrs_in              Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] attrs_out         Optional attributes for newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */
	 fsal_status_t (*open2)(struct fsal_obj_handle *obj_hdl,
				struct state_t *state,
				fsal_openflags_t openflags,
				enum fsal_create_mode createmode,
				const char *name,
				struct fsal_attrlist *attrs_in,
				fsal_verifier_t verifier,
				struct fsal_obj_handle **new_obj,
				struct fsal_attrlist *attrs_out,
				bool *caller_perm_check);

/**
 * @brief Check the exclusive create verifier for a file.
 *
 * @param[in] obj_hdl     File to check verifier
 * @param[in] verifier    Verifier to use for exclusive create
 *
 * @retval true if verifier matches
 */
	 bool (*check_verifier)(struct fsal_obj_handle *obj_hdl,
				fsal_verifier_t verifier);

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. The st_lock must be held.
 *
 * @param[in] obj_hdl     File owning state
 * @param[in] state File state to interrogate
 *
 * @retval Flags representing current open status
 */
	fsal_openflags_t (*status2)(struct fsal_obj_handle *obj_hdl,
				    struct state_t *state);

/**
 * @brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. The st_lock must be held.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics. One example would be 9P lopen.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       state_t to use for this operation
 * @param[in] openflags   Mode for re-open
 *
 * @return FSAL status.
 */
	 fsal_status_t (*reopen2)(struct fsal_obj_handle *obj_hdl,
				  struct state_t *state,
				  fsal_openflags_t openflags);

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.  This is
 * an (optionally) asynchronous call.  When the I/O is complete, the done
 * callback is called with the results.
 *
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */
	 void (*read2)(struct fsal_obj_handle *obj_hdl,
		       bool bypass,
		       fsal_async_cb done_cb,
		       struct fsal_io_arg *read_arg,
		       void *caller_arg);

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * This is an (optionally) asynchronous call.  When the I/O is complete, the @a
 * done_cb callback is called.
 *
 * @param[in]     obj_hdl       File on which to operate
 * @param[in]     bypass        If state doesn't indicate a share reservation,
 *                              bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] write_arg	Info about write, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */
	 void (*write2)(struct fsal_obj_handle *obj_hdl,
			bool bypass,
			fsal_async_cb done_cb,
			struct fsal_io_arg *write_arg,
			void *caller_arg);

/**
 * @brief Seek to data or hole
 *
 * This function seek to data or hole in a file.
 *
 * @param[in]     obj_hdl   File on which to operate
 * @param[in]     state     state_t to use for this operation
 * @param[in,out] info      Information about the data
 *
 * @return FSAL status.
 */
	 fsal_status_t (*seek2)(struct fsal_obj_handle *obj_hdl,
				struct state_t *state,
				struct io_info *info);
/**
 * @brief IO Advise
 *
 * This function give hints to fs.
 *
 * @param[in]     obj_hdl          File on which to operate
 * @param[in]     state            state_t to use for this operation
 * @param[in,out] info             Information about the data
 *
 * @return FSAL status.
 */
	 fsal_status_t (*io_advise2)(struct fsal_obj_handle *obj_hdl,
				     struct state_t *state,
				     struct io_hints *hints);

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */
	 fsal_status_t (*commit2)(struct fsal_obj_handle *obj_hdl,
				  off_t offset,
				  size_t len);

/**
 * @brief Perform a lock operation
 *
 * This function performs a lock operation (lock, unlock, test) on a
 * file. This method assumes the FSAL is able to support lock owners,
 * though it need not support asynchronous blocking locks. Passing the
 * lock state allows the FSAL to associate information with a specific
 * lock owner for each file (which may include use of a "file descriptor".
 *
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  state            state_t to use for this operation
 * @param[in]  owner            Lock owner
 * @param[in]  lock_op          Operation to perform
 * @param[in]  request_lock     Lock to take/release/test
 * @param[out] conflicting_lock Conflicting lock
 *
 * @return FSAL status.
 */
	 fsal_status_t (*lock_op2)(struct fsal_obj_handle *obj_hdl,
				   struct state_t *state,
				   void *owner,
				   fsal_lock_op_t lock_op,
				   fsal_lock_param_t *request_lock,
				   fsal_lock_param_t *conflicting_lock);

/**
 * @brief Acquire or Release delegation
 *
 * This functions acquires/releases delegation/lease_lock.
 *
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  state            state_t to use for this operation
 * @param[in]  owner            Opaque state owner token
 * @param[in]  deleg            Requested delegation state
 *
 * @return FSAL status.
 */
	 fsal_status_t (*lease_op2)(struct fsal_obj_handle *obj_hdl,
				    struct state_t *state,
				    void *owner,
				    fsal_deleg_t deleg);

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrib_set->mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] bypass     If state doesn't indicate a share reservation,
 *                       bypass any non-mandatory deny write
 * @param[in] state      state_t to use for this operation
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
	 fsal_status_t (*setattr2)(struct fsal_obj_handle *obj_hdl,
				   bool bypass,
				   struct state_t *state,
				   struct fsal_attrlist *attrib_set);

/**
 * @brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 *
 * @return FSAL status.
 */
	 fsal_status_t (*close2)(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state);

/**@}*/

/**
 * @brief Determine if the given handle is a referral point
 *
 * @param[in]	  obj_hdl	Handle on which to operate
 * @param[in|out] attrs		Attributes of the handle
 * @param[in]	  cache_attrs	Cache the received attrs
 *
 * @return true if it is a referral point, false otherwise
 */

	 bool (*is_referral)(struct fsal_obj_handle *obj_hdl,
			     struct fsal_attrlist *attrs,
			     bool cache_attrs);

/**@{*/

/**
 * ASYNC API functions.
 *
 * These are asyncronous versions of some of the API functions.  FSALs are
 * expected to implement these, but the upper layers are not expected to call
 * them.  Instead, they will be called by MDCACHE at the appropriate points.
 */

/**@}*/
};

/**
 * @brief FSAL pNFS Data Server operations vector
 */

struct fsal_pnfs_ds_ops {
/**@{*/

/**
 * Lifecycle management.
 */

/**
 * @brief Clean up a server
 *
 * This function cleans up private resources associated with a
 * server and deallocates it.  A default is supplied.
 *
 * This function should not be called directly.
 *
 * @param[in]  pds	FSAL pNFS DS to release
 */
	 void (*ds_release)(struct fsal_pnfs_ds *const pds);

/**
 * @brief Initialize FSAL specific permissions per pNFS DS
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  req      Incoming request.
 *
 * @return NFSv4.1 error codes:
 *			NFS4_OK, NFS4ERR_ACCESS, NFS4ERR_WRONGSEC.
 */
	 nfsstat4(*ds_permissions)(struct fsal_pnfs_ds *const pds,
				struct svc_req *req);
/**@}*/

/**@{*/

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  hdl_desc Buffer from which to create the struct
 * @param[out] handle   FSAL DS handle
 *
 * @return NFSv4.1 error codes.
 */
	 nfsstat4(*make_ds_handle)(struct fsal_pnfs_ds *const pds,
				   const struct gsh_buffdesc *
				   const hdl_desc,
				   struct fsal_ds_handle **const handle,
				   int flags);

/**
 * DS handle Lifecycle management.
 */

/**
 * @brief Clean up a DS handle
 *
 * This function cleans up private resources associated with a
 * filehandle and deallocates it.  Implement this method or you will
 * leak.  This function should not be called directly.
 *
 * @param[in] ds_hdl Handle to release
 */
	 void (*dsh_release)(struct fsal_ds_handle *const ds_hdl);
/**@}*/

/**@{*/

/**
 * DS handle I/O Functions
 */

/**
 * @brief Read from a data-server handle.
 *
 * NFSv4.1 data server handles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
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
	 nfsstat4 (*dsh_read)(struct fsal_ds_handle *const ds_hdl,
			      const stateid4 *stateid,
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
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
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
	 nfsstat4 (*dsh_read_plus)(struct fsal_ds_handle *const ds_hdl,
				   const stateid4 *stateid,
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
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_hdl           FSAL DS handle
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
	 nfsstat4 (*dsh_write)(struct fsal_ds_handle *const ds_hdl,
			       const stateid4 *stateid,
			       const offset4 offset,
			       const count4 write_length,
			       const void *buffer,
			       const stable_how4 stability_wanted,
			       count4 * const written_length,
			       verifier4 * const writeverf,
			       stable_how4 * const stability_got);

/**
 * @brief Commit a byte range to a DS handle.
 *
 * NFSv4.1 data server filehandles are disjount from normal
 * filehandles (in Ganesha, there is a ds_flag in the filehandle_v4_t
 * structure) and do not get loaded into mdcache or processed the
 * normal way.
 *
 * @param[in]  ds_hdl    FSAL DS handle
 * @param[in]  offset    Start of commit window
 * @param[in]  count     Length of commit window
 * @param[out] writeverf Write verifier
 *
 * @return An NFSv4.1 status code.
 */
	 nfsstat4 (*dsh_commit)(struct fsal_ds_handle *const ds_hdl,
				const offset4 offset,
				const count4 count,
				verifier4 * const writeverf);
/**@}*/
};

/**
 * @brief FSAL object definition
 *
 * This structure is the base FSAL instance definition, providing the
 * public face to a single, loaded FSAL.
 */

struct fsal_module {
	struct glist_head fsals;	/*< link in list of loaded fsals */
	struct glist_head exports;	/*< Head of list of exports from
					   this FSAL */
	struct glist_head handles;	/*< Head of list of object handles */
	struct glist_head servers;	/*< Head of list of Data Servers */
	char *path;		/*< Path to .so file */
	char *name;		/*< Name set from .so and/or config */
	void *dl_handle;	/*< Handle to the dlopen()d shared
				   library. NULL if statically linked */
	struct fsal_ops m_ops;	/*< FSAL module methods vector */

	pthread_rwlock_t lock;		/*< Lock to be held when
					    manipulating its lists (above). */
	int32_t refcount;		/*< Reference count */
	struct fsal_stats *stats;   /*< for storing the FSAL specific stats */
	struct fsal_staticfsinfo_t fs_info; /*< for storing FSAL static info */
};

/**
 * @brief Get a reference to a module
 *
 * @param[in] fsal_hdl FSAL on which to acquire reference.
 */

static inline void fsal_get(struct fsal_module *fsal_hdl)
{
	(void) atomic_inc_int32_t(&fsal_hdl->refcount);
	assert(fsal_hdl->refcount > 0);
}

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

	refcount = atomic_dec_int32_t (&fsal_hdl->refcount);

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
	struct glist_head exports;	/*< Link in list of exports from
					   the same FSAL. */
	struct fsal_module *fsal;	/*< Link back to the FSAL module */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	struct export_ops exp_ops;	/*< Vector of operations */
	struct fsal_export *sub_export;	/*< Sub export for stacking */
	struct fsal_export *super_export;/*< Super export for stacking */
	struct gsh_export *owning_export; /*< The gsh_export this belongs to */
	struct fsal_filesystem *root_fs;
	struct glist_head filesystems;
	uint16_t export_id; /*< Export ID copied from gsh_export, initialized
				by  fsal_export_init */
};

/*
 * Link fsal_filesystems and fsal_exports
 * Supports a many-to-many relationship
 */
struct fsal_filesystem_export_map {
	struct fsal_filesystem_export_map *parent_map;
	struct fsal_export *exp;
	struct fsal_filesystem *fs;
	struct glist_head child_maps;
	struct glist_head on_parent;
	struct glist_head on_exports;
	struct glist_head on_filesystems;
	enum claim_type claim_type;
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
 * handle, since mdcache is managing life cycle and concurrency?
 * That is, do we expect fsal_obj_handle to have a reference count
 * that would be separate from that managed by mdcache_lru?
 */

struct fsal_obj_handle {
	struct glist_head handles;	/*< Link in list of handles under
					   the same FSAL. */
	struct fsal_filesystem *fs;	/*< Owning filesystem */
	struct fsal_module *fsal;	/*< Link back to fsal module */
	struct fsal_obj_ops *obj_ops;	/*< Operations vector */

	pthread_rwlock_t obj_lock;		/*< Lock on handle */

	/* Static attributes */
	object_file_type_t type;	/*< Object file type */
	fsal_fsid_t fsid;	/*< Filesystem on which this object is
				   stored */
	uint64_t fileid;	/*< Unique identifier for this object within
				   the scope of the fsid, (e.g. inode number) */

	struct state_hdl *state_hdl;	/*< State related to this handle */
	int32_t exp_refcnt;	/*< ref count by export root nodes or
				    export junction nodes*/
};

/**
 * @brief Public structure for pNFS Data Servers
 *
 * This structure is used for files of all types including directories
 * and anything else that can be operated on via NFS.  Having an
 * independent reference count and lock here makes sense, since there
 * is no caching infrastructure overlaying this system.
 *
 */

/**
 * @brief PNFS Data Server
 *
 * This represents a Data Server for PNFS.  It may be stand-alone, or may be
 * associated with an export (which represents an MDS).
 *
 * NOTE: While a fsal_pnfs_ds is stored in a lookup table, if it has an
 *       mds_export attached, an export reference MUST be held. This is
 *       accomplished by pnfs_ds_insert and pnfs_ds_remove.
 */
struct fsal_pnfs_ds {
	struct glist_head ds_list;	/**< Entry in list of all DSs */
	struct glist_head server;	/**< Link in list of Data Servers under
					   the same FSAL. */
	struct fsal_module *fsal;	/**< Link back to fsal module */
	struct fsal_pnfs_ds_ops s_ops;	/**< Operations vector */
	struct gsh_export *mds_export;	/**< related export */
	struct fsal_export *mds_fsal_export;	/**< related FSAL export (avoids
						  MDS stacking) */

	struct avltree_node ds_node;	/**< Node in tree of all Data Servers */
	int32_t ds_refcount;		/**< Reference count */
	uint16_t id_servers;		/**< Identifier */
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
};

/**
 * @brief Get a reference on a fsal object handle by export
 *
 * This function increments the reference count on export root object
 * or PseudoFS export junction nodes.
 *
 */
static inline void export_root_object_get(struct fsal_obj_handle *obj_hdl)
{
	(void) atomic_inc_int32_t (&obj_hdl->exp_refcnt);
}

/**
 * @brief Put a reference on a fsal object handle by export
 *
 * This function releases the reference count on export root object
 * or PseudoFS export junction nodes.
 *
 */
static inline void export_root_object_put(struct fsal_obj_handle *obj_hdl)
{
	int32_t ref = atomic_dec_int32_t (&obj_hdl->exp_refcnt);

	assert(ref >= 0);
}

/**
 * @brief Determines whether the object handle is referenced
 * by one or more exports root
 *
 * @param[in] obj_hdl  object handle need to be judged
 * @return true if referenced by export, false otherwise
 */
static inline bool is_export_pin(struct fsal_obj_handle *obj_hdl)
{
	int32_t ref = atomic_fetch_int32_t (&obj_hdl->exp_refcnt);

	if (ref > 0)
		return true;    /* pin */
	return false; /* unpin */
}

/**
** Resolve forward declarations
*/
#include "client_mgr.h"
#include "export_mgr.h"
#include "fsal_up.h"

#endif				/* !FSAL_API */
/** @} */
