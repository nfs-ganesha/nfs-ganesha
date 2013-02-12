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
#include "log.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"
#include "abstract_mem.h"

u8 qid_type_file = _9P_QTFILE ;
u8 qid_type_symlink = _9P_QTSYMLINK ;
u8 qid_type_dir = _9P_QTDIR ;
char pathdot[] = "." ;
char pathdotdot[] = ".." ;

typedef struct _9p_cb_entry
{
   u64    qid_path ;
   u8   * qid_type ;
   char * name_str ;
   u16    name_len ;
} _9p_cb_entry_t ;

typedef struct _9p_cb_data 
{
   _9p_cb_entry_t * entries ;
   size_t count ;
   size_t max ;
} _9p_cb_data_t ;

static bool_t _9p_readdir_callback( void* opaque,
                                    char *name,
                                    cache_entry_t *entry,
                                    fsal_op_context_t *context,
                                    uint64_t cookie)
{
   _9p_cb_data_t * cb_data = opaque ;

  if( cb_data == NULL )
   return FALSE ;

  if( cb_data->count > cb_data->max )
   return FALSE ;

  cb_data->entries[cb_data->count].qid_path = entry->attributes.fileid ;
  cb_data->entries[cb_data->count].name_str = name ;
  cb_data->entries[cb_data->count].name_len = strlen( name ) ;
 
  switch( entry->attributes.type ) 
   {
      case FSAL_TYPE_FIFO:
      case FSAL_TYPE_CHR:
      case FSAL_TYPE_BLK:
      case FSAL_TYPE_FILE:
      case FSAL_TYPE_SOCK:
        cb_data->entries[cb_data->count].qid_type = &qid_type_file ;
        break ;

      case FSAL_TYPE_JUNCTION:
      case FSAL_TYPE_DIR:
        cb_data->entries[cb_data->count].qid_type = &qid_type_dir ;
        break ;

      case FSAL_TYPE_LNK:
        cb_data->entries[cb_data->count].qid_type = &qid_type_symlink ;
        break ;
    
      default:
        return FALSE;  
   }
 
  cb_data->count += 1 ; 
  return TRUE ;

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
  u32  recsize     = 0 ;
  u16  name_len    = 0 ;
  
  char * name_str = NULL ;

  u8  * qid_type    = NULL ;
  u64 * qid_path    = NULL ;

  char * dcount_pos = NULL ;

  cache_inode_status_t cache_status;
  bool_t eod_met;
  cache_entry_t * pentry_dot_dot = NULL ;

  unsigned int cookie = 0;
  unsigned int estimated_num_entries = 0 ;
  unsigned int num_entries = 0 ;
  unsigned int delta = 0 ;
  u64 i = 0LL ;

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
   return _9p_rerror( preq9p, msgtag, ERANGE, plenout, preply ) ;

   pfid = &preq9p->pconn->fids[*fid] ;

  /* Use Cache Inode to read the directory's content */
  cookie = (unsigned int)*offset ;

  /* For each entry, returns:
   * qid     = 13 bytes
   * offset  = 8 bytes
   * type    = 1 byte
   * namelen = 2 bytes
   * namestr = ~16 bytes (average size)
   * -------------------
   * total   = ~40 bytes (average size) per dentry */
  estimated_num_entries = (unsigned int)( *count / 40 ) ;

  if((cb_data.entries = gsh_calloc(estimated_num_entries,
                                   sizeof(_9p_cb_entry_t))) == NULL)
    return _9p_rerror( preq9p, msgtag, EIO, plenout, preply ) ;

   /* Is this the first request ? */
  if( *offset == 0 )
   {
      /* compute the parent entry */
      if( ( pentry_dot_dot = cache_inode_lookupp( pfid->pentry,
                                                  &pfid->fsal_op_context,
                                                  &cache_status ) ) == NULL )
        return _9p_rerror( preq9p, msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;

      /* Deal with "." and ".." */
      cb_data.entries[0].qid_path =  pfid->pentry->attributes.fileid ;
      cb_data.entries[0].qid_type =  &qid_type_dir ;
      cb_data.entries[0].name_str =  pathdot ;
      cb_data.entries[0].name_len =  strlen( pathdot ) ;


      cb_data.entries[1].qid_path =  pentry_dot_dot->attributes.fileid ;
      cb_data.entries[1].qid_type =  &qid_type_dir ;
      cb_data.entries[1].name_str =  pathdotdot ;
      cb_data.entries[1].name_len =  strlen( pathdotdot ) ;

      delta = 2 ;
   }
  else
   delta = 0 ;

  if( *offset == 2 )
   {
      /* offset == 2 as an input as one and only reason:
       *   - a former call with offset=0 was made and the dir was empty
       *   - '.' and '..' were returned and nothing else
       *   - the client makes a new call, expecting it to have empty return
       */
      num_entries = 0 ; /* Empty return */
   }
  else
   {
     cb_data.count = delta ;
     cb_data.max = _9P_MAXDIRCOUNT - delta ;

     if(cache_inode_readdir( pfid->pentry,
                             cookie,
                             &num_entries,
                             &eod_met,
                             &pfid->fsal_op_context,
                             0, /* no attr */
                             _9p_readdir_callback,
                             &cb_data,
                             &cache_status) != CACHE_INODE_SUCCESS)
        return _9p_rerror( preq9p, msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;
   }
  /* Never go behind _9P_MAXDIRCOUNT */
  if( num_entries > _9P_MAXDIRCOUNT ) num_entries = _9P_MAXDIRCOUNT ;


  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RREADDIR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  /* Remember dcount position for later use */
  _9p_savepos( cursor, dcount_pos, u32 ) ;

  /* fills in the dentry in 9P marshalling */
  for( i = 0 ; i < num_entries + delta ; i++ )
   {
     recsize = 0 ;

     /* Build qid */
     qid_path = &cb_data.entries[i].qid_path ;
     qid_type = cb_data.entries[i].qid_type ;

     /* Get dirent name information */
     name_str = cb_data.entries[i].name_str ;
     name_len = cb_data.entries[i].name_len ;

     /* Add 13 bytes in recsize for qid + 8 bytes for offset + 1 for type + 2 for strlen = 24 bytes*/
     recsize = 24 + name_len  ;

     /* Check if there is room left for another dentry */
     if( dcount + recsize > *count )
       break ; /* exit for loop */
     else
       dcount += recsize ;

     /* qid in 3 parts */
     _9p_setptr( cursor, qid_type, u8 ) ;
     _9p_setvalue( cursor, 0, u32 ) ; /* qid_version set to 0 to prevent the client from caching */
     _9p_setptr( cursor, qid_path, u64 ) ;
     
     /* offset */
     _9p_setvalue( cursor, i+cookie+1, u64 ) ;   

     /* Type (again ?) */
     _9p_setptr( cursor, qid_type, u8 ) ;

     /* name */
     _9p_setstr( cursor, name_len, name_str ) ;
  
     LogDebug( COMPONENT_9P, "RREADDIR dentry: recsize=%u dentry={ off=%llu,qid=(type=%u,version=%u,path=%llu),type=%u,name=%s",
               recsize, (unsigned long long)i+cookie+1, *qid_type, 0, (unsigned long long)*qid_path, 
               *qid_type, name_str ) ;
   } /* for( i = 0 , ... ) */

  gsh_free( cb_data.entries ) ;
  /* Set buffsize in previously saved position */
  _9p_setvalue( dcount_pos, dcount, u32 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RREADDIR: tag=%u fid=%u dcount=%u",
            (u32)*msgtag, *fid , dcount ) ;

  return 1 ;
}

