/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 * ---------------------------------------
 */

/**
 * \file    pnfs_layoutcommit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * pnfs_lock.c : Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <stdint.h>
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "pnfs.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#ifdef _USE_FSALMDS
#include "fsal.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"
#endif /* _USE_FSALMDS */

#include "pnfs_internal.h"

/**
 *
 * \brief The NFS4_OP_LAYOUTCOMMIT operation.
 *
 * This function implements the NFS4_OP_LAYOUTCOMMIT operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see all the pnfs_<*> function
 * @see nfs4_Compound
 *
 */

nfsstat4 FSAL_pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs,
                                 compound_data_t * data,
                                 LAYOUTCOMMIT4res  * pres )
{
     char __attribute__ ((__unused__)) funcname[] = "pnfs_layoutcommit";
#ifdef _USE_FSALMDS
     /* Return from cache_inode calls */
     cache_inode_status_t cache_status = 0;
     /* NFS4 return code */
     nfsstat4 nfs_status = 0;
     /* State indicated by client */
     state_t *layout_state = NULL;
     /* Tag for logging in state operations */
     const char *tag = "LAYOUTCOMMIT";
     /* Iterator along linked list */
     struct glist_head *glist = NULL;
     /* Input arguments of FSAL_layoutcommit */
     struct fsal_layoutcommit_arg arg;
     /* Input/output and output arguments of FSAL_layoutcommit */
     struct fsal_layoutcommit_res res;
     /* The segment being traversed */
     state_layout_segment_t *segment;
     /* The FSAL handle */
     fsal_handle_t *handle = NULL;
     /* XDR stream holding the lrf_body opaque */
     XDR lou_body;
     /* The beginning of the stream */
     unsigned int beginning = 0;
#endif /* _USE_FSALMDS */

#ifdef _USE_FSALMDS
     if ((nfs_status = nfs4_sanity_check_FH(data,
                                            REGULAR_FILE))
         != NFS4_OK) {
          goto out;
     }

     handle = cache_inode_get_fsal_handle(data->current_entry,
                                       &cache_status);

     if (cache_status != CACHE_INODE_SUCCESS) {
          nfs_status = nfs4_Errno(cache_status);
          goto out;
     }

     memset(&arg, 0, sizeof(struct fsal_layoutcommit_arg));
     memset(&res, 0, sizeof(struct fsal_layoutcommit_res));

     /* Suggest a new size, if we have it */
     if (pargs->loca_last_write_offset.no_newoffset) {
          arg.new_offset = TRUE;
          arg.last_write =
               pargs->loca_last_write_offset.newoffset4_u.no_offset;
     } else {
          arg.new_offset = FALSE;
     }

     arg.reclaim = pargs->loca_reclaim;

     xdrmem_create(&lou_body,
                   pargs->loca_layoutupdate.lou_body.lou_body_val,
                   pargs->loca_layoutupdate.lou_body.lou_body_len,
                   XDR_DECODE);

     beginning = xdr_getpos(&lou_body);

     /* Suggest a new modification time if we have it */
     if (pargs->loca_time_modify.nt_timechanged) {
          arg.time_changed = TRUE;
          arg.new_time.seconds =
               pargs->loca_time_modify.newtime4_u.nt_time.seconds;
          arg.new_time.nseconds =
               pargs->loca_time_modify.newtime4_u.nt_time.nseconds;
     }

     /* Retrieve state corresponding to supplied ID */

     if ((nfs_status
          = nfs4_Check_Stateid(&pargs->loca_stateid,
                               data->current_entry,
                               0LL,
                               &layout_state,
                               data,
                               STATEID_SPECIAL_CURRENT,
                               tag)) != NFS4_OK) {
          goto out;
     }

     arg.type = layout_state->state_data.layout.state_layout_type;

     /* Check to see if the layout is valid */

     glist_for_each(glist,
                    &layout_state->state_data.layout.state_segments) {
          segment = glist_entry(glist,
                                state_layout_segment_t,
                                sls_state_segments);

          pthread_mutex_lock(&segment->sls_mutex);

          /*
           * XXX This assumes a single FSAL and must be changed after
           * XXX the Lieb Rearchitecture.  The MDS function structure
           * XXX associated with the current filehandle should be
           * XXX used.
           */

          nfs_status
               = fsal_mdsfunctions.layoutcommit(handle,
                                                data->pcontext,
                                                &lou_body,
                                                &arg,
                                                &res);

          pthread_mutex_unlock(&segment->sls_mutex);

          if (nfs_status != NFS4_OK) {
               goto out;
          }

          if (res.commit_done) {
               break;
          }

          /* This really should work in all cases for an in-memory
             decode stream. */
          xdr_setpos(&lou_body, beginning);
     }

     if (pargs->loca_time_modify.nt_timechanged ||
         pargs->loca_last_write_offset.no_newoffset ||
         res.size_supplied) {
          if (cache_inode_kill_entry(data->current_entry,
                                     WT_LOCK,
                                     data->ht,
                                     data->pclient,
                                     &cache_status)
              != CACHE_INODE_SUCCESS) {
               nfs_status = nfs4_Errno(cache_status);
               goto out;
          }
     }

     (pres->LAYOUTCOMMIT4res_u.locr_resok4
      .locr_newsize.ns_sizechanged) = res.size_supplied;

     if (res.size_supplied) {
          (pres->LAYOUTCOMMIT4res_u.locr_resok4
           .locr_newsize.newsize4_u.ns_size) = res.new_size;
     }

     nfs_status = NFS4_OK;

out:

     xdr_destroy(&lou_body);

     pres->locr_status = nfs_status;

#else
     pres->locr_status = NFS4ERR_NOTSUPP;
#endif
     return pres->locr_status;
}                               /* pnfs_layoutcommit */

