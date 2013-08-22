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
 * \file    mnt_Mnt.c
 * \date    $Date: 2006/01/18 07:29:11 $
 * \version $Revision: 1.18 $
 * \brief   MOUNTPROC_MNT for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_EXPORT in V1, V3.
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashTable.h"
#include "log.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "client_mgr.h"
#include "export_mgr.h"

/**
 * @brief The Mount proc mount function, for all versions.
 *
 * The MOUNT proc proc function, for all versions.
 *
 * @param[in]  parg     The export path to be mounted
 * @param[in]  pexport  The export list
 * @param[in]  req_ctx  ignored
 * @param[in]  pworker  ignored
 * @param[in]  preq     ignored
 * @param[out] pres     Result structure.
 *
 */

int mnt_Mnt(nfs_arg_t *parg,
            exportlist_t *pexport,
	    struct req_op_context *req_ctx,
            nfs_worker_data_t *pworker,
            struct svc_req *preq,
            nfs_res_t *pres)
{

  exportlist_t *p_current_item = NULL;
  struct gsh_export *exp = NULL;
  struct fsal_obj_handle *pfsal_handle;
  struct fsal_export *exp_hdl;
  int auth_flavor[NB_AUTH_FLAVOR];
  int index_auth = 0;
  int i = 0;
  char *hostname;
  char dumpfh[1024];
  export_perms_t export_perms;
  int retval = NFS_REQ_OK;

  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Mnt path=%s",
           parg->arg_mnt);

  /* Paranoid command to clean the result struct. */
  memset(pres, 0, sizeof(nfs_res_t));

  if(parg->arg_mnt == NULL)
    {
      LogCrit(COMPONENT_NFSPROTO,
              "NULL path passed as Mount argument !!!");
      retval = NFS_REQ_DROP;
      goto out;
    }

  /* If the path ends with a '/', get rid if it should it be a while()?? */
  if(parg->arg_mnt[strlen(parg->arg_mnt) - 1] == '/')
    parg->arg_mnt[strlen(parg->arg_mnt) - 1] = '\0';

  /*
   * Find the export for the dirname (using as well Path or Tag ) 
   */
  if(parg->arg_mnt[0] == '/')
    exp = get_gsh_export_by_path(parg->arg_mnt);
  else
    exp = get_gsh_export_by_tag(parg->arg_mnt);

  if(exp == NULL)
    {
      /* No export found, return ACCESS error. */
      LogEvent(COMPONENT_NFSPROTO,
               "MOUNT: Export entry for %s not found",
               parg->arg_mnt);

      /* entry not found. */
      /* @todo : not MNT3ERR_NOENT => ok */
      switch (preq->rq_vers)
        {
        case MOUNT_V1:
          pres->res_mnt1.status = NFSERR_ACCES;
          break;

        case MOUNT_V3:
          pres->res_mnt3.fhs_status = MNT3ERR_ACCES;
          break;
        }
      goto out;
    }
  p_current_item = &exp->export;
  /* Check access based on client. Don't bother checking TCP/UDP as some
   * clients use UDP for MOUNT even when they will use TCP for NFS.
   */
  nfs_export_check_access(req_ctx->caller_addr,
                          p_current_item,
                          &export_perms);

  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      if((export_perms.options & EXPORT_OPTION_NFSV2) != 0)
        break;
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Export entry %s does not support NFS v2 for client %s",
              p_current_item->fullpath,
	      req_ctx->client->hostaddr_str);
      pres->res_mnt1.status = NFSERR_ACCES;
      goto out;

    case MOUNT_V3:
      if((export_perms.options & EXPORT_OPTION_NFSV3) != 0)
        break;
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Export entry %s does not support NFS v3 for client %s",
              p_current_item->fullpath, req_ctx->client->hostaddr_str);
      pres->res_mnt3.fhs_status = MNT3ERR_ACCES;
      goto out;
    }

  /*
   * retrieve the associated NFS handle
   */
  if(parg->arg_mnt[0] != '/' || !strcmp(parg->arg_mnt, p_current_item->fullpath))
    {
      pfsal_handle = p_current_item->exp_root_cache_inode->obj_handle;
    }
  else
    {
      exp_hdl = p_current_item->export_hdl;
      LogEvent(COMPONENT_NFSPROTO,
               "MOUNT: Performance warning: Export entry is not cached");
      if(FSAL_IS_ERROR(exp_hdl->ops->lookup_path(exp_hdl, req_ctx,
						 parg->arg_mnt,
						 &pfsal_handle)))
        {
          switch (preq->rq_vers)
            {
            case MOUNT_V1:
              pres->res_mnt1.status = NFSERR_ACCES;
              break;

            case MOUNT_V3:
              pres->res_mnt3.fhs_status = MNT3ERR_ACCES;
              break;
            }
          goto out;
        }
    }
  /* convert the fsal_handle to a file handle */
  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      if(!nfs2_FSALToFhandle(&(pres->res_mnt1.fhstatus2_u.directory),
                             pfsal_handle))
        {
          pres->res_mnt1.status = NFSERR_IO;
        }
      else
        {
          pres->res_mnt1.status = NFS_OK;
        }
      break;

    case MOUNT_V3:
/* FIXME: The mountinfo.fhandle definition is an overlay on/of nfs_fh3.
 * redefine and eliminate one or the other.
 */
      pres->res_mnt3.fhs_status =
	      nfs3_AllocateFH((nfs_fh3 *) &pres->res_mnt3.mountres3_u.mountinfo.fhandle);
      if(pres->res_mnt3.fhs_status ==  MNT3_OK)
        {
          if(!nfs3_FSALToFhandle
             ((nfs_fh3 *) &(pres->res_mnt3.mountres3_u.mountinfo.fhandle),
	      pfsal_handle))
            {
              pres->res_mnt3.fhs_status = MNT3ERR_INVAL;
            }
          else
            {
              if(isDebug(COMPONENT_NFSPROTO))
                sprint_fhandle3(dumpfh,
                                (nfs_fh3 *) &pres->res_mnt3.mountres3_u.mountinfo.fhandle);
              pres->res_mnt3.fhs_status = MNT3_OK;
            }
        }

      break;
    }

  /* Return the supported authentication flavor in V3 based
   * on the client's export permissions.
   */
  if(preq->rq_vers == MOUNT_V3)
    {
      if(export_perms.options & EXPORT_OPTION_AUTH_NONE)
        auth_flavor[index_auth++] = AUTH_NONE;
      if(export_perms.options & EXPORT_OPTION_AUTH_UNIX)
        auth_flavor[index_auth++] = AUTH_UNIX;
#ifdef _HAVE_GSSAPI
      if(nfs_param.krb5_param.active_krb5 == TRUE)
        {
	  if(export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_NONE;
	  if(export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_INTEGRITY;
	  if(export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_PRIVACY;
        }
#endif

      LogDebug(COMPONENT_NFSPROTO,
               "MOUNT: Entry supports %d different flavours handle=%s for client %s",
               index_auth, dumpfh, req_ctx->client->hostaddr_str);

      mountres3_ok *const RES_MOUNTINFO = &pres->res_mnt3.mountres3_u.mountinfo;
      if((RES_MOUNTINFO->auth_flavors.auth_flavors_val =
          gsh_calloc(index_auth, sizeof(int))) == NULL)
        return NFS_REQ_DROP;

      RES_MOUNTINFO->auth_flavors.auth_flavors_len = index_auth;
      for(i = 0; i < index_auth; i++)
        RES_MOUNTINFO->auth_flavors.auth_flavors_val[i] = auth_flavor[i];
    }

  /* Add the client to the mount list */
  if(preq->rq_cred.oa_flavor == AUTH_SYS)
    {
      hostname = ((struct authunix_parms *)(preq->rq_clntcred))->aup_machname;
    }
  else
    {
      hostname = req_ctx->client->hostaddr_str;
    }

  if(!nfs_Add_MountList_Entry(hostname, parg->arg_mnt))
    {
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Error when adding entry (%s,%s) to the mount list,"
	      " Mount command will be successfull anyway",
              hostname, parg->arg_mnt);
    }
out:
  if(exp != NULL)
    put_gsh_export(exp);
  return retval;

}                               /* mnt_Mnt */

/**
 * mnt_Mnt_Free: Frees the result structure allocated for mnt_Mnt.
 * 
 * Frees the result structure allocated for mnt_Mnt.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */

void mnt1_Mnt_Free(nfs_res_t * pres)
{
  return;
}                               /* mnt_Mnt_Free */

void mnt3_Mnt_Free(nfs_res_t * pres)
{
  if(pres->res_mnt3.fhs_status == MNT3_OK)
    {
      gsh_free(pres->res_mnt3.mountres3_u.mountinfo.
               auth_flavors.auth_flavors_val);
      gsh_free(pres->res_mnt3.mountres3_u.mountinfo.fhandle.fhandle3_val);
    }
  return;
}                               /* mnt_Mnt_Free */
