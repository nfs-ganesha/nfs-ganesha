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
 * \file    9p_fid.c
 * \brief   9P internal routines to manage fids
 *
 * 9p_fid.c : _9P_interpretor, protocol's service functions dedicated to fids's management.
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
#include "9p.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "fsal.h"

hash_table_t * ht_fid ;

unsigned long int _9p_hash_fid_key_value_hash_func(hash_parameter_t * p_hparam,
                                                   hash_buffer_t * buffclef)
{
  _9p_hash_fid_key_t * p9pkey = (_9p_hash_fid_key_t *)buffclef->pdata;

  return (unsigned long int)( (p9pkey->sockfd+1 + p9pkey->fid+1 + p9pkey->birth.tv_sec + p9pkey->birth.tv_usec) % p_hparam->index_size ) ;
} /* _9p_hash_fid_key_value_hash_func */


unsigned long int _9p_hash_fid_rbt_hash_func(hash_parameter_t * p_hparam,
                                              hash_buffer_t * buffclef)
{
  _9p_hash_fid_key_t * p9pkey = (_9p_hash_fid_key_t *)buffclef->pdata;

  return (unsigned long int)( ( p9pkey->sockfd+1) * (p9pkey->fid+1) + (p9pkey->birth.tv_sec + p9pkey->birth.tv_usec) )  ;
} /* _9p_hash_fid_key_value_hash_func */


int _9p_compare_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  _9p_hash_fid_key_t * p9pkey1 = (_9p_hash_fid_key_t *)buff1->pdata;
  _9p_hash_fid_key_t * p9pkey2 = (_9p_hash_fid_key_t *)buff2->pdata;

  if( p9pkey1 == NULL || p9pkey2 == NULL )
    return 1;

  if( p9pkey1->sockfd != p9pkey2->sockfd )
    return 1;

  if( p9pkey1->fid != p9pkey2->fid )
    return 1;

  if( p9pkey1->birth.tv_sec != p9pkey2->birth.tv_sec )
    return 1;

  if( p9pkey1->birth.tv_usec != p9pkey2->birth.tv_usec )
    return 1;

  /* Keys are the same */
  return 0 ;
}

int display_9p_hash_fid_key(hash_buffer_t * pbuff, char *str)
{
  return sprintf( str, "sockfd=%lu,fid=%u", 
                  ((_9p_hash_fid_key_t *)pbuff->pdata)->sockfd, 
	 	  ((_9p_hash_fid_key_t *)pbuff->pdata)->fid ) ; 
} /* display_9p_hash_fid_key */

int display_9p_hash_fid_val(hash_buffer_t * pbuff, char *str)
{
  return sprintf( str, "type=%u,version=%u,path=%llu", 
                  ((_9p_qid_t *)pbuff->pdata)->type, 
                  ((_9p_qid_t *)pbuff->pdata)->version, 
	 	  (unsigned long long)((_9p_qid_t *)pbuff->pdata)->path ) ; 
} /* display_9p_hash_fid_val */

_9p_fid_t * _9p_hash_fid_get( _9p_conn_t * pconn, 
                              u32 fid,
                              int * prc )
{
 _9p_hash_fid_key_t key ;
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0 ;

  if( !pconn || !prc ) 
    return NULL ;

  HashTable_Print( ht_fid ) ;

  /* Prepare struct to be inserted to the Hash */
  key.sockfd = pconn->sockfd ;
  key.birth = pconn->birth ;
  key.fid = fid ;

  buffkey.pdata = (caddr_t)&key ;
  buffkey.len = sizeof(_9p_hash_fid_key_t);

  /* Call HashTable */
  if( ( rc = HashTable_Get( ht_fid, 
                            &buffkey, 
                            &buffdata ) ) != HASHTABLE_SUCCESS )
   {
      *prc = ENOENT ;
      return NULL ;
   }
 
  return (_9p_fid_t *)(buffdata.pdata) ;
} /* _9p_hash_fid_get */

int _9p_hash_fid_update( _9p_conn_t * pconn, 
                         _9p_fid_t  * pfid ) /* This fid has to be obtained from a pool */
{
  _9p_hash_fid_key_t key ;
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0 ;

  if( !pconn || !pfid ) 
    return -1 ;

  /* Prepare struct to be inserted to the Hash */
  key.sockfd = pconn->sockfd ;
  key.birth = pconn->birth ;
  key.fid = pfid->fid ;

  buffkey.pdata = (caddr_t)&key ;
  buffkey.len = sizeof(_9p_hash_fid_key_t);

  buffdata.pdata = (caddr_t) pfid;
  buffdata.len = sizeof(_9p_fid_t);

  /* Call HashTable */
  if( ( rc = HashTable_Test_And_Set( ht_fid, 
                                     &buffkey, 
                                     &buffdata, 
                                     HASHTABLE_SET_HOW_SET_OVERWRITE ) ) != HASHTABLE_SUCCESS )
    return -rc ;

  return 0 ;
} /* _9p_hash_fid_add */

int _9p_hash_fid_del( _9p_conn_t * pconn, 
                      u32 fid,
                      _9p_fid_t ** ppoldfid ) 
{
   _9p_hash_fid_key_t key ;
  hash_buffer_t buffkey;
  hash_buffer_t old_key, old_value;

  int rc = 0 ; 

  if( !pconn ) 
    return -1 ;

  /* Prepare the key */
  key.sockfd = pconn->sockfd ;
  key.birth = pconn->birth ;
  key.fid = fid ;

  buffkey.pdata = (caddr_t)&key ;
  buffkey.len = sizeof(_9p_hash_fid_key_t);

  if( (rc = HashTable_Del( ht_fid, &buffkey, &old_key, &old_value)) != HASHTABLE_SUCCESS )
   return -rc ;

  if( ppoldfid != NULL )
    *ppoldfid = (_9p_fid_t *)old_value.pdata ;

  return 0 ;
} /* 9p_hash_fid_del */


int _9p_hash_fid_init( _9p_parameter_t * pparam )
{
   if((ht_fid = HashTable_Init(pparam->hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "9P: Cannot init FID's Hashtable");
      return -1;
    }

  return 0 ;
} /* _9p_hash_fid_init */


