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
 * \file    9p_xattrwalk.c
 * \brief   9P version
 *
 * 9p_xattrwalk.c : _9P_interpretor, request XATTRWALK
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
#include "log.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"


int _9p_xattrwalk( _9p_request_data_t * preq9p, 
                   void  * pworker_data,
                   u32 * plenout, 
                   char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  // nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u32 * attrfid = NULL ;
  u16 * name_len ;
  char * name_str ;
  u64 attrsize = 0LL ;

  int rc = 0 ;
  u32 err = 0 ;

  fsal_status_t fsal_status ; 
  fsal_name_t name;

  _9p_fid_t * pfid = NULL ;
  _9p_fid_t * pxattrfid = NULL ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, attrfid, u32 ) ; 

  LogDebug( COMPONENT_9P, "TXATTRWALK: tag=%u fid=%u attrfid=%u" ,
            (u32)*msgtag, *fid, *attrfid ) ;

  _9p_getstr( cursor, name_len, name_str ) ;
  LogDebug( COMPONENT_9P, "TXATTRWALK (component): tag=%u fid=%u attrfid=%u nwnames=%.*s",
            (u32)*msgtag, *fid, *attrfid, *name_len, name_str ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }
 
  if( *attrfid >= _9P_FID_PER_CONN )
   {
     err = ERANGE ;
     rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
     return rc ;
   }
 
  pfid = &preq9p->pconn->fids[*fid] ;
  pxattrfid = &preq9p->pconn->fids[*attrfid] ;

  /* Initiate xattr's fid by copying file's fid in it */
  memcpy( (char *)pxattrfid, (char *)pfid, sizeof( _9p_fid_t ) ) ;

  snprintf( name.name, FSAL_MAX_NAME_LEN, "%.*s", *name_len, name_str ) ;
  fsal_status = FSAL_GetXAttrIdByName( &pxattrfid->pentry->handle,
                                       &name, 
                                       &pxattrfid->fsal_op_context,
                                       &pxattrfid->specdata.xattr.xattr_id);

  if(FSAL_IS_ERROR(fsal_status))
   {
      err = _9p_tools_errno( cache_inode_error_convert(fsal_status) ) ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
   }

  if( ( pxattrfid->specdata.xattr.xattr_content = Mem_Alloc( XATTR_BUFFERSIZE ) ) == NULL ) 
    {
      err = ENOMEM ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  fsal_status = FSAL_GetXAttrValueByName( &pxattrfid->pentry->handle,
                                          &name, 
                                          &pxattrfid->fsal_op_context,
                                          pxattrfid->specdata.xattr.xattr_content, 
                                          XATTR_BUFFERSIZE, 
                                          &attrsize );

  if(FSAL_IS_ERROR(fsal_status))
   {
      err = _9p_tools_errno( cache_inode_error_convert(fsal_status) ) ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
   }

  printf( "---> name=%s id=%u size=%llu val=#%s#\n", 
          name.name, pxattrfid->specdata.xattr.xattr_id,
          attrsize, pxattrfid->specdata.xattr.xattr_content ) ;

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RXATTRWALK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setvalue( cursor, attrsize, u64 ) ; /* No xattr for now */

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RXATTRWALK: tag=%u fid=%u attrfid=%u name=%.*s size=%llu",
            (u32)*msgtag, *fid, *attrfid,  *name_len, name_str, attrsize ) ;

  return 1 ;
} /* _9p_xattrwalk */

