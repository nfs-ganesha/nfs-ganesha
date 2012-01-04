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


nfsstat4 LUSTREFSAL_layoutget( fsal_handle_t                   * exthandle,
                               fsal_op_context_t               * extcontext,
                               XDR                             * loc_body,
                               const struct fsal_layoutget_arg * arg,
                               struct fsal_layoutget_res       * res)
{
     /* We support only LAYOUT4_NFSV4_1_FILES layouts */
     if (arg->type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     return NFS4_OK;
} /* LUSTREFSAL_layoutget */

nfsstat4 LUSTREFSAL_layoutreturn( fsal_handle_t                      * handle,
                                  fsal_op_context_t                  * context,
                                  XDR                                * lrf_body,
                                  const struct fsal_layoutreturn_arg * arg)

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
} /* LUSTREFSAL_layoutreturn */

nfsstat4 LUSTREFSAL_layoutcommit( fsal_handle_t                      * exthandle,
                                  fsal_op_context_t                  * extcontext,
                                  XDR                                * lou_body,
                                  const struct fsal_layoutcommit_arg * arg,
                                  struct fsal_layoutcommit_res       * res)
{
     /* Sanity check on type */
     if (arg->type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  arg->type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     return NFS4_OK;
}

nfsstat4 LUSTREFSAL_getdeviceinfo( fsal_op_context_t          * extcontext,
                                   XDR                        * da_addr_body,
                                   layouttype4                  type,
                                   const struct pnfs_deviceid * deviceid)
{
     /* Sanity check on type */
     if (type != LAYOUT4_NFSV4_1_FILES) {
          LogCrit(COMPONENT_PNFS,
                  "Unsupported layout type: %x",
                  type);
          return NFS4ERR_UNKNOWN_LAYOUTTYPE;
     }

     return NFS4_OK;
} /* LUSTREFSAL_getdeviceinfo */

nfsstat4 LUSTREFSAL_getdevicelist( fsal_handle_t                        * handle,
                                   fsal_op_context_t                    * context,
                                   const struct fsal_getdevicelist_arg  * arg,
                                   struct fsal_getdevicelist_res        * res)
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
} /* LUSTREFSAL_getdevicelist */
