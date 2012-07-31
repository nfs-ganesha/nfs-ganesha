/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * -------------
 */

/**
 * @file   export.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Thu Jul  5 16:37:47 2012
 *
 * @brief Implementation of FSAL export functions for Ceph
 *
 * This file implements the Ceph specific functionality for the FSAL
 * export handle.
 */

#include <limits.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include <cephfs/libcephfs.h>
#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "internal.h"
#include "fsal_pnfs_files.h"

/**
 * @brief Clean up an export
 *
 * This function cleans up an export after the last reference is
 * released.
 *
 * @param[in,out] export The export to be released
 *
 * @retval ERR_FSAL_NO_ERROR on success.
 * @retval ERR_FSAL_BUSY if the export is in use.
 */

static fsal_status_t
release(struct fsal_export *export_pub)
{
        /* The priate, expanded export */
        struct export *export
                = container_of(export_pub, struct export, export);
        /* Return code */
        fsal_status_t status = {ERR_FSAL_INVAL, 0};

        pthread_mutex_lock(&export->export.lock);
        if((export->export.refs > 0) ||
           (!glist_empty(&export->export.handles))) {
                pthread_mutex_lock(&export->export.lock);
                status.major = ERR_FSAL_INVAL;
                return status;
        }
        fsal_detach_export(export->export.fsal, &export->export.exports);
        pthread_mutex_unlock(&export->export.lock);

        export->export.ops = NULL;
        ceph_shutdown(export->cmount);
        export->cmount = NULL;
        pthread_mutex_destroy(&export->export.lock);
        gsh_free(export);
        export = NULL;

        return status;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object
 * handle.  Because the root path specified for the export is a Ceph
 * style root as supplied to mount -t ceph of ceph-fuse (of the form
 * host:/path), we check to see if the path begins with / and, if not,
 * skip until we find one.
 *
 * @param[in]  export_pub The export in which to look up the file
 * @param[in]  path       The path to look up
 * @param[out] pub_handle The created public FSAL handle
 *
 * @return FSAL status.
 */

static fsal_status_t
lookup_path(struct fsal_export *export_pub,
            const char *path,
            struct fsal_obj_handle **pub_handle)
{
        /* The 'private' full export handle */
        struct export *export = container_of(export_pub,
                                             struct export,
                                             export);
        /* The 'private' full object handle */
        struct handle *handle = NULL;
        /* The buffer in which to store stat info */
        struct stat st;
        /* FSAL status structure */
        fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
        /* Return code from Ceph */
        int rc = 0;
        /* Find the actual path in the supplied path */
        const char *realpath;

        if (*path != '/') {
                realpath = strchr(path, ':');
                if (realpath == NULL) {
                        status.major = ERR_FSAL_INVAL;
                        goto out;
                }
                if (*(++realpath) != '/') {
                        status.major = ERR_FSAL_INVAL;
                        goto out;
                }
        } else {
                realpath = path;
        }

        *pub_handle = NULL;

        if (strcmp(realpath, "/") == 0) {
                vinodeno_t root;
                root.ino.val = CEPH_INO_ROOT;
                root.snapid.val = CEPH_NOSNAP;
                rc = ceph_ll_getattr(export->cmount, root, &st, 0, 0);
        } else {
                rc = ceph_ll_walk(export->cmount, realpath, &st);
        }
        if (rc < 0) {
                status = ceph2fsal_error(rc);
                goto out;
        }

        rc = construct_handle(&st, export, &handle);
        if (rc < 0) {
                status = ceph2fsal_error(rc);
                goto out;
        }

        *pub_handle = &handle->handle;

out:
        return status;
}

/**
 * @brief Extract handle from buffer
 *
 * This function, in the Ceph FSAL, merely checks that the supplied
 * buffer is the appropriate size, or returns the size of the wire
 * handle if FSAL_DIGEST_SIZEOF is passed as the type.
 *
 * @param[in]     export_pub Public export
 * @param[in]     type       The type of digest this buffer represents
 * @param[in,out] fh_desc    The buffer from which to extract/buffer
 *                           containing extracted handle
 *
 * @return FSAL status.
 */

static fsal_status_t
extract_handle(struct fsal_export *export_pub,
               fsal_digesttype_t type,
               struct netbuf *fh_desc)
{
        if (type == FSAL_DIGEST_SIZEOF) {
                fh_desc->len = sizeof(struct wire_handle);
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        } else if (fh_desc->len != sizeof(struct wire_handle)) {
                return fsalstat(ERR_FSAL_SERVERFAULT, 0);
        } else {
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }
}

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  export_pub The export in which to create the handle
 * @param[in]  desc       Buffer from which to create the file
 * @param[out] ds_pub     FSAL data server handle
 *
 * @return NFSv4.1 error codes.
 */
nfsstat4
create_ds_handle(struct fsal_export *const export_pub,
                 const struct gsh_buffdesc *const desc,
                 struct fsal_ds_handle **const ds_pub)
{
        /* Full 'private' export structure */
        struct export *export = container_of(export_pub,
                                             struct export,
                                             export);
        /* Handle to be created */
        struct ds *ds = NULL;

        *ds_pub= NULL;

        if (desc->len != sizeof(struct ds_wire)) {
                return NFS4ERR_BADHANDLE;
        }

        ds = gsh_calloc(1, sizeof(struct ds));

        if (ds == NULL) {
                return NFS4ERR_SERVERFAULT;
        }

        /* Connect lazily when a FILE_SYNC4 write forces us to, not
           here. */

        ds->connected = FALSE;


        memcpy(&ds->wire, desc->addr, desc->len);

        if (ds->wire.layout.fl_stripe_unit == 0) {
                gsh_free(ds);
                return NFS4ERR_BADHANDLE;
        }

        if (fsal_ds_handle_init(&ds->ds,
                                export->export.fsal->ds_ops,
                                &export->export)) {
                gsh_free(ds);
                return NFS4ERR_SERVERFAULT;
        }

        *ds_pub = &ds->ds;

        return NFS4_OK;
}

/**
 * @brief Create a handle object from a wire handle
 *
 * The wire handle is given in a buffer outlined by desc, which it
 * looks like we shouldn't modify.
 *
 * @param[in]  export_pub Public export
 * @param[in]  desc       Handle buffer descriptor
 * @param[out] pub_handle The created handle
 *
 * @return FSAL status.
 */
static fsal_status_t
create_handle(struct fsal_export *export_pub,
              struct gsh_buffdesc *desc,
              struct fsal_obj_handle **pub_handle)
{
        /* Full 'private' export structure */
        struct export *export = container_of(export_pub,
                                             struct export,
                                             export);
        /* FSAL status to return */
        fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
        /* The FSAL specific portion of the handle received by the
           client */
        struct wire_handle *wire = desc->addr;
        /* Ceph return code */
        int rc = 0;
        /* Stat buffer */
        struct stat st;
        /* Handle to be created */
        struct handle *handle = NULL;

        *pub_handle = NULL;

        if (desc->len != sizeof(struct wire_handle)) {
                status.major = ERR_FSAL_INVAL;
                goto out;
        }

        rc = ceph_ll_connectable_m(export->cmount, &wire->vi,
                                   wire->parent_ino,
                                   wire->parent_hash);
        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        /* The ceph_ll_connectable_m should have populated libceph's
           cache with all this anyway */
        rc = ceph_ll_getattr(export->cmount, wire->vi, &st, 0, 0);
        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        rc = construct_handle(&st, export, &handle);
        if (rc < 0) {
                status = ceph2fsal_error(rc);
                goto out;
        }

        *pub_handle = &handle->handle;

out:
        return status;
}

/**
 * @brief Get dynamic filesystem info
 *
 * This function returns dynamic filesystem information for the given
 * export.
 *
 * @param[in]  export_pub The public export handle
 * @param[out] info       The dynamic FS information
 *
 * @return FSAL status.
 */

static fsal_status_t
get_fs_dynamic_info(struct fsal_export *export_pub,
                    fsal_dynamicfsinfo_t *info)
{
        /* Full 'private' export */
        struct export* export
                = container_of(export_pub, struct export, export);
        /* Return value from Ceph calls */
        int rc = 0;
        /* Filesystem stat */
        struct statvfs vfs_st;
        /* The root of whatever filesystem this is */
        vinodeno_t root;

        root.ino.val = CEPH_INO_ROOT;
        root.snapid.val = CEPH_NOSNAP;

        rc = ceph_ll_statfs(export->cmount, root, &vfs_st);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
        info->total_bytes = vfs_st.f_frsize * vfs_st.f_blocks;
        info->free_bytes = vfs_st.f_frsize * vfs_st.f_bfree;
        info->avail_bytes = vfs_st.f_frsize * vfs_st.f_bavail;
        info->total_files = vfs_st.f_files;
        info->free_files = vfs_st.f_ffree;
        info->avail_files = vfs_st.f_favail;
        info->time_delta.seconds = 1;
        info->time_delta.nseconds = 0;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Query the FSAL's capabilities
 *
 * This function queries the capabilities of an FSAL export.
 *
 * @param[in] export_pub The public export handle
 * @param[in] option     The option to check
 *
 * @retval TRUE if the option is supported.
 * @retval FALSE if the option is unsupported (or unknown).
 */

static bool_t
fs_supports(struct fsal_export *export_pub,
            fsal_fsinfo_options_t option)
{
        switch (option) {
        case no_trunc:
                return TRUE;

        case chown_restricted:
                return TRUE;

        case case_insensitive:
                return FALSE;


        case case_preserving:
                return TRUE;

        case link_support:
                return TRUE;

        case symlink_support:
                return TRUE;

        case lock_support:
                return FALSE;

        case lock_support_owner:
                return FALSE;

        case lock_support_async_block:
                return FALSE;

        case named_attr:
                return FALSE;

        case unique_handles:
                return TRUE;

        case cansettime:
                return TRUE;

        case homogenous:
                return TRUE;

        case auth_exportpath_xdev:
                return FALSE;

        case dirs_have_sticky_bit:
                return TRUE;

        case accesscheck_support:
                return FALSE;

        case share_support:
                return FALSE;

        case share_support_owner:
                return FALSE;

        case pnfs_mds_supported:
                return TRUE;

        case pnfs_ds_supported:
                return FALSE;
        }

        return FALSE;
}

/**
 * @brief Return the longest file supported
 *
 * This function returns the length of the longest file supported.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT64_MAX.
 */

static uint64_t
fs_maxfilesize(struct fsal_export *export_pub)
{
        return UINT64_MAX;
}

/**
 * @brief Return the longest read supported
 *
 * This function returns the length of the longest read supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t
fs_maxread(struct fsal_export *export_pub)
{
        return 0x400000;
}

/**
 * @brief Return the longest write supported
 *
 * This function returns the length of the longest write supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t
fs_maxwrite(struct fsal_export *export_pub)
{
        return 0x400000;
}

/**
 * @brief Return the maximum number of hard links to a file
 *
 * This function returns the maximum number of hard links supported to
 * any file.
 *
 * @param[in] export_pub The public export
 *
 * @return 1024.
 */

static uint32_t
fs_maxlink(struct fsal_export *export_pub)
{
        /* Ceph does not like hard links.  See the anchor table
           design.  We should fix this, but have to do it in the Ceph
           core. */
        return 1024;
}

/**
 * @brief Return the maximum size of a Ceph filename
 *
 * This function returns the maximum filename length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t
fs_maxnamelen(struct fsal_export *export_pub)
{
        /* Ceph actually supports filenames of unlimited length, at
           least according to the protocol docs.  We may wish to
           constrain this later. */
        return UINT32_MAX;
}

/**
 * @brief Return the maximum length of a Ceph path
 *
 * This function returns the maximum path length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t
fs_maxpathlen(struct fsal_export *export_pub)
{
        /* Similarly unlimited int he protocol */
        return UINT32_MAX;
}

/**
 * @brief Return the FH expiry type
 *
 * This function returns the FH expiry type.
 *
 * @param[in] export_pub The public export
 *
 * @return FSAL_EXPTYPE_PERSISTENT
 */

static fsal_fhexptype_t
fs_fh_expire_type(struct fsal_export *export_pub)
{
        return FSAL_EXPTYPE_PERSISTENT;
}

/**
 * @brief Return the lease time
 *
 * This function returns the lease time.
 *
 * @param[in] export_pub The public export
 *
 * @return five minutes.
 */

static gsh_time_t
fs_lease_time(struct fsal_export *export_pub)
{
        gsh_time_t lease = {300, 0};

        return lease;
}

/**
 * @brief Return ACL support
 *
 * This function returns the export's ACL support.
 *
 * @param[in] export_pub The public export
 *
 * @return FSAL_ACLSUPPORT_DENY.
 */

static fsal_aclsupp_t
fs_acl_support(struct fsal_export *export_pub)
{
        return FSAL_ACLSUPPORT_DENY;
}

/**
 * @brief Return the attributes supported by this FSAL
 *
 * This function returns the mask of attributes this FSAL can support.
 *
 * @param[in] export_pub The public export
 *
 * @return supported_attributes as defined in internal.c.
 */

static attrmask_t
fs_supported_attrs(struct fsal_export *export_pub)
{
        return supported_attributes;
}

/**
 * @brief Return the mode under which the FSAL will create files
 *
 * This function returns the default mode on any new file created.
 *
 * @param[in] export_pub The public export
 *
 * @return 0600.
 */

static uint32_t
fs_umask(struct fsal_export *export_pub)
{
        return 0600;
}

/**
 * @brief Return the mode for extended attributes
 *
 * This function returns the access mode applied to extended
 * attributes.  This seems a bit dubious
 *
 * @param[in] export_pub The public export
 *
 * @return 0644.
 */

static uint32_t
fs_xattr_access_rights(struct fsal_export *export_pub)
{
        return 0644;
}


/**
 * @brief Describe a Ceph striping pattern
 *
 * At present, we support a files based layout only.  The CRUSH
 * striping pattern is a-periodic
 *
 * @param[in]  export_pub   Public export handle
 * @param[out] da_addr_body Stream we write the result to
 * @param[in]  type         Type of layout that gave the device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */

nfsstat4
static getdeviceinfo(struct fsal_export *export_pub,
                     XDR *da_addr_body,
                     const layouttype4 type,
                     const struct pnfs_deviceid *deviceid)
{
        /* Full 'private' export */
        struct export* export
                = container_of(export_pub, struct export, export);
        /* The number of Ceph OSDs in the cluster */
        unsigned num_osds = ceph_ll_num_osds(export->cmount);
        /* Minimal information needed to get layout info */
        vinodeno_t vinode;
        /* Structure containing the storage parameters of the file within
           the Ceph cluster. */
        struct ceph_file_layout file_layout;
        /* Currently, all layouts have the same number of stripes */
        uint32_t stripes = BIGGEST_PATTERN;
        /* Index for iterating over stripes */
        size_t stripe  = 0;
        /* Index for iterating over OSDs */
        size_t osd = 0;
        /* NFSv4 status code */
        nfsstat4 nfs_status = 0;

        vinode.ino.val = deviceid->devid;
        vinode.snapid.val = CEPH_NOSNAP;

        /* Sanity check on type */
        if (type != LAYOUT4_NFSV4_1_FILES) {
                LogCrit(COMPONENT_PNFS,
                        "Unsupported layout type: %x",
                        type);
                return NFS4ERR_UNKNOWN_LAYOUTTYPE;
        }

        /* Retrieve and calculate storage parameters of layout */

        memset(&file_layout, 0, sizeof(struct ceph_file_layout));
        ceph_ll_file_layout(export->cmount, vinode, &file_layout);

        /* As this is large, we encode as we go rather than building a
           structure and encoding it all at once. */


        /* The first entry in the nfsv4_1_file_ds_addr4 is the array
           of stripe indices. First we encode the count of stripes.
           Since our pattern doesn't repeat, we have as many indices
           as we do stripes. */

        if (!xdr_uint32_t(da_addr_body, &stripes)) {
                LogCrit(COMPONENT_PNFS, "Failed to encode length of "
                        "stripe_indices array: %" PRIu32 ".", stripes);
                return NFS4ERR_SERVERFAULT;
        }

        for (stripe = 0; stripe < stripes; stripe++) {
                uint32_t stripe_osd
                        = stripe_osd
                        = ceph_ll_get_stripe_osd(export->cmount,
                                                 vinode,
                                                 stripe,
                                                 &file_layout);
                if (stripe_osd < 0) {
                        LogCrit(COMPONENT_PNFS, "Failed to retrieve OSD for "
                                "stripe %lu of file %" PRIu64 ".  Error: %u",
                                stripe, deviceid->devid, -stripe_osd);
                        return NFS4ERR_SERVERFAULT;
                }
                if (!xdr_uint32_t(da_addr_body, &stripe_osd)) {
                        LogCrit(COMPONENT_PNFS,
                                "Failed to encode OSD for stripe %lu.",
                                stripe);
                        return NFS4ERR_SERVERFAULT;
                }
        }

        /* The number of OSDs in our cluster is the length of our
           array of multipath_lists */

        if (!xdr_uint32_t(da_addr_body, &num_osds)) {
                LogCrit(COMPONENT_PNFS, "Failed to encode length of "
                        "multipath_ds_list array: %u", num_osds);
                return NFS4ERR_SERVERFAULT;
        }

        /* Since our index is the OSD number itself, we have only one
           host per multipath_list. */

        for(osd = 0; osd < num_osds; osd++) {
                fsal_multipath_member_t host;
                memset(&host, 0, sizeof(fsal_multipath_member_t));
                host.proto = 6;
                if (ceph_ll_osdaddr(export->cmount, osd, &host.addr) < 0) {
                        LogCrit(COMPONENT_PNFS,
                                "Unable to get IP address for OSD %lu.",
                                osd);
                        return NFS4ERR_SERVERFAULT;
                }
                host.port = 2049;
                if ((nfs_status
                     = FSAL_encode_v4_multipath(da_addr_body,
                                                1,
                                                &host))
                    != NFS4_OK) {
                        return nfs_status;
                }
        }

        return NFS4_OK;
}

/**
 * @brief Get list of available devices
 *
 * We do not support listing devices and just set EOF without doing
 * anything.
 *
 * @param[in]     export_pub Export handle
 * @param[in]     type      Type of layout to get devices for
 * @param[in]     cb        Function taking device ID halves
 * @param[in,out] res       In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 365-6.
 */
static nfsstat4
getdevicelist(struct fsal_export *export_pub,
              layouttype4 type,
              void *opaque,
              bool_t (*cb)(void *opaque,
                           const uint64_t id),
              struct fsal_getdevicelist_res *res)
{
        res->eof = TRUE;
        return NFS4_OK;
}


/**
 * @brief Get layout types supported by export
 *
 * We just return a pointer to the single type and set the count to 1.
 *
 * @param[in]  export_pub Public export handle
 * @param[out] count      Number of layout types in array
 * @param[out] types      Static array of layout types that must not be
 *                        freed or modified and must not be dereferenced
 *                        after export reference is relinquished
 */

static void
fs_layouttypes(struct fsal_export *export_pub,
               size_t *count,
               const layouttype4 **types)
{
        static const layouttype4 supported_layout_type
                = LAYOUT4_NFSV4_1_FILES;
        *types = &supported_layout_type;
        *count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just return the Ceph default.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 4 MB.
 */
static uint32_t
fs_layout_blocksize(struct fsal_export *export_pub)
{
        return 0x400000;
}

/**
 * @brief Maximum number of segments we will use
 *
 * Since current clients only support 1, that's what we'll use.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 1
 */
static uint32_t
fs_maximum_segments(struct fsal_export *export_pub)
{
        return 1;
}

/**
 * @brief Size of the buffer needed for a loc_body
 *
 * Just a handle plus a bit.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a loc_body
 */
static size_t
fs_loc_body_size(struct fsal_export *export_pub)
{
        return 0x100;
}

/**
 * @brief Size of the buffer needed for a ds_addr
 *
 * This one is huge, due to the striping pattern.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a ds_addr
 */
size_t fs_da_addr_size(struct fsal_export *export_pub)
{
        return 0x1400;
}

/**
 * @brief Set operations for exports
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */

void
export_ops_init(struct export_ops *ops)
{
        ops->release = release;
        ops->lookup_path = lookup_path;
        ops->extract_handle = extract_handle;
        ops->create_handle = create_handle;
        ops->create_ds_handle = create_ds_handle;
        ops->get_fs_dynamic_info = get_fs_dynamic_info;
        ops->fs_supports = fs_supports;
        ops->fs_maxfilesize = fs_maxfilesize;
        ops->fs_maxread = fs_maxread;
        ops->fs_maxwrite = fs_maxwrite;
        ops->fs_maxlink = fs_maxlink;
        ops->fs_maxnamelen = fs_maxnamelen;
        ops->fs_maxpathlen = fs_maxpathlen;
        ops->fs_fh_expire_type = fs_fh_expire_type;
        ops->fs_lease_time = fs_lease_time;
        ops->fs_acl_support = fs_acl_support;
        ops->fs_supported_attrs = fs_supported_attrs;
        ops->fs_umask = fs_umask;
        ops->fs_xattr_access_rights = fs_xattr_access_rights;
        ops->getdeviceinfo = getdeviceinfo;
        ops->getdevicelist = getdevicelist;
        ops->fs_layouttypes = fs_layouttypes;
        ops->fs_layout_blocksize = fs_layout_blocksize;
        ops->fs_maximum_segments = fs_maximum_segments;
        ops->fs_loc_body_size = fs_loc_body_size;
        ops->fs_da_addr_size = fs_da_addr_size;
}

