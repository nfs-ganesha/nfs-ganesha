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
 * \file    nfs41_op_layoutreturn.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#ifdef _USE_FSALMDS
#include "fsal.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"
#endif /* _USE_FSALMDS */

#include "pnfs_internal.h"

/**
 *
 * \brief The NFS4_OP_LAYOUTRETURN operation.
 *
 * This function implements the NFS4_OP_LAYOUTRETURN operation.
 *
 * \param op    [IN]    pointer to nfs4_op arguments
 * \param data  [INOUT] Pointer to the compound request's data
 * \param resp  [IN]    Pointer to nfs4_op results
 *
 * \return NFS4_OK if successfull, other values show an error.
 *
 * \see all the nfs41_op_<*> function
 * \see nfs4_Compound
 *
 */


nfsstat4 CEPH_pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
			    compound_data_t   * data,
			    LAYOUTRETURN4res  * pres ) 
{
     char __attribute__ ((__unused__)) funcname[] = "nfs41_op_layoutreturn";
#ifdef _USE_FSALMDS
     /* Return code from cache_inode operations */
     cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
     /* Return code from state operations */
     state_status_t state_status = STATE_SUCCESS;
     /* NFS4 status code */
     nfsstat4 nfs_status = 0;
     /* File attributes, used to compare FSID supplied to FSID of file. */
     fsal_attrib_list_t attrs;
     /* FSID of candidate file to return */
     fsal_fsid_t fsid = {0, 0};
     /* True if the supplied layout state was deleted */
     bool_t deleted = FALSE;
     /* State specified in the case of LAYOUTRETURN4_FILE */
     state_t *layout_state = NULL;
     /* State owner associated with this clientid, for bulk returns */
     state_owner_t *clientid_owner = NULL;
     /* Linked list node for iteration */
     struct glist_head *glist = NULL;
     /* Saved next node for safe iteration */
     struct glist_head *glistn = NULL;
     /* Tag to identify caller in tate log messages */
     const char* tag = "LAYOUTRETURN";
     /* Segment selecting which segments to return. */
     struct pnfs_segment spec = {0, 0, 0};
#endif /* _USE_FSALMDS */

#ifdef _USE_FSALMDS

     switch (pargs->lora_layoutreturn.lr_returntype) {
     case LAYOUTRETURN4_FILE:
          if ((nfs_status = nfs4_sanity_check_FH(data,
                                                 REGULAR_FILE))
              != NFS4_OK) {
               pres->lorr_status = nfs_status;
               return pres->lorr_status;
          }
          /* Retrieve state corresponding to supplied ID */
          if (pargs->lora_reclaim) {
               layout_state = NULL;
          } else {
               if ((nfs_status
                    = nfs4_Check_Stateid(&pargs->lora_layoutreturn
                                         .layoutreturn4_u.lr_layout
                                         .lrf_stateid,
                                         data->current_entry,
                                         0LL,
                                         &layout_state,
                                         data,
                                         STATEID_SPECIAL_CURRENT,
                                         tag)) != NFS4_OK) {
                    pres->lorr_status = nfs_status;
                    return pres->lorr_status;
               }
          }

          spec.io_mode = pargs->lora_iomode;
          spec.offset = (pargs->lora_layoutreturn
                         .layoutreturn4_u.lr_layout.lrf_offset);
          spec.length = (pargs->lora_layoutreturn
                          .layoutreturn4_u.lr_layout.lrf_length);

          pres->lorr_status =
               nfs4_return_one_state(
                    data->current_entry,
                    data->pclient,
                    data->pcontext,
                    FALSE,
                    pargs->lora_reclaim,
                    pargs->lora_layoutreturn.lr_returntype,
                    layout_state,
                    spec,
                    pargs->lora_layoutreturn
                    .layoutreturn4_u.lr_layout.lrf_body.lrf_body_len,
                    pargs->lora_layoutreturn
                    .layoutreturn4_u.lr_layout.lrf_body.lrf_body_val,
                    &deleted);
          if (pres->lorr_status == NFS4_OK) {
               if (deleted) {
                    memset(data->current_stateid.other, 0,
                           sizeof(data->current_stateid.other));
                    data->current_stateid.seqid = NFS4_UINT32_MAX;
                    pres->LAYOUTRETURN4res_u.lorr_stateid
                         .lrs_present = 0;
               } else {
                    (pres->LAYOUTRETURN4res_u
                     .lorr_stateid.lrs_present) = 1;
                    /* Update stateid.seqid and copy to current */
                    update_stateid(
                         layout_state,
                         &pres->LAYOUTRETURN4res_u.lorr_stateid
                         .layoutreturn_stateid_u.lrs_stateid,
                         data,
                         tag);
               }
          }
          break;

     case LAYOUTRETURN4_FSID:
          if ((nfs_status
               = nfs4_sanity_check_FH(data,
                                      0))
              != NFS4_OK) {
               pres->lorr_status = nfs_status;
               return pres->lorr_status;
          }
          if (!nfs4_pnfs_supported(data->pexport)) {
               pres->lorr_status = NFS4_OK;
               return pres->lorr_status;
          }
          memset(&attrs, 0, sizeof(fsal_attrib_list_t));
          attrs.asked_attributes |= FSAL_ATTR_FSID;
          cache_status
               = cache_inode_getattr(data->current_entry,
                                     &attrs,
                                     data->ht,
                                     data->pclient,
                                     data->pcontext,
                                     &cache_status);
          if (cache_status != CACHE_INODE_SUCCESS) {
               pres->lorr_status = nfs4_Errno(cache_status);
               return pres->lorr_status;
          }
          fsid = attrs.fsid;
     case LAYOUTRETURN4_ALL:
          spec.io_mode = pargs->lora_iomode;
          spec.offset = 0;
          spec.length = NFS4_UINT64_MAX;

          if ((state_status
               = get_clientid_owner(data->psession->clientid,
                                    &clientid_owner))
              != STATE_SUCCESS) {
               pres->lorr_status =
                    nfs4_Errno_state(state_status);
               return pres->lorr_status;
          }

          /* We need the safe version because return_one_state can
             delete the current state. */

          glist_for_each_safe(glist,
                              glistn,
                              (&clientid_owner->so_owner.so_nfs4_owner
                               .so_state_list)) {
               state_t *candidate_state
                    = glist_entry(glist,
                                  state_t,
                                  owner_states);
               if (candidate_state->state_type != STATE_TYPE_LAYOUT) {
                    continue;
               } else {
                    layout_state = candidate_state;
               }

               if (pargs->lora_layoutreturn.lr_returntype
                   == LAYOUTRETURN4_FSID) {
                    memset(&attrs, 0, sizeof(fsal_attrib_list_t));
                    attrs.asked_attributes |= FSAL_ATTR_FSID;
                    cache_inode_getattr(layout_state->state_pentry,
                                        &attrs,
                                        data->ht,
                                        data->pclient,
                                        data->pcontext,
                                        &cache_status);
                    if (cache_status != CACHE_INODE_SUCCESS) {
                         pres->lorr_status
                              = nfs4_Errno(cache_status);
                         return pres->lorr_status;
                    }

              memset(&attrs, 0, sizeof(fsal_attrib_list_t));
              attrs.asked_attributes |= FSAL_ATTR_FSID;

              if (memcmp(&fsid, &(attrs.fsid), sizeof(fsal_fsid_t)))
                   continue;
               }

               pres->lorr_status =
                    nfs4_return_one_state(layout_state->state_pentry,
                                          data->pclient,
                                          data->pcontext,
                                          TRUE,
                                          pargs->lora_reclaim,
                                          (pargs->lora_layoutreturn.lr_returntype),
                                          layout_state,
                                          spec,
                                          0,
                                          NULL,
                                          &deleted);
               if (pres->lorr_status != NFS4_OK) {
                    break;
               }
          }

          memset(data->current_stateid.other, 0,
                 sizeof(data->current_stateid.other));
          data->current_stateid.seqid = NFS4_UINT32_MAX;
          pres->LAYOUTRETURN4res_u.lorr_stateid.lrs_present = 0;
          break;

     default:
          pres->lorr_status = NFS4ERR_INVAL;
          return pres->lorr_status;
     }
#else
     pres->lorr_status = NFS4ERR_NOTSUPP;
#endif
     return pres->lorr_status;
}                               /* nfs41_op_layoutreturn */

