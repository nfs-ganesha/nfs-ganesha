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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_clunk.c
 * \brief   9P version
 *
 * 9p_clunk.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "fsal.h"
#include "9p.h"
#include "export_mgr.h"

/**
 * @brief Free this fid after releasing its resources.
 *
 * @param pfid   [IN] pointer to fid entry
 * @param fid    [IN] pointer to fid acquired from message
 * @param preq9p [IN] pointer to request data
 */

static void free_fid(_9p_fid_t * pfid,
		     u32 *fid,
		     _9p_request_data_t * preq9p)
{
	struct gsh_export *exp;

        cache_inode_put(pfid->pentry);
        if( pfid->from_attach )
         {
	   exp = container_of(pfid->pexport, struct gsh_export, export);
	   put_gsh_export(exp);
         }
	gsh_free(pfid);
	preq9p->pconn->fids[*fid] = NULL; /* poison the entry */
}

int _9p_clunk( _9p_request_data_t * preq9p,
               void  * pworker_data,
               u32 * plenout,
               char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;

  _9p_fid_t * pfid = NULL ;
  cache_inode_status_t cache_status ;
  fsal_status_t fsal_status ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 

  LogDebug( COMPONENT_9P, "TCLUNK: tag=%u fid=%u", (u32)*msgtag, *fid ) ;

  if( *fid >= _9P_FID_PER_CONN )
    return  _9p_rerror( preq9p, pworker_data,  msgtag, ERANGE, plenout, preply ) ;

  pfid =  preq9p->pconn->fids[*fid] ;

  /* Check that it is a valid fid */
  if (pfid == NULL || pfid->pentry == NULL) 
  {
    LogDebug( COMPONENT_9P, "clunk request on invalid fid=%u", *fid ) ;
    return  _9p_rerror( preq9p, pworker_data,  msgtag, EIO, plenout, preply ) ;
  }

  /* If the fid is related to a xattr, free the related memory */
  if( pfid->specdata.xattr.xattr_content != NULL )
  {

    if(  pfid->specdata.xattr.xattr_write == TRUE )
    {
      /* Check size give at TXATTRCREATE against the one resulting from the writes */
      if( pfid->specdata.xattr.xattr_size != pfid->specdata.xattr.xattr_offset )
      {
         free_fid(pfid, fid, preq9p);
         return  _9p_rerror( preq9p, pworker_data,  msgtag, EINVAL, plenout, preply ) ;
      }

      /* Write the xattr content */
      fsal_status = pfid->pentry->obj_handle->ops->setextattr_value_by_id( pfid->pentry->obj_handle,
                                                                           &pfid->op_context,
                                                                           pfid->specdata.xattr.xattr_id,
                                                                           pfid->specdata.xattr.xattr_content,
                                                                           pfid->specdata.xattr.xattr_size ) ;
      if(FSAL_IS_ERROR(fsal_status))
      {
         free_fid(pfid, fid, preq9p);
         return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_inode_error_convert(fsal_status) ), plenout, preply ) ;
      }
     }

    gsh_free( pfid->specdata.xattr.xattr_content ) ;
  }

  /* If object is an opened file, close it */
  if( ( pfid->pentry->type == REGULAR_FILE ) && 
      is_open( pfid->pentry ) )
   {
     if( pfid->opens )
      {
        cache_inode_dec_pin_ref(pfid->pentry, FALSE);
        pfid->opens = 0; /* dead */

        /* Under this flag, pin ref is still checked */
        cache_status = cache_inode_close(pfid->pentry,
				      CACHE_INODE_FLAG_REALLYCLOSE);
        if(cache_status != CACHE_INODE_SUCCESS)
        {
           free_fid(pfid, fid, preq9p);
           return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;
        }
	cache_status = cache_inode_refresh_attrs_locked(pfid->pentry, &pfid->op_context);
	if (cache_status != CACHE_INODE_SUCCESS && cache_status != CACHE_INODE_FSAL_ESTALE)
        {
           free_fid(pfid, fid, preq9p);
           return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;
        }
      }
   }

  free_fid(pfid, fid, preq9p);

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RCLUNK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RCLUNK: tag=%u fid=%u", (u32)*msgtag, *fid ) ;

  return 1 ;
}

