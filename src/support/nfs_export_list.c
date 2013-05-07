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
 * @brief Gets an export entry from its export id.
 *
 * @todo Danger Will Robinson!!!
 * We put back the gsh_export but pass back a pointer to the export
 * inside it.  Phase out calls to this.  the req_ctx has what we
 * (may) need and it is properly ref counted.
 *
 * @param[in] exportroot The root for the export list
 * @param[in] exportid   The id for the entry to be found.
 *
 * @return the pointer to the export list or NULL if failed.
 */

exportlist_t *nfs_Get_export_by_id(struct glist_head *exportroot, unsigned short exportid)
{
  exportlist_t *piter;
  struct gsh_export *exp;

  exp = get_gsh_export(exportid, true);
  if(exp == NULL)
	  return NULL;
  piter = &exp->export;
  put_gsh_export(exp);
  return piter;
}

/** @todo we can avl tag but path has to be ordered
 */

/**
 *
 * nfs_Get_export_by_path: Gets an export entry from its path. 
 *
 * Gets an export entry from its path using a substring match and
 * linear search of the export list. 
 * If path has a trailing '/', ignor it.
 *
 * @param exportroot [IN] the root for the export list
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
	int len_path = strlen(path);
	int len_export;

	if(path[len_path - 1] == '/')
		len_path--;
	glist_for_each(glist, exportlist)
	{
		p_current_item = glist_entry(glist, exportlist_t, exp_list);
		len_export = strlen(p_current_item->fullpath);
		/* a path shorter than the full path cannot match
		 */
		if(len_path < len_export)
			continue;
		/* if the char in fullpath just after the end of path is not '/'
		 * it is a name token longer, i.e. /mnt/foo != /mnt/foob/
		 */
		if(p_current_item->fullpath[len_path] != '/' &&
		   p_current_item->fullpath[len_path] != '\0')
			continue;
		/* we agree on size, now compare the leadingt substring
		 */
		if( !strncmp(p_current_item->fullpath,
			     path,
			     len_path))
			return p_current_item;
	}
	return NULL;
}

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
  const gid_t *maybe_gid = NULL;

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

	  return true;
	}

      pthread_rwlock_rdlock(&idmapper_user_lock);
      idmapper_lookup_by_uid(user_credentials->caller_uid,
			     NULL,
			     &maybe_gid);
      if (maybe_gid)
	user_credentials->caller_gid = *maybe_gid;
      else
	{
	  LogMajor(COMPONENT_DISPATCH,
		   "FAILURE: Could not resolve uidgid map for %u",
		   user_credentials->caller_uid);
	  user_credentials->caller_gid = -1;
	}
      pthread_rwlock_unlock(&idmapper_user_lock);
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

int nfs_export_tag2path(struct glist_head * exportlist, char *tag, int taglen,
                        char *path, int pathlen)
{
  exportlist_t *piter;
  struct glist_head * glist;

  if(!tag || !path)
    return -1;

  glist_for_each(glist, exportlist)
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
}
