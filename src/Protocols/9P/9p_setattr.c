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
 * \file    9p_setattr.c
 * \brief   9P version
 *
 * 9p_setattr.c : _9P_interpretor, request SETATTR
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
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"

int _9p_setattr( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16  * msgtag     = NULL ;
  u32  * fid        = NULL ;
  u32  * valid      = NULL ;
  u32  * mode       = NULL ;
  u32  * uid        = NULL ;
  u32  * gid        = NULL ;
  u64  * size       = NULL ;
  u64  * atime_sec  = NULL ;
  u64  * atime_nsec = NULL ;
  u64  * mtime_sec  = NULL ;
  u64  * mtime_nsec = NULL ;

  _9p_fid_t * pfid = NULL ;

  fsal_attrib_list_t    fsalattr ;
  cache_inode_status_t  cache_status ;

  struct timeval t;

  int rc = 0 ; 
  int err = 0 ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 

  _9p_getptr( cursor, fid,        u32 ) ; 
  _9p_getptr( cursor, valid,      u32 ) ;
  _9p_getptr( cursor, mode,       u32 ) ;
  _9p_getptr( cursor, uid,        u32 ) ;
  _9p_getptr( cursor, gid,        u32 ) ;
  _9p_getptr( cursor, size,       u64 ) ;
  _9p_getptr( cursor, atime_sec,  u64 ) ;
  _9p_getptr( cursor, atime_nsec, u64 ) ;
  _9p_getptr( cursor, mtime_sec,  u64 ) ;
  _9p_getptr( cursor, mtime_nsec, u64 ) ;

  LogDebug( COMPONENT_9P, "TSETATTR: tag=%u fid=%u mode=0%o uid=%u gid=%u size=%llu atime=(%llu|%llu) mtime=(%llu|%llu)",
            (u32)*msgtag, *fid, *mode, *uid, *gid, *size,  (unsigned long long)*atime_sec, (unsigned long long)*atime_nsec, 
            (unsigned long long)*mtime_sec, (unsigned long long)*mtime_nsec  ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  pfid = &preq9p->pconn->fids[*fid] ;

  /* If a "time" change is required, but not with the "_set" suffix, use gettimeofday */
  if( *valid & (_9P_SETATTR_ATIME|_9P_SETATTR_CTIME|_9P_SETATTR_MTIME) )
   {
     if( gettimeofday( &t, NULL ) == -1 )
       {
         LogMajor( COMPONENT_9P, "TSETATTR: tag=%u fid=%u ERROR !! gettimeofday returned -1 with errno=%u",
                   (u32)*msgtag, *fid, errno ) ;

         err = errno ;
         rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
         return rc ;
       }
   }

  /* Let's do the job */
  memset( (char *)&fsalattr, 0, sizeof( fsalattr ) ) ;
  if( *valid & _9P_SETATTR_MODE )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_MODE ;
      fsalattr.mode = *mode ;
   }

  if( *valid & _9P_SETATTR_UID )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_OWNER ;
      fsalattr.owner = *uid ;
   }

  if( *valid & _9P_SETATTR_GID )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_GROUP ;
      fsalattr.group = *gid ;
   }

  if( *valid & _9P_SETATTR_SIZE )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_SIZE ;
      fsalattr.filesize = *size ;
   }

  if( *valid & _9P_SETATTR_ATIME )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_ATIME ;
      fsalattr.atime.seconds  = t.tv_sec ;
      fsalattr.atime.nseconds = t.tv_usec * 1000 ;
   }

  if( *valid & _9P_SETATTR_MTIME )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_MTIME ;
      fsalattr.mtime.seconds  = t.tv_sec ;
      fsalattr.mtime.nseconds = t.tv_usec * 1000 ;
   }

  if( *valid & _9P_SETATTR_CTIME )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_CTIME ;
      fsalattr.ctime.seconds  = t.tv_sec ;
      fsalattr.ctime.nseconds = t.tv_usec * 1000 ;
   }

  if( *valid & _9P_SETATTR_ATIME_SET )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_ATIME ;
      fsalattr.atime.seconds  = *atime_sec ;
      fsalattr.atime.nseconds = *atime_nsec ;
   }

  if( *valid & _9P_SETATTR_MTIME_SET )
   {
      fsalattr.asked_attributes |= FSAL_ATTR_MTIME ;
      fsalattr.mtime.seconds  = *mtime_sec ;
      fsalattr.mtime.nseconds = *mtime_nsec ;
   }

  /* Now set the attr */ 
  if( cache_inode_setattr( pfid->pentry,
			   &fsalattr,
                           pwkrdata->ht,
			   &pwkrdata->cache_inode_client,
			   &pfid->fsal_op_context,
                           &cache_status ) != CACHE_INODE_SUCCESS )
    {
      err = _9p_tools_errno( cache_status ) ; ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RSETATTR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  return 1 ;
}

