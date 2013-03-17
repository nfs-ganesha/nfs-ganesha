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

  char exportPath[MNTPATHLEN + 1];
  exportlist_t *p_current_item;

  struct fsal_obj_handle *pfsal_handle;
  struct fsal_export *exp_hdl;
  int auth_flavor[NB_AUTH_FLAVOR];
  int index_auth = 0;
  int i = 0;

  char exported_path[MAXPATHLEN];
  char tmplist_path[MAXPATHLEN];
  char tmpexport_path[MAXPATHLEN];
  char *hostname;
  bool bytag = false;

  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Mnt path=%s",
           parg->arg_mnt);

  /* Paranoid command to clean the result struct. */
  memset(pres, 0, sizeof(nfs_res_t));

  if(parg->arg_mnt == NULL)
    {
      LogCrit(COMPONENT_NFSPROTO,
              "MOUNT: NULL path passed as Mount argument !!!");
      return NFS_REQ_DROP;
    }

  /* Retrieving arguments */
  strncpy(exportPath, parg->arg_mnt, MNTPATHLEN + 1);

  /*
   * Find the export for the dirname (using as well Path or Tag ) 
   */
  for(p_current_item = pexport; p_current_item != NULL;
      p_current_item = p_current_item->next)
    {
      if(exportPath[0] != '/')
        {
          /* The input value may be a "Tag" */
          if(!strcmp(exportPath, p_current_item->FS_tag))
            {
              strncpy(exported_path, p_current_item->fullpath, MAXPATHLEN);
              bytag = true;
              break;
            }
        }
      else
        {
          /* Make sure the path in export entry ends with a '/', if not adds one */
          if(p_current_item->fullpath[strlen(p_current_item->fullpath) - 1] == '/')
            strncpy(tmplist_path, p_current_item->fullpath, MAXPATHLEN);
          else
            snprintf(tmplist_path, MAXPATHLEN, "%s/", p_current_item->fullpath);

          /* Make sure that the argument from MNT ends with a '/', if not adds one */
          if(exportPath[strlen(exportPath) - 1] == '/')
            strncpy(tmpexport_path, exportPath, MAXPATHLEN);
          else
            snprintf(tmpexport_path, MAXPATHLEN, "%s/", exportPath);

          /* Is tmplist_path a subdirectory of tmpexport_path ? */
          if(!strncmp(tmplist_path, tmpexport_path, strlen(tmplist_path)))
            {
              strncpy(exported_path, p_current_item->fullpath, MAXPATHLEN);
              break;
            }
        }
    }

  /* if p_current_item is not null,
   * it points to the asked export entry.
   */

  if(!p_current_item)
    {
      LogCrit(COMPONENT_NFSPROTO, "MOUNT: Export entry %s not found",
              exportPath);

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
      return NFS_REQ_OK;
    }

  LogDebug(COMPONENT_NFSPROTO,
           "MOUNT: Export entry Path=%s Tag=%s matches %s, export_id=%u",
           exported_path, p_current_item->FS_tag, exportPath,
           p_current_item->id);

  /* @todo : check wether mount is allowed.
   *  to do so, retrieve client identifier from the credential.
   */

  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      pres->res_mnt1.status = NFSERR_ACCES;
      return NFS_REQ_OK;

    case MOUNT_V3:
      if((p_current_item->options & EXPORT_OPTION_NFSV3) != 0)
        break;
      pres->res_mnt3.fhs_status = MNT3ERR_ACCES;
      return NFS_REQ_OK;
    }
  

  /*
   * retrieve the associated NFS handle
   */
  if(!(bytag || !strncmp(tmpexport_path, tmplist_path, MAXPATHLEN)))
    {
      exp_hdl = p_current_item->export_hdl;
      LogEvent(COMPONENT_NFSPROTO,
               "MOUNT: Performance warning: Export entry is not cached");
      if(FSAL_IS_ERROR(exp_hdl->ops->lookup_path(exp_hdl, req_ctx,
						 tmpexport_path,
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
          return NFS_REQ_OK;
        }
    }
  else
    {
      pfsal_handle = p_current_item->proot_handle;
    }
  /* convert the fsal_handle to a file handle */
  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      pres->res_mnt1.status = NFSERR_ACCES;
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
              pres->res_mnt3.fhs_status = MNT3_OK;

              /* Auth et nfs_SetPostOpAttr ici */
            }
        }

      break;
    }

  /* Return the supported authentication flavor in V3 */
  if(preq->rq_vers == MOUNT_V3)
    {
      if(p_current_item->options & EXPORT_OPTION_AUTH_NONE)
        auth_flavor[index_auth++] = AUTH_NONE;
      if(p_current_item->options & EXPORT_OPTION_AUTH_UNIX)
        auth_flavor[index_auth++] = AUTH_UNIX;
#ifdef _HAVE_GSSAPI
      if(nfs_param.krb5_param.active_krb5)
        {
	  if(p_current_item->options & EXPORT_OPTION_RPCSEC_GSS_NONE)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_NONE;
	  if(p_current_item->options & EXPORT_OPTION_RPCSEC_GSS_INTG)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_INTEGRITY;
	  if(p_current_item->options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
	    auth_flavor[index_auth++] = MNT_RPC_GSS_PRIVACY;
        }
#endif

      LogDebug(COMPONENT_NFSPROTO,
               "MOUNT: Entry support %d different flavours", index_auth);

#define RES_MOUNTINFO pres->res_mnt3.mountres3_u.mountinfo
      if((RES_MOUNTINFO.auth_flavors.auth_flavors_val =
          gsh_calloc(index_auth, sizeof(int))) == NULL)
        return NFS_REQ_DROP;

      RES_MOUNTINFO.auth_flavors.auth_flavors_len = index_auth;
      for(i = 0; i < index_auth; i++)
        RES_MOUNTINFO.auth_flavors.auth_flavors_val[i] = auth_flavor[i];
    }

  /* Add the client to the mount list */
  /* @todo: BUGAZOMEU; seul AUTHUNIX est supporte */
  hostname = ((struct authunix_parms *)(preq->rq_clntcred))->aup_machname;

  if(!nfs_Add_MountList_Entry(hostname, exportPath))
    {
      LogCrit(COMPONENT_NFSPROTO,
              "MOUNT: Error when adding entry (%s,%s) to the mount list, Mount command will be successfull anyway",
              hostname, exportPath);
    }
  else
    LogFullDebug(COMPONENT_NFSPROTO,
                 "MOUNT: mount list entry (%s,%s) added", hostname, exportPath);

  return NFS_REQ_OK;

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
