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
 * \file    nfs_export_list.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/20 13:44:57 $
 * \version $Revision: 1.13 $
 * \brief   routines for managing the export list.
 *
 * nfs_export_list.c : routines for managing the export list.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_export_list.c,v 1.13 2006/01/20 13:44:57 deniel Exp $
 *
 * $Log: nfs_export_list.c,v $
 * Revision 1.13  2006/01/20 13:44:57  deniel
 * alt_groups are now handled
 *
 * Revision 1.12  2006/01/19 07:40:26  leibovic
 * Better exportlist management (test whether iterator is null).
 *
 * Revision 1.11  2005/12/20 10:52:18  deniel
 * exportlist is no longer dynamic but static
 *
 * Revision 1.9  2005/11/21 15:04:34  leibovic
 * Displaying acquired Credential.
 *
 * Revision 1.8  2005/11/21 11:32:07  deniel
 * Got ride of nfs_SetPostOpFh3 because of memory leaks
 *
 * Revision 1.7  2005/11/21 09:54:55  leibovic
 * Once for all thread's credential initialization.
 *
 * Revision 1.6  2005/11/07 09:03:39  deniel
 * Implementing access security
 *
 * Revision 1.5  2005/11/04 15:12:58  deniel
 * Added basic authentication support
 *
 * Revision 1.4  2005/10/12 08:28:00  deniel
 * Format of the errror message.
 *
 * Revision 1.3  2005/08/11 12:37:28  deniel
 * Added statistics management
 *
 * Revision 1.2  2005/08/03 13:13:59  deniel
 * memset to zero before building the filehandles
 *
 * Revision 1.1  2005/08/03 06:57:54  deniel
 * Added a libsupport for miscellaneous service functions
 *
 * Revision 1.2  2005/08/02 13:49:43  deniel
 * Ok NFSv3
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
#include <sys/types.h>
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
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
 *
 * nfs_Get_export_by_id: Gets an export entry from its export id. 
 *
 * Gets an export entry from its export id. 
 *
 * @paran exportroot [IN] the root for the export list
 * @param exportid   [IN] the id for the entry to be found.
 *
 * @return the pointer to the pointer to the export list or NULL if failed.
 *
 */
exportlist_t *nfs_Get_export_by_id(struct glist_head * pexportlist, unsigned short exportid)
{
  exportlist_t *piter;
  struct glist_head * glist;
  eid_cache_t * cache_slot;
  int found = 0;

  /* Get it from cache */
  cache_slot = (eid_cache_t *) &export_by_id.eid_cache[(exportid % EXPORT_BY_ID_HASHSIZE)];

  while(cache_slot->eidc_cache_entry != NULL)
    {
      if(cache_slot->eidc_cache_entry->id == exportid)
        return cache_slot->eidc_cache_entry;

      cache_slot = cache_slot->eidc_next;
      if(cache_slot == NULL)
        break;
    }

  /* Something not right; we had cache miss */
  LogCrit(COMPONENT_CONFIG,
          "Cache miss, it should never happen");

  glist_for_each(glist, pexportlist)
    {
      piter = glist_entry(glist, exportlist_t, exp_list);

      if(piter->id == exportid)
        {
          found = 1;
          break;
        }
    }                           /* for */

  if(found == 0)
    return NULL;
  else
    return piter;
}                               /* nfs_Get_export_by_id */

/**
 *
 * nfs_Get_export_by_path: Gets an export entry from its path. 
 *
 * Gets an export entry from its path. 
 *
 * @paran exportroot [IN] the root for the export list
 * @param path       [IN] the path for the entry to be found.
 *
 * @return the pointer to the pointer to the export list or NULL if failed.
 *
 */
exportlist_t *nfs_Get_export_by_path(struct glist_head * exportlist,
                                     char * path)
{
  exportlist_t *p_current_item = NULL;
  struct glist_head * glist;

  /*
   * Find the export for the path
   */
  glist_for_each(glist, exportlist)
    {
      p_current_item = glist_entry(glist, exportlist_t, exp_list);

      /* Is p_current_item->fullpath is equal to path ? */
      if(!strcmp(p_current_item->fullpath, path))
        {
          LogDebug(COMPONENT_CONFIG, "returning export id %u", p_current_item->id);
          return p_current_item;
        }
    }

  LogDebug(COMPONENT_CONFIG, "returning export NULL");
  return NULL;
}                               /* nfs_Get_export_by_id */

/**
 *
 * nfs_Get_export_by_pseudo: Gets an export entry from its pseudo path. 
 *
 * Gets an export entry from its pseudo path. 
 *
 * @paran exportroot [IN] the root for the export list
 * @param path       [IN] the path for the entry to be found.
 *
 * @return the pointer to the pointer to the export list or NULL if failed.
 *
 */
exportlist_t *nfs_Get_export_by_pseudo(struct glist_head * exportlist,
                                       char * path)
{
  exportlist_t *p_current_item = NULL;
  struct glist_head * glist;

  /*
   * Find the export for the path
   */
  glist_for_each(glist, exportlist)
    {
      p_current_item = glist_entry(glist, exportlist_t, exp_list);

      /* Is p_current_item->pseudopath is equal to path ? */
      if(!strcmp(p_current_item->pseudopath, path))
        {
          LogDebug(COMPONENT_CONFIG, "returning export id %u", p_current_item->id);
          return p_current_item;
        }
    }

  LogDebug(COMPONENT_CONFIG, "returning export NULL");
  return NULL;
}                               /* nfs_Get_export_by_id */

/**
 *
 * nfs_Get_export_by_tag: Gets an export entry from its tag. 
 *
 * Gets an export entry from its tag. 
 *
 * @paran exportroot [IN] the root for the export list
 * @param tag        [IN] the tag for the entry to be found.
 *
 * @return the pointer to the pointer to the export list or NULL if failed.
 *
 */
exportlist_t *nfs_Get_export_by_tag(struct glist_head * exportlist,
                                    char * tag)
{
  exportlist_t *p_current_item = NULL;
  struct glist_head * glist;

  /*
   * Find the export for the path
   */
  glist_for_each(glist, exportlist)
    {
      p_current_item = glist_entry(glist, exportlist_t, exp_list);

      /* Is p_current_item->FS_tag is equal to tag ? */
      if(!strcmp(p_current_item->FS_tag, tag))
        {
          LogDebug(COMPONENT_CONFIG, "returning export id %u", p_current_item->id);
          return p_current_item;
        }
    }

  LogDebug(COMPONENT_CONFIG, "returning export NULL");
  return NULL;
}                               /* nfs_Get_export_by_id */

/**
 *
 * get_req_uid_gid: 
 *
 * 
 *
 * @param req [IN]  incoming request.
 * @param pexport_client [IN] related export client
 * @param pexport [IN]  related export entry
 * @param user_credentials [OUT] Filled in structure with uid and gids
 * 
 * @return TRUE if successful, FALSE otherwise 
 *
 */
int get_req_uid_gid(struct svc_req *req,
                    struct user_cred *user_credentials)
{
  struct authunix_parms *punix_creds = NULL;
#ifdef _HAVE_GSSAPI
  struct svc_rpc_gss_data *gd = NULL;
  char principal[MAXNAMLEN+1];
  int ret, num_grps = NGROUPS;
  struct passwd pwd;
  struct passwd *pp;
  char buff[NFS4_MAX_DOMAIN_LEN];
#endif

  if (user_credentials == NULL)
    return FALSE;

  switch (req->rq_cred.oa_flavor)
    {
    case AUTH_NONE:
      /* Nothing to be done here... */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Request xid=%u has authentication AUTH_NONE,",
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
              LogCrit(COMPONENT_DISPATCH,
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
	  user_credentials->caller_flags |= USER_CRED_ANONYMOUS;

	  return TRUE;
	}

      if(uidgidmap_get(user_credentials->caller_uid,
                       &user_credentials->caller_gid) != ID_MAPPER_SUCCESS)
        {
          LogMajor(COMPONENT_IDMAPPER,
                   "FAILURE: Could not resolve uidgid map for %u",
                   user_credentials->caller_uid);
          user_credentials->caller_gid = -1;
        }

#ifdef _SOLARIS
      if(getpwuid_r(user_credentials->caller_uid, &pwd, buff, sizeof(buff)) != 0)
#else
      if((getpwuid_r(user_credentials->caller_uid, &pwd, buff, sizeof(buff), &pp) != 0) ||
               (pp == NULL))
#endif                          /* _SOLARIS */
        { 
          LogCrit(COMPONENT_IDMAPPER,"getpwnuid failed uid=%u", user_credentials->caller_uid);
          return FALSE;
        }

      LogFullDebug(COMPONENT_IDMAPPER,"Name: %s", pwd.pw_name);
      LogFullDebug(COMPONENT_IDMAPPER,"UID: %d", pwd.pw_uid);
      LogFullDebug(COMPONENT_IDMAPPER,"Primary GID: %d", pwd.pw_gid);

      user_credentials->caller_garray = gsh_malloc(NGROUPS * sizeof(gid_t));

      if(user_credentials->caller_garray == NULL)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Failure to allocate memory for GSS grouplist");
          return FALSE;
        }

      ret = getgrouplist(pwd.pw_name,
                         pwd.pw_gid,
                         user_credentials->caller_garray,
                         &num_grps);

      if (ret == -1)
        {
          LogEvent(COMPONENT_IDMAPPER,"getgrouplist failed with -1. name=%s gid=%d num_grps=%d, retry.",
                         pwd.pw_name, pwd.pw_gid, num_grps);
          /* resize and retry */
          gsh_free(user_credentials->caller_garray);
          user_credentials->caller_garray = gsh_malloc(num_grps * sizeof(gid_t));

          if(user_credentials->caller_garray == NULL)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Failure to allocate memory for GSS grouplist");
              return FALSE;
            }

          ret = getgrouplist(pwd.pw_name,
                             pwd.pw_gid,
                             user_credentials->caller_garray,
                             &num_grps);

          if (ret < 0)
            {
              LogCrit(COMPONENT_IDMAPPER,"getgrouplist failed with %d. name=%s gid=%d",
                         ret, pwd.pw_name, pwd.pw_gid);

              gsh_free(user_credentials->caller_garray);
              user_credentials->caller_garray = NULL;
              return FALSE;
            }
        }
      else if (ret < -1)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "getgrouplist failed with %d. name=%s gid=%d",
                  ret, pwd.pw_name, pwd.pw_gid);

          gsh_free(user_credentials->caller_garray);
          user_credentials->caller_garray = NULL;
          return FALSE;
        }

      LogFullDebug(COMPONENT_DISPATCH, "----> uid=%d,gid=%d",
                   (int)user_credentials->caller_uid,
                   (int)user_credentials->caller_gid);

      user_credentials->caller_glen = num_grps;
      break;
#endif                          /* _USE_GSSRPC */

    default:
      LogFullDebug(COMPONENT_DISPATCH,
                   "FAILURE: Request xid=%u, has unsupported authentication %d",
                   req->rq_xid, req->rq_cred.oa_flavor);
      /* Reject the request for weak authentication and return to worker */
      return FALSE;

      break;
    }                           /* switch( req->rq_cred.oa_flavor ) */
  return TRUE;
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
                    fsal_attrib_list_t * attr)
{
  if(attr->asked_attributes & FSAL_ATTR_OWNER)
    {
      if(pexport_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)
        attr->owner = pexport_perms->anonymous_uid;
      else if(!(pexport_perms->options & EXPORT_OPTION_ROOT) &&
              (attr->owner == 0) &&
              (user_credentials->caller_uid == pexport_perms->anonymous_uid))
        attr->owner = pexport_perms->anonymous_uid;
    }

  if(attr->asked_attributes & FSAL_ATTR_GROUP)
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
 *
 * nfs_build_fsal_context: Builds the FSAL context according to the request
 * and the export entry.
 *
 * Builds the FSAL credentials according to the request and the export entry.
 *
 * @param req [IN]  incoming request.
 * @param pexport_client [IN] related export client
 * @param pexport [IN]  related export entry
 * @param pcred   [IN/OUT] initialized credential of caller thread.
 * @param user_credentials [OUT] Filled in structure with uid and gids
 * 
 * @return TRUE if successful, FALSE otherwise 
 *
 * NOTE: The caller_garray is made a part of the user_credentials, if
 *       the context needs to be retained for longer than the duration
 *       of the NFS request, these credentials must be copied.
 *
 */
int nfs_build_fsal_context(struct svc_req *req,
                           exportlist_t * pexport,
                           fsal_op_context_t * pcontext,
                           struct user_cred *user_credentials)
{
  fsal_status_t fsal_status;

  if (user_credentials == NULL)
    return FALSE;

  /* Build the credentials */
  fsal_status = FSAL_GetClientContext(pcontext,
                                      &pexport->FS_export_context,
                                      user_credentials->caller_uid,
                                      user_credentials->caller_gid,
                                      user_credentials->caller_garray,
                                      user_credentials->caller_glen);

  /*
   * TODO: Fix this hack
   * This hack put here to pass the IP address to the fsal
   * via the fsal_op_context_t->credential.
   *
   * This a hack because it breaks the fsal API. fsal_op_context_t is an
   * fsal specific structure that should only be handled in the fsal
   *
   * But we do this here because passing the ip through the
   * FSAL_GetClientContext parameters requires a lot of code change
   *
   * The plan is to correct this hack when we roll over to the new
   * API where this struct has been made common
   */
  copy_xprt_addr(&pcontext->credential.caller_addr, req->rq_xprt);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogEvent(COMPONENT_DISPATCH,
               "NFS DISPATCHER: FAILURE: Could not get credentials for "
               "(exportid=%d,uid=%d,gid=%d), fsal error=(%d,%d)",
               pexport->id,
               user_credentials->caller_uid, user_credentials->caller_gid,
               fsal_status.major, fsal_status.minor);
      return FALSE;
    }
  else
    LogDebug(COMPONENT_DISPATCH,
             "NFS DISPATCHER: FSAL Cred acquired for (exportid=%d,uid=%d,gid=%d)",
             pexport->id,
             user_credentials->caller_uid, user_credentials->caller_gid);

  return TRUE;
}                               /* nfs_build_fsal_context */

/**
 *
 * nfs_compare_rpc_cred: Compares two RPC creds
 *
 * @param pcred1 [IN] first RPC cred
 * @param pcred2 [IN] second RPC cred
 * 
 * @return TRUE if same, FALSE otherwise
 *
 */
int nfs_compare_clientcred(nfs_client_cred_t * pcred1,
                           nfs_client_cred_t *pcred2)
{
  if(pcred1 == NULL)
    return FALSE;
  if(pcred2 == NULL)
    return FALSE;

  if(pcred1->flavor != pcred2->flavor)
    return FALSE;

  switch (pcred1->flavor)
    {
    case AUTH_UNIX:
      if(pcred1->auth_union.auth_unix.aup_uid !=
         pcred2->auth_union.auth_unix.aup_uid)
        return FALSE;
      if(pcred1->auth_union.auth_unix.aup_gid !=
         pcred2->auth_union.auth_unix.aup_gid)
        return FALSE;
      break;

    default:
      if(memcmp(&pcred1->auth_union, &pcred2->auth_union, pcred1->length))
        return FALSE;
      break;
    }

  /* If this point is reach, structures are the same */
  return TRUE;
}                               /* nfs_compare_clientcred */

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
}                               /* nfs_rpc_req2client_cred */

int nfs_export_tag2path(struct glist_head * pexportlist,
                        char *tag, int taglen,
                        char *path, int pathlen)
{
  exportlist_t *piter;
  struct glist_head * glist;

  if(!tag || !path)
    return -1;

  glist_for_each(glist, pexportlist)
    {
      piter = glist_entry(glist, exportlist_t, exp_list);

      if(!strncmp(tag, piter->FS_tag, taglen))
        {
          strncpy(path, piter->fullpath, pathlen);
          return 0;
          break;
        }
    }                           /* for */

  return -1;
}                               /* nfs_export_tag2path */
