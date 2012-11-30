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
 * \file    9p_lock.c
 * \brief   9P version
 *
 * 9p_lock.c : _9P_interpretor, request LOCK
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
#include <netdb.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "sal_functions.h"
#include "fsal.h"
#include "9p.h"

extern int h_errno;

int _9p_lock( _9p_request_data_t * preq9p, 
              void  * pworker_data,
              u32 * plenout, 
              char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
 
  u16  * msgtag        = NULL ;
  u32  * fid           = NULL ;
  u8   * type          = NULL ;
  u32  * flags         = NULL ;
  u64  * start         = NULL ;
  u64  * length        = NULL ;
  u32  * proc_id       = NULL ;
  u16  * client_id_len = NULL ;
  char * client_id_str = NULL ;

  u8 status = 0  ;
  state_status_t state_status = STATE_SUCCESS;
  state_owner_t      * holder ;
  state_owner_t      * powner ;
  state_t              state ;
  fsal_lock_param_t    lock ;
  fsal_lock_param_t    conflict;

  char name[MAXNAMLEN+1] ;

  struct hostent *hp ;
  struct sockaddr_storage client_addr ; 

  _9p_fid_t * pfid = NULL ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  
  _9p_getptr( cursor, fid,     u32 ) ; 
  _9p_getptr( cursor, type,    u8 )  ;
  _9p_getptr( cursor, flags,   u32 ) ;
  _9p_getptr( cursor, start,   u64 ) ;
  _9p_getptr( cursor, length,  u64 ) ;
  _9p_getptr( cursor, proc_id, u32 ) ;
  _9p_getstr( cursor, client_id_len, client_id_str ) ;

  LogDebug( COMPONENT_9P, "TLOCK: tag=%u fid=%u type=%u flags=0x%x start=%llu length=%llu proc_id=%u client=%.*s",
            (u32)*msgtag, *fid, *type, *flags, (unsigned long long)*start, (unsigned long long)*length, 
            *proc_id, *client_id_len, client_id_str ) ;

  if( *fid >= _9P_FID_PER_CONN )
    return _9p_rerror( preq9p, msgtag, ERANGE, plenout, preply ) ;

  pfid = &preq9p->pconn->fids[*fid] ;

  /* get the client's ip addr */
  snprintf( name, MAXNAMLEN, "%.*s",*client_id_len, client_id_str ) ;

  if( ( hp = gethostbyname( name ) ) == NULL )
    return _9p_rerror( preq9p, msgtag, EINVAL, plenout, preply ) ;

  memcpy( (char *)&client_addr, hp->h_addr, hp->h_length ) ;

  if( ( powner = get_9p_owner( &client_addr, *proc_id ) ) == NULL )
    return _9p_rerror( preq9p, msgtag, EINVAL, plenout, preply ) ;

  /* Do the job */
  switch( *type )
   {
      case _9P_LOCK_TYPE_RDLCK:
      case _9P_LOCK_TYPE_WRLCK:
        /* Fill in plock */
        lock.lock_type   = (*type == _9P_LOCK_TYPE_WRLCK) ? FSAL_LOCK_W : FSAL_LOCK_R;
        lock.lock_start  = *start ;
        lock.lock_length = *length ;

        if(nfs_in_grace())
         {
             status = _9P_LOCK_GRACE ;
             break ;
         }

        if( state_lock( pfid->pentry,
                        &pfid->fsal_op_context,
                        pfid->pexport,
                        powner,
                        &state,
                        STATE_NON_BLOCKING,
                        NULL,
                        &lock,
                        &holder,
                        &conflict,
                        &state_status) != STATE_SUCCESS)
           {
              if( state_status == STATE_LOCK_BLOCKED ) 
                status = _9P_LOCK_BLOCKED ;
              else
                status = _9P_LOCK_ERROR ;
           }
        else
          status = _9P_LOCK_SUCCESS ;

        break ;

      case _9P_LOCK_TYPE_UNLCK:
         if(state_unlock( pfid->pentry,
                          &pfid->fsal_op_context,
                          pfid->pexport,
                          powner,
                          NULL,
                          &lock,
                          &state_status) != STATE_SUCCESS)
             status = _9P_LOCK_ERROR ;
         else
             status = _9P_LOCK_SUCCESS ;

        break ;

      default:
        return _9p_rerror( preq9p, msgtag, EINVAL, plenout, preply ) ;
        break ;
   } /* switch( *type ) */ 

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RLOCK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setvalue( cursor, status, u8 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RLOCK: tag=%u fid=%u type=%u flags=0x%x start=%llu length=%llu proc_id=%u client=%.*s status=%u",
            (u32)*msgtag, *fid, *type, *flags, (unsigned long long)*start, (unsigned long long)*length, 
            *proc_id, *client_id_len, client_id_str, status ) ;

  return 1 ;
}

