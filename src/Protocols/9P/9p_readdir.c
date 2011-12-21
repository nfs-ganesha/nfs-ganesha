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
#include "stuff_alloc.h"
#include "log_macros.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"

u8 qid_type_file = _9P_QTFILE ;
u8 qid_type_symlink = _9P_QTSYMLINK ;
u8 qid_type_dir = _9P_QTDIR ;

int _9p_readdir( _9p_request_data_t * preq9p, 
                 void  * pworker_data,
                 u32 * plenout, 
                 char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  int rc = 0 ;
  u32 err = 0 ;

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
  cache_inode_dir_entry_t **dirent_array = NULL;
  cache_inode_endofdir_t eod_met;

  unsigned int cookie = 0;
  unsigned int end_cookie = 0;
  unsigned int estimated_num_entries = 0 ;
  unsigned int num_entries = 0 ;
  int unlock = FALSE ;
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
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

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

  if((dirent_array = (cache_inode_dir_entry_t **) Mem_Alloc_Label(
          estimated_num_entries * sizeof(cache_inode_dir_entry_t*),
          "cache_inode_dir_entry_t in _9p_readdir")) == NULL)
    {
      err = EIO ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }


  if(cache_inode_readdir( pfid->pentry,
                          pfid->pexport->cache_inode_policy,
                          cookie,
                          estimated_num_entries,
                          &num_entries,
                          (uint64_t *)&end_cookie,
                          &eod_met,
                          dirent_array,
                          pwkrdata->ht,
                          &unlock,
                          &pwkrdata->cache_inode_client,
                          &pfid->fsal_op_context, 
                          &cache_status) != CACHE_INODE_SUCCESS)
    {
      err = _9p_tools_errno( cache_status ) ; ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  /* Never go behind _9P_MAXDIRCOUNT */
  if( num_entries > _9P_MAXDIRCOUNT ) num_entries = _9P_MAXDIRCOUNT ;

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RREADDIR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  /* Remember dcount position for later use */
  _9p_savepos( cursor, dcount_pos, u32 ) ;


  /* fills in the dentry in 9P marshalling */
  for( i = 0 ; i < num_entries ; i++ )
   {
     recsize = 0 ; 

     /* Build qid */
     switch( dirent_array[i]->pentry->internal_md.type )
      {
        case REGULAR_FILE:
          qid_path = (u64 *)&dirent_array[i]->pentry->object.file.attributes.fileid ;
          qid_type = &qid_type_file ;
	  break ;

        case CHARACTER_FILE:
        case BLOCK_FILE:
        case SOCKET_FILE:
        case FIFO_FILE:
          qid_path = (u64 *)&dirent_array[i]->pentry->object.special_obj.attributes.fileid ;
          qid_type = &qid_type_file ;
	  break ;

        case SYMBOLIC_LINK:
          qid_path = (u64 *)&dirent_array[i]->pentry->object.symlink->attributes.fileid ;
          qid_type = &qid_type_symlink;
	  break ;

        case DIRECTORY:
        case FS_JUNCTION:
          qid_path = (u64 *)&dirent_array[i]->pentry->object.dir.attributes.fileid ;
          qid_type = &qid_type_dir ;
	  break ;

        case UNASSIGNED:
        case RECYCLED:
        default:
          LogMajor( COMPONENT_9P, "implementation error, you should not see this message !!!!!!" ) ;
          err = EINVAL ;
          rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
          return rc ;
          break ;
      }

     /* Get dirent name information */
     name_str = dirent_array[i]->name.name ;
     name_len = dirent_array[i]->name.len ;
 
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
  
     LogDebug( COMPONENT_9P, "RREADDIR dentry: tag=%u fid=%u recsize=%u dentry={ off=%llu,qid=(type=%u,version=%u,path=%llu),type=%u,name=%s",
              (u32)*msgtag, *fid , recsize, (unsigned long long)i, *qid_type, 0, (unsigned long long)*qid_path, *qid_type, name_str) ;
   } /* for( i = 0 , ... ) */


  if( !CACHE_INODE_KEEP_CONTENT( pfid->pentry->policy ) )
    cache_inode_release_dirent( dirent_array, num_entries, &pwkrdata->cache_inode_client ) ;
  Mem_Free((char *)dirent_array);

  
  /* Set buffsize in previously saved position */
  _9p_setvalue( dcount_pos, dcount, u32 ) ; 

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RREADDIR: tag=%u fid=%u dcount=%u",
            (u32)*msgtag, *fid , dcount ) ;

  return 1 ;
}

