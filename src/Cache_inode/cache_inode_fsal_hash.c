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

/** @TODO
 * If there is need/desire to have multiple hash functions, an ops
 * vector for these two functions can be used and the additional
 * functions would get defined here as static functions.  The callers
 * in this file would then use the index from the configuration to call
 * the function.  For now, the VFS version, the most common is used.
 * Note that the hash_buffer_t, including the use of len rather than a
 * fixed sizeof is also used.  It is the responsibility of the fsal
 * in its expand_handle and handle_to_key methods to define what these
 * keys are.
 */

/**
 * Handle_to_HashIndex
 * This function is used for hashing a FSAL handle
 * in order to dispatch entries into the hash table array.
 * Copied from old VFS fsal_tools.c
 *
 * \param p_handle      The handle to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 * \param alphabet_len  Parameter for polynomial hashing algorithm
 * \param index_size    The range of hash value will be [0..index_size-1]
 *
 * \return The hash value
 */
static unsigned int Handle_to_HashIndex(hash_buffer_t *buffclef,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size)
{
  unsigned char *hashkey = buffclef->pdata;
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = buffclef->len % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < buffclef->len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &hashkey[cpt], sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = buffclef->len - mod; cpt < buffclef->len; cpt++ )
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)hashkey[cpt];
        }
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  return sum;
}

/*
 * Handle_to_RBTIndex
 * This function is used for generating a RBT node ID
 * in order to identify entries into the RBT.
 * Copied from old VFS fsal_tools.c
 *
 * \param buffclef      The key to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 *
 * \return The hash value
 */

static unsigned int Handle_to_RBTIndex(hash_buffer_t *buffclef, unsigned int cookie)
{
  unsigned char *hashkey = buffclef->pdata;
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod;

  h = cookie;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = buffclef->len % sizeof(unsigned int);

  for(cpt = 0; cpt < buffclef->len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &hashkey[cpt], sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = buffclef->len - mod; cpt < buffclef->len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)hashkey[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

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

    h = Handle_to_HashIndex(buffclef, 0,
                                 p_hparam->alphabet_length,
                                 p_hparam->index_size);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
	  char printbuf[512];

          snprintHandle(printbuf,
			(buffclef->len < 512) ? buffclef->len : 512,
			buffclef->pdata);
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

    h = Lookup3_hash_buff((char *)buffclef->pdata, buffclef->len );

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
	  char printbuf[512];

          snprintHandle(printbuf,
			(buffclef->len < 512) ? buffclef->len : 512,
			buffclef->pdata);
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

    h = Handle_to_RBTIndex(buffclef, 0);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
	  char printbuf[512];

          snprintHandle(printbuf,
			(buffclef->len < 512) ? buffclef->len : 512,
			buffclef->pdata);
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
    *phashval = Handle_to_HashIndex(buffclef,
				    0,
				    p_hparam->alphabet_length,
				    p_hparam->index_size);
    *prbtval = Handle_to_RBTIndex(buffclef, 0);

    if(isFullDebug(COMPONENT_HASHTABLE))
      {
	  char printbuf[512];

          snprintHandle(printbuf,
			(buffclef->len < 512) ? buffclef->len : 512,
			buffclef->pdata);
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
    uint32_t h1 = 0 ;
    uint32_t h2 = 0 ;

    Lookup3_hash_buff_dual(buffclef->pdata, buffclef->len,
                           &h1, &h2  );

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ;

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
	    char printbuf[512];

            snprintHandle(printbuf,
			  (buffclef->len < 512) ? buffclef->len : 512,
			  buffclef->pdata);
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
    char buffer[512];

    snprintHandle(buffer,
		  (pbuff->len < 512) ? pbuff->len : 512,
		  pbuff->pdata);

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Handle=%s, Cookie=%"PRIu64")", buffer, 0UL);
}

int display_not_implemented(hash_buffer_t * pbuff, char *str)
{

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "Print Not Implemented");
}

/** @TODO this function is ???!!! It assumes a whole lot.
 *  leave for now but candidate for janitorial
 */
int display_value(hash_buffer_t * pbuff, char *str)
{
    cache_entry_t *pentry;

    pentry = (cache_entry_t *) pbuff->pdata;

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Type=%d, Address=%p)",
                    pentry->type, pentry);
}
