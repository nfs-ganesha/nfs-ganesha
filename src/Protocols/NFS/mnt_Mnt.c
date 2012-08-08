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
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 07:29:11 $
 * \version $Revision: 1.18 $
 * \brief   MOUNTPROC_MNT for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_EXPORT in V1, V3.
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

/* Just define a static initialized to zero set of credentials */
struct user_cred mnt_user_credentials;

/**
 * @brief The Mount proc mount function, for all versions.
 *
 * The MOUNT proc proc function, for all versions.
 *
 * @param[in]  parg     The export path to be mounted
 * @param[in]  pexport  The export list
 * @param[in]  pcontext ignored
 * @param[in]  pworker  ignored
 * @param[in]  preq     ignored
 * @param[out] pres     Result structure.
 *
 */
int mnt_Mnt(nfs_arg_t *parg,
            exportlist_t *pexport,
            fsal_op_context_t *pcontext,
            nfs_worker_data_t *pworker_data,
            struct svc_req *preq,
            nfs_res_t *pres)
{
  exportlist_t         * p_current_item = NULL;
  struct glist_head    * glist;
  fsal_handle_t          pfsal_handle;
  fsal_status_t          fsal_status;
  cache_inode_status_t   cache_inode_status;
  int                    auth_flavor[NB_AUTH_FLAVOR];
  int                    index_auth = 0;
  int                    i = 0;
  char                   MountPath[MAXPATHLEN+2];
  char                 * hostname;
  fsal_path_t            fsal_path;
  unsigned int           bytag = FALSE;
  fsal_op_context_t      context;
  char                 * ListPath;
  char                   dumpfh[1024];
  export_perms_t         export_perms;

  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Mnt path=%s",
           parg->arg_mnt);

  /* Paranoid command to clean the result struct. */
  memset(pres, 0, sizeof(nfs_res_t));

  if(parg->arg_mnt == NULL)
    {
      LogEvent(COMPONENT_NFSPROTO,
              "MOUNT: NULL path passed as Mount argument !!!");
      return NFS_REQ_DROP;
    }

  /* Make sure that the argument from MNT ends with a '/', if not adds one */
  if(parg->arg_mnt[strlen(parg->arg_mnt) - 1] == '/')
    strncpy(MountPath, parg->arg_mnt, MAXPATHLEN + 1);
  else
    snprintf(MountPath, MAXPATHLEN+2, "%s/", parg->arg_mnt);

  /*
   * Find the export for the dirname (using as well Path or Tag ) 
   */
  glist_for_each(glist, nfs_param.pexportlist)
    {
      p_current_item = glist_entry(glist, exportlist_t, exp_list);

      if(MountPath[0] != '/')
        {
          /* The input value may be a "Tag" */
          if(!strcmp(MountPath, p_current_item->FS_tag))
            {
              LogDebug(COMPONENT_NFSPROTO,
                       "MOUNT: Mount matched Tag %s for Path %s, export_id=%u",
                       p_current_item->FS_tag,
                       p_current_item->fullpath,
                       p_current_item->id);
              ListPath = p_current_item->fullpath;
              bytag = TRUE;
              break;
            }
        }
      else
        {
          ListPath = MountPath;

          /* Is MountPath a subdirectory of ExportedPath? */
          if(!strncmp(p_current_item->fullpath, MountPath, strlen(p_current_item->fullpath)))
            {
              LogDebug(COMPONENT_NFSPROTO,
                       "MOUNT: Mount matched Path %s, export_id=%u",
                       p_current_item->fullpath,
                       p_current_item->id);
              break;
            }
        }
    }

  /* if p_current_item is not null,
   * it points to the asked export entry.
   */

  if(!p_current_item)
    {
      /* No export found, return ACCESS error. */
      LogEvent(COMPONENT_NFSPROTO,
               "MOUNT: Export entry for path %s not found",
               MountPath);

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

  /* Check access based on client. Don't bother checking TCP/UDP as some
   * clients use UDP for MOUNT even when they will use TCP for NFS.
   */
  nfs_export_check_access(&pworker_data->hostaddr,
                          p_current_item,
                          &export_perms);

  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      if((export_perms.options & EXPORT_OPTION_NFSV2) != 0)
        break;
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Export entry %s does not support NFS v2 for client %s",
              p_current_item->fullpath, pworker_data->hostaddr_str);
      pres->res_mnt1.status = NFSERR_ACCES;
      return NFS_REQ_OK;

    case MOUNT_V3:
      if((export_perms.options & EXPORT_OPTION_NFSV3) != 0)
        break;
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Export entry %s does not support NFS v3 forclient %s",
              p_current_item->fullpath, pworker_data->hostaddr_str);
      pres->res_mnt3.fhs_status = MNT3ERR_ACCES;
      return NFS_REQ_OK;
    }

  /*
   * retrieve the associated NFS handle
   */

  pfsal_handle = *p_current_item->proot_handle;

  if(!bytag && (strcmp(MountPath, p_current_item->fullpath) != 0))
    {
      /* If mounting a subdirectory, fetch the handle for the exported directory */

      /* First convert MountPath into an FSAL path */
      fsal_status = FSAL_str2path(MountPath, MAXPATHLEN, &fsal_path);

      if(FSAL_IS_ERROR(fsal_status))
        {
          cache_inode_status = cache_inode_error_convert(fsal_status);
          LogEvent(COMPONENT_NFSPROTO,
                   "MOUNT: path %s error %s",
                   MountPath,
                   cache_inode_err_str(cache_inode_status));
          switch (preq->rq_vers)
            {
            case MOUNT_V1:
              pres->res_mnt1.status = nfs2_Errno(cache_inode_status);
              break;

            case MOUNT_V3:
              pres->res_mnt3.fhs_status = nfs3_Errno(cache_inode_status);
              break;
            }
          return NFS_REQ_OK;
        }

      /* Next build an fsal_op_context_t for this export using fake root credentials.
       * Per NFS Illusttrated, mount request need not be authenticated.
       */
      if(nfs_build_fsal_context(preq,
                                p_current_item,
                                &context,
                                &mnt_user_credentials) == FALSE)
        {
          LogCrit(COMPONENT_NFSPROTO,
                   "MOUNT: Could not build FSAL context for mount %s",
                   MountPath);

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

      /* And finally try and lookup the handle */
      fsal_status = FSAL_lookupPath(&fsal_path, &context, &pfsal_handle, NULL);

      if(FSAL_IS_ERROR(fsal_status))
        {
          cache_inode_status = cache_inode_error_convert(fsal_status);
          LogEvent(COMPONENT_NFSPROTO,
                   "MOUNT: lookup %s error %s",
                   MountPath,
                   cache_inode_err_str(cache_inode_status));
          switch (preq->rq_vers)
            {
            case MOUNT_V1:
              pres->res_mnt1.status = nfs2_Errno(cache_inode_status);
              break;

            case MOUNT_V3:
              pres->res_mnt3.fhs_status = nfs3_Errno(cache_inode_status);
              break;
            }
          return NFS_REQ_OK;
        }

    }
  /* convert the fsal_handle to a file handle */
  switch (preq->rq_vers)
    {
    case MOUNT_V1:
      if(!nfs2_FSALToFhandle(&(pres->res_mnt1.fhstatus2_u.directory),
                             &pfsal_handle,
                             p_current_item))
        {
          pres->res_mnt1.status = NFSERR_IO;
        }
      else
        {
          if(isDebug(COMPONENT_NFSPROTO))
            sprint_fhandle2(dumpfh,
                            (fhandle2 *) &pres->res_mnt1.fhstatus2_u.directory);
          pres->res_mnt1.status = NFS_OK;
        }
      break;

    case MOUNT_V3:
      pres->res_mnt3.fhs_status =
	      nfs3_AllocateFH((nfs_fh3 *) &pres->res_mnt3.mountres3_u.mountinfo.fhandle);
      if(pres->res_mnt3.fhs_status == MNT3_OK)
        {
          if(!nfs3_FSALToFhandle
             ((nfs_fh3 *) & (pres->res_mnt3.mountres3_u.mountinfo.fhandle), &pfsal_handle,
              p_current_item))
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
               index_auth, dumpfh, pworker_data->hostaddr_str);

#define RES_MOUNTINFO pres->res_mnt3.mountres3_u.mountinfo
      if((RES_MOUNTINFO.auth_flavors.auth_flavors_val =
          gsh_calloc(index_auth, sizeof(int))) == NULL)
        return NFS_REQ_DROP;

      RES_MOUNTINFO.auth_flavors.auth_flavors_len = index_auth;
      for(i = 0; i < index_auth; i++)
        RES_MOUNTINFO.auth_flavors.auth_flavors_val[i] = auth_flavor[i];
    }

  /* Add the client to the mount list */
  if(preq->rq_cred.oa_flavor == AUTH_SYS)
    {
      hostname = ((struct authunix_parms *)(preq->rq_clntcred))->aup_machname;
    }
  else
    {
      hostname = pworker_data->hostaddr_str;
    }

  if(!nfs_Add_MountList_Entry(hostname, ListPath))
    {
      LogInfo(COMPONENT_NFSPROTO,
              "MOUNT: Error when adding entry (%s,%s) to the mount list, Mount command will be successfull anyway",
              hostname, MountPath);
    }

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
