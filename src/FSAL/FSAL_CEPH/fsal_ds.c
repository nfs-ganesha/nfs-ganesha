/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box Corporation
 * All Rights Reserved
 *
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
 * \file    fsal_ds.c
 * \brief   DS realisation for the filesystem abstraction
 *
 * filelayout.c: DS realisation for the filesystem abstraction
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
#include "stuff_alloc.h"
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "pnfs_common.h"
#include "fsal_pnfs_files.h"

#define min(a,b)          \
     ({ typeof (a) _a = (a);                    \
          typeof (b) _b = (b);                  \
          _a < _b ? _a : _b; })

nfsstat4
CEPHFSAL_DS_read(fsal_handle_t *exthandle,
                 fsal_op_context_t *extcontext,
                 const stateid4 *stateid,
                 offset4 offset,
                 count4 requested_length,
                 caddr_t buffer,
                 count4 *supplied_length,
                 fsal_boolean_t *end_of_file)
{
     /* Our format for the file handle */
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
     /* Our format for the operational context */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* Mount parameter specified for all calls to Ceph */
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* The OSD number for this machine */
     int local_OSD = 0;
     /* Width of a stripe in the file */
     uint32_t stripe_width = 0;
     /* Beginning of a block */
     uint64_t block_start = 0;
     /* Number of the stripe being read */
     uint32_t stripe = 0;
     /* Internal offset within the stripe*/
     uint32_t internal_offset = 0;
     /* The amount actually read */
     int amount_read = 0;

     /* Find out what my OSD ID is, so we can avoid talking to other
        OSDs. */

     local_OSD = ceph_get_local_osd(cmount);
     if (local_OSD < 0) {
          return posix2nfs4_error(-local_OSD);
     }

     /* Find out what stripe we're writing to and where within the
        stripe. */

     stripe_width = handle->data.layout.fl_stripe_unit;
     if (stripe_width == 0) {
          /* READ isn't actually allowed to return BADHANDLE */
          return NFS4ERR_INVAL;
     }
     stripe = offset / stripe_width;
     block_start = stripe * stripe_width;
     internal_offset = offset - block_start;

     if (local_OSD
         != ceph_ll_get_stripe_osd(cmount,
                                   VINODE(handle),
                                   stripe,
                                   &(handle->data.layout))) {
          return NFS4ERR_PNFS_IO_HOLE;
     }

     amount_read
          = ceph_ll_read_block(cmount,
                               VINODE(handle),
                               stripe,
                               buffer,
                               internal_offset,
                               min((stripe_width -
                                    internal_offset),
                                   requested_length),
                               &(handle->data.layout));
     if (amount_read < 0) {
          return posix2nfs4_error(-amount_read);
     }

     *supplied_length = amount_read;

     *end_of_file = FALSE;

     return NFS4_OK;
}

nfsstat4
CEPHFSAL_DS_write(fsal_handle_t *exthandle,
                  fsal_op_context_t *extcontext,
                  const stateid4 *stateid,
                  offset4 offset,
                  count4 write_length,
                  caddr_t buffer,
                  stable_how4 stability_wanted,
                  count4 *written_length,
                  verifier4 *writeverf,
                  stable_how4 *stability_got)
{
     /* Our format for the file handle */
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
     /* Our format for the operational context */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* Mount parameter specified for all calls to Ceph */
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* User ID and group ID for permissions */
     int uid = FSAL_OP_CONTEXT_TO_UID(context);
     int gid = FSAL_OP_CONTEXT_TO_GID(context);
     /* The OSD number for this host */
     int local_OSD = 0;
     /* Width of a stripe in the file */
     uint32_t stripe_width = 0;
     /* Beginning of a block */
     uint64_t block_start = 0;
     /* Number of the stripe being written */
     uint32_t stripe = 0;
     /* Internal offset within the stripe*/
     uint32_t internal_offset = 0;
     /* The amount actually written */
     int32_t amount_written = 0;
     /* The adjusted write length, confined to one object */
     uint32_t adjusted_write = 0;
     /* Return code from ceph calls */
     int ceph_status = 0;

     /* Zero the verifier.  All our DS writes are stable, so we don't
        use it, but we do want to rpevent spurious junk from making it
        look like there was a failure. */

     memset(*writeverf, 0, NFS4_VERIFIER_SIZE);

     /* Find out what my OSD ID is, so we can avoid talking to other
        OSDs. */

     local_OSD = ceph_get_local_osd(cmount);

     /* Find out what stripe we're writing to and where within the
        stripe. */

     stripe_width = handle->data.layout.fl_stripe_unit;
     if (stripe_width == 0) {
          /* WRITE isn't actually allowed to return BADHANDLE */
          return NFS4ERR_INVAL;
     }
     stripe = offset / stripe_width;
     block_start = stripe * stripe_width;
     internal_offset = offset - block_start;

     if (local_OSD
         != ceph_ll_get_stripe_osd(cmount,
                                   VINODE(handle),
                                   stripe,
                                   &(handle->data.layout))) {
          return NFS4ERR_PNFS_IO_HOLE;
     }

     adjusted_write = min((stripe_width - internal_offset),
                          write_length);

     /* If the client specifies FILE_SYNC4, then we have to connect
        the filehandle and use the MDS to update size and access
        time. */
     if (stability_wanted == FILE_SYNC4) {
          Fh* descriptor = NULL;

          if ((ceph_status = ceph_ll_connectable_m(cmount, &VINODE(handle),
                                                   handle->data.parent_ino,
                                                   handle->data.parent_hash))
              != 0) {
               printf("Filehandle connection failed with: %d\n", ceph_status);
               return posix2nfs4_error(-ceph_status);
          }
          if ((ceph_status = ceph_ll_open(cmount,
                                          VINODE(handle),
                                          O_WRONLY,
                                          &descriptor,
                                          uid,
                                          gid)) != 0) {
               printf("Open failed with: %d\n", ceph_status);
               return posix2nfs4_error(-ceph_status);
          }

          amount_written
               = ceph_ll_write(cmount,
                               descriptor,
                               offset,
                               adjusted_write,
                               buffer);

          if (amount_written < 0) {
               printf("Write failed with: %d\n", amount_written);
               ceph_ll_close(cmount, descriptor);
               return posix2nfs4_error(-amount_written);
          }

          if ((ceph_status = ceph_ll_fsync(cmount, descriptor, 0)) < 0) {
               printf("fsync failed with: %d\n", ceph_status);
               ceph_ll_close(cmount, descriptor);
               return posix2nfs4_error(-ceph_status);
          }

      if ((ceph_status = ceph_ll_close(cmount, descriptor)) < 0) {
           printf("close failed with: %d\n", ceph_status);
           return posix2nfs4_error(-ceph_status);
      }
      *written_length = amount_written;
      *stability_got = FILE_SYNC4;
     } else {
          /* FILE_SYNC4 wasn't specified, so we don't have to bother with
             the MDS. */

          if ((amount_written
               = ceph_ll_write_block(cmount,
                                     VINODE(handle),
                                     stripe,
                                     buffer,
                                     internal_offset,
                                     adjusted_write,
                                     &(handle->data.layout),
                                     handle->data.snapseq,
                                     (stability_wanted
                                      == DATA_SYNC4)))
              < 0) {
               return posix2nfs4_error(-amount_written);
          }

          *written_length = amount_written;
          *stability_got = stability_wanted;
     }

     return NFS4_OK;
}

nfsstat4
CEPHFSAL_DS_commit(fsal_handle_t *exthandle,
                   fsal_op_context_t *extcontext,
                   offset4 offset,
                   count4 count,
                   verifier4 *writeverf)
{
     /* Our format for the file handle */
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
     /* Our format for the operational context */
     cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
     /* Mount parameter specified for all calls to Ceph */
     struct ceph_mount_info *cmount = context->export_context->cmount;
     /* Width of a stripe in the file */
     const uint32_t stripe_width = handle->data.layout.fl_stripe_unit;
     /* Error return from Ceph */
     int rc = 0;

     /* Find out what stripe we're writing to and where within the
        stripe. */

     if (stripe_width == 0) {
          /* COMMIT isn't actually allowed to return BADHANDLE */
          return NFS4ERR_INVAL;
     }

     memset(*writeverf, 0, NFS4_VERIFIER_SIZE);

     rc = ceph_ll_commit_blocks(cmount,
                                VINODE(handle),
                                offset,
                                count);
     if (rc < 0) {
          return posix2nfs4_error(rc);
     }

     return NFS4_OK;
}
