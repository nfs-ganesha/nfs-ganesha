/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * \file    9p_proto_tools.c
 * \brief   9P version
 *
 * 9p_proto_tools.c : _9P_interpretor, protocol's service functions
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
#include <sys/types.h>
#include <pwd.h>
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "9p.h"

int _9p_init(  _9p_parameter_t * pparam ) 
{
  return _9p_hash_fid_init( pparam ) ;
} /* _9p_init */

int _9p_tools_get_fsal_op_context( int uname_len, char * uname_str, _9p_fid_t * pfid ) 
{
  char name[1024] ;
  char buff[1024];
  struct passwd p;
  struct passwd *pp;
  uid_t uid ;
  gid_t gid ;
  fsal_status_t fsal_status ;

  strncpy( name, uname_str, uname_len) ;

  if(name2uid(name, &uid) )
    {
      LogFullDebug(COMPONENT_IDMAPPER, "uidmap_get mapped %s to uid= %d",
                   name, uid);
    }
  else
    return -ENOENT ;

  if((getpwuid_r( uid, &p, buff, MAXPATHLEN, &pp) != 0) || (pp == NULL))
    {
      LogFullDebug(COMPONENT_IDMAPPER, "getpwuid_r %d failed", uid ) ;
      return -ENOENT;
    }
  else
   gid = p.pw_gid ;
  

  fsal_status = FSAL_GetClientContext( &pfid->fsal_op_context,
                                       &pfid->pexport->FS_export_context,
                                       uid, gid, NULL, 0 ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
   return -fsal_status.major ; 

  return 0 ;
} /* _9p_tools_get_fsal_cred */

int _9p_tools_errno( cache_inode_status_t cache_status )
{
  return (int)cache_status ; /** @todo put a more sophisticated stuff here */
} /* _9p_tools_errno */
