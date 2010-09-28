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

#include "log_functions.h"
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"
#include "stuff_alloc.h"

#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <string.h>
#include <sys/types.h>

hash_table_t *mfsl_ht = NULL;
hash_parameter_t mfsl_hparam;

/**
 *
 * mfsl_async_hash_func: Compute the hash value for the cache_inode hash table.
 *
 * Computes the hash value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL. 
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed hash value.
 *
 */
unsigned long mfsl_async_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  unsigned long h = 0;

  mfsl_object_t *mobject = (mfsl_object_t *) (buffclef->pdata);

  h = FSAL_Handle_to_HashIndex(&mobject->handle, 0, mfsl_hparam.alphabet_length,
                               mfsl_hparam.index_size);

  if (isFullDebug(COMPONENT_HASHTABLE))
    {
      char printbuf[128];
      snprintHandle(printbuf, 128, &mobject->handle);
      LogFullDebug(COMPONENT_HASHTABLE, "hash_func key: buff =(Handle=%s), hash value=%lu\n", printbuf, h);
    }

  return h;
}                               /* mfsl_async_hash_func */

/**
 *
 * mfsl_async_rbt_func: Compute the rbt value for the cache_inode hash table.
 *
 * Computes the rbt value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL. 
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed rbt value.
 *
 */
unsigned long mfsl_async_rbt_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  /* A polynomial function too, but reversed, to avoid producing same value as decimal_simple_hash_func */
  unsigned long h = 0;

  mfsl_object_t *mobject = (mfsl_object_t *) (buffclef->pdata);

  h = FSAL_Handle_to_RBTIndex(&mobject->handle, 0);


  if (isFullDebug(COMPONENT_HASHTABLE))
    {
      char printbuf[128];
      snprintHandle(printbuf, 128, &mobject->handle);
      LogFullDebug(COMPONENT_HASHTABLE, "hash_func rbt: buff =(Handle=%s), value=%lu\n", printbuf, h);
    }

  return h;
}                               /* mfsl_async_rbt_func */

int mfsl_async_display_key(hash_buffer_t * pbuff, char *str)
{
  mfsl_object_t *pfsdata;
  char buffer[128];

  pfsdata = (mfsl_object_t *) pbuff->pdata;

  snprintHandle(buffer, 128, &(pfsdata->handle));

  return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "(Handle=%s)", buffer);
}                               /* mfsl_async_display_key */

int mfsl_async_display_not_implemented(hash_buffer_t * pbuff, char *str)
{
  return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "Print Not Implemented");
}

/**
 *
 * mfsl_async_compare_keyl: Compares two keys 
 *
 * Compare two keys used to cache mfsl object asynchronous status 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 * @return 0 if keys are the same, 1 otherwise
 * 
 * @see FSAL_handlecmp 
 *
 */
int mfsl_async_compare_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  fsal_status_t status;
  mfsl_object_t *mobject1 = NULL;
  mfsl_object_t *mobject2 = NULL;

  /* Test if one of teh entries are NULL */
  if(buff1->pdata == NULL)
    return (buff2->pdata == NULL) ? 0 : 1;
  else
    {
      if(buff2->pdata == NULL)
        return -1;              /* left member is the greater one */
      else
        {
          int rc;
          mobject1 = (mfsl_object_t *) (buff1->pdata);
          mobject2 = (mfsl_object_t *) (buff2->pdata);

          rc = FSAL_handlecmp(&mobject1->handle, &mobject2->handle, &status);

          return rc;
        }

    }
  /* This line should never be reached */
}                               /* mfsl_async_compare_key */

int mfsl_async_hash_init(void)
{
  mfsl_hparam.index_size = 31;
  mfsl_hparam.alphabet_length = 10;
  mfsl_hparam.nb_node_prealloc = 100;
  mfsl_hparam.hash_func_key = mfsl_async_hash_func;
  mfsl_hparam.hash_func_rbt = mfsl_async_rbt_func;
  mfsl_hparam.compare_key = mfsl_async_compare_key;
  mfsl_hparam.key_to_str = mfsl_async_display_key;
  mfsl_hparam.val_to_str = mfsl_async_display_not_implemented;

  /* Init de la table */
  if((mfsl_ht = HashTable_Init(mfsl_hparam)) == NULL)
    return 0;

  return 1;
}                               /* mfsl_async_hash_init */

int mfsl_async_set_specdata(mfsl_object_t * key, mfsl_object_specific_data_t * value)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc;

  /* Build the key */
  buffkey.pdata = (caddr_t) key;
  buffkey.len = sizeof(mfsl_object_t);

  /* Build the value */
  buffdata.pdata = (caddr_t) value;
  buffdata.len = sizeof(mfsl_object_specific_data_t);

  rc = HashTable_Test_And_Set(mfsl_ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return 0;

  if (isFullDebug(COMPONENT_HASHTABLE))
      HashTable_Log(COMPONENT_MFSL, mfsl_ht);

  return 1;
}                               /* mfsl_async_set_specdata */

int mfsl_async_get_specdata(mfsl_object_t * key, mfsl_object_specific_data_t ** ppvalue)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  int rc = 0;

  if(key == NULL || ppvalue == NULL)
    return 0;

  if (isFullDebug(COMPONENT_HASHTABLE))
      HashTable_Log(COMPONENT_MFSL, mfsl_ht);

  buffkey.pdata = (caddr_t) key;
  buffkey.len = sizeof(mfsl_object_t);

  rc = HashTable_Get(mfsl_ht, &buffkey, &buffval);
  if(rc == HASHTABLE_SUCCESS)
    {
      *ppvalue = (mfsl_object_specific_data_t *) (buffval.pdata);
      status = 1;
    }
  else
    {
      status = 0;
    }

  return status;
}                               /* mfslasync_get_specdata */

int mfsl_async_remove_specdata(mfsl_object_t * key)
{
  hash_buffer_t buffkey, old_key;
  int status;

  if(key == NULL)
    return 0;

  buffkey.pdata = (caddr_t) key;
  buffkey.len = sizeof(mfsl_object_t);

  if(HashTable_Del(mfsl_ht, &buffkey, &old_key, NULL) == HASHTABLE_SUCCESS)
    {
      status = 1;
        /** @todo release previously allocated specdata */
      // Mem_Free( old_key.pdata ) ;
    }
  else
    {
      status = 0;
    }

  return status;
}                               /* mfsl_async_remove_specdata */

int mfsl_async_is_object_asynchronous(mfsl_object_t * object)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  int rc = 0;

  if(object == NULL)
    return 0;

  if (isFullDebug(COMPONENT_HASHTABLE))
  HashTable_Log(COMPONENT_MFSL, mfsl_ht);

  buffkey.pdata = (caddr_t) object;
  buffkey.len = sizeof(mfsl_object_t);

  rc = HashTable_Get(mfsl_ht, &buffkey, &buffval);
  if(rc == HASHTABLE_SUCCESS)
    status = 1;
  else
    status = 0;

  return status;
}                               /* mfsl_async_is_object_asynchronous */
