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
            LogFullDebug(COMPONENT_HASHTABLE, "hash_func key: buff = "
                         "(Handle=%s, Cookie=%u), hash value=%lu\n",
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
    unsigned long h = 0;
    char printbuf[512];

    cache_inode_fsal_data_t *pfsdata = (cache_inode_fsal_data_t *) (buffclef->pdata);

    h = FSAL_Handle_to_RBTIndex(&pfsdata->handle, pfsdata->cookie);

    if(isFullDebug(COMPONENT_HASHTABLE))
        {
            snprintHandle(printbuf, 512, &pfsdata->handle);
            LogFullDebug(COMPONENT_HASHTABLE, "hash_func rbt: buff = "
                         "(Handle=%s, Cookie=%u), value=%lu\n",
                         printbuf, pfsdata->cookie, h);
        }
    return h;
}                               /* cache_inode_fsal_rbt_func */

int display_key(hash_buffer_t * pbuff, char *str)
{
    cache_inode_fsal_data_t *pfsdata;
    char buffer[128];

    pfsdata = (cache_inode_fsal_data_t *) pbuff->pdata;

    snprintHandle(buffer, 128, &(pfsdata->handle));

    return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                    "(Handle=%s, Cookie=%u)", buffer,
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
