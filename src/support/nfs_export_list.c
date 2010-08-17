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
#ifdef _USE_GSSRPC
#include <gssapi/gssapi.h>
#ifdef HAVE_KRB5
#include <gssapi/gssapi_krb5.h>
#endif
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"

#ifdef _USE_GSSRPC
#define SVCAUTH_PRIVATE(auth) \
        (*(struct svc_rpc_gss_data **)&(auth)->svc_ah_private)

struct svc_rpc_gss_data
{
  bool_t established;           /* context established */
  gss_ctx_id_t ctx;             /* context id */
  struct rpc_gss_sec sec;       /* security triple */
  gss_buffer_desc cname;        /* GSS client name */
  u_int seq;                    /* sequence number */
  u_int win;                    /* sequence window */
  u_int seqlast;                /* last sequence number */
  uint32_t seqmask;             /* bitmask of seqnums */
  gss_name_t client_name;       /* unparsed name string */
  gss_buffer_desc checksum;     /* so we can free it */
};

#endif

const char *Rpc_gss_svc_name[] =
    { "no name", "RPCSEC_GSS_SVC_NONE", "RPCSEC_GSS_SVC_INTEGRITY",
  "RPCSEC_GSS_SVC_PRIVACY"
};

#ifdef _USE_GSSRPC
/* Cred Name is "name@DOMAIN" */
static void split_credname(gss_buffer_desc credname, char *username, char *domainname)
{
  char *ptr = NULL;
  int pos = 0;
  if(credname.value == NULL)
    return;

  ptr = (char *)credname.value;
  for(pos = 0; pos < credname.length; pos++)
    {
      if(ptr[pos] == '@' && pos + 1 < credname.length)
        {
          strncpy(username, ptr, pos);
          username[pos] = '\0';
          strncpy(domainname, ptr + pos + 1, credname.length - pos - 1);
          domainname[credname.length - pos - 1] = '\0';
          break;
        }
    }
}
#endif

#ifdef _HAVE_GSSAPI
/**
 *
 * convert_gss_status2str: Converts GSSAPI error into human-readable string.
 *
 * Converts GSSAPI error into human-readable string.
 *
 * @paran str      [OUT] the string that will contain the translated error code
 * @param maj_stat [IN] the GSSAPI major status (focused on GSSAPI's mechanism)
 * @param min_stat [IN] the GSSAPI minor status (focused on the mechanism)
 *
 * @return nothing (void function)
 *
 */

static void convert_gss_status2str(char *str, OM_uint32 maj_stat, OM_uint32 min_stat)
{
  OM_uint32 min;
  gss_buffer_desc msg;
  int msg_ctx = 0;
  FILE *tmplog;

  gss_display_status(&min, maj_stat, GSS_C_GSS_CODE, GSS_C_NULL_OID, &msg_ctx, &msg);
  sprintf(str, "GSS_CODE=%s - ", (char *)msg.value);
  gss_release_buffer(&min, &msg);

  gss_display_status(&min, min_stat, GSS_C_MECH_CODE, GSS_C_NULL_OID, &msg_ctx, &msg);
  sprintf(str, "MECH_CODE=%s\n", (char *)msg.value);
  gss_release_buffer(&min, &msg);
}                               /* convert_gss_status2str */
#endif

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
    }                           /* for */

  if(found == 0)
    return NULL;
  else
    return piter;
}                               /* nfs_Get_export_by_id */

/**
 *
 * nfs_build_fsal_context: Builds the FSAL context according to the request and the export entry.
 *
 * Builds the FSAL credentials according to the request and the export entry.
 *
 * @param ptr_req [IN]  incoming request.
 * @param pexport_client [IN] related export client
 * @param pexport [IN]  related export entry
 * @param pcred   [IN/OUT] initialized credential of caller thread.
 * 
 * @return TRUE if successful, FALSE otherwise 
 *
 */
int nfs_build_fsal_context(struct svc_req *ptr_req,
                           exportlist_client_entry_t * pexport_client,
                           exportlist_t * pexport, fsal_op_context_t * pcontext)
{
  struct authunix_parms *punix_creds = NULL;
#ifdef _USE_GSSRPC
  struct svc_rpc_gss_data *gd = NULL;
  gss_buffer_desc oidbuff;
  OM_uint32 maj_stat = 0;
  OM_uint32 min_stat = 0;
  char username[MAXNAMLEN];
  char domainname[MAXNAMLEN];
#endif
  fsal_status_t fsal_status;
  uid_t caller_uid = 0;
  gid_t caller_gid = 0;
  unsigned int caller_glen = 0;
  gid_t *caller_garray = NULL;
  unsigned int rpcxid = 0;

  char *ptr;

  rpcxid = get_rpc_xid(ptr_req);

  switch (ptr_req->rq_cred.oa_flavor)
    {
    case AUTH_NONE:
      /* Nothing to be done here... */
      LogFullDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCH: Request xid=%u has authentication AUTH_NONE",
                   rpcxid);
      break;

    case AUTH_UNIX:
      LogFullDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCH: Request xid=%u has authentication AUTH_UNIX",
                   rpcxid);
      /* We map the rq_cred to Authunix_parms */
      punix_creds = (struct authunix_parms *)ptr_req->rq_clntcred;

      /* Get the uid/gid couple */
      caller_uid = punix_creds->aup_uid;
      caller_gid = punix_creds->aup_gid;
      caller_glen = punix_creds->aup_len;
      caller_garray = punix_creds->aup_gids;

      break;

#ifdef _USE_GSSRPC
    case RPCSEC_GSS:
      LogFullDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCH: Request xid=%u has authentication RPCSEC_GSS",
                   rpcxid);
      /* Get the gss data to process them */
      gd = SVCAUTH_PRIVATE(ptr_req->rq_xprt->xp_auth);

      if(isFullDebug(COMPONENT_RPCSEC_GSS))
        {
          LogFullDebug(COMPONENT_RPCSEC_GSS,
               "----> RPCSEC_GSS svc=%u RPCSEC_GSS_SVC_NONE=%u RPCSEC_GSS_SVC_INTEGRITY=%u RPCSEC_GSS_SVC_PRIVACY=%u\n",
               gd->sec.svc, RPCSEC_GSS_SVC_NONE, RPCSEC_GSS_SVC_INTEGRITY,
               RPCSEC_GSS_SVC_PRIVACY);

          memcpy(&ptr, (void *)gd->ctx + 4, 4);
          LogFullDebug(COMPONENT_RPCSEC_GSS,
                 "----> Client=%s length=%u  Qop=%u established=%u gss_ctx_id=%p|%p\n",
                 (char *)gd->cname.value, gd->cname.length, gd->established, gd->sec.qop,
                 gd->ctx, ptr);
       }

      if((maj_stat = gss_oid_to_str(&min_stat, gd->sec.mech, &oidbuff)) != GSS_S_COMPLETE)
        {
          LogCrit(COMPONENT_DISPATCH, "Error in gss_oid_to_str: %u|%u\n",
                  maj_stat, min_stat);
          exit(1);
        }
      LogFullDebug(COMPONENT_RPCSEC_GSS, "----> Client mech=%s len=%u\n",
                   (char *)oidbuff.value, oidbuff.length);

      /* Je fais le menage derriere moi */
      (void)gss_release_buffer(&min_stat, &oidbuff);

      split_credname(gd->cname, username, domainname);

      LogFullDebug(COMPONENT_RPCSEC_GSS, "----> User=%s Domain=%s\n",
                   username, domainname);

      /* Convert to uid */
      if(!name2uid(username, &caller_uid))
        return FALSE;

      if(uidgidmap_get(caller_uid, &caller_gid) != ID_MAPPER_SUCCESS)
        {
          LogMajor(COMPONENT_DISPATCH,
                   "NFS_DISPATCH: FAILURE: Could not resolve uidgid map for %u",
                   caller_uid);
          caller_gid = -1;
        }
      LogFullDebug(COMPONENT_RPCSEC_GSS, "----> Uid=%u Gid=%u\n",
                   (unsigned int)caller_uid, (unsigned int)caller_gid);
      caller_glen = 0;
      caller_garray = 0;

      break;
#endif                          /* _USE_GSSRPC */

    default:
      LogFullDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCH: FAILURE : Request xid=%u, has unsupported authentication %d",
                   rpcxid, ptr_req->rq_cred.oa_flavor);
      /* Reject the request for weak authentication and return to worker */
      return FALSE;

      break;
    }                           /* switch( ptr_req->rq_cred.oa_flavor ) */

  /* Do we have root access ? */
  if((caller_uid == 0) && !(pexport_client->options & EXPORT_OPTION_ROOT))
    {
      /* caller_uid = ANON_UID ; */
      caller_uid = pexport->anonymous_uid;
      caller_gid = ANON_GID;
    }

  /* Build the credentials */
  fsal_status = FSAL_GetClientContext(pcontext,
                                      &pexport->FS_export_context,
                                      caller_uid, caller_gid, caller_garray, caller_glen);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogEvent(COMPONENT_DISPATCH,
               "NFS DISPATCHER: FAILURE: Could not get credentials for (uid=%d,gid=%d), fsal error=(%d,%d)",
               caller_uid, caller_gid, fsal_status.major, fsal_status.minor);
      return FALSE;
    }
  else
    LogDebug(COMPONENT_DISPATCH, "NFS DISPATCHER: FSAL Cred acquired for (uid=%d,gid=%d)",
             caller_uid, caller_gid);

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
int nfs_compare_clientcred(nfs_client_cred_t * pcred1, nfs_client_cred_t * pcred2)
{
  if(pcred1 == NULL)
    return FALSE;
  if(pcred2 == NULL)
    return FALSE;

  if(pcred1->flavor != pcred2->flavor)
    return FALSE;

  if(pcred1->length != pcred2->length)
    return FALSE;

  switch (pcred1->flavor)
    {
    case AUTH_UNIX:
      if(pcred1->auth_union.auth_unix.aup_uid != pcred2->auth_union.auth_unix.aup_uid)
        return FALSE;
      if(pcred1->auth_union.auth_unix.aup_gid != pcred2->auth_union.auth_unix.aup_gid)
        return FALSE;
      if(pcred1->auth_union.auth_unix.aup_time != pcred2->auth_union.auth_unix.aup_time)
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
  gss_buffer_desc mechname;
  gss_buffer_desc clientname;
  OM_uint32 maj_stat = 0;
  OM_uint32 min_stat = 0;
#ifdef _USE_GSSRPC
  struct svc_rpc_gss_data *gd = NULL;
#endif
  gss_buffer_desc oidbuff;
  char errbuff[1024];
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

#ifdef _USE_GSSRPC
    case RPCSEC_GSS:
      /* Extract the information from the RPCSEC_GSS opaque structure */
      gd = SVCAUTH_PRIVATE(reqp->rq_xprt->xp_auth);

      pcred->auth_union.auth_gss.svc = (unsigned int)(gd->sec.svc);
      pcred->auth_union.auth_gss.qop = (unsigned int)(gd->sec.qop);
      pcred->auth_union.auth_gss.gss_context_id = gd->ctx;
      strncpy(pcred->auth_union.auth_gss.cname, gd->cname.value, NFS_CLIENT_NAME_LEN);

      if((maj_stat = gss_oid_to_str(&min_stat, gd->sec.mech, &oidbuff)) != GSS_S_COMPLETE)
        {
          convert_gss_status2str(errbuff, maj_stat, min_stat);
          LogCrit(COMPONENT_DISPATCH, "GSSAPI ERROR: %u|%u = %s",
                  maj_stat, min_stat, errbuff);
          return -1;
        }
      strncpy(pcred->auth_union.auth_gss.stroid, oidbuff.value, NFS_CLIENT_NAME_LEN);

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
}                               /* nfs_rpc_req2client_cred */

int nfs_export_tag2path(exportlist_t * exportroot, char *tag, int taglen, char *path,
                        int pathlen)
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
    }                           /* for */

  return -1;
}                               /* nfs_export_tag2path */
