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
 * \file    9p_readdir.c
 * \brief   9P version
 *
 * 9p_readdir.c : _9P_interpretor, request READDIR
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"
#include "abstract_mem.h"

char pathdot[] = "." ;
char pathdotdot[] = ".." ;

typedef struct _9p_cb_entry
{
   u64          qid_path ;
   u8         * qid_type ;
   char         d_type   ; /* Attention, this is a VFS d_type, not a 9P type */
   const char * name_str ;
   u16          name_len ;
   uint64_t     cookie   ;
} _9p_cb_entry_t ;

typedef struct _9p_cb_data 
{
   u8         * cursor ;
   unsigned int count ;
   unsigned int max ;
} _9p_cb_data_t ;

static inline u8 *fill_entry(u8 *cursor, u8 qid_type, u64 qid_path, u64 cookie, u8 d_type, u16 name_len, const char *name_str) {
  /* qid in 3 parts */
  _9p_setvalue( cursor, qid_type, u8 ) ; /* 9P entry type */
  _9p_setvalue( cursor, 0, u32 ) ; /* qid_version set to 0 to prevent the client from caching */
  _9p_setvalue( cursor, qid_path, u64 ) ;

  /* offset */
  _9p_setvalue( cursor, cookie, u64 ) ;

  /* Type (this time outside the qid)) */
  _9p_setvalue( cursor, d_type, u8 ) ; /* VFS d_type (like in getdents) */

  /* name */
  _9p_setstr( cursor, name_len, name_str ) ;

  return cursor;
}

static bool _9p_readdir_callback( void                         * opaque,
                                  const char                   * name,
                                  const struct fsal_obj_handle * handle,
                                  uint64_t                       cookie)
{
   _9p_cb_data_t * cb_data = opaque ;
   int name_len = strlen(name);
   u8 qid_type, d_type;

  if( cb_data == NULL )
   return false ;

  if( cb_data->count + 24 + name_len > cb_data->max )
   return false ;


  switch( handle->attributes.type ) 
   {
      case FIFO_FILE:
        qid_type = _9P_QTFILE ;
        d_type = DT_FIFO ;
        break ;

      case CHARACTER_FILE:
        qid_type = _9P_QTFILE ;
        d_type = DT_CHR ;
        break ;

      case BLOCK_FILE:
        qid_type = _9P_QTFILE ;
        d_type = DT_BLK ;
        break ;

      case REGULAR_FILE:
        qid_type = _9P_QTFILE ;
        d_type = DT_REG ;
        break ;

      case SOCKET_FILE:
        qid_type = _9P_QTFILE ;
        d_type = DT_SOCK ;
        break ;

      case FS_JUNCTION:
      case DIRECTORY:
        qid_type = _9P_QTDIR ;
        d_type = DT_DIR ;
        break ;

      case SYMBOLIC_LINK:
        qid_type = _9P_QTSYMLINK ;
        d_type = DT_LNK ;
        break ;
    
      default:
        return false;  
   }

  /* Add 13 bytes in recsize for qid + 8 bytes for offset + 1 for type + 2 for strlen = 24 bytes*/
  cb_data->count += 24 + name_len ;
 
  cb_data->cursor = fill_entry(cb_data->cursor, qid_type, handle->attributes.fileid, cookie, d_type, name_len, name);

  return true ;
}

int _9p_readdir( _9p_request_data_t * preq9p,
                 void  * pworker_data,
                 u32 * plenout,
                 char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  _9p_cb_data_t cb_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * offset = NULL ;
  u32 * count  = NULL ;

  u32  dcount      = 0 ;
  
  char * dcount_pos = NULL ;

  cache_inode_status_t cache_status;
  bool eod_met;
  cache_entry_t * pentry_dot_dot = NULL ;

  uint64_t cookie = 0LL ;
  unsigned int num_entries = 0 ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  _9p_fid_t * pfid = NULL ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, offset, u64 ) ; 
  _9p_getptr( cursor, count,  u32 ) ; 
  
  LogDebug( COMPONENT_9P, "TREADDIR: tag=%u fid=%u offset=%llu count=%u",
            (u32)*msgtag, *fid, (unsigned long long)*offset, *count  ) ;

  if( *fid >= _9P_FID_PER_CONN )
   return  _9p_rerror( preq9p, pworker_data,  msgtag, ERANGE, plenout, preply ) ;

   pfid = preq9p->pconn->fids[*fid] ;

  /* Make sure the requested amount of data respects negotiated msize */
  if (*count + _9P_ROOM_RREADDIR > preq9p->pconn->msize)
        return  _9p_rerror( preq9p, pworker_data,  msgtag, ERANGE, plenout, preply ) ;

  /* Check that it is a valid fid */
  if (pfid == NULL || pfid->pentry == NULL) 
  {
    LogDebug( COMPONENT_9P, "request on invalid fid=%u", *fid ) ;
    return  _9p_rerror( preq9p, pworker_data,  msgtag, EIO, plenout, preply ) ;
  }

  /* For each entry, returns:
   * qid     = 13 bytes
   * offset  = 8 bytes
   * type    = 1 byte
   * namelen = 2 bytes
   * namestr = ~16 bytes (average size)
   * -------------------
   * total   = ~40 bytes (average size) per dentry */

  if( *count < 52 ) /* require room for . and .. */
    return  _9p_rerror( preq9p, pworker_data,  msgtag, EIO, plenout, preply ) ;

  /* Build the reply - it'll just be overwritten if error */
  _9p_setinitptr( cursor, preply, _9P_RREADDIR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  /* Remember dcount position for later use */
  _9p_savepos( cursor, dcount_pos, u32 ) ;

   /* Is this the first request ? */
  if( *offset == 0LL )
   {
      /* compute the parent entry */
     cache_status = cache_inode_lookupp(pfid->pentry,
					&pfid->op_context,
					&pentry_dot_dot);
      if(pentry_dot_dot == NULL )
        return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;


      /* Deal with "." and ".." */
      cursor = fill_entry(cursor, _9P_QTDIR, pfid->pentry->obj_handle->attributes.fileid, 1LL, DT_DIR, strlen( pathdot ), pathdot);
      dcount += 24 + strlen( pathdot ) ;

      cursor = fill_entry(cursor, _9P_QTDIR, pentry_dot_dot->obj_handle->attributes.fileid, 2LL, DT_DIR, strlen( pathdotdot ), pathdotdot);
      dcount += 24 + strlen( pathdotdot ) ;

      /* put the parent */
      cache_inode_put(pentry_dot_dot);

      cookie = 0LL ;
   }
  else if( *offset == 1LL )
   {
      /* compute the parent entry */
      cache_status = cache_inode_lookupp(pfid->pentry,
					 &pfid->op_context,
					 &pentry_dot_dot);
      if (pentry_dot_dot == NULL)
        return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;

      cursor = fill_entry(cursor, _9P_QTDIR, pentry_dot_dot->obj_handle->attributes.fileid, 2LL, DT_DIR, strlen( pathdotdot ), pathdotdot);
      dcount += 24 + strlen( pathdotdot ) ;

      /* put the parent */
      cache_inode_put(pentry_dot_dot);

      cookie = 0LL ;
   }
  else if( *offset == 2LL )
   {
      cookie = 0LL ;
   }
  else
   {
     cookie = (uint64_t)(*offset) ;
   }


  cb_data.cursor = cursor ;
  cb_data.count = dcount ;
  cb_data.max = *count ;

  cache_status = cache_inode_readdir(pfid->pentry,
				     cookie,
				     &num_entries,
				     &eod_met,
				     &pfid->op_context,
				     _9p_readdir_callback,
				     &cb_data);
  if(cache_status != CACHE_INODE_SUCCESS)
   {
     /* The avl lookup will try to get the next entry after 'cookie'. If none is found CACHE_INODE_NOT_FOUND is returned */
     /* In the 9P logic, this situation just mean "end of directory reached */
     if( cache_status != CACHE_INODE_NOT_FOUND )
       return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;
   }


  cursor = cb_data.cursor;

  /* Set buffsize in previously saved position */
  _9p_setvalue( dcount_pos, cb_data.count, u32 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RREADDIR: tag=%u fid=%u dcount=%u",
            (u32)*msgtag, *fid , dcount ) ;

  return 1 ;
}

