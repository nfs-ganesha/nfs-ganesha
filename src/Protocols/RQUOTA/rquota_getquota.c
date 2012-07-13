/*
 * Copyright CEA/DAM/DIF  2010
 *  Author: Philippe Deniel (philippe.deniel@cea.fr)
 *
 * --------------------------
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
#include <sys/quota.h>          /* For USRQUOTA */
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "rquota.h"
#include "nfs_proto_functions.h"

/**
 * @brief The Rquota getquota function, for all versions.
 *
 * The RQUOTA getquota function, for all versions.
 *
 * @param[in]  parg     Ignored
 * @param[in]  pexport  Ignored
 * @param[in]  pcontext Ignored
 * @param[in]  pworker  Ignored
 * @param[in]  preq     Ignored
 * @param[out] pres     Ignored
 *
 */
int rquota_getquota(nfs_arg_t  *parg,
                    exportlist_t  *pexport,
		    struct req_op_context *req_ctx,
                    nfs_worker_data_t *pworker,
                    struct svc_req *preq,
                    nfs_res_t * pres)
{
  fsal_status_t fsal_status;
  fsal_quota_t fsal_quota;
  int quota_type = USRQUOTA;
  char work[MAXPATHLEN];

  LogFullDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling rquota_getquota");

  if(preq->rq_vers == EXT_RQUOTAVERS)
    {
      quota_type = parg->arg_ext_rquota_getquota.gqa_type;
    }
  else
    {
      quota_type = USRQUOTA;
    }

  if(parg->arg_rquota_getquota.gqa_pathp[0] == '/')
    strncpy(work, parg->arg_rquota_getquota.gqa_pathp, MAXPATHLEN);
  else
    {
      if(nfs_export_tag2path(nfs_param.pexportlist,
                             parg->arg_rquota_getquota.gqa_pathp,
                             strnlen(parg->arg_rquota_getquota.gqa_pathp, MAXPATHLEN),
                             work, MAXPATHLEN) == -1)

        {
          pres->res_rquota_getquota.status = Q_EPERM;
          return NFS_REQ_OK;
        }
    }

  fsal_status = pexport->export_hdl->ops->get_quota(pexport->export_hdl,
						    work,
						    quota_type,
						    req_ctx,
						    &fsal_quota);
  if(FSAL_IS_ERROR(fsal_status))
    {
      if(fsal_status.major == ERR_FSAL_NO_QUOTA)
        pres->res_rquota_getquota.status = Q_NOQUOTA;
      else
        pres->res_rquota_getquota.status = Q_EPERM;
      return NFS_REQ_OK;
    }

  /* success */
  pres->res_rquota_getquota.status = Q_OK;

  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_active = TRUE;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_bsize = fsal_quota.bsize;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_bhardlimit =
      fsal_quota.bhardlimit;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_bsoftlimit =
      fsal_quota.bsoftlimit;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_curblocks =
      fsal_quota.curblocks;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_curfiles = fsal_quota.curfiles;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_fhardlimit =
      fsal_quota.fhardlimit;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_fsoftlimit =
      fsal_quota.fsoftlimit;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_btimeleft =
      fsal_quota.btimeleft;
  pres->res_rquota_getquota.getquota_rslt_u.gqr_rquota.rq_ftimeleft =
      fsal_quota.ftimeleft;

  return NFS_REQ_OK;
}                               /* rquota_getquota */

/**
 * rquota_getquota_Free: Frees the result structure allocated for rquota_getquota
 *
 * Frees the result structure allocated for rquota_getquota. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void rquota_getquota_Free(nfs_res_t * pres)
{
  return;
}
