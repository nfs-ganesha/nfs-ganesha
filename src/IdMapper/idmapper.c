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
 * \file    idmapper.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.2 $
 * \brief   Id mapping functions
 *
 * idmapper.c : Id mapping functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs_tools.h"
#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#ifdef _MSPAC_SUPPORT
#include <stdint.h>
#include <stdbool.h>
#include <wbclient.h>
#endif

#ifdef _USE_NFSIDMAP

#define _PATH_IDMAPDCONF     "/etc/idmapd.conf"

extern pthread_mutex_t idmap_conf_mtx;

typedef void (*nfs4_idmap_log_function_t) (const char *, ...);

int nfs4_init_name_mapping(char *conffile);
int nfs4_get_default_domain(char *server, char *domain, size_t len);
int nfs4_uid_to_name(uid_t uid, char *domain, char *name, size_t len);
int nfs4_gid_to_name(gid_t gid, char *domain, char *name, size_t len);
int nfs4_name_to_uid(char *name, uid_t * uid);
int nfs4_name_to_gid(char *name, gid_t * gid);
int nfs4_gss_princ_to_ids(char *secname, char *princ, uid_t * uid, gid_t * gid);
int nfs4_gss_princ_to_grouplist(char *secname, char *princ, gid_t * groups, int *ngroups);
void nfs4_set_debug(int dbg_level, nfs4_idmap_log_function_t dbg_logfunc);

char idmap_domain[NFS4_MAX_DOMAIN_LEN];
static int nfsidmap_conf_read = FALSE;

int nfsidmap_set_conf()
{
  /* check if idmap conf is read already, if not grab the mutex
     and check again. If still not, process it */
  if(!nfsidmap_conf_read)
    {
      pthread_mutex_lock(&idmap_conf_mtx);
      if(nfsidmap_conf_read)
        {
          pthread_mutex_unlock(&idmap_conf_mtx);
          return 1;
        }
      if(nfs4_init_name_mapping(_PATH_IDMAPDCONF))
        {
          pthread_mutex_unlock(&idmap_conf_mtx);
          return 0;
        }

      if(nfs4_get_default_domain(NULL, idmap_domain, sizeof(idmap_domain)))
        {
          pthread_mutex_unlock(&idmap_conf_mtx);
          return 0;
        }

      nfsidmap_conf_read = TRUE;
      pthread_mutex_unlock(&idmap_conf_mtx);
    }
  return 1;
}
#endif                          /* _USE_NFSIDMAP */

/**
 *
 * uid2name: convert a uid to a name. 
 *
 * convert a uid to a name. 
 *
 * @param name [OUT]  the name of the user
 * @param uid  [IN]   the input uid
 *
 * return 1 if successful, 0 otherwise
 *
 */
int uid2name(char *name, uid_t * puid, size_t namesize)
{
#ifdef _USE_NFSIDMAP
  char fqname[NFS4_MAX_DOMAIN_LEN];

  int rc;

  if(!nfsidmap_set_conf())
    {
      LogCrit(COMPONENT_IDMAPPER,
              "uid2name: nfsidmap_set_conf failed");
      return 0;
    }

  if(unamemap_get(*puid, name, namesize) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "uid2name: unamemap_get uid %u returned %s",
                   *puid, name);
      return 1;
    }
  else
    {
      rc = nfs4_uid_to_name(*puid, idmap_domain, name, namesize);
      if(rc != 0)
        {
          LogCrit(COMPONENT_IDMAPPER,
                   "uid2name: nfs4_uid_to_name %u returned %d (%s)",
                   *puid, -rc, strerror(-rc));
          return 0;
        }

      strmaxcpy(fqname, name, sizeof(fqname));
      if(strchr(name, '@') == NULL)
        {
          LogFullDebug(COMPONENT_IDMAPPER,
                       "uid2name: adding domain %s",
                       idmap_domain);
          snprintf(fqname, sizeof(fqname), "%s@%s", name, idmap_domain);
          strmaxcpy(name, fqname, namesize);
        }

      LogFullDebug(COMPONENT_IDMAPPER,
                   "uid2name: nfs4_uid_to_name uid %u returned %s",
                   *puid, name);

      if(unamemap_add(*puid, fqname, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "uid2name: uidmap_add %s %u failed",
                  fqname, *puid);
          return 0;
        }
    }
  return 1;

#else
  struct passwd p;
  struct passwd *pp;
  char buff[NFS4_MAX_DOMAIN_LEN];

  if(unamemap_get(*puid, name, namesize) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "uid2name: unamemap_get uid %u returned %s",
                   *puid, name);
      return 1;
    }
  else
    {
#ifdef _SOLARIS
      if(getpwuid_r(*puid, &p, buff, sizeof(buff)) != 0)
#else
      if((getpwuid_r(*puid, &p, buff, sizeof(buff), &pp) != 0) ||
	 (pp == NULL))
#endif                          /* _SOLARIS */
        {
          LogCrit(COMPONENT_IDMAPPER,
                       "uid2name: getpwuid_r %u failed",
                       *puid);
          return 0;
        }

      strmaxcpy(name, p.pw_name, namesize);

      LogFullDebug(COMPONENT_IDMAPPER,
                   "uid2name: getpwuid_r uid %u returned %s",
                   *puid, name);

      if(unamemap_add(*puid, name, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "uid2name: uidmap_add %s %u failed",
                  name, *puid);
          return 0;
        }
    }

  return 1;
#endif                          /* _USE_NFSIDMAP */
}                               /* uid2name */

/**
 *
 * name2uid: convert a name to a uid
 *
 * convert a name to a uid
 *
 * @param name [IN]  the name of the user
 * @param puid [OUT] the resulting uid
 *
 * return 1 if successful, 0 otherwise
 *
 */
int name2uid(char *name, uid_t * puid)
{
  char *end = NULL, *at = NULL;
  uid_t uid;
#ifdef _USE_NFSIDMAP
#ifdef _HAVE_GSSAPI
  gid_t gss_gid;
  uid_t gss_uid;
#endif
  char fqname[NFS4_MAX_DOMAIN_LEN];
  int rc;
#else  /* !_USE_NFSIDMAP */
  struct passwd passwd;
  struct passwd *res;
  char buff[NFS4_MAX_DOMAIN_LEN];
#endif /* _USE_NFSIDMAP */

  if(uidmap_get(name, &uid) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "name2uid: uidmap_get mapped %s to uid=%u",
                   name, uid);
      *puid = uid;
      goto success;
    }
  else
    {
#ifndef _USE_NFSIDMAP
#ifdef _SOLARIS
      if((res = getpwnam_r(name, &passwd, buff, sizeof(buff))) == NULL)
#else
      if(getpwnam_r(name, &passwd, buff, sizeof(buff), &res) != 0)
#endif                          /* _SOLARIS */
        {
          LogCrit(COMPONENT_IDMAPPER,
                       "name2uid: getpwnam_r %s failed",
                       name);
          goto v3compat;
        }
      else if (res != NULL)
        {
          *puid = res->pw_uid;
#ifdef _HAVE_GSSAPI
          if(uidgidmap_add(res->pw_uid, res->pw_gid) != ID_MAPPER_SUCCESS)
            {
              LogMajor(COMPONENT_IDMAPPER,
                      "name2uid: uidgidmap_add gss_uid %u gss_gid %u failed",
                      res->pw_uid, res->pw_gid);
            }
#endif                          /* _HAVE_GSSAPI */
          if(uidmap_add(name, res->pw_uid, 1) != ID_MAPPER_SUCCESS)
            {
              LogMajor(COMPONENT_IDMAPPER,
                       "name2uid: uidmap_add %s %u failed",
                       name, res->pw_uid);
            }

          goto success; /* Job is done */
        }
#else /* _USE_NFSIDMAP */
      if(!nfsidmap_set_conf())
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "name2uid: nfsidmap_set_conf failed");
          goto v3compat;
        }

      /* obtain fully qualified name */
      if(strchr(name, '@') == NULL)
        snprintf(fqname, sizeof(fqname), "%s@%s", name, idmap_domain);
      else
        strmaxcpy(fqname, name, sizeof(fqname));

      rc = nfs4_name_to_uid(fqname, puid);
      if(rc)
        {
          LogInfo(COMPONENT_IDMAPPER,
                       "name2uid: nfs4_name_to_uid %s failed %d (%s)",
                       fqname, -rc, strerror(-rc));
          goto v3compat;
        }

      LogFullDebug(COMPONENT_IDMAPPER,
                   "name2uid: nfs4_name_to_uid %s returned %u",
                   fqname, *puid);

      if(uidmap_add(fqname, *puid, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "name2uid: uidmap_add %s %u failed",
                  fqname, *puid);
        }

#ifdef _HAVE_GSSAPI
      /* nfs4_gss_princ_to_ids required to extract uid/gid from gss creds
       * XXX: currently uses unqualified name as per libnfsidmap comments */
      rc = nfs4_gss_princ_to_ids("krb5", name, &gss_uid, &gss_gid);
      if(rc)
        {
          LogInfo(COMPONENT_IDMAPPER,
                       "name2uid: nfs4_gss_princ_to_ids %s failed %d (%s)",
                       name, -rc, strerror(-rc));
        }

      if(uidgidmap_add(gss_uid, gss_gid) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "name2uid: uidgidmap_add gss_uid %u gss_gid %u failed",
                  gss_uid, gss_gid);
        }
#endif                          /* _HAVE_GSSAPI */
        goto success;

#endif                           /* _USE_NFSIDMAP */

v3compat:
      /* If the name is numeric with no leading zeros and with no '@', 
       * return the int value if numeric user/group names are allowed by the configuration */
      at = strchr(name, '@');
      if(!at && name[0] != '0')
        {
          uid = strtol(name, &end, 10);
          if(end && *end != '\0')
            return 0;
       
          if(uidmap_add(name, uid, 0) != ID_MAPPER_SUCCESS)
            {
              /* Failure to update the in-core table is not fatal */
              LogMajor(COMPONENT_IDMAPPER,
                      "name2uid: uidmap_add %s %u failed",
                      name, uid);
            }
          *puid = uid;
          goto success;
        }
      return 0;
    }
success:
  return 1;
}                               /* name2uid */

/**
 *
 * principal2uid: convert a principal (as returned by gss_display_name()) to a uid/gid
 *
 * convert a principal (as returned by gss_display_name()) to a uid/gid
 *
 * @param name [IN]  the principal of the user as returned by gss_display_name()
 * @param puid [OUT] the resulting uid
 *
 * return 1 if successful, 0 otherwise
 *
 */
#ifdef _HAVE_GSSAPI
#ifdef _MSPAC_SUPPORT
int principal2uid(char *principal, uid_t * puid, struct svc_rpc_gss_data *gd)
#else
int principal2uid(char *principal, uid_t * puid)
#endif
{
#ifdef _USE_NFSIDMAP
  gid_t gss_gid;
  uid_t gss_uid;
  int rc;

  /* NFSv4 specific features: RPCSEC_GSS will provide principal like:
   *  nfs/<host> 
   *  root/<host>
   *  host/<host>
   * choice is made to map them to root */
  if(!strncmp(principal, "nfs/", 4) || 
     !strncmp(principal, "root/", 5) ||
     !strncmp(principal, "host/", 5) )
    {
      /* This is a "root" request made from the hostbased nfs principal, use root */
      LogFullDebug(COMPONENT_IDMAPPER,
                   "principal2uid: mapping %s to root (uid = 0)", principal);
      *puid = 0;

      return 1;
    }

  if(uidmap_get(principal, &gss_uid) != ID_MAPPER_SUCCESS)
    {
      if(!nfsidmap_set_conf())
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "principal2uid: nfsidmap_set_conf failed");
          return 0;
        }

      /* nfs4_gss_princ_to_ids required to extract uid/gid from gss creds */
      LogFullDebug(COMPONENT_IDMAPPER,
                   "calling nfs4_gss_princ_to_ids() to map principal to uid/gid");

      rc = nfs4_gss_princ_to_ids("krb5", principal, &gss_uid, &gss_gid);
      if(rc)
        {
#ifdef _MSPAC_SUPPORT
          short found_uid=false;
          short found_gid=false;
          if (gd->flags & SVC_RPC_GSS_FLAG_MSPAC)
          {
            struct wbcAuthUserParams params;
            wbcErr wbc_err;
            struct wbcAuthUserInfo *info;
            struct wbcAuthErrorInfo *error = NULL;

            memset(&params, 0, sizeof(params));
            params.level = WBC_AUTH_USER_LEVEL_PAC;
            params.password.pac.data = (uint8_t *)gd->pac.ms_pac.value;
            params.password.pac.length = gd->pac.ms_pac.length;

            wbc_err = wbcAuthenticateUserEx(&params, &info, &error);
            if (!WBC_ERROR_IS_OK(wbc_err)) {
              LogCrit(COMPONENT_IDMAPPER,"wbcAuthenticateUserEx returned %s",
                       wbcErrorString(wbc_err));
              return 0;
            }

            if (error) {
              LogCrit(COMPONENT_IDMAPPER,"nt_status: %s, display_string %s",
                      error->nt_string, error->display_string);
              wbcFreeMemory(error); 
              return 0;
            }

            LogFullDebug(COMPONENT_IDMAPPER,"account_name: %s", info->account_name);
            LogFullDebug(COMPONENT_IDMAPPER,"domain_name: %s", info->domain_name);
            LogFullDebug(COMPONENT_IDMAPPER,"num_sids: %d", info->num_sids);

            /* 1st SID is account sid, see wbclient.h */
            wbc_err = wbcSidToUid(&info->sids[0].sid, &gss_uid);
            if (!WBC_ERROR_IS_OK(wbc_err)) {
              LogCrit(COMPONENT_IDMAPPER,"wbcSidToUid for uid returned %s",
                      wbcErrorString(wbc_err));
              wbcFreeMemory(info);
              return 0;
            }

            /* 2nd SID is primary_group sid, see wbclient.h */
            wbc_err = wbcSidToGid(&info->sids[1].sid, &gss_gid);
            if (!WBC_ERROR_IS_OK(wbc_err)) {
              LogCrit(COMPONENT_IDMAPPER,"wbcSidToUid for gid returned %s\n",
                      wbcErrorString(wbc_err));
              wbcFreeMemory(info);
              return 0;
            }
            wbcFreeMemory(info);
            found_uid = true;
            found_gid = true;
          }
#endif
          LogFullDebug(COMPONENT_IDMAPPER,
                       "principal2uid: nfs4_gss_princ_to_ids %s failed %d (%s)",
                       principal, -rc, strerror(-rc));
#ifdef _MSPAC_SUPPORT
          if ((found_uid == true) && (found_gid == true))
          {
            goto principal_found;
          }
#endif
      
          return 0;
        }
#ifdef _MSPAC_SUPPORT
principal_found:
#endif
      if(uidmap_add(principal, gss_uid, 0) != ID_MAPPER_SUCCESS)
	{
	  LogCrit(COMPONENT_IDMAPPER,
		  "principal2uid: uidmap_add %s %u failed",
		  principal, gss_uid);
	  return 0;
	}
      if(uidgidmap_add(gss_uid, gss_gid) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "principal2uid: uidgidmap_add gss_uid %u gss_gid %u failed",
                  gss_uid, gss_gid);
          return 0;
        }
    }

  LogFullDebug(COMPONENT_IDMAPPER,
               "principal2uid: uidmap_get mapped %s to uid= %u",
               principal, gss_uid);
  *puid = gss_uid;

  return 1;
#else
  return 0 ;
#endif /* _USE_NFSIDMAP */
}                               /* principal2uid */
#endif                          /* _HAVE_GSSAPI */

/**
 *
 * gid2name: convert a gid to a name. 
 *
 * convert a uid to a name. 
 *
 * @param name [OUT]  the name of the user
 * @param gid  [IN]   the input gid
 *
 * return 1 if successful, 0 otherwise
 *
 */
int gid2name(char *name, gid_t * pgid, size_t namesize)
{
#ifndef _USE_NFSIDMAP
  struct group g;
#ifndef _SOLARIS
  struct group *pg = NULL;
#endif
  static char buff[NFS4_MAX_DOMAIN_LEN]; /* Working area for getgrnam_r */
#endif

#ifdef _USE_NFSIDMAP
  int rc;

  if(gnamemap_get(*pgid, name, namesize) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "gid2name: gnamemap_get gid %u returned %s",
                   *pgid, name);
      return 1;
    }
  else
    {
      if(!nfsidmap_set_conf())
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "gid2name: nfsidmap_set_conf failed");
          return 0;
        }

      rc = nfs4_gid_to_name(*pgid, idmap_domain, name, namesize);
      if(rc != 0)
        {
          LogInfo(COMPONENT_IDMAPPER,
                   "gid2name: nfs4_gid_to_name %u returned %d (%s)",
                   *pgid, -rc, strerror(-rc));
          return 0;
        }

      LogFullDebug(COMPONENT_IDMAPPER,
                   "gid2name: nfs4_gid_to_name gid %u returned %s",
                   *pgid, name);

      if(gidmap_add(name, *pgid, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "gid2name: gidmap_add %s %u failed",
                  name, *pgid);
          return 0;
        }
    }

  return 1;

#else
  if(gnamemap_get(*pgid, name, namesize) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "gid2name: gnamemap_get gid %u returned %s",
                   *pgid, name);
      return 1;
    }
  else
    {
#ifdef _SOLARIS
      if(getgrgid_r(*pgid, &g, buff, sizeof(buff)) != 0)
#else
      if((getgrgid_r(*pgid, &g, buff, sizeof(buff), &pg) != 0) ||
	 (pg == NULL))
#endif                          /* _SOLARIS */
        {
          LogCrit(COMPONENT_IDMAPPER,
                       "gid2name: getgrgid_r %u failed",
                       *pgid);
          return 0;
        }

      strmaxcpy(name, g.gr_name, namesize);

      LogFullDebug(COMPONENT_IDMAPPER,
                   "gid2name: getgrgid_r gid %u returned %s",
                   *pgid, name);

      if(gidmap_add(name, *pgid, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "gid2name: gidmap_add %s %u failed",
                  name, *pgid);
          return 0;
        }
    }

  return 1;
#endif                          /* _USE_NFSIDMAP */
}                               /* gid2name */

/**
 *
 * name2gid: convert a name to a gid
 *
 * convert a name to a gid
 *
 * @param name [IN]  the name of the user
 * @param pgid [OUT] the resulting gid
 *
 * return 1 if successful, 0 otherwise
 *
 */
int name2gid(char *name, gid_t * pgid)
{
  gid_t gid;
  char *at = NULL, *end = NULL;

  if(gidmap_get(name, &gid) == ID_MAPPER_SUCCESS)
    {
      LogFullDebug(COMPONENT_IDMAPPER,
                   "name2gid: gidmap_get mapped %s to gid= %u",
                   name, gid);
      *pgid = gid;
      goto success;
    }
  else
    {
#ifdef _USE_NFSIDMAP
      int rc;
      if(!nfsidmap_set_conf())
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "name2gid: nfsidmap_set_conf failed");
          goto v3compat;
        }

      rc = nfs4_name_to_gid(name, pgid);
      if(rc)
        {
          LogInfo(COMPONENT_IDMAPPER,
                       "name2gid: nfs4_name_to_gid %s failed %d (%s)",
                       name, -rc, strerror(-rc));
          goto v3compat;
        }

      LogFullDebug(COMPONENT_IDMAPPER,
                   "name2gid: nfs4_name_to_gid %s returned %u",
                   name, *pgid);

      if(gidmap_add(name, *pgid, 1) != ID_MAPPER_SUCCESS)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "name2gid: gidmap_add %s %u failed",
                  name, *pgid);
        }
      goto success;

#else
      struct group g;
      struct group *pg = NULL;
      static char buff[NFS4_MAX_DOMAIN_LEN]; /* Working area for getgrnam_r */

#ifdef _SOLARIS
      if((pg = getgrnam_r(name, &g, buff, sizeof(buff))) == NULL)
#else
      if(getgrnam_r(name, &g, buff, sizeof(buff), &pg) != 0)
#endif
        {
          LogCrit(COMPONENT_IDMAPPER,
                       "name2gid: getgrnam_r %s failed",
                       name);
          goto v3compat;
        }
      else if (pg != NULL)
        {
          *pgid = pg->gr_gid;

          if(gidmap_add(name, pg->gr_gid, 1) != ID_MAPPER_SUCCESS)
            {
              LogMajor(COMPONENT_IDMAPPER,
                       "name2gid: gidmap_add %s %u failed",
                       name, pg->gr_gid);
            }
          goto success;
        }
#endif                          /* _USE_NFSIDMAP */
v3compat:
      /* If the name is numeric with no leading zeros and with no '@', 
       * return the int value if numeric user/group names are allowed by the configuration */
      at = strchr(name, '@');
      if(!at && name[0] != '0')
        {
          gid = strtol(name, &end, 10);
          if(end && *end != '\0')
            return 0;
       
          if(gidmap_add(name, gid, 0) != ID_MAPPER_SUCCESS)
            {
              /* Failure to update the in-core table is not fatal */
              LogMajor(COMPONENT_IDMAPPER,
                      "name2gid: gidmap_add %s %u failed",
                      name, gid);
            }
          *pgid = gid;
          goto success;
        }
      return 0;
    }

success:
  return 1;
}                               /* name2gid */

/**
 *
 * uid2ustr: convert a uid to a string.
 *
 * Convert a gidonvert a uid to a string. to a string.
 *
 * @param uid [IN]  the input gid
 * @param str [OUT] computed string
 *
 * @return the length of the utf8 buffer if succesfull, -1 if failed
 *
 */
int uid2str(uid_t uid, char *str, size_t bufsize)
{
  char buffer[NFS4_MAX_DOMAIN_LEN];
  uid_t local_uid = uid;
  int rc;

  if(uid2name(buffer, &local_uid, sizeof(buffer)) == 0)
    return -1;

#ifndef _USE_NFSIDMAP
  rc = snprintf(str, bufsize, "%s@%s", buffer, nfs_param.nfsv4_param.domainname);
#else
  rc = snprintf(str, bufsize, "%s", buffer);
#endif

  LogDebug(COMPONENT_IDMAPPER,
           "uid2str %u returning %s",
           uid, str);

  return rc;
}                               /* uid2utf8 */

/**
 *
 * gid2ustr: convert a gid to a string.
 *
 * Convert a gidonvert a gid to a string. to a string.
 *
 * @param gid [IN]  the input gid
 * @param str [OUT] computed string
 *
 * @return the length of the utf8 buffer if succesfull, -1 if failed
 *
 */
int gid2str(gid_t gid, char *str, size_t bufsize)
{
  char buffer[NFS4_MAX_DOMAIN_LEN];
  gid_t local_gid = gid;
  int rc;

  if(gid2name(buffer, &local_gid, sizeof(buffer)) == 0)
    return -1;

#ifndef _USE_NFSIDMAP
  rc = snprintf(str, bufsize, "%s@%s", buffer, nfs_param.nfsv4_param.domainname);
#else
  rc = snprintf(str, bufsize, "%s", buffer);
#endif

  LogDebug(COMPONENT_IDMAPPER,
           "gid2str %u returning %s",
           gid, str);

  return rc;
}                               /* gid2str */

/**
 *
 * uid2utf8: converts a uid to a utf8 string descriptor.
 *
 * Converts a uid to a utf8 string descriptor.
 *
 * @param uid     [IN]  the input uid
 * @param utf8str [OUT] computed UTF8 string descriptor
 *
 * @return the length of the utf8 buffer if succesfull, -1 if failed
 *
 */
int uid2utf8(uid_t uid, utf8string * utf8str)
{
  char buff[NFS4_MAX_DOMAIN_LEN];
  unsigned int len = 0;

  if(uid2str(uid, buff, sizeof(buff)) == -1)
    return -1;

  len = strlen(buff);

  /* A matching uid was found, now do the conversion to utf8 */
  if((utf8str->utf8string_val = gsh_malloc(len)) == NULL)
    return -1;
  else
    utf8str->utf8string_len = len;

  return str2utf8(buff, utf8str);

}                               /* uid2utf8 */

/**
 *
 * gid2utf8: converts a gid to a utf8 string descriptor.
 *
 * Converts a gid to a utf8 string descriptor.
 *
 * @param gid     [IN]  the input gid
 * @param utf8str [OUT] computed UTF8 string descriptor
 *
 * @return the length of the utf8 buffer if succesfull, -1 if failed
 *
 */
int gid2utf8(gid_t gid, utf8string * utf8str)
{
  char buff[NFS4_MAX_DOMAIN_LEN];
  unsigned int len = 0;

  if(gid2str(gid, buff, sizeof(buff)) == -1)
    return -1;

  len = strlen(buff);

  /* A matching gid was found */
  /* Do the conversion to uft8 format */
  if((utf8str->utf8string_val = gsh_malloc(len)) == NULL)
    return -1;
  else
    utf8str->utf8string_len = len;

  return str2utf8(buff, utf8str);
}                               /* gid2utf8 */

void drop_at_domain(char * buff)
{
  char * c = buff;

  /* Look for '@' in string */
  while(*c != '\0' && *c != '@')
    c++;

  /* Whether there was an @ or not, string ends at current position. */
  *c = '\0';
}

/**
 *
 *  utf82uid: converts a utf8 string descriptor to a uid .
 *
 * Converts a utf8 string descriptor to a uid.
 *
 * @param utf8str [IN]  group's name as UTF8 string.
 * @param Uid     [OUT] pointer to the computed uid.
 *
 * @return 0 if successful, -1 otherwise.
 */
void utf82uid(utf8string * utf8str, uid_t * Uid, uid_t anon_uid)
{
  char buff[2 * NFS4_MAX_DOMAIN_LEN];
  int  rc;

  if(utf8str->utf8string_len == 0)
    {
      *Uid = anon_uid;                /* Nobody */
      LogCrit(COMPONENT_IDMAPPER,
              "utf82uid: empty user name mapped to uid %u", anon_uid);
      return;
    }

  if(utf82str(buff, sizeof(buff), utf8str) == -1)
    {
      *Uid = anon_uid;                /* Nobody */
      LogCrit(COMPONENT_IDMAPPER,
              "utf82uid: invalid UTF8 name mapped to uid %u", anon_uid);
      return;
    }

#ifndef _USE_NFSIDMAP
  /* User is shown as a string 'user@domain', remove it if libnfsidmap is not used */
  drop_at_domain(buff);
#endif

  rc = name2uid(buff, Uid);

  if(rc == 0)
    {
      *Uid = anon_uid;                /* Nobody */
      LogDebug(COMPONENT_IDMAPPER,
               "utf82uid: Mapped %s to anonymous uid = %u",
               buff, *Uid);
    }
  else
    {
      LogDebug(COMPONENT_IDMAPPER,
               "utf82uid: Mapped %s to uid = %u",
               buff, *Uid);
    }

}                               /* utf82uid */

/**
 *
 *  utf82gid: converts a utf8 string descriptorto a gid .
 *
 * Converts a utf8 string descriptor to a gid .
 *
 * @param utf8str [IN]  group's name as UTF8 string.
 * @param Gid     [OUT] pointer to the computed gid.
 *
 * @return 0 in all cases
 */
void utf82gid(utf8string * utf8str, gid_t * Gid, gid_t anon_gid)
{
  char buff[2 * NFS4_MAX_DOMAIN_LEN];
  int  rc;

  if(utf8str->utf8string_len == 0)
    {
      *Gid = anon_gid;                /* Nobody */
      LogCrit(COMPONENT_IDMAPPER,
              "utf82gid: empty group name mapped to gid %u", anon_gid);
      return;
    }

  if(utf82str(buff, sizeof(buff), utf8str) == -1)
    {
      *Gid = anon_gid;                /* Nobody */
      LogCrit(COMPONENT_IDMAPPER,
              "utf82uid: invalid UTF8 group name mapped to uid %u", anon_gid);
      return;
    }

#ifndef _USE_NFSIDMAP
  /* Group is shown as a string 'group@domain' , remove it if libnfsidmap is not used */
  drop_at_domain(buff);
#endif

  rc = name2gid(buff, Gid);

  if(rc == 0)
    {
      *Gid = anon_gid;                /* Nobody */
      LogDebug(COMPONENT_IDMAPPER,
               "utf82gid: Mapped %s to anonymous gid = %u",
               buff, *Gid);
    }
  else
    {
      LogDebug(COMPONENT_IDMAPPER,
               "utf82gid: Mapped %s to gid = %u",
               buff, *Gid);
    }

}                               /* utf82gid */
