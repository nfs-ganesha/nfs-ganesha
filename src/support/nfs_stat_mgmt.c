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
 * \file    nfs_stat_mgmt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:03 $
 * \version $Revision: 1.4 $
 * \brief   routines for managing the nfs statistics.
 *
 * nfs_stat_mgmt.c : routines for managing the nfs statistics.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_stat_mgmt.c,v 1.4 2005/11/28 17:03:03 deniel Exp $
 *
 * $Log: nfs_stat_mgmt.c,v $
 *
 * Revision 1.3  2005/09/30 15:50:19  deniel
 * Support for mount and nfs protocol different from the default
 *
 * Revision 1.2  2005/09/30 14:27:34  deniel
 * Adding some configurationsa items in nfs_core
 *
 * Revision 1.1  2005/08/11 12:37:28  deniel
 * Added statistics management
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include "nfs_stat.h"

/**
 *
 * nfs_stat_update: Update a client's statistics.
 *
 * Update a client's statistics.
 *
 * @param type    [IN]    type of the stat to dump
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 *
 * @return nothing (void function)
 *
 */
void nfs_stat_update(nfs_stat_type_t      type,
                     nfs_request_stat_t * pstat_req,
                     struct svc_req     * preq,
#ifdef _USE_QUEUE_TIMER
                     msectimer_t          await_time,
#endif
                     msectimer_t          latency
                     )
{
  nfs_request_stat_item_t *pitem = NULL;

  if(preq->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pitem = &pstat_req->stat_req_nfs2[preq->rq_proc];
          pstat_req->nb_nfs2_req += 1;
          break;

        case NFS_V3:
          pitem = &pstat_req->stat_req_nfs3[preq->rq_proc];
          pstat_req->nb_nfs3_req += 1;
          break;

        case NFS_V4:
          pitem = &pstat_req->stat_req_nfs4[preq->rq_proc];
          pstat_req->nb_nfs4_req += 1;

          break;

        default:
          /* Bad vers ? */
          LogCrit(COMPONENT_DISPATCH,
               "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %d",
               __FILE__, __LINE__);
          return;
        }
    }
  else if(preq->rq_prog == nfs_param.core_param.program[P_MNT])
    {
      switch (preq->rq_vers)
        {
        case MOUNT_V1:
          pitem = &pstat_req->stat_req_mnt1[preq->rq_proc];
          pstat_req->nb_mnt1_req += 1;
          break;

        case MOUNT_V3:
          pitem = &pstat_req->stat_req_mnt3[preq->rq_proc];
          pstat_req->nb_mnt3_req += 1;
          break;

        default:
          /* Bad vers ? */
          LogCrit(COMPONENT_DISPATCH,
               "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %d",
               __FILE__, __LINE__);
          return;
        }
    }
#ifdef _USE_NLM
  else if(preq->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      switch (preq->rq_vers)
        {
        case NLM4_VERS:
          pitem = &pstat_req->stat_req_nlm4[preq->rq_proc];
          pstat_req->nb_nlm4_req += 1;
          break;
        default:
          /* Bad vers ? */
          LogCrit(COMPONENT_DISPATCH,
               "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %d",
               __FILE__, __LINE__);
          return;
        }
    }
#endif
#ifdef _USE_RQUOTA
  else if(preq->rq_prog == nfs_param.core_param.program[P_RQUOTA])
    {
      switch (preq->rq_vers)
        {
        case RQUOTAVERS:
          pitem = &pstat_req->stat_req_rquota1[preq->rq_proc];
          pstat_req->nb_rquota1_req += 1;
          break;
        case EXT_RQUOTAVERS:
          pitem = &pstat_req->stat_req_rquota2[preq->rq_proc];
          pstat_req->nb_rquota2_req += 1;
          break;
        default:
          /* Bad vers ? */
          LogCrit(COMPONENT_DISPATCH,
               "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %d",
               __FILE__, __LINE__);
          return;
        }

    }
#endif
  else
    {
      /* Bad program ? */
      LogCrit(COMPONENT_DISPATCH,
           "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %d",
           __FILE__, __LINE__);
      return;
    }

  if(pitem->total == 0)
    {
      /* Set the initial value of latencies */
      pitem->max_latency = latency;
      pitem->min_latency = latency;
    }

  /* Update total, min and max latency */
  pitem->tot_latency += latency;

  if(latency > pitem->max_latency)
    {
      pitem->max_latency = latency;
    }
  else if(latency < pitem->min_latency)
    {
      pitem->min_latency = latency;
    }

#ifdef _USE_QUEUE_TIMER
  pitem->tot_await_time += await_time;
#endif

  pitem->total += 1;

  if(type == GANESHA_STAT_SUCCESS)
    pitem->success += 1;
  else
    pitem->dropped += 1;

  return;

}                               /* nfs_stat_update */
