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

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#endif

#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_tools.h"
#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

extern nfs_parameter_t nfs_param;

#ifdef _USE_NFSIDMAP

#define _PATH_IDMAPDCONF     "/etc/idmapd.conf"
#define NFS4_MAX_DOMAIN_LEN 512
#define NFSIDMAP_VERBOSITY 99

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

void nfsidmap_logger(const char *str, ...)
{
  va_list arg;
  va_start(arg, str);
  LogDebug(COMPONENT_IDMAPPER, "nfsidmap_logger: ");
  vprintf(str, arg);
  va_end(arg);
}

int nfsidmap_set_conf()
{
  if(!nfsidmap_conf_read)
    {

      /* nfs4_set_debug(NFSIDMAP_VERBOSITY,&nfsidmap_logger); */

      if(nfs4_init_name_mapping(_PATH_IDMAPDCONF))
        return 0;

      if(nfs4_get_default_domain(NULL, idmap_domain, sizeof(idmap_domain)))
        return 0;

      nfsidmap_conf_read = TRUE;
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
int uid2name(char *name, uid_t * puid)
{
  char fqname[MAXNAMLEN];
#ifdef _USE_NFSIDMAP
  if(!nfsidmap_set_conf())
    return 0;

  if(unamemap_get(*puid, name) == ID_MAPPER_SUCCESS)
    {
      return 1;
    }
  else
    {
      if(!nfsidmap_set_conf())
        return 0;

      if(nfs4_uid_to_name(*puid, idmap_domain, name, MAXNAMLEN))
        return 0;

      strncpy(fqname, name, MAXNAMLEN);
      if(strchr(name, '@') == NULL)
        {
          sprintf(fqname, "%s@%s", name, idmap_domain);
          strncpy(name, fqname, MAXNAMLEN);
        }

      if(uidmap_add(fqname, *puid) != ID_MAPPER_SUCCESS)
        return 0;
    }
  return 1;

#else
  struct passwd p;
  struct passwd *pp;
  char buff[MAXPATHLEN];

  if(unamemap_get(*puid, name) == ID_MAPPER_SUCCESS)
    {
      return 1;
    }
  else
    {
#ifdef _SOLARIS
      if(getpwuid_r(*puid, &p, buff, MAXPATHLEN) != 0)
#else
      if((getpwuid_r(*puid, &p, buff, MAXPATHLEN, &pp) != 0) ||
	 (pp == NULL))
#endif                          /* _SOLARIS */
        return 0;

      strncpy(name, p.pw_name, MAXNAMLEN);
      if(uidmap_add(name, *puid) != ID_MAPPER_SUCCESS)
        return 0;

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
  struct passwd passwd;
  struct passwd *ppasswd;
  char buff[MAXPATHLEN];
  uid_t uid;
  gid_t gss_gid;
  uid_t gss_uid;
  char fqname[MAXNAMLEN];

  /* NFsv4 specific features: RPCSEC_GSS will provide user like nfs/<host>
   * choice is made to map them to root */
  if(!strncmp(name, "nfs/", 4))
    {
      /* This is a "root" request made from the hostbased nfs principal, use root */
      *puid = 0;

      return 1;
    }

  if(uidmap_get(name, (unsigned long *)&uid) == ID_MAPPER_SUCCESS)
    {
      *puid = uid;
    }
  else
    {
#ifdef _USE_NFSIDMAP
      if(!nfsidmap_set_conf())
        return 0;

      /* obtain fully qualified name */
      strncpy(fqname, name, MAXNAMLEN - 1);
      if(strchr(name, '@') == NULL)
        sprintf(fqname, "%s@%s", name, idmap_domain);

      if(nfs4_name_to_uid(fqname, puid))
        return 0;

      if(uidmap_add(fqname, *puid) != ID_MAPPER_SUCCESS)
        return 0;

#ifdef _USE_GSSRPC
      /* nfs4_gss_princ_to_ids required to extract uid/gid from gss creds
       * XXX: currently uses unqualified name as per libnfsidmap comments */
      if(nfs4_gss_princ_to_ids("krb5", name, &gss_uid, &gss_gid))
        return 0;
      if(uidgidmap_add(gss_uid, gss_gid) != ID_MAPPER_SUCCESS)
        return 0;
#endif                          /* _USE_GSSRPC */

#else                           /* _USE_NFSIDMAP */

#ifdef _SOLARIS
      if(getpwnam_r(name, &passwd, buff, MAXPATHLEN) != 0)
#else
      if(getpwnam_r(name, &passwd, buff, MAXPATHLEN, &ppasswd) != 0)
#endif                          /* _SOLARIS */
        {
          *puid = -1;
          return 0;
        }
      else
        {
          *puid = passwd.pw_uid;
#ifdef _USE_GSSRPC
          if(uidgidmap_add(passwd.pw_uid, passwd.pw_gid) != ID_MAPPER_SUCCESS)
            return 0;
#endif                          /* _USE_GSSRPC */
          if(uidmap_add(name, passwd.pw_uid) != ID_MAPPER_SUCCESS)
            return 0;

        }
#endif                          /* _USE_NFSIDMAP */
    }

  return 1;
}                               /* name2uid */

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
int gid2name(char *name, gid_t * pgid)
{
  struct group g;
  struct group *pg;
  char buff[MAXPATHLEN];
#ifdef _USE_NFSIDMAP

  if(gnamemap_get(*pgid, name) == ID_MAPPER_SUCCESS)
    {
      return 1;
    }
  else
    {
      if(!nfsidmap_set_conf())
        return 0;

      if(nfs4_gid_to_name(*pgid, idmap_domain, name, MAXNAMLEN))
        return 0;

      if(gidmap_add(name, *pgid) != ID_MAPPER_SUCCESS)
        return 0;
    }

  return 1;

#else
  if(gnamemap_get(*pgid, name) == ID_MAPPER_SUCCESS)
    {
      return 1;
    }
  else
    {
#ifdef _SOLARIS
      if(getgrgid_r(*pgid, &g, buff, MAXPATHLEN) != 0)
#else
      if((getgrgid_r(*pgid, &g, buff, MAXPATHLEN, &pg) != 0) ||
	 (pg == NULL))
#endif                          /* _SOLARIS */
        return 0;

      strncpy(name, g.gr_name, MAXNAMLEN);
      if(gidmap_add(name, *pgid) != ID_MAPPER_SUCCESS)
        return 0;

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
  struct group g;
  struct group *pg = NULL;
  static char buff[MAXPATHLEN]; /* Working area for getgrnam_r */
  gid_t gid;

  if(gidmap_get(name, (unsigned long *)&gid) == ID_MAPPER_SUCCESS)
    {
      *pgid = gid;
    }
  else
    {
#ifdef _USE_NFSIDMAP
      if(!nfsidmap_set_conf())
        return 0;

      if(nfs4_name_to_gid(name, pgid))
        return 0;

      if(gidmap_add(name, *pgid) != ID_MAPPER_SUCCESS)
        return 0;

#else

#ifdef _SOLARIS
      if(getgrnam_r(name, &g, buff, MAXPATHLEN) != 0)
#else
      if(getgrnam_r(name, &g, buff, MAXPATHLEN, &pg) != 0)
#endif
        {
          *pgid = -1;
          return 0;
        }
      else
        {
          *pgid = g.gr_gid;

          if(gidmap_add(name, g.gr_gid) != ID_MAPPER_SUCCESS)
            return 0;

        }
#endif                          /* _USE_NFSIDMAP */
    }
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
int uid2str(uid_t uid, char *str)
{
  char buffer[MAXNAMLEN];
  uid_t local_uid = uid;

  if(uid2name(buffer, &local_uid) == 0)
    return -1;
    
#ifndef _USE_NFSIDMAP
  return sprintf(str, "%s@%s", buffer, nfs_param.nfsv4_param.domainname);
#else

  return sprintf(str, "%s", buffer);
#endif
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
int gid2str(gid_t gid, char *str)
{
  char buffer[MAXNAMLEN];
  gid_t local_gid = gid;

  if(gid2name(buffer, &local_gid) == 0)
    return -1;

#ifndef _USE_NFSIDMAP
  return sprintf(str, "%s@%s", buffer, nfs_param.nfsv4_param.domainname);
#else

  return sprintf(str, "%s", buffer);
#endif
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
  char buff[MAXNAMLEN];
  unsigned int len = 0;

  if(uid2str(uid, buff) == -1)
    return -1;

  len = strlen(buff);

  /* A matching uid was found, now do the conversion to utf8 */
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("uid2utf8");
#endif

  if((utf8str->utf8string_val = (char *)Mem_Alloc(len)) == NULL)
    return -1;
  else
    utf8str->utf8string_len = len;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

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
  char buff[MAXNAMLEN];
  unsigned int len = 0;

  if(gid2str(gid, buff) == -1)
    return -1;

  len = strlen(buff);

  /* A matching gid was found */
  /* Do the conversion to uft8 format */
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("gid2utf8");
#endif

  if((utf8str->utf8string_val = (char *)Mem_Alloc(len)) == NULL)
    return -1;
  else
    utf8str->utf8string_len = len;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  return str2utf8(buff, utf8str);
}                               /* gid2utf8 */

/**
 *
 *  utf82gid: converts a utf8 string descriptor to a uid .
 *
 * Converts a utf8 string descriptor to a uid.
 *
 * @param utf8str [IN]  group's name as UTF8 string.
 * @param Uid     [OUT] pointer to the computed uid.
 *
 * @return 0 if successful, 0 otherwise.
 */
int utf82uid(utf8string * utf8str, uid_t * Uid)
{
  char buff[2 * MAXNAMLEN];
  char uidname[MAXNAMLEN];
  char domainname[MAXNAMLEN];

  if(utf8str->utf8string_len == 0)
    {
      *Uid = -1;                /* Nobofy */
      return -1;
    }

  strncpy(buff, utf8str->utf8string_val, utf8str->utf8string_len);
  buff[utf8str->utf8string_len] = '\0';

#ifndef _USE_NFSIDMAP
  /* User is shown as a string 'user@domain', remove it if libnfsidmap is not used */
  nfs4_stringid_split(buff, uidname, domainname);
#else
  strncpy(uidname, buff, MAXNAMLEN);
#endif

  name2uid(uidname, Uid);

  return 0;
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
int utf82gid(utf8string * utf8str, gid_t * Gid)
{
  char buff[2 * MAXNAMLEN];
  char gidname[MAXNAMLEN];
  char domainname[MAXNAMLEN];

  if(utf8str->utf8string_len == 0)
    {
      *Gid = -1;                /* Nobody */
      return 0;
    }

  strncpy(buff, utf8str->utf8string_val, utf8str->utf8string_len);
  buff[utf8str->utf8string_len] = '\0';

#ifndef _USE_NFSIDMAP
  /* Group is shown as a string 'group@domain' , remove it if libnfsidmap is not used */
  nfs4_stringid_split(buff, gidname, domainname);
#else
  strncpy(gidname, buff, MAXNAMLEN);
#endif

  name2gid(gidname, Gid);

  return 0;
}                               /* utf82gid */
