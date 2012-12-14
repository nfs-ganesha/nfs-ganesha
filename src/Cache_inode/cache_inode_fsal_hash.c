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
#include "nfs_core.h"
#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <strings.h>
#include <sys/types.h>

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
          char                  printbuf[FSAL_HANDLE_STR_LEN];
          struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

          (void) display_FSAL_handle(&dspbuf, pfsal_handle);

          LogMajor(COMPONENT_CACHE_INODE,
                   "Unable to hash (Handle=%s)",
                   printbuf);
          return 0 ;
      }

    if(isFullDebug(COMPONENT_HASHTABLE) && isFullDebug(COMPONENT_CACHE_INODE))
      {
          char                  printbuf[FSAL_HANDLE_STR_LEN];
          struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

          (void) display_FSAL_handle(&dspbuf, pfsal_handle);

          LogFullDebug(COMPONENT_CACHE_INODE,
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
    fsal_handle_t *pfsal_handle = (fsal_handle_t *) (buffclef->pdata);

    Lookup3_hash_buff_dual((char *)pfsal_handle, buffclef->len,
                           &h1, &h2  );

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ;

    if(isFullDebug(COMPONENT_HASHTABLE) && isFullDebug(COMPONENT_CACHE_INODE))
        {
            char                  printbuf[FSAL_HANDLE_STR_LEN];
            struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

            (void) display_FSAL_handle(&dspbuf, pfsal_handle);

            LogFullDebug(COMPONENT_CACHE_INODE,
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
  if(cache_inode_params.use_fsal_hash == FALSE )
    return cache_inode_fsal_rbt_both_locally( p_hparam, buffclef, phashval, prbtval ) ;
  else
    return cache_inode_fsal_rbt_both_on_fsal( p_hparam, buffclef, phashval, prbtval ) ;
}
