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

#include "config.h"
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
#include "idmapper.h"
#include "export_mgr.h"

const char *Rpc_gss_svc_name[] =
    { "no name", "RPCSEC_GSS_SVC_NONE", "RPCSEC_GSS_SVC_INTEGRITY",
  "RPCSEC_GSS_SVC_PRIVACY"
};

/**
 * @brief Get numeric credentials from request
 *
 * @todo This MUST be refactored to not use TI-RPC private structures.
 * Instead, export appropriate functions from lib(n)tirpc.
 * 
 * @param[in]  req              Incoming request.
 * @param[out] user_credentials Filled in structure with UID and GIDs
 * 
 * @return true if successful, false otherwise 
 *
 */
bool get_req_uid_gid(struct svc_req *req,
		     struct user_cred *user_credentials)
{
  struct authunix_parms *punix_creds = NULL;
#ifdef _HAVE_GSSAPI
  struct svc_rpc_gss_data *gd = NULL;
  char principal[MAXNAMLEN+1];
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
      user_credentials->caller_flags |= USER_CRED_ANONYMOUS;
      break;

    case AUTH_UNIX:
      /* We map the rq_cred to Authunix_parms */
      punix_creds = (struct authunix_parms *) req->rq_clntcred;

      LogFullDebug(COMPONENT_DISPATCH,
                   "Request xid=%u has authentication AUTH_UNIX, uid=%d, gid=%d",
                   req->rq_xid,
                   (int)punix_creds->aup_uid,
                   (int)punix_creds->aup_gid);

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
      if(user_credentials->caller_flags & USER_CRED_GSS_PROCESSED)
        {
          /* Only process credentials once. */
          LogFullDebug(COMPONENT_DISPATCH,
                       "Request xid=%u has authentication RPCSEC_GSS, uid=%d, gid=%d",
                       req->rq_xid,
                       user_credentials->caller_uid,
                       user_credentials->caller_gid);
          break;
        }

      user_credentials->caller_flags |= USER_CRED_GSS_PROCESSED;
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
              LogFullDebug(COMPONENT_DISPATCH,
			   "Error in gss_oid_to_str: %u|%u",
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
      if(!principal2uid(principal, &user_credentials->caller_uid, &user_credentials->caller_gid, gd))
#else
      if(!principal2uid(principal, &user_credentials->caller_uid, &user_credentials->caller_gid))
#endif
	{
          LogWarn(COMPONENT_IDMAPPER,
		  "WARNING: Could not map principal to uid; mapping principal "
		  "to anonymous uid/gid");

	  /* For compatibility with Linux knfsd, we set the uid/gid
	   * to anonymous when a name->uid mapping can't be found. */
	  user_credentials->caller_flags |= USER_CRED_ANONYMOUS;

	  return true;
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

void nfs_check_anon(export_perms_t * pexport_perms,
                    exportlist_t * pexport,
                    struct user_cred *user_credentials)
{
  /* Do we need to revert? */
  if(user_credentials->caller_flags & USER_CRED_SAVED)
    {
      user_credentials->caller_uid  = user_credentials->caller_uid_saved;
      user_credentials->caller_gid  = user_credentials->caller_gid_saved;
      user_credentials->caller_glen = user_credentials->caller_glen_saved;
      if(user_credentials->caller_gpos_root < user_credentials->caller_glen_saved)
        user_credentials->caller_garray[user_credentials->caller_gpos_root] = 0;
    }

  /* Do we have root access ? */
  /* Are we squashing _all_ users to the anonymous uid/gid ? */
  if( ((user_credentials->caller_uid == 0)
       && !(pexport_perms->options & EXPORT_OPTION_ROOT))
      || pexport_perms->options & EXPORT_OPTION_ALL_ANONYMOUS
      || ((user_credentials->caller_flags & USER_CRED_ANONYMOUS) != 0))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Anonymizing for export %d caller uid=%d gid=%d to uid=%d gid=%d",
                   pexport->id,
                   user_credentials->caller_uid,
                   user_credentials->caller_gid,
                   pexport_perms->anonymous_uid,
                   pexport_perms->anonymous_gid);

      /* Save old credentials */
      user_credentials->caller_uid_saved  = user_credentials->caller_uid;
      user_credentials->caller_gid_saved  = user_credentials->caller_gid;
      user_credentials->caller_glen_saved = user_credentials->caller_glen;
      user_credentials->caller_gpos_root  = user_credentials->caller_glen + 1;
      user_credentials->caller_flags |= USER_CRED_SAVED;

      /* Map uid and gid to "nobody" */
      user_credentials->caller_uid = pexport_perms->anonymous_uid;
      user_credentials->caller_gid = pexport_perms->anonymous_gid;
      
      /* No alternate groups for "nobody" */
      user_credentials->caller_glen = 0 ;
      user_credentials->caller_garray = NULL ;
    }
  else if ((user_credentials->caller_gid == 0)
       && !(pexport_perms->options & EXPORT_OPTION_ROOT))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Anonymizing for export %d caller uid=%d gid=%d to uid=%d gid=%d",
                   pexport->id,
                   user_credentials->caller_uid,
                   user_credentials->caller_gid,
                   user_credentials->caller_uid,
                   pexport_perms->anonymous_gid);

      /* Save old credentials */
      user_credentials->caller_uid_saved  = user_credentials->caller_uid;
      user_credentials->caller_gid_saved  = user_credentials->caller_gid;
      user_credentials->caller_glen_saved = user_credentials->caller_glen;
      user_credentials->caller_gpos_root  = user_credentials->caller_glen + 1;
      user_credentials->caller_flags |= USER_CRED_SAVED;

      /* Map gid to "nobody" */
      user_credentials->caller_gid = pexport_perms->anonymous_gid;
      
      /* Keep alternate groups, we may squash them below */
    }
  else
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Accepting credentials for export %d caller uid=%d gid=%d",
                   pexport->id,
                   user_credentials->caller_uid,
                   user_credentials->caller_gid);
    }

  /* Check the garray for gid 0 to squash */
  if(!(pexport_perms->options & EXPORT_OPTION_ROOT) &&
     user_credentials->caller_glen > 0)
    {
      unsigned int i;
      for(i = 0; i < user_credentials->caller_glen; i++)
        {
          if(user_credentials->caller_garray[i] == 0)
            {
              if((user_credentials->caller_flags & USER_CRED_SAVED) == 0)
                {
                  /* Save old credentials */
                  user_credentials->caller_uid_saved  = user_credentials->caller_uid;
                  user_credentials->caller_gid_saved  = user_credentials->caller_gid;
                  user_credentials->caller_glen_saved = user_credentials->caller_glen;
                  user_credentials->caller_gpos_root  = user_credentials->caller_glen + 1;
                  user_credentials->caller_flags |= USER_CRED_SAVED;
                }

              /* Save the position of the first instance of root in the garray */
              LogFullDebug(COMPONENT_DISPATCH,
                           "Squashing alternate group #%d to %d",
                           i, pexport_perms->anonymous_gid);
              if(user_credentials->caller_gpos_root >= user_credentials->caller_glen_saved)
                user_credentials->caller_gpos_root = i;
              user_credentials->caller_garray[i] = pexport_perms->anonymous_gid;
            }
        }
    }
}

void squash_setattr(export_perms_t     * pexport_perms,
                    struct user_cred   * user_credentials,
                    struct attrlist * attr)
{
  if(attr->mask & ATTR_OWNER)
    {
      if(pexport_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)
        attr->owner = pexport_perms->anonymous_uid;
      else if(!(pexport_perms->options & EXPORT_OPTION_ROOT) &&
              (attr->owner == 0) &&
              (user_credentials->caller_uid == pexport_perms->anonymous_uid))
        attr->owner = pexport_perms->anonymous_uid;
    }

  if(attr->mask & ATTR_GROUP)
    {
      /* If all squashed, then always squash the owner_group.
       *
       * If root squashed, then squash owner_group if
       * caller_gid has been squashed or one of the caller's
       * alternate groups has been squashed.
       */
      if(pexport_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)
        attr->group = pexport_perms->anonymous_gid;
      else if(!(pexport_perms->options & EXPORT_OPTION_ROOT) &&
              (attr->group == 0) &&
              ((user_credentials->caller_gid == pexport_perms->anonymous_gid) ||
               (((user_credentials->caller_flags & USER_CRED_SAVED) &&
               (user_credentials->caller_gpos_root <
                user_credentials->caller_glen_saved)))))
        attr->group = pexport_perms->anonymous_gid;
    }
}

void init_credentials(struct user_cred *user_credentials)
{
  memset(user_credentials, 0, sizeof(*user_credentials));
  user_credentials->caller_uid = (uid_t) ANON_UID;
  user_credentials->caller_gid = (gid_t) ANON_GID;
}

void clean_credentials(struct user_cred *user_credentials)
{
#ifdef _HAVE_GSSAPI
  if(((user_credentials->caller_flags & USER_CRED_GSS_PROCESSED) != 0) &&
     (user_credentials->caller_garray != NULL))
    {
      gsh_free(user_credentials->caller_garray);
    }
#endif

  init_credentials(user_credentials);
}

/**
 * @brief Compares two RPC creds
 *
 * @param[in] cred1 First RPC cred
 * @param[in] cred2 Second RPC cred
 *
 * @return true if same, false otherwise
 */
bool nfs_compare_clientcred(nfs_client_cred_t *cred1,
			    nfs_client_cred_t *cred2)
{
  if(cred1 == NULL)
    return false;
  if(cred2 == NULL)
    return false;

  if(cred1->flavor != cred2->flavor)
    return false;

  if(cred1->length != cred2->length)
    return false;

  switch (cred1->flavor)
    {
    case AUTH_UNIX:
      if(cred1->auth_union.auth_unix.aup_uid !=
         cred2->auth_union.auth_unix.aup_uid)
        return false;
      if(cred1->auth_union.auth_unix.aup_gid !=
         cred2->auth_union.auth_unix.aup_gid)
        return false;

      /**
       * @todo ACE: I have removed the comparison of the aup_time
       * values.  The RFC is unclear as to their function, and this
       * fixes a situation where the Linux client was sending
       * SETCLIENTID once on connection and later on state-bearing
       * operations that differed only in timestamp.  Someone familiar
       * with ONC RPC and AUTH_UNIX in particular should review this
       * change.
       */
      break;

    default:
      if(memcmp(&cred1->auth_union, &cred2->auth_union, cred1->length))
        return false;
      break;
    }

  /* If this point is reached, structures are the same */
  return true;
}

int nfs_rpc_req2client_cred(struct svc_req *reqp, nfs_client_cred_t * pcred)
{
  /* Structure for managing basic AUTH_UNIX authentication */
  struct authunix_parms *aup = NULL;

  /* Stuff needed for managing RPCSEC_GSS */
#ifdef _HAVE_GSSAPI
  struct svc_rpc_gss_data *gd = NULL;
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
      break;
#endif

    default:
      /* Unsupported authentication flavour */
      return -1;
      break;
    }

  return 1;
}

