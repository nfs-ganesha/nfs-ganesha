/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2005)
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
 * \file    cache_inode_fsal_hash.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:39:23 $
 * \version $Revision: 1.9 $
 * \brief   Glue functions between the FSAL and the Cache inode layers.
 *
 * cache_inode_fsal_glue.c : Glue functions between the FSAL and the Cache inode layers.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "err_fsal.h"
#include "err_cache_inode.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <strings.h>
#include <sys/types.h>

/**
 *
 * cache_inode_fsal_hash_func: Compute the hash value for the cache_inode hash table.
 *
 * Computes the hash value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL.
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed hash value.
 *
 */
uint32_t cache_inode_fsal_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t * buffclef)
{
    unsigned long h = 0;
    char printbuf[512];
    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);

    h = FSAL_Handle_to_HashIndex(pfsal_handle, 0,
                                 p_hparam->alphabet_length,
                                 p_hparam->index_size);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, pfsal_handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func key: buff = (Handle=%s, Cookie=%"PRIu64"), hash value=%lu",
                         printbuf, 0UL, h);
        }

    return h;
}                               /* cache_inode_fsal_hash_func */

/**
 *
 * cache_inode_fsal_rbt_func: Compute the rbt value for the cache_inode hash table.
 *
 * Computes the rbt value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL.
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed rbt value.
 *
 */
uint64_t cache_inode_fsal_rbt_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef)
{
    /*
     * A polynomial function too, but reversed, to avoid
     * producing same value as decimal_simple_hash_func
     */
    uint32_t h = 0;
    char printbuf[512];
    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);

    h = Lookup3_hash_buff((char *)pfsal_handle, buffclef->len );

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, pfsal_handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt: buff = (Handle=%s, Cookie=%"PRIu64"), value=%u",
                         printbuf, 0UL, h);
        }
    return h;
}                               /* cache_inode_fsal_rbt_func */

unsigned long __cache_inode_fsal_rbt_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
    /*
     * A polynomial function too, but reversed, to avoid
     * producing same value as decimal_simple_hash_func
     */
    unsigned long h = 0;
    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);
    char printbuf[512];

    h = FSAL_Handle_to_RBTIndex(pfsal_handle, 0);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, pfsal_handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt: buff = (Handle=%s, Cookie=%"PRIu64"), value=%lu",
                         printbuf, 0UL, h);
        }
    return h;
}                               /* cache_inode_fsal_rbt_func */

/**
 *
 * cache_inode_fsal_rbt_both: Computes both the rbt value and the hash value in one pass.
 *
 *  Computes both the rbt value and the hash value in one pass.
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @param phashval [OUT] a pointer to uint32_t to receive the computed hash value
 * @param prbtval [OUT] a pointer to uint32_t to receive the computed rbt value
 *
 * @return 1 if successful, 0 if failed
 *
 */

static int cache_inode_fsal_rbt_both_on_fsal(hash_parameter_t * p_hparam,
                                             hash_buffer_t    * buffclef,
                                             uint32_t * phashval,
                                             uint64_t * prbtval)
{
    char printbuf[512];
    unsigned int rc = 0 ;
    /* ACE: This is a temporary hack so we don't have to change every FSAL
       right now. */
    unsigned int FSALindex = 0;
    unsigned int FSALrbt = 0;

    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);

    /**
     * @todo ACE: This is a temporary hack so we don't have to change
     * every FSAL right now.
     */

    rc = FSAL_Handle_to_Hash_both( pfsal_handle, 0,
				   p_hparam->alphabet_length, p_hparam->index_size,
				   &FSALindex, &FSALrbt);

    *phashval = FSALindex;
    *prbtval = FSALrbt;
    if( rc == 0 )
      {
          snprintHandle(printbuf, 512, pfsal_handle);
          LogMajor(COMPONENT_HASHTABLE,
                   "Unable to hash (Handle=%s)",
                   printbuf);
          return 0 ;
      }

    if(isFullDebug(COMPONENT_HASHTABLE))
      {
          snprintHandle(printbuf, 512, buffclef->pdata);
          LogFullDebug(COMPONENT_HASHTABLE,
                       "hash_func rbt both: buff = (Handle=%s, Cookie=%"PRIu64"), hashvalue=%u rbtvalue=%u",
                       printbuf, 0UL, FSALindex, FSALrbt);
      }

   /* Success */
   return 1 ;
} /*  cache_inode_fsal_rbt_both */

static int cache_inode_fsal_rbt_both_locally(hash_parameter_t * p_hparam,
                                             hash_buffer_t    * buffclef,
                                             uint32_t * phashval,
                                             uint64_t * prbtval)
{
    char printbuf[512];
    uint32_t h1 = 0 ;
    uint32_t h2 = 0 ;
    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);

    Lookup3_hash_buff_dual((char *)pfsal_handle, buffclef->len,
                           &h1, &h2  );

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ;

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, pfsal_handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt both: buff = (Handle=%s, Cookie=%"PRIu64"), hashvalue=%u rbtvalue=%u",
                         printbuf, 0UL, h1, h2 );
        }

   /* Success */
   return 1 ;
} /*  cache_inode_fsal_rbt_both */


int cache_inode_fsal_rbt_both( hash_parameter_t * p_hparam,
                               hash_buffer_t    * buffclef,
                               uint32_t * phashval, uint64_t * prbtval )
{
  if( nfs_param.cache_layers_param.cache_inode_client_param.use_fsal_hash == FALSE )
    return cache_inode_fsal_rbt_both_locally( p_hparam, buffclef, phashval, prbtval ) ;
  else
    return cache_inode_fsal_rbt_both_on_fsal( p_hparam, buffclef, phashval, prbtval ) ;
}



int display_key(hash_buffer_t * pbuff, char *str)
{
    char buffer[128];

    snprintHandle(buffer, 128, pbuff->pdata);

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Handle=%s, Cookie=%"PRIu64")", buffer, 0UL);
}

int display_not_implemented(hash_buffer_t * pbuff, char *str)
{

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "Print Not Implemented");
}

int display_value(hash_buffer_t * pbuff, char *str)
{
    cache_entry_t *pentry;

    pentry = (cache_entry_t *) pbuff->pdata;

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Type=%d, Address=%p)",
                    pentry->type, pentry);
}
