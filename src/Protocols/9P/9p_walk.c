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
 * \file    9p_walk.c
 * \brief   9P version
 *
 * 9p_walk.c : _9P_interpretor, request WALK
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


int _9p_walk( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u32 * newfid = NULL ;
  u16 * nwname = NULL ;
  u16 * wnames_len[_9P_MAXWELEM] ;
  char * wnames_str[_9P_MAXWELEM] ;

  u16 * nwqid ;

  unsigned int i = 0 ;
  int rc = 0 ;
  u32 err = 0 ;

  fsal_name_t name ; 
  fsal_attrib_list_t fsalattr ;
  cache_inode_status_t cache_status ;
  cache_entry_t * pentry = NULL ;

  _9p_fid_t * pfid = NULL ;
  _9p_fid_t * pnewfid = NULL ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, newfid, u32 ) ; 
  _9p_getptr( cursor, nwname, u16 ) ; 

  LogDebug( COMPONENT_9P, "TWALK: tag=%u fid=%u newfid=%u nwname=%u",
            (u32)*msgtag, *fid, *newfid, *nwname ) ;

  for( i = 0 ; i < *nwname ; i++ )
   {
     _9p_getstr( cursor, wnames_len[i], wnames_str[i] ) ;
      LogDebug( COMPONENT_9P, "TWALK (component): tag=%u fid=%u newfid=%u nwnames=%.*s",
                (u32)*msgtag, *fid, *newfid, *(wnames_len[i]), wnames_str[i] ) ;
   }

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }
 
  if( *newfid >= _9P_FID_PER_CONN )
       {
         err = ERANGE ;
         rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
         return rc ;
       }


  pfid = &preq9p->pconn->fids[*fid] ;
  pnewfid = &preq9p->pconn->fids[*newfid] ;

  /* Is this a lookup or a fid cloning operation ? */
  if( *nwname == 0 )
   {
      /* Cloning operation */
      memcpy( (char *)pnewfid, (char *)pfid, sizeof( _9p_fid_t ) ) ;
  
      /* Set the new fid id */
      pnewfid->fid = *newfid ;
   }
  else 
   {
      pnewfid->fid = *newfid ;
      pnewfid->fsal_op_context = pfid->fsal_op_context ;
      pnewfid->pexport = pfid->pexport ;

      /* the walk is in fact a lookup */
      pentry = pfid->pentry ;
      for( i = 0 ; i <  *nwname ; i ++ )
        {
           snprintf( name.name, FSAL_MAX_NAME_LEN, "%.*s", *(wnames_len[i]), wnames_str[i] ) ;
           LogDebug( COMPONENT_9P, "TWALK (lookup): tag=%u fid=%u newfid=%u (component %u/%u:%s)",
            (u32)*msgtag, *fid, *newfid, i+1, *nwname, name.name ) ;

           if( ( pnewfid->pentry = cache_inode_lookup( pentry,
                                                       &name,
                                                       pfid->pexport->cache_inode_policy,
                                                       &fsalattr,
                                                       pwkrdata->ht,
                                                       &pwkrdata->cache_inode_client,
                                                       &pfid->fsal_op_context, 
                                                       &cache_status ) ) == NULL )
            {
              err = _9p_tools_errno( cache_status ) ; ;
              rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
             return rc ;
            }
           pentry =  pnewfid->pentry ;
        }

     _9p_tools_fsal_attr2stat( &fsalattr, &pnewfid->attr ) ;

     /* Build the qid */
     pnewfid->qid.version = 0 ; /* No cache, we want the client to stay synchronous with the server */
     switch( pfid->pentry->internal_md.type )
      {
        case REGULAR_FILE:
          pnewfid->qid.path = (u64)pnewfid->pentry->object.file.attributes.fileid ;
          pnewfid->qid.type = _9P_QTFILE ;
	  break ;

        case CHARACTER_FILE:
        case BLOCK_FILE:
        case SOCKET_FILE:
        case FIFO_FILE:
          pnewfid->qid.path = (u64)pnewfid->pentry->object.special_obj.attributes.fileid ;
          pnewfid->qid.type = _9P_QTFILE ;
	  break ;

        case SYMBOLIC_LINK:
          pnewfid->qid.path = (u64)pnewfid->pentry->object.symlink->attributes.fileid ;
          pnewfid->qid.type = _9P_QTSYMLINK ;
	  break ;

        case DIRECTORY:
        case FS_JUNCTION:
          pnewfid->qid.path = (u64)pnewfid->pentry->object.dir.attributes.fileid ;
          pnewfid->qid.type = _9P_QTDIR ;
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

   }

  /* As much qid as requested fid */
  nwqid = nwname ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RWALK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  
  _9p_setptr( cursor, nwqid, u16 ) ;
  for( i = 0 ; i < *nwqid ; i++ )
    { 
      _9p_setqid( cursor, pnewfid->qid ) ;
    }

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RWALK: tag=%u fid=%u newfid=%u nwqid=%u",
            (u32)*msgtag, *fid, *newfid, *nwqid ) ;

  return 1 ;
}

