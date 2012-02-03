/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box Corporation
 * All Rights Reserved
 * Contributor: Adam C. Emerson
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    fsal_mds.c
 * \brief   MDS realisation for the filesystem abstraction
 *
 * fsal_mds.c: MDS realisation for the filesystem abstraction
 *             Obviously, all of these functions should dispatch
 *             on type if more than one layout type is supported.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "nfsv41.h"
#include <cephfs/libcephfs.h>
#include <fcntl.h>
#include "HashTable.h"
#include <pthread.h>
#include <stdint.h>
#include "stuff_alloc.h"
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "pnfs_common.h"
#include "fsal_pnfs_files.h"

const size_t BIGGEST_PATTERN = 1024; /* Linux supports a stripe
                                        pattern with no more than 4096
                                        stripes, but for now we stick
                                        to 1024 to keep them da_addrs
                                        from being too gigantic. */



nfsstat4
CEPHFSAL_layoutget(fsal_handle_t *exthandle,
                   fsal_op_context_t *extcontext,
                   XDR *loc_body,
                   const struct fsal_layoutget_arg *arg,
                   struct fsal_layoutget_res *res)
{
     /* The FSAL handle as defined for the CEPH FSAL */
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
     /* The FSAL operation context as defined for the CEPH FSAL */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* The mount passed to all*/
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* Structure containing the storage parameters of the file within
        the Ceph cluster. */
     struct ceph_file_layout file_layout;
     /* Width of each stripe on the file */
     uint32_t stripe_width = 0;
     /* Utility parameter */
     nfl_util4 util = 0;
     /* The last byte that can be accessed through pNFS */
     uint64_t last_possible_byte = 0;
     /* The deviceid for this layout */
     struct pnfs_deviceid deviceid = {0, 0};
     /* Data server handle */
     cephfsal_handle_t ds_handle;
     /* NFS Status */
     nfsstat4 nfs_status = 0;

     /* We support only LAYOUT4_NFSV4_1_FILES layouts */

     if (arg->type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     /* Get basic information on the file and calculate the dimensions
        of the layout we can support. */

     memset(&file_layout, 0, sizeof(struct ceph_file_layout));

     ceph_ll_file_layout(cmount, VINODE(handle), &file_layout);
     stripe_width = file_layout.fl_stripe_unit;
     last_possible_byte = (BIGGEST_PATTERN * stripe_width) - 1;

     /* Since the Linux kernel refuses to work with any layout that
        doesn't cover the whole file, if a whole file layout is
        requested, lie.

        Otherwise, make sure the required layout doesn't go beyond
        what can be accessed through pNFS. */
     if (!((res->segment.offset == 0) &&
           (res->segment.length == NFS4_UINT64_MAX))) {
          struct pnfs_segment smallest_acceptable = {
               .io_mode = res->segment.io_mode,
               .offset = res->segment.offset,
               .length = arg->minlength
          };
          struct pnfs_segment forbidden_area = {
               .io_mode = res->segment.io_mode,
               .offset = last_possible_byte + 1,
               .length = NFS4_UINT64_MAX
          };
          if (pnfs_segments_overlap(smallest_acceptable,
                                    forbidden_area)) {
               LogCrit(COMPONENT_PNFS,
                       "Required layout extends beyond allowed region."
                       "offset: %"PRIu64", minlength: %" PRIu64".",
                       res->segment.offset,
                       arg->minlength);
               return NFS4ERR_BADLAYOUT;
          }
          res->segment.offset = 0;
          res->segment.length = stripe_width * BIGGEST_PATTERN;
          res->segment.io_mode = LAYOUTIOMODE4_RW;
     }

     /* For now, just make the low quad of the deviceid be the inode
        number.  With the span of the layouts constrained above, this
        lets us generate the device address on the fly from the
        deviceid rather than storing it. */

     deviceid.export_id = arg->export_id;
     deviceid.devid = VINODE(handle).ino.val;

     /* We return exactly one filehandle, filling in the necessary
        information for the DS server to speak to the Ceph OSD
        directly. */

     ds_handle = *handle;
     ds_handle.data.layout = file_layout;
     ds_handle.data.snapseq = ceph_ll_snap_seq(cmount, VINODE(handle));

     /* We are using sparse layouts with commit-through-DS, so our
        utility word contains only the stripe width, our first stripe
        is always at the beginning of the layout, and there is no
        pattern offset. */

     if ((stripe_width & ~NFL4_UFLG_STRIPE_UNIT_SIZE_MASK) != 0) {
          LogCrit(COMPONENT_PNFS,
                  "Ceph returned stripe width that is disallowed by NFS: "
                  "%"PRIu32".", stripe_width);
          return NFS4ERR_SERVERFAULT;
     }
     util = stripe_width;

     if ((nfs_status
          = FSAL_encode_file_layout(loc_body,
                                    extcontext,
                                    &deviceid,
                                    util,
                                    0,
                                    0,
                                    1,
                                    (fsal_handle_t *)&ds_handle))) {
          LogCrit(COMPONENT_PNFS, "Failed to encode nfsv4_1_file_layout.");
          return nfs_status;
     }

     /* We grant only one segment, and we want it back when the file
        is closed. */

     res->return_on_close = TRUE;
     res->last_segment = TRUE;

     return NFS4_OK;
}

nfsstat4
CEPHFSAL_layoutreturn(fsal_handle_t* handle,
                      fsal_op_context_t* context,
                      XDR *lrf_body,
                      const struct fsal_layoutreturn_arg *arg)

{
     /* Sanity check on type */
     if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->lo_type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     /* Since we no longer store DS addresses, we no longer have
        anything to free.  Later on we should unravel the Ceph client
        a bit more and coordinate with the Ceph MDS's notion of read
        and write pins, but that isn't germane until we have
        LAYOUTRECALL. */

     return NFS4_OK;
}

nfsstat4
CEPHFSAL_layoutcommit(fsal_handle_t *exthandle,
                      fsal_op_context_t *extcontext,
                      XDR *lou_body,
                      const struct fsal_layoutcommit_arg *arg,
                      struct fsal_layoutcommit_res *res)
{
     /* Filehandle for Ceph calls */
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
     /* Operation context */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* Mount structure that must be supplied with each call to Ceph */
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* User ID and group ID for permissions */
     int uid = FSAL_OP_CONTEXT_TO_UID(context);
     int gid = FSAL_OP_CONTEXT_TO_GID(context);
     /* Old stat, so we don't truncate file or reverse time */
     struct stat stold;
     /* new stat to set time and size */
     struct stat stnew;
     /* Mask to determine exactly what gets set */
     int attrmask = 0;
     /* Error returns from Ceph */
     int ceph_status = 0;

     /* Sanity check on type */
     if (arg->type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     /* A more proper and robust implementation of this would use Ceph
        caps, but we need to hack at the client to expose those before
        it can work. */

     memset(&stold, 0, sizeof(struct stat));
     if ((ceph_status = ceph_ll_getattr(cmount, VINODE(handle),
                                        &stold, uid, gid)) < 0) {
          if (ceph_status == -EPERM) {
               LogCrit(COMPONENT_PNFS,
                       "User %u, Group %u not permitted to get attributes "
                       "of file %" PRIu64 ".",
                       uid, gid, VINODE(handle).ino.val);
               return NFS4ERR_ACCESS;
          } else {
               LogCrit(COMPONENT_PNFS,
                       "Error %d in attempt to get attributes of "
                       "file %" PRIu64 ".",
                       -ceph_status, VINODE(handle).ino.val);
               return posix2nfs4_error(-ceph_status);
          }
     }

     memset(&stnew, 0, sizeof(struct stat));
     if (arg->new_offset) {
          if (stold.st_size < arg->last_write + 1) {
               attrmask |= CEPH_SETATTR_SIZE;
               stnew.st_size = arg->last_write + 1;
               res->size_supplied = TRUE;
               res->new_size = arg->last_write + 1;
          }
     }

     if ((arg->time_changed) &&
         (arg->new_time.seconds > stold.st_mtime)) {
          stnew.st_mtime = arg->new_time.seconds;
     } else {
          stnew.st_mtime = time(NULL);
     }

     attrmask |= CEPH_SETATTR_MTIME;

     if ((ceph_status = ceph_ll_setattr(cmount, VINODE(handle), &stnew,
                                        attrmask, uid, gid)) < 0) {
          if (ceph_status == -EPERM) {
               LogCrit(COMPONENT_PNFS,
                       "User %u, Group %u not permitted to get attributes "
                       "of file %" PRIu64 ".",
                       uid, gid, VINODE(handle).ino.val);
               return NFS4ERR_ACCESS;
          } else {
               LogCrit(COMPONENT_PNFS,
                       "Error %d in attempt to get attributes of "
                       "file %" PRIu64 ".",
                       -ceph_status, VINODE(handle).ino.val);
               return posix2nfs4_error(-ceph_status);
          }
     }

     /* This is likely universal for files. */

     res->commit_done = TRUE;

     return NFS4_OK;
}

nfsstat4
CEPHFSAL_getdeviceinfo(fsal_op_context_t *extcontext,
                       XDR* da_addr_body,
                       layouttype4 type,
                       const struct pnfs_deviceid *deviceid)
{
     /* Operation context */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* Mount structure that must be supplied with each call to Ceph */
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* The number of Ceph OSDs in the cluster */
     unsigned num_osds = ceph_ll_num_osds(cmount);
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
     ceph_ll_file_layout(cmount, vinode, &file_layout);

     /* As this is large, we encode as we go rather than building a
        structure and encoding it all at once. */

     /* The first entry in the nfsv4_1_file_ds_addr4 is the array of
        stripe indices. */

     /* First we encode the count of stripes.  Since our pattern
        doesn't repeat, we have as many indices as we do stripes. */

     if (!xdr_uint32_t(da_addr_body, &stripes)) {
          LogCrit(COMPONENT_PNFS, "Failed to encode length of "
                  "stripe_indices array: %" PRIu32 ".", stripes);
          return NFS4ERR_SERVERFAULT;
     }

     for (stripe = 0; stripe < stripes; stripe++) {
          uint32_t stripe_osd
               = stripe_osd = ceph_ll_get_stripe_osd(cmount,
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
               LogCrit(COMPONENT_PNFS, "Failed to encode OSD for stripe %lu.",
                       stripe);
               return NFS4ERR_SERVERFAULT;
          }
     }

     /* The number of OSDs in our cluster is the length of our array
        of multipath_lists */

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
          if (ceph_ll_osdaddr(cmount, osd, &host.addr) < 0) {
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

nfsstat4
CEPHFSAL_getdevicelist(fsal_handle_t *handle,
                       fsal_op_context_t *context,
                       const struct fsal_getdevicelist_arg *arg,
                       struct fsal_getdevicelist_res *res)
{
     /* Sanity check on type */
     if (arg->type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     /* We have neither the ability nor the desire to return all valid
        deviceids, so we do nothing successfully. */

     res->count = 0;
     res->eof = TRUE;

     return NFS4_OK;
}
