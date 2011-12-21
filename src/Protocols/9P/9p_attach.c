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
 * \file    9p_attach.c
 * \brief   9P version
 *
 * 9p_attach.c : _9P_interpretor, request ATTACH
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
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"


int _9p_attach( _9p_request_data_t * preq9p, 
                void  * pworker_data,
                u32 * plenout, 
                char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid = NULL ;
  u32 * afid = NULL ;
  u16 * uname_len = NULL ;
  char * uname_str = NULL ;
  u16 * aname_len = NULL ;
  char * aname_str = NULL ;
  u32 * n_aname = NULL ;

  fsal_attrib_list_t fsalattr ;

  int rc = 0 ;
  u32 err = 0 ;
 
  _9p_fid_t * pfid = NULL ;

  exportlist_t * pexport = NULL;
  unsigned int found = FALSE;
  cache_inode_status_t cache_status ;
  cache_inode_fsal_data_t fsdata ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, afid,   u32 ) ; 
  _9p_getstr( cursor, uname_len, uname_str ) ;
  _9p_getstr( cursor, aname_len, aname_str ) ;
  _9p_getptr( cursor, n_aname, u32 ) ; 

  LogDebug( COMPONENT_9P, "TATTACH: tag=%u fid=%u afid=%d uname='%.*s' aname='%.*s' n_uname=%d", 
            (u32)*msgtag, *fid, *afid, (int)*uname_len, uname_str, (int)*aname_len, aname_str, *n_aname ) ;

  /*
   * Find the export for the aname (using as well Path or Tag ) 
   */
  for( pexport = nfs_param.pexportlist; pexport != NULL;
       pexport = pexport->next)
    {
      if(aname_str[0] != '/')
        {
          /* The input value may be a "Tag" */
          if(!strncmp(aname_str, pexport->FS_tag, strlen( pexport->FS_tag ) ) )
            {
	      found = TRUE ;
              break;
            }
        }
      else
        {
          if(!strncmp(aname_str, pexport->fullpath, strlen( pexport->fullpath ) ) )
           {
	      found = TRUE ;
              break;
           }
        }
    } /* for */

  /* Did we find something ? */
  if( found == FALSE )
    {
      err = ENOENT ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }
 
  /* Set pexport and fid id in fid */
  pfid= &preq9p->pconn->fids[*fid] ;
  pfid->pexport = pexport ;
  pfid->fid = *fid ;
 
#ifdef _USE_SHARED_FSAL
  /* At this step, the export entry is known and it's required to use the right fsalid */
  FSAL_SetId( pexport->fsalid ) ;

  memcpy( &pfid->fsal_op_context, &pwkrdata->thread_fsal_context[pexport->fsalid], sizeof( fsal_op_context_t ) ) ;
#else
  memcpy( &pfid->fsal_op_context, &pwkrdata->thread_fsal_context, sizeof( fsal_op_context_t ) ) ;
#endif

  /* Is user name provided as a string or as an uid ? */
  if( *uname_len != 0 )
   {
     /* Build the fid creds */
    if( ( err = _9p_tools_get_fsal_op_context_by_name( *uname_len, uname_str, pfid ) ) !=  0 )
     {
       err = -err ; /* The returned value from 9p service functions is always negative is case of errors */
       rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
       return rc ;
     }
   }
  else
   {
    /* Build the fid creds */
    if( ( err = _9p_tools_get_fsal_op_context_by_uid( *n_aname, pfid ) ) !=  0 )
     {
       err = -err ; /* The returned value from 9p service functions is always negative is case of errors */
       rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
       return rc ;
     }

   }

  /* Get the related pentry */
  memcpy( (char *)&fsdata.handle, (char *)pexport->proot_handle, sizeof( fsal_handle_t ) ) ;
  fsdata.cookie = 0;

  pfid->pentry = cache_inode_get( &fsdata,
                                  pexport->cache_inode_policy,      
                                  &fsalattr, 
                                  pwkrdata->ht,
                                  &pwkrdata->cache_inode_client,
                                  &pfid->fsal_op_context, 
                                  &cache_status ) ;

  if( pfid->pentry == NULL )
   {
     err = _9p_tools_errno( cache_status ) ; ;
     rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
     return rc ;
   }


  /* Compute the qid */
  pfid->qid.type = _9P_QTDIR ;
  pfid->qid.version = 0 ; /* No cache, we want the client to stay synchronous with the server */
  pfid->qid.path = fsalattr.fileid ;

  /* Cache the attr */
  _9p_tools_fsal_attr2stat( &fsalattr, &pfid->attr ) ;

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RATTACH ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setqid( cursor, pfid->qid ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RATTACH: tag=%u fid=%u qid=(type=%u,version=%u,path=%llu)", 
            *msgtag, *fid, (u32)pfid->qid.type, pfid->qid.version, (unsigned long long)pfid->qid.path ) ;

  return 1 ;
}

