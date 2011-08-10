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

unsigned long int _9p_hash_fid_key_value_hash_func(hash_parameter_t * p_hparam,
                                                   hash_buffer_t * buffclef)
{
  //return hash_sockaddr((sockaddr_t *)buffclef->pdata, IGNORE_PORT) % p_hparam->index_size;
  return 1 ;
}

unsigned long int _9p_hash_fid_rbt_hash_func(hash_parameter_t * p_hparam,
                                              hash_buffer_t * buffclef)
{
  //return hash_sockaddr((sockaddr_t *)buffclef->pdata, IGNORE_PORT);
  return 1 ;
} 

int _9p_compare_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return 0 ;
}

int display_9p_hash_fid_key(hash_buffer_t * pbuff, char *str)
{
  return 0 ;
}

int display_9p_hash_fid_val(hash_buffer_t * pbuff, char *str)
{
  return 0 ;
}


int _9p_fid_new( _9p_conn_t * pconn) 
{
  return 0 ;
}

int _9p_fid_init( void )
{
  return 0 ;
}

