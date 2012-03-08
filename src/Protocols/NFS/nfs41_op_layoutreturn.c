/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#ifdef _PNFS_MDS
#include "fsal.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"
#endif /* _PNFS_MDS */

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


#define arg_LAYOUTRETURN4 op->nfs_argop4_u.oplayoutreturn
#define res_LAYOUTRETURN4 resp->nfs_resop4_u.oplayoutreturn

int nfs41_op_layoutreturn(struct nfs_argop4 *op,
                          compound_data_t *data,
                          struct nfs_resop4 *resp)
{
     char __attribute__ ((__unused__)) funcname[] = "nfs41_op_layoutreturn";
#ifdef _PNFS_MDS
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
#endif /* _PNFS_MDS */

     resp->resop = NFS4_OP_LAYOUTRETURN;

#ifdef _PNFS_MDS
     switch (arg_LAYOUTRETURN4.lora_layoutreturn.lr_returntype) {
     case LAYOUTRETURN4_FILE:
          if ((nfs_status = nfs4_sanity_check_FH(data,
                                                 REGULAR_FILE))
              != NFS4_OK) {
               res_LAYOUTRETURN4.lorr_status = nfs_status;
               return res_LAYOUTRETURN4.lorr_status;
          }
          /* Retrieve state corresponding to supplied ID */
          if (arg_LAYOUTRETURN4.lora_reclaim) {
               layout_state = NULL;
          } else {
               if ((nfs_status
                    = nfs4_Check_Stateid(&arg_LAYOUTRETURN4.lora_layoutreturn
                                         .layoutreturn4_u.lr_layout
                                         .lrf_stateid,
                                         data->current_entry,
                                         0LL,
                                         &layout_state,
                                         data,
                                         STATEID_SPECIAL_CURRENT,
                                         tag)) != NFS4_OK) {
                    res_LAYOUTRETURN4.lorr_status = nfs_status;
                    return res_LAYOUTRETURN4.lorr_status;
               }
          }

          spec.io_mode = arg_LAYOUTRETURN4.lora_iomode;
          spec.offset = (arg_LAYOUTRETURN4.lora_layoutreturn
                         .layoutreturn4_u.lr_layout.lrf_offset);
          spec.length = (arg_LAYOUTRETURN4.lora_layoutreturn
                          .layoutreturn4_u.lr_layout.lrf_length);

          res_LAYOUTRETURN4.lorr_status =
               nfs4_return_one_state(
                    data->current_entry,
                    data->pclient,
                    data->pcontext,
                    FALSE,
                    arg_LAYOUTRETURN4.lora_reclaim,
                    arg_LAYOUTRETURN4.lora_layoutreturn.lr_returntype,
                    layout_state,
                    spec,
                    arg_LAYOUTRETURN4.lora_layoutreturn
                    .layoutreturn4_u.lr_layout.lrf_body.lrf_body_len,
                    arg_LAYOUTRETURN4.lora_layoutreturn
                    .layoutreturn4_u.lr_layout.lrf_body.lrf_body_val,
                    &deleted);
          if (res_LAYOUTRETURN4.lorr_status == NFS4_OK) {
               if (deleted) {
                    memset(data->current_stateid.other, 0,
                           sizeof(data->current_stateid.other));
                    data->current_stateid.seqid = NFS4_UINT32_MAX;
                    res_LAYOUTRETURN4.LAYOUTRETURN4res_u.lorr_stateid
                         .lrs_present = 0;
               } else {
                    (res_LAYOUTRETURN4.LAYOUTRETURN4res_u
                     .lorr_stateid.lrs_present) = 1;
                    /* Update stateid.seqid and copy to current */
                    update_stateid(
                         layout_state,
                         &res_LAYOUTRETURN4.LAYOUTRETURN4res_u.lorr_stateid
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
               res_LAYOUTRETURN4.lorr_status = nfs_status;
               return res_LAYOUTRETURN4.lorr_status;
          }
          if (!nfs4_pnfs_supported(data->pexport)) {
               res_LAYOUTRETURN4.lorr_status = NFS4_OK;
               return res_LAYOUTRETURN4.lorr_status;
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
               res_LAYOUTRETURN4.lorr_status = nfs4_Errno(cache_status);
               return res_LAYOUTRETURN4.lorr_status;
          }
          fsid = attrs.fsid;
     case LAYOUTRETURN4_ALL:
          spec.io_mode = arg_LAYOUTRETURN4.lora_iomode;
          spec.offset = 0;
          spec.length = NFS4_UINT64_MAX;

          if ((state_status
               = get_clientid_owner(data->psession->clientid,
                                    &clientid_owner))
              != STATE_SUCCESS) {
               res_LAYOUTRETURN4.lorr_status =
                    nfs4_Errno_state(state_status);
               return res_LAYOUTRETURN4.lorr_status;
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
                                  state_owner_list);
               if (candidate_state->state_type != STATE_TYPE_LAYOUT) {
                    continue;
               } else {
                    layout_state = candidate_state;
               }

               if (arg_LAYOUTRETURN4.lora_layoutreturn.lr_returntype
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
                         res_LAYOUTRETURN4.lorr_status
                              = nfs4_Errno(cache_status);
                         return res_LAYOUTRETURN4.lorr_status;
                    }

              memset(&attrs, 0, sizeof(fsal_attrib_list_t));
              attrs.asked_attributes |= FSAL_ATTR_FSID;

              if (memcmp(&fsid, &(attrs.fsid), sizeof(fsal_fsid_t)))
                   continue;
               }

               res_LAYOUTRETURN4.lorr_status =
                    nfs4_return_one_state(layout_state->state_pentry,
                                          data->pclient,
                                          data->pcontext,
                                          TRUE,
                                          arg_LAYOUTRETURN4.lora_reclaim,
                                          (arg_LAYOUTRETURN4.lora_layoutreturn.lr_returntype),
                                          layout_state,
                                          spec,
                                          0,
                                          NULL,
                                          &deleted);
               if (res_LAYOUTRETURN4.lorr_status != NFS4_OK) {
                    break;
               }
          }

          memset(data->current_stateid.other, 0,
                 sizeof(data->current_stateid.other));
          data->current_stateid.seqid = NFS4_UINT32_MAX;
          res_LAYOUTRETURN4.LAYOUTRETURN4res_u.lorr_stateid.lrs_present = 0;
          break;

     default:
          res_LAYOUTRETURN4.lorr_status = NFS4ERR_INVAL;
          return res_LAYOUTRETURN4.lorr_status;
     }
#else /* !_PNFS_MDS */
     res_LAYOUTRETURN4.lorr_status = NFS4ERR_NOTSUPP;
#endif /* !_PNFS_MDS */
     return res_LAYOUTRETURN4.lorr_status;
} /* nfs41_op_layoutreturn */

/**
 * nfs41_op_layoutreturn_Free: frees what was allocared to handle nfs41_op_layoutreturn.
 *
 * Frees what was allocared to handle nfs41_op_layoutreturn.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs41_op_layoutreturn_Free(LOCK4res * resp)
{
     return;
} /* nfs41_op_layoutreturn_Free */

#ifdef _PNFS_MDS
/**
 *
 * return_one_state: Return layouts corresponding to one stateid
 *
 * This function returns one or more layouts corresponding to a layout
 * stateid, calling FSAL_layoutreturn for each layout falling within
 * the specified range and iomode.  If all layouts have been returned,
 * it deletes the state.
 *
 * @param handle       [IN]     Handle for the file whose layouts we return
 * @param pclient      [IN,OUT] Client pointer for memory pools
 * @param context      [IN,OUT] Operation context for FSAL calls
 * @param synthetic    [IN]     True if this is a bulk or synthesized
 *                              (e.g. last close or lease expiry) return
 * @param layout_state [IN,OUT] State whose segments we return
 * @param iomode       [IN]     IO mode specifying which segments to
 *                              return
 * @param offset       [IN]     Offset of range to return
 * @param length       [IN]     Length of range to return
 * @param body_len     [IN]     Length of type-specific layout return
 *                              data
 * @param body_val     [IN]     Type-specific layout return data
 * @param deleted      [OUT]    True if the layout state has been deleted
 *
 * @return NFSv4.1 status codes
 */

nfsstat4 nfs4_return_one_state(cache_entry_t *entry,
                               cache_inode_client_t* pclient,
                               fsal_op_context_t* context,
                               fsal_boolean_t synthetic,
                               fsal_boolean_t reclaim,
                               layoutreturn_type4 return_type,
                               state_t *layout_state,
                               struct pnfs_segment spec_segment,
                               u_int body_len,
                               const char* body_val,
                               bool_t* deleted)
{
     /* Return from cache_inode calls */
     cache_inode_status_t cache_status = 0;
     /* Return from SAL calls */
     state_status_t state_status = 0;
     /* Return from this function */
     nfsstat4 nfs_status = 0;
     /* Iterator along linked list */
     struct glist_head *glist = NULL;
     /* Saved 'next' pointer for glist_for_each_safe */
     struct glist_head *glistn = NULL;
     /* Input arguments to FSAL_layoutreturn */
     struct fsal_layoutreturn_arg arg;
     /* The FSAL file handle */
     fsal_handle_t *handle = NULL;
     /* XDR stream holding the lrf_body opaque */
     XDR lrf_body;
     /* The beginning of the stream */
     unsigned int beginning = 0;
     /* The current segment in iteration */
     state_layout_segment_t *segment = NULL;
     /* If we have a lock on the segment */
     bool_t seg_locked = FALSE;

     if (body_val) {
          xdrmem_create(&lrf_body,
                        (char*) body_val, /* Decoding won't modify this */
                        body_len,
                        XDR_DECODE);
          beginning = xdr_getpos(&lrf_body);
     }

     handle = cache_inode_get_fsal_handle(entry,
                                          &cache_status);

     if (cache_status != CACHE_INODE_SUCCESS) {
          return nfs4_Errno(cache_status);
     }

     memset(&arg, 0, sizeof(struct fsal_layoutreturn_arg));

     arg.reclaim = reclaim;
     arg.lo_type = layout_state->state_data.layout.state_layout_type;
     arg.return_type = return_type;
     arg.spec_segment = spec_segment;
     arg.synthetic = synthetic;

     if (!reclaim) {
          /* The _safe version of glist_for_each allows us to delete
             segments while we iterate. */
          glist_for_each_safe(glist,
                              glistn,
                              &layout_state->state_data.layout.state_segments) {
               segment = glist_entry(glist,
                                     state_layout_segment_t,
                                     sls_state_segments);

               pthread_mutex_lock(&segment->sls_mutex);
               seg_locked = TRUE;

               arg.cur_segment = segment->sls_segment;
               arg.fsal_seg_data = segment->sls_fsal_data;
               arg.last_segment = (glistn->next == glistn);

               if (pnfs_segment_contains(spec_segment,
                                         segment->sls_segment)) {
                    arg.dispose = TRUE;
               } else if (pnfs_segments_overlap(spec_segment,
                                                segment->sls_segment)) {
                    arg.dispose = FALSE;
               } else {
                    pthread_mutex_unlock(&segment->sls_mutex);
                    continue;
               }

               nfs_status =
                    fsal_mdsfunctions
                    .layoutreturn(handle,
                                  context,
                                  (body_val ? &lrf_body : NULL),
                                  &arg);

               if (nfs_status != NFS4_OK) {
                    goto out;
               }

               if (arg.dispose) {
                    if (state_delete_segment(segment)
                        != STATE_SUCCESS) {
                         nfs_status = nfs4_Errno_state(state_status);
                         goto out;
                    }
               } else {
                    segment->sls_segment
                         = pnfs_segment_difference(spec_segment,
                                                   segment->sls_segment);
                    pthread_mutex_unlock(&segment->sls_mutex);
               }
          }
          seg_locked = FALSE;

          if (body_val) {
               /* This really should work in all cases for an
                  in-memory decode stream. */
               xdr_setpos(&lrf_body, beginning);
          }
          if (glist_empty(&layout_state->state_data.layout.state_segments)) {
               state_del(layout_state, pclient, &state_status);
               *deleted = TRUE;
          } else {
               *deleted = FALSE;
          }
     } else {
          /* For a reclaim return, there are no recorded segments in
             state. */
          arg.cur_segment.io_mode = 0;
          arg.cur_segment.offset = 0;
          arg.cur_segment.length = 0;
          arg.fsal_seg_data = NULL;
          arg.last_segment = FALSE;
          arg.dispose = FALSE;

          nfs_status =
               fsal_mdsfunctions
               .layoutreturn(handle,
                             context,
                             (body_val ? &lrf_body : NULL),
                             &arg);

          if (nfs_status != NFS4_OK) {
               goto out;
          }
          *deleted = TRUE;
     }

     nfs_status = NFS4_OK;

out:
     if (body_val) {
          xdr_destroy(&lrf_body);
     }
     if (seg_locked) {
          pthread_mutex_unlock(&segment->sls_mutex);
     }

     return nfs_status;
}

/**
 *
 * nfs4_pnfs_supported: Check whether a given export supports pNFS
 *
 * This function returns true if the export supports pNFS metadata
 * operations.
 *
 * @param export [IN] The export to check.
 *
 * @return TRUE or FALSE.
 */
fsal_boolean_t nfs4_pnfs_supported(const exportlist_t *export)
{
  if (!export)
    {
      return FALSE;
    }
  else
    {
      return export->FS_export_context.fe_static_fs_info->pnfs_supported;
    }
}
#endif /* _PNFS_MDS */
