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

#include "log_macros.h"
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
unsigned long cache_inode_fsal_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
    unsigned long h = 0;
    char printbuf[512];
    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    h = FSAL_Handle_to_HashIndex(&pfsdata->handle, pfsdata->cookie,
                                 p_hparam->alphabet_length,
                                 p_hparam->index_size);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, &pfsdata->handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func key: buff = (Handle=%s, Cookie=%"PRIu64"), hash value=%lu",
                         printbuf, pfsdata->cookie, h);
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
unsigned long cache_inode_fsal_rbt_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
    /*
     * A polynomial function too, but reversed, to avoid
     * producing same value as decimal_simple_hash_func
     */
    uint32_t h = 0;
    char printbuf[512];

    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    h = Lookup3_hash_buff((char *)(&pfsdata->handle.data), sizeof(pfsdata->handle.data ) );

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, &pfsdata->handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt: buff = (Handle=%s, Cookie=%"PRIu64"), value=%u",
                         printbuf, pfsdata->cookie, h);
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
    char printbuf[512];

    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    h = FSAL_Handle_to_RBTIndex(&pfsdata->handle, pfsdata->cookie);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, &pfsdata->handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt: buff = (Handle=%s, Cookie=%"PRIu64"), value=%lu",
                         printbuf, pfsdata->cookie, h);
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

unsigned int cache_inode_fsal_rbt_both_on_fsal( hash_parameter_t * p_hparam,
				               hash_buffer_t    * buffclef, 
				               uint32_t * phashval, uint32_t * prbtval )
{
    char printbuf[512];
    unsigned int rc = 0 ;

    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    rc = FSAL_Handle_to_Hash_both( &pfsdata->handle, pfsdata->cookie,
				   p_hparam->alphabet_length, p_hparam->index_size,
				   phashval, prbtval ) ;

    if( rc == 0 )
      {
          snprintHandle(printbuf, 512, &pfsdata->handle);
          LogMajor(COMPONENT_HASHTABLE,
                   "Unable to hash (Handle=%s, Cookie=%"PRIu64")",
                   printbuf, pfsdata->cookie);
          return 0 ;
      }

    if(isFullDebug(COMPONENT_HASHTABLE))
      {
          snprintHandle(printbuf, 512, &pfsdata->handle);
          LogFullDebug(COMPONENT_HASHTABLE,
                       "hash_func rbt both: buff = (Handle=%s, Cookie=%"PRIu64"), hashvalue=%u rbtvalue=%u",
                       printbuf, pfsdata->cookie, *phashval, *prbtval );
      }

   /* Success */
   return 1 ;
} /*  cache_inode_fsal_rbt_both */

unsigned int cache_inode_fsal_rbt_both_locally( hash_parameter_t * p_hparam,
				              hash_buffer_t    * buffclef, 
				              uint32_t * phashval, uint32_t * prbtval )
{
    char printbuf[512];
    uint32_t h1 = 0 ;
    uint32_t h2 = 0 ;

    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    Lookup3_hash_buff_dual((char *)(&pfsdata->handle.data), sizeof(pfsdata->handle.data),
                           &h1, &h2  );

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ;

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, &pfsdata->handle);
            LogFullDebug(COMPONENT_HASHTABLE,
                         "hash_func rbt both: buff = (Handle=%s, Cookie=%"PRIu64"), hashvalue=%u rbtvalue=%u",
                         printbuf, pfsdata->cookie, h1, h2 );
        }

   /* Success */
   return 1 ;
} /*  cache_inode_fsal_rbt_both */


unsigned int cache_inode_fsal_rbt_both( hash_parameter_t * p_hparam,
				        hash_buffer_t    * buffclef, 
				        uint32_t * phashval, uint32_t * prbtval )
{
  if( nfs_param.cache_layers_param.cache_inode_client_param.use_fsal_hash == FALSE )
    return cache_inode_fsal_rbt_both_locally( p_hparam, buffclef, phashval, prbtval ) ;
  else
    return cache_inode_fsal_rbt_both_on_fsal( p_hparam, buffclef, phashval, prbtval ) ;
}



int display_key(hash_buffer_t * pbuff, char *str)
{
    cache_inode_fsal_data_t *pfsdata;
    char buffer[128];

    pfsdata = (cache_inode_fsal_data_t *) pbuff->pdata;

    snprintHandle(buffer, 128, &(pfsdata->handle));

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Handle=%s, Cookie=%"PRIu64")", buffer,
                    pfsdata->cookie);
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
                    pentry->internal_md.type, pentry);
}
