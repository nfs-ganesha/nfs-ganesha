/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2005)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#ifdef _DEBUG_HASHTABLE
  char printbuf[128];
#endif
  mfsl_object_t *mobject = (mfsl_object_t *) (buffclef->pdata);

  h = FSAL_Handle_to_HashIndex(&mobject->handle, 0, mfsl_hparam.alphabet_length,
                               mfsl_hparam.index_size);

#ifdef _DEBUG_HASHTABLE
  snprintHandle(printbuf, 128, &mobject->handle);
  printf("hash_func key: buff =(Handle=%s), hash value=%lu\n", printbuf, h);
#endif

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
#ifdef _DEBUG_HASHTABLE
  char printbuf[128];
#endif

  mfsl_object_t *mobject = (mfsl_object_t *) (buffclef->pdata);

  h = FSAL_Handle_to_RBTIndex(&mobject->handle, 0);

#ifdef _DEBUG_HASHTABLE
  snprintHandle(printbuf, 128, &mobject->handle);
  printf("hash_func rbt: buff =(Handle=%s), value=%lu\n", printbuf, h);
#endif
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

#ifdef _DEBUG_HASHTABLE
  HashTable_Print(mfsl_ht);
#endif

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

#ifdef _DEBUG_HASHTABLE
  HashTable_Print(mfsl_ht);
#endif

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

#ifdef _DEBUG_HASHTABLE
  HashTable_Print(mfsl_ht);
#endif

  buffkey.pdata = (caddr_t) object;
  buffkey.len = sizeof(mfsl_object_t);

  rc = HashTable_Get(mfsl_ht, &buffkey, &buffval);
  if(rc == HASHTABLE_SUCCESS)
    status = 1;
  else
    status = 0;

  return status;
}                               /* mfsl_async_is_object_asynchronous */
