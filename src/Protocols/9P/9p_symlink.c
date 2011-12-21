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
 * \file    9p_symlink.c
 * \brief   9P version
 *
 * 9p_symlink.c : _9P_interpretor, request SYMLINK
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

int _9p_symlink( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u16  * name_len = NULL ;
  char * name_str = NULL ;
  u16  * linkcontent_len = NULL ;
  char * linkcontent_str = NULL ;
  u32 * gid = NULL ;

  _9p_fid_t * pfid = NULL ;
  _9p_qid_t qid_symlink ;

  cache_entry_t       * pentry_symlink = NULL ;
  fsal_name_t           symlink_name ; 
  fsal_attrib_list_t    fsalattr ;
  cache_inode_status_t  cache_status ;
  fsal_accessmode_t mode = 0777;
  cache_inode_create_arg_t create_arg;

  int rc = 0 ; 
  int err = 0 ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 

  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getstr( cursor, name_len,        name_str ) ;
  _9p_getstr( cursor, linkcontent_len, linkcontent_str ) ;
  _9p_getptr( cursor, gid,    u32 ) ;

  LogDebug( COMPONENT_9P, "TSYMLINK: tag=%u fid=%u name=%.*s linkcontent=%.*s gid=%u",
            (u32)*msgtag, *fid, *name_len, name_str, *linkcontent_len, linkcontent_str, *gid ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

   pfid = &preq9p->pconn->fids[*fid] ;
 
   snprintf( symlink_name.name, FSAL_MAX_NAME_LEN, "%.*s", *name_len, name_str ) ;
   snprintf( create_arg.link_content.path, FSAL_MAX_PATH_LEN, "%.*s", *linkcontent_len, linkcontent_str ) ;

   /* Let's do the job */
   /* BUGAZOMEU: @todo : the gid parameter is not used yet, flags is not yet used */
   if( ( pentry_symlink = cache_inode_create( pfid->pentry,
                                              &symlink_name,
                                              SYMBOLIC_LINK,
                                              pfid->pexport->cache_inode_policy,
                                              mode,
                                              &create_arg,
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
   qid_symlink.type    = _9P_QTSYMLINK ;
   qid_symlink.version = 0 ;
   qid_symlink.path    = fsalattr.fileid ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RSYMLINK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setqid( cursor, qid_symlink ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, 
            "RSYMLINK: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu)",
            (u32)*msgtag, *fid, *name_len, name_str, qid_symlink.type, qid_symlink.version, (unsigned long long)qid_symlink.path ) ;


  return 1 ;
}

