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
 * \file    9p_mkdir.c
 * \brief   9P version
 *
 * 9p_mkdir.c : _9P_interpretor, request MKDIR
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

int _9p_mkdir( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u32  * mode  = NULL ;
  u32  * gid   = NULL ;
  u16  * name_len = NULL ;
  char * name_str = NULL ;

  _9p_fid_t * pfid = NULL ;
  _9p_qid_t qid_newdir ;

  cache_entry_t       * pentry_newdir = NULL ;
  fsal_name_t           dir_name ; 
  fsal_attrib_list_t    fsalattr ;
  cache_inode_status_t  cache_status ;

  int rc = 0 ; 
  int err = 0 ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 

  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getstr( cursor, name_len, name_str ) ;
  _9p_getptr( cursor, mode,   u32 ) ;
  _9p_getptr( cursor, gid,    u32 ) ;

  LogDebug( COMPONENT_9P, "TMKDIR: tag=%u fid=%u name=%.*s mode=0%o gid=%u",
            (u32)*msgtag, *fid, *name_len, name_str, *mode, *gid ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

   pfid = &preq9p->pconn->fids[*fid] ;

  snprintf( dir_name.name, FSAL_MAX_NAME_LEN, "%.*s", *name_len, name_str ) ;

   /* Create the directory */

   /* BUGAZOMEU: @todo : the gid parameter is not used yet */
   if( ( pentry_newdir = cache_inode_create( pfid->pentry,
                                             &dir_name,
                                             DIRECTORY,
                                             pfid->pexport->cache_inode_policy,
                                             *mode,
                                             NULL,
                                             &fsalattr,
                                             pwkrdata->ht,
                                             &pwkrdata->cache_inode_client, 
                                             &pfid->fsal_op_context, 
     					     &cache_status)) == NULL)
   {
      err = _9p_tools_errno( cache_status ) ; ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
   }

   /* Build the qid */
   qid_newdir.type    = _9P_QTDIR ;
   qid_newdir.version = 0 ;
   qid_newdir.path    = fsalattr.fileid ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RMKDIR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setqid( cursor, qid_newdir ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, 
            "RMKDIR: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu)",
            (u32)*msgtag, *fid, *name_len, name_str, qid_newdir.type, qid_newdir.version, (unsigned long long)qid_newdir.path ) ;

  return 1 ;
}

