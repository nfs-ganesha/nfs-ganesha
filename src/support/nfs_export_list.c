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
 * @file  nfs_export_list.c
 * @brief Routines for managing the export list.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <ctype.h> /* for having isalnum */
#include <stdlib.h> /* for having atoi */
#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h> /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"

const char *Rpc_gss_svc_name[] =
    { "no name", "RPCSEC_GSS_SVC_NONE", "RPCSEC_GSS_SVC_INTEGRITY",
  "RPCSEC_GSS_SVC_PRIVACY"
};

/**
 * @brief Gets an export entry from its export id.
 *
 * @todo Exports should really be managed as an object...  exportroot
 * should be a static in this module and the only thing that leaks out
 * to the rest of the code is entries that are created here and
 * searched for here.  this means that the exportroot arg here will
 * disappear at some point, probably when exportlist_t is overhauled
 * to use nlm_list etc.
 *
 * @paran[in] exportroot The root for the export list
 * @param[in] exportid   The id for the entry to be found.
 *
 * @return the pointer to the export list or NULL if failed.
 */

exportlist_t *nfs_Get_export_by_id(exportlist_t * exportroot, unsigned short exportid)
{
  exportlist_t *piter;
  int found = 0;

  for(piter = exportroot; piter != NULL; piter = piter->next)
    {
      if(piter->id == exportid)
        {
          found = 1;
          break;
        }
    }

  if(found == 0)
    return NULL;
  else
    return piter;
}


/**
 * @brief Get numeric credentials from request
 *
 * @todo This MUST be refactored to not use TI-RPC private structures.
 * Instead, export appropriate functions from lib(n)tirpc.
 * 
 * @param[in]  req              Incoming request.
 * @param[in]  pexport          Related export entry
 * @param[out] user_credentials Filled in structure with UID and GIDs
 * 
 * @return true if successful, false otherwise 
 *
 */
bool get_req_uid_gid(struct svc_req *req,
		     exportlist_t * pexport,
		     struct user_cred *user_credentials)
{
  struct authunix_parms *punix_creds = NULL;
#ifdef _HAVE_GSSAPI
  struct svc_rpc_gss_data *gd = NULL;
  char principal[MAXNAMLEN];
#endif

  if (user_credentials == NULL)
    return false;

  switch (req->rq_cred.oa_flavor)
    {
    case AUTH_NONE:
      /* Nothing to be done here... */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Request xid=%u has authentication AUTH_NONE",
                   req->rq_xid);
      user_credentials->caller_uid = pexport->anonymous_uid;
      user_credentials->caller_gid = pexport->anonymous_gid;
      user_credentials->caller_glen = 0;
      user_credentials->caller_garray = NULL;
      break;

    case AUTH_UNIX:
      LogFullDebug(COMPONENT_DISPATCH,
                   "Request xid=%u has authentication AUTH_UNIX",
                   req->rq_xid);
      /* We map the rq_cred to Authunix_parms */
      punix_creds = (struct authunix_parms *) req->rq_clntcred;

      /* Get the uid/gid couple */
      user_credentials->caller_uid = punix_creds->aup_uid;
      user_credentials->caller_gid = punix_creds->aup_gid;
      user_credentials->caller_glen = punix_creds->aup_len;
      user_credentials->caller_garray = punix_creds->aup_gids;

      LogFullDebug(COMPONENT_DISPATCH, "----> Uid=%u Gid=%u",
                   (unsigned int)user_credentials->caller_uid,
                   (unsigned int)user_credentials->caller_gid);

      break;

#ifdef _HAVE_GSSAPI
    case RPCSEC_GSS:
      LogFullDebug(COMPONENT_DISPATCH,
                   "Request xid=%u has authentication RPCSEC_GSS",
                   req->rq_xid);
      /* Get the gss data to process them */
      gd = SVCAUTH_PRIVATE(req->rq_auth);

      if(isFullDebug(COMPONENT_RPCSEC_GSS))
        {
          OM_uint32 maj_stat = 0;
          OM_uint32 min_stat = 0;
	  char ptr[256];

          gss_buffer_desc oidbuff;

          LogFullDebug(COMPONENT_RPCSEC_GSS,
                       "----> RPCSEC_GSS svc=%u RPCSEC_GSS_SVC_NONE=%u "
                       "RPCSEC_GSS_SVC_INTEGRITY=%u RPCSEC_GSS_SVC_PRIVACY=%u",
                       gd->sec.svc, RPCSEC_GSS_SVC_NONE,
                       RPCSEC_GSS_SVC_INTEGRITY,
                       RPCSEC_GSS_SVC_PRIVACY);

          memcpy(&ptr, (void *)gd->ctx + 4, 4);
          LogFullDebug(COMPONENT_RPCSEC_GSS,
                       "----> Client=%s length=%lu  Qop=%u established=%u "
                       "gss_ctx_id=%p|%p",
                       (char *)gd->cname.value, gd->cname.length,
                       gd->established, gd->sec.qop,
                       gd->ctx, ptr);

          if((maj_stat = gss_oid_to_str(
                  &min_stat, gd->sec.mech, &oidbuff)) != GSS_S_COMPLETE)
            {
              LogFullDebug(COMPONENT_DISPATCH, "Error in gss_oid_to_str: %u|%u",
                           maj_stat, min_stat);
            }
          else
            {
              LogFullDebug(COMPONENT_RPCSEC_GSS, "----> Client mech=%s len=%lu",
                           (char *)oidbuff.value, oidbuff.length);

              /* Release the string */
              (void)gss_release_buffer(&min_stat, &oidbuff); 
            }
       }

      LogFullDebug(COMPONENT_RPCSEC_GSS, "Mapping principal %s to uid/gid",
                   (char *)gd->cname.value);

      memcpy(principal, gd->cname.value, gd->cname.length);
      principal[gd->cname.length] = 0;

      /* Convert to uid */
#if _MSPAC_SUPPORT
      if(!principal2uid(principal, &user_credentials->caller_uid, gd))
#else
      if(!principal2uid(principal, &user_credentials->caller_uid))
#endif
	{
          LogWarn(COMPONENT_IDMAPPER,
		  "WARNING: Could not map principal to uid; mapping principal "
		  "to anonymous uid/gid");

	  /* For compatibility with Linux knfsd, we set the uid/gid
	   * to anonymous when a name->uid mapping can't be found. */
	  user_credentials->caller_uid = pexport->anonymous_uid;
	  user_credentials->caller_gid = pexport->anonymous_gid;
	  
	  /* No alternate groups for "nobody" */
	  user_credentials->caller_glen = 0 ;
	  user_credentials->caller_garray = NULL ;

	  return true;
	}

      if(uidgidmap_get(user_credentials->caller_uid,
                       &user_credentials->caller_gid) != ID_MAPPER_SUCCESS)
        {
          LogMajor(COMPONENT_DISPATCH,
                   "FAILURE: Could not resolve uidgid map for %u",
                   user_credentials->caller_uid);
          user_credentials->caller_gid = -1;
        }
      LogFullDebug(COMPONENT_DISPATCH, "----> Uid=%u Gid=%u",
                   (unsigned int)user_credentials->caller_uid,
                   (unsigned int)user_credentials->caller_gid);
      user_credentials->caller_glen = 0;
      user_credentials->caller_garray = 0;

      break;
#endif                          /* _USE_GSSRPC */

    default:
      LogFullDebug(COMPONENT_DISPATCH,
                   "FAILURE: Request xid=%u, has unsupported authentication %d",
                   req->rq_xid, req->rq_cred.oa_flavor);
      /* Reject the request for weak authentication and return to worker */
      return false;

      break;
    }
  return true;
}

int nfs_check_anon(exportlist_client_entry_t * pexport_client,
                   exportlist_t * pexport,
                   struct user_cred *user_credentials)
{
  if (user_credentials == NULL)
    return false;

  /* Do we have root access ? */
  /* Are we squashing _all_ users to the anonymous uid/gid ? */
  if( ((user_credentials->caller_uid == 0)
       && !(pexport_client->options & EXPORT_OPTION_ROOT))
      || pexport->all_anonymous == true)
    {
      user_credentials->caller_uid = pexport->anonymous_uid;
      user_credentials->caller_gid = pexport->anonymous_gid;

      /* No alternate groups for "nobody" */
      user_credentials->caller_glen = 0 ;
      user_credentials->caller_garray = NULL ;
    }

  return true;
}


/**
 * @brief Compares two RPC creds
 *
 * @param[in] pcred1 First RPC cred
 * @param[in] pcred2 Second RPC cred
 * 
 * @return true if same, false otherwise
 */
bool nfs_compare_clientcred(nfs_client_cred_t * pcred1,
			    nfs_client_cred_t *pcred2)
{
  if(pcred1 == NULL)
    return false;
  if(pcred2 == NULL)
    return false;

  if(pcred1->flavor != pcred2->flavor)
    return false;

  if(pcred1->length != pcred2->length)
    return false;

  switch (pcred1->flavor)
    {
    case AUTH_UNIX:
      if(pcred1->auth_union.auth_unix.aup_uid !=
         pcred2->auth_union.auth_unix.aup_uid)
        return false;
      if(pcred1->auth_union.auth_unix.aup_gid !=
         pcred2->auth_union.auth_unix.aup_gid)
        return false;
      if(pcred1->auth_union.auth_unix.aup_time !=
         pcred2->auth_union.auth_unix.aup_time)
        return false;
      break;

    default:
      if(memcmp(&pcred1->auth_union, &pcred2->auth_union, pcred1->length))
        return false;
      break;
    }

  /* If this point is reach, structures are the same */
  return true;
}

int nfs_rpc_req2client_cred(struct svc_req *reqp, nfs_client_cred_t * pcred)
{
  /* Structure for managing basic AUTH_UNIX authentication */
  struct authunix_parms *aup = NULL;

  /* Stuff needed for managing RPCSEC_GSS */
#ifdef _HAVE_GSSAPI
  OM_uint32 maj_stat = 0;
  OM_uint32 min_stat = 0;
  struct svc_rpc_gss_data *gd = NULL;
  gss_buffer_desc oidbuff;
#endif

  if(reqp == NULL || pcred == NULL)
    return -1;

  pcred->flavor = reqp->rq_cred.oa_flavor;
  pcred->length = reqp->rq_cred.oa_length;

  switch (reqp->rq_cred.oa_flavor)
    {
    case AUTH_NONE:
      /* Do nothing... because there seems like nothing is to be done... */
      break;

    case AUTH_UNIX:
      aup = (struct authunix_parms *)(reqp->rq_clntcred);

      pcred->auth_union.auth_unix.aup_uid = aup->aup_uid;
      pcred->auth_union.auth_unix.aup_gid = aup->aup_gid;
      pcred->auth_union.auth_unix.aup_time = aup->aup_time;

      break;

#ifdef _HAVE_GSSAPI
    case RPCSEC_GSS:
      /* Extract the information from the RPCSEC_GSS opaque structure */
      gd = SVCAUTH_PRIVATE(reqp->rq_auth);

      pcred->auth_union.auth_gss.svc = (unsigned int)(gd->sec.svc);
      pcred->auth_union.auth_gss.qop = (unsigned int)(gd->sec.qop);
      pcred->auth_union.auth_gss.gss_context_id = gd->ctx;

      /* XXX */
      strncpy(pcred->auth_union.auth_gss.cname, gd->cname.value,
              NFS_CLIENT_NAME_LEN);

      if((maj_stat = gss_oid_to_str(
              &min_stat, gd->sec.mech, &oidbuff)) != GSS_S_COMPLETE)
        {
          char errbuff[1024];
          log_sperror_gss(errbuff, maj_stat, min_stat);
          LogCrit(COMPONENT_DISPATCH,
                  "GSSAPI ERROR: %u|%u = %s",
                  maj_stat, min_stat, errbuff);
          return -1;
        }

      /* XXX */
      strncpy(pcred->auth_union.auth_gss.stroid, oidbuff.value,
              NFS_CLIENT_NAME_LEN);

      /* Je fais le menage derriere moi */
      (void)gss_release_buffer(&min_stat, &oidbuff);
      break;
#endif

    default:
      /* Unsupported authentication flavour */
      return -1;
      break;
    }

  return 1;
}

int nfs_export_tag2path(exportlist_t * exportroot, char *tag, int taglen,
                        char *path, int pathlen)
{
  if(!tag || !path)
    return -1;

  exportlist_t *piter;

  for(piter = exportroot; piter != NULL; piter = piter->next)
    {
      if(!strncmp(tag, piter->FS_tag, taglen))
        {
          strncpy(path, piter->fullpath, pathlen);
          return 0;
          break;
        }
    }

  return -1;
}
