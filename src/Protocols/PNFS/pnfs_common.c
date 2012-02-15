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

/**
 * \brief frees what was allocared to handle pnfs_layoutcommit.
 *
 * Frees what was allocared to handle pnfs_layoutcommit.
 *
 * \param resp  [INOUT]    Pointer to nfs4_op results
 *
 * \return nothing (void function )
 *
 */
void COMMON_pnfs_layoutcommit_Free( LAYOUTCOMMIT4res * resp)
{
     /* Nothing to Mem_Free */
     return;
}                               /* pnfs_layoutcommit_Free */

/**
 * \brief Frees what was allocared to handle pnfs_layoutget.
 *
 * Frees what was allocared to handle pnfs_layoutget.
 *
 * \param resp  [INOUT]    Pointer to nfs4_op results
 *
 * \return nothing (void function )
 *
 */
void COMMON_pnfs_layoutget_Free(LAYOUTGET4res * resp)
{
#ifdef _USE_PNFS
     size_t i = 0;
     if (resp->logr_status == NFS4_OK) {
          for (i = 0;
               i < (resp->LAYOUTGET4res_u.logr_resok4
                    .logr_layout.logr_layout_len);
               i++) {
               Mem_Free((char *)resp->LAYOUTGET4res_u.logr_resok4.logr_layout.
                        logr_layout_val[i].lo_content.loc_body.loc_body_val);
          }
     }
#endif
}

/**
 * \brief Frees what was allocared to handle pnfs_layoutreturn.
 *
 * Frees what was allocared to handle pnfs_layoutreturn.
 *
 * \param resp  [INOUT]    Pointer to nfs4_op results
 *
 * \return nothing (void function )
 *
 */
void COMMON_pnfs_layoutreturn_Free( LAYOUTRETURN4res * pres )
{
  return ;
}

/**
 * \brief frees what was allocared to handle nfs4_op_getdevicelist.
 *
 * Frees what was allocared to handle nfs4_op_getdevicelist.
 *
 * \param resp  [INOUT]    Pointer to nfs4_op results
 *
 * \return nothing (void function )
 *
 */
void COMMON_pnfs_getdevicelist_Free(GETDEVICELIST4res * resp)
{
#ifdef _USE_PNFS
     if (resp->gdlr_status == NFS4_OK) {
          Mem_Free(resp->GETDEVICELIST4res_u.gdlr_resok4
                   .gdlr_deviceid_list.gdlr_deviceid_list_val);
     }
#endif
  return;
}

/**
 * \brief frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * Frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * \param resp  [INOUT]    Pointer to nfs4_op results
 *
 * \return nothing (void function )
 *
 */
void COMMON_pnfs_getdeviceinfo_Free(GETDEVICEINFO4res * resp)
{
#ifdef _USE_PNFS
     if (resp->gdir_status == NFS4_OK) {
          if (resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr
              .da_addr_body.da_addr_body_val != NULL) {
               Mem_Free(resp->GETDEVICEINFO4res_u.gdir_resok4
                        .gdir_device_addr.da_addr_body.da_addr_body_val);
          }
     }
#endif
     return;
}
