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
 * \file    9p_xattrcreate.c
 * \brief   9P version
 *
 * 9p_xattrcreate.c : _9P_interpretor, request XATTRCREATE
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
#include <sys/types.h>
#include <attr/xattr.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"


int _9p_xattrcreate( _9p_request_data_t * preq9p, 
                     void  * pworker_data,
                     u32 * plenout, 
                     char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  u8   * pmsgtype =  preq9p->_9pmsg + _9P_HDR_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;
  int create = FALSE ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * size ;
  u32 * flag ;
  u16 * name_len ;
  char * name_str ;

  _9p_fid_t * pfid = NULL ;

  fsal_status_t fsal_status ; 
  char name[MAXNAMLEN];

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getstr( cursor, name_len, name_str ) ;
  _9p_getptr( cursor, size,   u64 ) ; 
  _9p_getptr( cursor, flag,   u32 ) ; 

  LogDebug( COMPONENT_9P, "TXATTRCREATE: tag=%u fid=%u name=%.*s size=%llu flag=%u",
            (u32)*msgtag, *fid, *name_len, name_str, (unsigned long long)*size, *flag ) ;

  if( *fid >= _9P_FID_PER_CONN )
    return  _9p_rerror( preq9p, pworker_data,  msgtag, ERANGE, plenout, preply ) ;

  pfid = &preq9p->pconn->fids[*fid] ;

  /* Check that it is a valid fid */
  if (pfid->pentry == NULL) 
  {
    LogDebug( COMPONENT_9P, "request on invalid fid=%u", *fid ) ;
    return  _9p_rerror( preq9p, pworker_data,  msgtag, EIO, plenout, preply ) ;
  }
  
  snprintf( name, MAXNAMLEN, "%.*s", *name_len, name_str ) ;

  if( *size == 0LL ) 
   {
     /* Size == 0 : this is in fact a call to removexattr */
      LogDebug( COMPONENT_9P, "TXATTRCREATE: tag=%u fid=%u : will remove xattr %s",
                 (u32)*msgtag, *fid,  name ) ;
    
    fsal_status = pfid->pentry->obj_handle->ops->remove_extattr_by_name( pfid->pentry->obj_handle, name ) ;

     if(FSAL_IS_ERROR(fsal_status))
       return   _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_inode_error_convert(fsal_status) ),  plenout, preply ) ;
   }
  else
   {
     /* Size != 0 , this is a creation/replacement of xattr */

     /* Create the xattr at the FSAL level and cache result */
     if( ( pfid->specdata.xattr.xattr_content = gsh_malloc( XATTR_BUFFERSIZE ) ) == NULL ) 
       return  _9p_rerror( preq9p, pworker_data,  msgtag, ENOMEM, plenout, preply ) ;

     if( ( *flag == 0 ) ||  (*flag & XATTR_CREATE ) ) 
        create = TRUE ;
     else
        create = FALSE ;

     fsal_status = pfid->pentry->obj_handle->ops->setextattr_value( pfid->pentry->obj_handle,
                                                                    name,
                                                                    pfid->specdata.xattr.xattr_content, 
                                                                    *size,
                                                                    create )  ;

     if(FSAL_IS_ERROR(fsal_status))
       return   _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_inode_error_convert(fsal_status) ),  plenout, preply ) ;

     fsal_status = pfid->pentry->obj_handle->ops->getextattr_id_by_name( pfid->pentry->obj_handle,
                                                                         name, 
                                                                         &pfid->specdata.xattr.xattr_id);

     if(FSAL_IS_ERROR(fsal_status))
       return   _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_inode_error_convert(fsal_status) ),  plenout, preply ) ;
   }

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RXATTRCREATE ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RXATTRCREATE: tag=%u fid=%u name=%.*s size=%llu flag=%u",
            (u32)*msgtag, *fid, *name_len, name_str, (unsigned long long)*size, *flag ) ;

  _9p_stat_update( *pmsgtype, TRUE, &pwkrdata->stats._9p_stat_req ) ;
  return 1 ;
} /* _9p_xattrcreate */

