/*
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
 * @addtogroup uid2grp
 * @{
 */

/**
 * @file uid2grp.c
 * @brief Uid to group list conversion 
 */

#include "config.h"
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs_tools.h"
#include <unistd.h> /* for using gethostname */
#include <stdlib.h> /* for using exit */
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <stdbool.h>
#include "common_utils.h"
#include "uid2grp.h"

#define MAX_GRP 100
bool pwentname2grp( char * namebuff, uid_t * puid, struct group_data * pgdata ) 
{
  char buff[1024];
  struct passwd p;
  struct passwd *pp;
 
  if((getpwnam_r( namebuff, &p, buff, MAXPATHLEN, &pp) != 0) || (pp == NULL))
    {
      LogEvent(COMPONENT_IDMAPPER, "getpwnam_r %s failed", namebuff ) ;
      return false;
    }
 
  /** @todo Waste of memory here. To be fixed */ 
  if( ( pgdata->pgroups = (gid_t *)gsh_malloc( MAX_GRP * sizeof( gid_t ) ) ) == NULL )
    return false ;

  pgdata->nbgroups = MAX_GRP ;
  if( getgrouplist( p.pw_name, p.pw_gid, pgdata->pgroups, &pgdata->nbgroups ) == -1 )
   {
      LogEvent(COMPONENT_IDMAPPER, "getgrouplist %s failed", p.pw_name ) ;
      gsh_free(pgdata->pgroups) ;
      return false;
   }

  /* Resize pgroups to what it should be */
  gsh_realloc(  pgdata->pgroups, pgdata->nbgroups*sizeof( gid_t ) ) ;

  /* Set puid */
  *puid = p.pw_uid ;
 
  /* Set uid/gid */
  pgdata->uid = p.pw_uid ; 
  pgdata->gid = p.pw_gid ; 

  return true ;
}

bool pwentuid2grp( uid_t uid, struct gsh_buffdesc * name, struct group_data * pgdata ) 
{
  char buff[1024];
  struct passwd p;
  struct passwd *pp;
 
  if((getpwuid_r( uid, &p, buff, MAXPATHLEN, &pp) != 0) || (pp == NULL))
    {
      LogEvent(COMPONENT_IDMAPPER, "getpwnam_r %u failed", uid ) ;
      return false;
    }
 
  /** @todo Waste of memory here. To be fixed */ 
  if( ( pgdata->pgroups = (gid_t *)gsh_malloc( MAX_GRP * sizeof( gid_t ) ) ) == NULL )
    return false ;

  pgdata->nbgroups = MAX_GRP ;
  if( getgrouplist( p.pw_name, p.pw_gid, pgdata->pgroups, &pgdata->nbgroups ) == -1 )
   {
      LogEvent(COMPONENT_IDMAPPER, "getgrouplist %s failed", p.pw_name ) ;
      gsh_free(pgdata->pgroups) ;
      return false;
   }

  /* Resize pgroups to what it should be */
  pgdata->pgroups = gsh_realloc(  pgdata->pgroups, pgdata->nbgroups*sizeof( gid_t ) ) ;

  /* Set puid */
  name->addr = p.pw_name ;
  name->len = strlen( p.pw_name ) ;

  /* Set uid/gid */
  pgdata->uid = p.pw_uid ; 
  pgdata->gid = p.pw_gid ; 

  return true ;
}

/**
 * @brief Convert a name to an ID
 *
 * @param[in]  name  The name of the user
 * @param[out] id    The resulting id
 * @param[in]  group True if this is a group name
 * @param[in]  anon  ID to return if look up fails
 *
 * @return true if successful, false otherwise
 */

bool name2grp( const struct gsh_buffdesc * name, struct group_data ** pgdata ) 
{
  bool success;
  uid_t uid=-1 ;

  pthread_rwlock_rdlock(&uid2grp_user_lock);

  success = uid2grp_lookup_by_uname(name, &uid, pgdata );

  pthread_rwlock_unlock(&uid2grp_user_lock);

  if (success)
    return true;
  else
    {
      /* Something we can mutate and count on as terminated */
      char *namebuff = alloca(name->len + 1);

      memcpy(namebuff, name->addr, name->len);
      *(namebuff + name->len) = '\0';

 
      if( pwentname2grp( namebuff, &uid, *pgdata ) )
       {
         pthread_rwlock_wrlock(&uid2grp_user_lock);

         success = uid2grp_add_user(name, uid, *pgdata ) ;

         pthread_rwlock_unlock(&uid2grp_user_lock);

         if (!success)
	   LogMajor(COMPONENT_IDMAPPER, "name2grp %s", namebuff ) ;
         return true;
       }
      else
	return false ;
    }
}

bool uid2grp( uid_t uid, struct group_data ** pgdata ) 
{
  bool success;
   
  struct gsh_buffdesc name;
  struct gsh_buffdesc * pname = &name ;

  pthread_rwlock_rdlock(&uid2grp_user_lock);

  success = uid2grp_lookup_by_uid( uid, &pname, pgdata );

  pthread_rwlock_unlock(&uid2grp_user_lock);

  if (success)
    return true;
  else
    {
      if( pwentuid2grp( uid, &name, *pgdata ) )
       {
         pthread_rwlock_wrlock(&uid2grp_user_lock);

         success = uid2grp_add_user(&name, uid, *pgdata ) ;

         pthread_rwlock_unlock(&uid2grp_user_lock);

         if (!success)
	   LogMajor(COMPONENT_IDMAPPER, "uid2grp %u", uid ) ;
         return true;
       }
      else
	return false ;
    }
}

/** @} */
