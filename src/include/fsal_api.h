/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#ifndef FSAL_API__
#define FSAL_API__

#include "fsal_pnfs.h"

/* FSAL API
 * object oriented fsal api.
 */

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
 * pointer to it gets saved in @c exportlist__ and it has a reference
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
 *	This vector is used to access methodsm e.g.:
 *
 * @code{.c}
 * exp_hdl->ops->lookup(exp_hdl, name, ...);
 * @endcode
 *
 * Note that exp_hdl is used to dereference the method and it is also
 * *always* the first argument to the method/function.  Think of it as
 * the 'this' argument.
 */

/**
 * @brief Major Version
 *
 * Increment this whenever any part of the existing API is changed,
 * e.g.  the argument list changed or a method is removed.
 */

#define FSAL_MAJOR_VERSION 1

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
struct exportlist__; /* We just need a pointer, not all of
		        nfs_exports.h full def in
		        include/nfs_exports.h */

/**
 * @brief FSAL object definition
 *
 * This structure is the base FSAL instance definition, providing the
 * public face to a single, loaded FSAL.
 */

struct fsal_module {
        struct glist_head fsals; /*< link in list of loaded fsals */
        pthread_mutex_t lock; /*< Lock to be held when
                                  incrementing/decrementing the
                                  reference count or manipulating the
                                  list of exports. */
        volatile int refs; /*< Reference count */
        struct glist_head exports; /*< Head of list of exports from
                                      this FSAL */
        char *name; /*< Name set from .so and/or config */
        char *path; /*< Path to .so file */
        void *dl_handle; /*< Handle to the dlopen()d shared
                             library. NULL if statically linked */
        struct fsal_ops *ops; /*< FSAL module methods vector */
        struct export_ops *exp_ops; /*< Shared export object methods vector */
        struct fsal_obj_ops *obj_ops;   /*< Shared handle methods vector */
};

/**
 * @brief FSAL module methods
 */

struct fsal_ops {
/*@{*/
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

/**
 * @brief Get the name of the FSAL
 *
 * This function looks up the name of the FSAL, as it would be used to
 * associate an export in the configuration file.  This function
 * should not be overridden.
 *
 * @param[in] fsal_hdl The FSAL to interrogate.
 *
 * @return A pointer to a statically allocated buffer containing the
 * name.  This buffer must not be freed or modified.  This pointer
 * must not be dereferenced after a call to @c put.
 */
        const char *(*get_name)(struct fsal_module *fsal_hdl);

/**
 * @brief Get the name of the library
 *
 * This function looks up the name of the shared object containing
 * code for the FSAL.  This function should not be overridden.
 *
 * @param[in] fsal_hdl The FSAL to interrogate.
 *
 * @return A pointer to a statically allocated buffer containing the
 * library name.  This buffer must not be freed or modified.  This
 * pointer must not be dereferenced after a call to @c put.
 */
        const char *(*get_lib_name)(struct fsal_module *fsal_hdl);
/**
 * @brief Relinquish a reference to the module
 *
 * This function relinquishes one reference to the FSAL.  After the
 * reference count falls to zero, the FSAL may be freed and unloaded.
 * This function should not be overridden.
 *
 * @param[in] fsal_hdl FSAL on which to release reference.
 *
 * @retval 0 on success.
 * @retval EINVAL if there are no references to put.
 */
        int (*put)(struct fsal_module *fsal_hdl);

/*@}*/


/*@{*/
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
        fsal_status_t (*init_config)(struct fsal_module *fsal_hdl,
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
        void (*dump_config)(struct fsal_module *fsal_hdl,
                            int log_fd);

/**
 * @brief Create a new export
 *
 * This function creates a new export in the FSAL using the supplied
 * path and options.  The function is expected to allocate its own
 * export (the full, private structure).  It must then initialize the
 * public portion like so:
 *
 * @code{.c}
 *         fsal_export_init(&private_export_handle->pub,
 *                          fsal_hdl->exp_ops,
 *                          exp_entry);
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
 * @param[in]     export_path Path to the root of the export
 * @param[in]     fs_options  String buffer of export options (unparsed)
 * @param[in,out] exp_entry   Entry in the Ganesha export list
 * @param[in]     next_fsal   Next FSAL in list, for stacking
 * @param[out]    export      Public export handle
 *
 * @return FSAL status.
 */
        fsal_status_t (*create_export)(struct fsal_module *fsal_hdl,
                                       const char *export_path,
                                       const char *fs_options,
                                       struct exportlist__ *exp_entry,
                                       struct fsal_module *next_fsal,
                                       /* upcall vector */
                                       struct fsal_export **export);
/*@}*/
};

/**
 * Global fsal manager functions
 * used by nfs_main to initialize fsal modules.
 */

int start_fsals(config_file_t config);
int load_fsal(const char *path,
              const char *name,
              struct fsal_module **fsal_hdl);
int init_fsals(config_file_t config);

/* Called only within MODULE_INIT and MODULE_FINI functions of a fsal
 * module
 */


/**
 * @brief Register a FSAL.
 *
 * This function registers an FSAL with ganesha and initializes the
 * public portion of the FSAL data structure, including providing
 * default operation vectors.
 *
 * @param[in,out] fsal_hdl      The FSAL module to register.
 * @param[in]     name          The FSAL's name
 * @param[in]     major_version Major version fo the API against which
 *                              the FSAL was written
 * @param[in]     minor_version Minor version of the API against which
 *                              the FSAL was written.
 *
 * @return 0 on success.
 * @return EINVAL on version mismatch.
 */

int register_fsal(struct fsal_module *fsal_hdl,
                  const char *name,
                  uint32_t major_version,
                  uint32_t minor_version);
/**
 * @brief Unregister an FSAL
 *
 * This function unregisters an FSAL from Ganesha.  It should be
 * called from the module finalizer as part of unloading.
 *
 * @param[in] fsal_hdl The FSAL to unregister
 *
 * @return 0 on success.
 * @return EBUSY if outstanding references or exports exist.
 */

int unregister_fsal(struct fsal_module *fsal_hdl);

/**
 * @brief Find and take a reference on an FSAL
 *
 * This function finds an FSAL by name and increments its reference
 * count.  It is used as part of export setup.  The @c put method
 * should be used  to release the reference before unloading.
 */
struct fsal_module *lookup_fsal(const char *name);

/**
 * @brief Export object
 *
 * This structure is created by the @c create_export method on the
 * FSAL module.  It is stored as part of the export list and is used
 * to manage individual exports, interrogate properties of the
 * filesystem, and create individual file handle objects.
 */

struct fsal_export {
        struct fsal_module *fsal; /*< Link back to the FSAL module */
        pthread_mutex_t lock; /*< A lock, to be held when
                                  taking/yielding references and
                                  manipulating the list of handles. */
        volatile int refs; /*< Reference count */
        struct glist_head handles; /*< Head of list of object handles */
        struct glist_head exports; /*< Link in list of exports from
                                       the same FSAL. */
        struct exportlist__ *exp_entry; /*< Pointer to the export
                                            list. */
        struct export_ops *ops; /*< Vector of operations */
};

/**
 * @brief Export operations
 */

struct export_ops {
/*@{*/

/**
* Export lifecycle management.
*/


/**
 * @brief Get a reference
 *
 * This function gets a reference on this export.  This function
 * should not be overridden.
 *
 * @param[in] exp_hdl The export to reference.
 */
        void (*get)(struct fsal_export *exp_hdl);

/**
 * @brief Relinquish a reference
 *
 * This function relinquishes a reference on the given export.  One
 * should make no attempt to access the export or even dereference
 * the handle after relinquishing the reference.  This function should
 * not be overridden.
 *
 * @param[in] exp_hdl The export handle to relinquish.
 *
 * @retval 0 on success.
 * @retval EINVAL if no reference exists.
 */
        int (*put)(struct fsal_export *exp_hdl);

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
        fsal_status_t (*release)(struct fsal_export *exp_hdl);
/*@}*/

/*@{*/
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
        fsal_status_t (*lookup_path)(struct fsal_export *exp_hdl,
                                     const char *path,
                                     struct fsal_obj_handle **handle);

/**
 * @brief Look up a junction
 *
 * This function returns a handle for the directory behind a junction
 * object.
 *
 * @deprecated The purpose of this function is unclear and it is not
 * called by the upper layers.  Junction support may be integrated in
 * the future, but the function as currently specified is not likely
 * to remain.
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
 * @brief Extract an opque handle
 *
 * This function extracts a "wire" handle from a buffer that may then
 * be passed to create_handle.
 *
 * @deprecated The signature of the function is likely to change.  We
 * currently use a netbuf to allow handle mapping for the Proxy FSAL.
 * This will likely be integrated more cleanly and this function may
 * disappear entirely.
 *
 * @note The extracted handle can obviously be used with
 * create_handle, but is it used as a key in the hash table?  If so we
 * run into a serious problem where we may be comparing long "wire"
 * handles against much shorter "key" handles and getting faulty
 * duplicates.
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
        fsal_status_t (*extract_handle)(struct fsal_export *exp_hdl,
                                        fsal_digesttype_t in_type,
                                        struct netbuf *fh_desc);
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
        fsal_status_t (*create_handle)(struct fsal_export *exp_hdl,
                                       struct gsh_buffdesc *hdl_desc,
                                       struct fsal_obj_handle **handle);
/*@}*/

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
 * @param[out] info    Buffer to fill with information
 *
 * @retval FSAL status.
 */
        fsal_status_t (*get_fs_dynamic_info)(struct fsal_export *exp_hdl,
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
 * @retval TRUE if the feature is supported.
 * @retval FALSE if the feature is unsupported or unknown.
 */
        bool_t (*fs_supports)(struct fsal_export *exp_hdl,
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
 * @brief Get the expiration type of filehandles
 *
 * @deprecated Ganesha does not properly support filehandle expiry,
 * and few if any clients are able to use them.  It is likely better
 * to remove this feature than to implement it properly.
 *
 * @param[in] exp_hdl Filesystem to interrogate
 *
 * @return Expire type.
 */
        fsal_fhexptype_t (*fs_fh_expire_type)(struct fsal_export *exp_hdl);

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
        gsh_time_t (*fs_lease_time)(struct fsal_export *exp_hdl);

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
 * struct attrlist, other NFS attributes (fileid and so forth) are
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
        uint32_t (*fs_xattr_access_rights)(struct fsal_export *exp_hdl);
/*@}*/

/*@{*/

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
 * @param[in] req_ctx    Request context, giving credentials
 *
 * @return FSAL types.
 */
        fsal_status_t (*check_quota)(struct fsal_export *exp_hdl,
                                     const char * filepath,
                                     int quota_type,
                                     struct req_op_context *req_ctx);


/**
 * @brief Get a user's quota
 *
 * This function retrieves a given user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[in]  req_ctx    Request context, giving credentials
 * @param[out] quota      The user's quota
 *
 * @return FSAL types.
 */
        fsal_status_t (*get_quota)(struct fsal_export *exp_hdl,
                                   const char * filepath,
                                   int quota_type,
                                   struct req_op_context *req_ctx,
                                   fsal_quota_t * quota);

/**
 * @brief Set a user's quota
 *
 * This function sets a user's quota.
 *
 * @param[in]  exp_hdl    The export to interrogate
 * @param[in]  filepath   The path within the export to check
 * @param[in]  quota_type Whether we are checking inodes or blocks
 * @param[in]  req_ctx    Request context, giving credentials
 * @param[in]  quota      The values to set for the quota
 * @param[out] resquota   New values set (optional)
 *
 * @return FSAL types.
 */
        fsal_status_t (*set_quota)(struct fsal_export *exp_hdl,
                                   const char * filepath,
                                   int quota_type,
                                   struct req_op_context *req_ctx,
                                   fsal_quota_t * quota,
                                   fsal_quota_t * resquota);
/*@}*/

/*@{*/
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
 * @param[in]  type         The tyep of layout the specified the
 *                          device
 *
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */
        nfsstat4 (*getdeviceinfo)(
                struct fsal_export *exp_hdl,
                XDR *da_addr_body,
                const layouttype4 type,
                const struct pnfs_deviceid *deviceid);

/**
* @brief Get list of available devices
*
* This function should populate calls @c cb @c values representing the
* low quad of deviceids it wishes to make the available to the
* caller.  it should continue calling @c cb until @c cb returns FALSE
* or it runs out of deviceids to make available.  If @c cb returns
* FALSE, it should assume that @c cb has not stored the most recent
* deviceid and set @c res->cookie to a value that will begin witht he
* most recently provided.
*
* If it wishes to return no deviceids, it may set @c res->eof to TRUE
* without calling @c cb at all.
*
* @param[in]     exp_hdl Export handle
* @param[in]     type    Type of layout to get devices for
* @param[in]     cb      Functioning taking device ID halves
* @param[in,out] res     In/outand output arguments of the function
*
* @return Valid error codes in RFC 5661, pp. 365-6.
*/
        nfsstat4 (*getdevicelist)(
                struct fsal_export *exp_hdl,
                layouttype4 type,
                void *opaque,
                bool_t (*cb)(void *opaque,
                             const uint64_t id),
                struct fsal_getdevicelist_res *res);


/**
 * @brief Get layout types supported by export
 *
 * @param[in]  exp_hdl Filesystem to interrogate
 * @param[out] count   Number of layout types in array
 * @param[out] types   Static array of layout types that must not be
 *                     freed or modified and must not be dereferenced
 *                     after export reference is relinquished
 */
        void (*fs_layouttypes)(struct fsal_export *exp_hdl,
                               size_t *count,
                               layouttype4 **types);

/**
 * @brief Get layout block size for export
 *
 * This is the preferred read/write block size.  Clients are requested
 * (but don't have to) read and write in multiples.
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
 * used to construct the response to any single layoutget call.
 *
 * @param[in]  exp_hdl Filesystem to interrogate
 *
 * @return The preferred layout block size.
 */
        uint32_t (*fs_maximum_segments)(struct fsal_export *exp_hdl);

/**
 * @brief Size of the buffer needed for a loc_body
 *
 * @param[in]  exp_hdl Filesystem to interrogate
 *
 * @return Size of the buffer needed for a loc_body
 */
        size_t (*fs_loc_body_size)(struct fsal_export *exp_hdl);

/**
 * @brief Size of the buffer needed for a ds_addr
 *
 * @param[in]  exp_hdl Filesystem to interrogate
 *
 * @return Size of the buffer needed for a ds_addr
 */
        size_t (*fs_da_addr_size)(struct fsal_export *exp_hdl);
/*@}*/
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
        pthread_mutex_t lock; /*< Lock on handle */
        struct glist_head handles; /*< Link in list of handles under
                                       an export */
        int refs;
        object_file_type_t type; /*< Object file type */
        struct fsal_export *export; /*< Link back to export */
        struct attrlist attributes; /*< Cached attributes */
        struct fsal_obj_ops *ops; /*< Operations vector */
};

/**
 * @brief Directory cookie
 *
 * This cookie gets allocated at cache_inode_dir_entry create time to
 * the size specified by size.  It is at the end of the dir entry so
 * the cookie[] allocation can expand as needed.  However, given how
 * GetFromPool works, these have to be fixed size as a result, we go
 * for a V4 handle size for things like proxy until we can fix
 * this. It is a crazy waste of space.  Make this go away with a
 * fixing of GetFromPool.  For now, make it big enough to hold a
 * SHA1...  Also note that the readdir code doesn't have to check this
 * (both are hard coded) so long as they obey the proper setting of
 * size.
 */

#define FSAL_READDIR_COOKIE_MAX 40
struct fsal_cookie {
        int size;
        unsigned char cookie[FSAL_READDIR_COOKIE_MAX];
};

/**
 * @brief FSAL objectoperations vector
 */

struct fsal_obj_ops {
/*@{*/

/**
 * Lifecycle management
 */

/**
 * @brief Get a reference on a handle
 *
 * This function increments the reference count on a handle.  It
 * should not be overridden.
 *
 * @param[in] obj_hdl The handle to reference
 */
        void (*get)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Release a reference on a handle
 *
 * This function releases a reference to a handle.  Once a caller's
 * reference is released they should make no attempt to access the
 * handle or even dereference a pointer to it.  This function should
 * not be overridden.
 *
 * @param[in] obj_hdl The handle to relinquish
 *
 * @retval 0 on success.
 * @retval EINVAL if no references were outstanding.
 */
        int (*put)(struct fsal_obj_handle *obj_hdl);

/**
 * @brief Clean up a filehandle
 *
 * This function cleans up private resources associated with a
 * filehandle and deallocates it.  Implement this method or you will
 * leak.
 *
 * @param[in] release Handle to release
 *
 * @return FSAL status.
 */
        fsal_status_t (*release)(struct fsal_obj_handle *obj_hdl);
/*@}*/

/*@{*/

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
        fsal_status_t (*lookup)(struct fsal_obj_handle *dir_hdl,
                                const char *path,
                                struct fsal_obj_handle **handle);

/**
 * @brief Read a directory
 *
 * This function reads directory entries from the FSAL and supplies
 * them to a callback.
 *
 * @param[in]  dir_hdl   Directory to read
 * @param[in]  entry_cnt Number of entries to return
 * @param[in]  whence    Point at which to start reading.  NULL to
 *                       start at beginning.
 * @param[in]  dir_state Opaque pointer to be passed to callback
 * @param[in]  cb        Callback to receive names
 * @param[out] eof       TRUE if the last entry was reached
 *
 * @note I think we would be better to follow the Cache inode practice
 * of changing the return value of the callback to be true or false
 * (to request more entries) rather than take a number.  Further,
 * since even in readdir(3), @c d_type is specified as not being
 * meaningful on all filesystems and our current callback just does a
 * lookup without inspecting @c d_type, it's probably worth removing.
 * We might also want to remove @c dir_hdl from the callback, since
 * the caller can stash it in @c dir_state, to save stack. -- ACE
 *
 * @return FSAL status.
 */
        fsal_status_t (*readdir)(struct fsal_obj_handle *dir_hdl,
                                 uint32_t entry_cnt,
                                 struct fsal_cookie *whence,
                                 void *dir_state,
                                 fsal_status_t (*cb)(
                                         const char *name,
                                         unsigned int dtype,
                                         struct fsal_obj_handle *dir_hdl,
                                         void *dir_state,
                                         struct fsal_cookie *cookie),
                                 bool_t *eof);
/*@}*/

/*@{*/

/**
 * Creation operations
 */

/**
 * @brief Create a regular file
 *
 * This function creates a new regular file.
 *
 * @param[in]  dir_hdl Directory in which to create the file
 * @param[in]  name    Name of file to create
 * @param[out] attrib  Attributes of newly created file
 * @param[out] new_obj Newly created object
 *
 * @deprecated The @c attrib argument is deprecated and will be
 * removed.  Attributes are contained within the returned object and
 * there is no need for a stack copy.
 *
 * @return FSAL status.
 */
        fsal_status_t (*create)(struct fsal_obj_handle *dir_hdl,
                                const char *name,
                                struct attrlist *attrib,
                                struct fsal_obj_handle **new_obj);


/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * @param[in]  dir_hdl Directory in which to create the directory
 * @param[in]  name    Name of directory to create
 * @param[out] attrib  Attributes of newly created directory
 * @param[out] new_obj Newly created object
 *
 * @deprecated The @c attrib argument is deprecated and will be
 * removed.  Attributes are contained within the returned object and
 * there is no need for a stack copy.
 *
 * @return FSAL status.
 */
        fsal_status_t (*mkdir)(struct fsal_obj_handle *dir_hdl,
                               const char *name,
                               struct attrlist *attrib,
                               struct fsal_obj_handle **new_obj);

/**
 * @brief Create a special file
 *
 * This function creates a new special file.
 *
 * @param[in]  dir_hdl  Directory in which to create the object
 * @param[in]  name     Name of object to create
 * @param[in]  nodetype Type of special file to create
 * @param[in]  dev      Major and minor device numbers for block or
 *                      character special
 * @param[out] attrib   Attributes of newly created object
 * @param[out] new_obj  Newly created object
 *
 * @deprecated The @c attrib argument is deprecated and will be
 * removed.  Attributes are contained within the returned object and
 * there is no need for a stack copy.
 *
 * @return FSAL status.
 */
        fsal_status_t (*mknode)(struct fsal_obj_handle *dir_hdl,
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
 * @param[in]  dir_hdl   Directory in which to create the object
 * @param[in]  name      Name of object to create
 * @param[in]  link_path Content of symbolic link
 * @param[out] attrib    Attributes of newly created object
 * @param[out] new_obj   Newly created object
 *
 * @deprecated The @c attrib argument is deprecated and will be
 * removed.  Attributes are contained within the returned object and
 * there is no need for a stack copy.
 *
 * @return FSAL status.
 */
        fsal_status_t (*symlink)(struct fsal_obj_handle *dir_hdl,
                                 const char *name,
                                 const char *link_path,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **new_obj);
/*@}*/

/*@{*/

/**
 * File object operations
 */

/**
 * @brief Read the content of a link
 *
 * This function reads the content of a symbolic link.
 *
 * @param[in]     obj_hdl      Link to read
 * @param[out]    link_content Buffer to which the contents are copied
 * @param[in,out] link_len     Total buffer size/Size of content
 *                             copied
 * @param[out]    refresh      TRUE if the content are to be retrieved
 *                             from the underlying filesystem rather
 *                             than cache
 *
 * @deprecated The argument structure of this method will change to
 * take a callback function that can copy the link content directly
 * into some response, rather than the content having to be copied
 * twice.
 *
 * @return FSAL status.
 */
        fsal_status_t (*readlink)(struct fsal_obj_handle *obj_hdl,
                                  char *link_content,
                                  size_t *link_len,
                                  bool_t refresh);

/**
 * @brief Check access for a given user against a given object
 *
 * This function checks whether a given user is allowed to perform the
 * specified operations against the supplied file.  The goal is to
 * allow filesystem specific semantics to be applied to cached
 * metadata.
 *
 * @param[in] obj_hdl     Handle to check
 * @param[in] req_ctx     Request context, includes credentials
 * @param[in] access_type Access requested
 *
 * @return FSAL status.
 */
        fsal_status_t (*test_access)(struct fsal_obj_handle *obj_hdl,
                                     struct req_op_context *req_ctx,
                                     fsal_accessflags_t access_type);

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
        fsal_status_t (*getattrs)(struct fsal_obj_handle *obj_hdl);

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
        fsal_status_t (*setattrs)(struct fsal_obj_handle *obj_hdl,
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
        fsal_status_t (*rename)(struct fsal_obj_handle *olddir_hdl,
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
        fsal_status_t (*unlink)(struct fsal_obj_handle *obj_hdl,
                                const char *name);

/**
 * @brief Truncate a file
 *
 * This function truncates a regular file to the given length (which
 * must be less than or equal to the current length.)
 *
 * @param[in] obj_hdl File to truncate
 * @param[in] length  New length
 *
 * @return FSAL status
 */
        fsal_status_t (*truncate)(struct fsal_obj_handle *obj_hdl,
                                  uint64_t length);
/*@}*/

/*@{*/
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
        fsal_status_t (*open)(struct fsal_obj_handle *obj_hdl,
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
        fsal_openflags_t (*status)(struct fsal_obj_handle *obj_hdl);

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
 * @param[out] end_of_file TRUE if the end of file has been reached
 *
 * @return FSAL status.
 */
        fsal_status_t (*read)(struct fsal_obj_handle *obj_hdl,
                              uint64_t offset,
                              size_t buffer_size,
                              void *buffer,
                              size_t *read_amount,
                              bool_t *end_of_file); /* needed? */

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
 * @param[out] wrote_amount Number of bytes written
 *
 * @return FSAL status.
 */
        fsal_status_t (*write)(struct fsal_obj_handle *obj_hdl,
                               uint64_t offset,
                               size_t buffer_size,
                               void *buffer,
                               size_t *wrote_amount);
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
        fsal_status_t (*commit)(struct fsal_obj_handle *obj_hdl, /* sync */
                                off_t offset,
                                size_t len);

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
        fsal_status_t (*lock_op)(struct fsal_obj_handle *obj_hdl,
                                 void * owner,
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
        fsal_status_t (*share_op)(struct fsal_obj_handle *obj_hdl,
                                  void *owner,
                                  fsal_share_param_t  request_share);
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
        fsal_status_t (*close)(struct fsal_obj_handle *obj_hdl);
/*@}*/

/*@{*/

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
                                        fsal_xattrent_t * xattrs_tab,
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
 * @param[in]  obj_hdl     File to interrogate
 * @param[in]  xattr_name  Name of attribute
 * @param[out] buffer_addr Buffer to store content
 * @param[in]  buffer_size Buffer size
 * @param[out] output_size Size of content
 *
 * @return FSAL status.
 */
        fsal_status_t (*getextattr_value_by_name)(struct fsal_obj_handle *obj_hdl,
                                                  const char *xattr_name,
                                                  caddr_t buffer_addr,
                                                  size_t buffer_size,
                                                  size_t * output_size);

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
        fsal_status_t (*getextattr_value_by_id)(struct fsal_obj_handle *obj_hdl,
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
 * @param[in] create      TRUE if attribute is to be created
 *
 * @return FSAL status.
 */
        fsal_status_t (*setextattr_value)(struct fsal_obj_handle *obj_hdl,
                                          const char *xattr_name,
                                          caddr_t buffer_addr,
                                          size_t buffer_size,
                                          int create);


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
        fsal_status_t (*setextattr_value_by_id)(struct fsal_obj_handle *obj_hdl,
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
        fsal_status_t (*getextattr_attrs)(struct fsal_obj_handle *obj_hdl,
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
        fsal_status_t (*remove_extattr_by_name)(struct fsal_obj_handle *obj_hdl,
                                                const char *xattr_name);
/*@}*/

/**
 * Handle operations
 */

/**
 * @brief Test handle type
 *
 * This function tests that a handle is of the specified type.
 *
 * @retval TRUE if it is.
 * @retval FALSE if it isn't.
 */
        bool_t (*handle_is)(struct fsal_obj_handle *obj_hdl,
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
        fsal_status_t (*lru_cleanup)(struct fsal_obj_handle *obj_hdl,
                                     lru_actions_t requests);

/**
 * @brief Compare two file handles
 *
 * This function compares two file handles to see if they represent
 * the same file.
 *
 * @param[in] obj1_hdl A handle
 * @param[in] obj2_hdl Another handle
 *
 * @retval TRUE if they are the same file.
 * @retval FALSE if they aren't.
 */
        bool_t (*compare)(struct fsal_obj_handle *obj1_hdl,
                          struct fsal_obj_handle *obj2_hdl);

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
        fsal_status_t (*handle_digest)(struct fsal_obj_handle *obj_hdl,
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
/*@}*/

/*@{*/

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
 *
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */
        nfsstat4 (*layoutget)(
                struct fsal_obj_handle *obj_hdl,
                struct req_op_context *req_ctx,
                XDR *loc_body,
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
        nfsstat4 (*layoutreturn)(
                struct fsal_obj_handle *obj_hdl,
                struct req_op_context *req_ctx,
                XDR *lrf_body,
                const struct fsal_layoutreturn_arg *arg);

/**
 * \brief Commit a segment of a layout
 *
 * This function is called once on every segment of a layout.  The
 * FSAL may avoid being called again after it has finished all tasks
 * necessary for the commit by setting res->commit_done to TRUE.
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
        nfsstat4 (*layoutcommit)(
                struct fsal_obj_handle *obj_hdl,
                struct req_op_context *req_ctx,
                XDR *lou_body,
                const struct fsal_layoutcommit_arg *arg,
                struct fsal_layoutcommit_res *res);
/*@}*/
};

#endif /* !FSAL_API__ */
