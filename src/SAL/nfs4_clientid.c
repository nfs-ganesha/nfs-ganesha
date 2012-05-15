/**
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 *
 * nfs4_client_id.c : The management of the client id cache.
 *
 * $Header$
 *
 * $Log$
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "nfs4.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"

/**
 *
 *  client_id_rbt_hash_func: computes the hash value for the entry in Client Id cache.
 *
 * Computes the hash value for the entry in Client Id cache. In fact,
 * it just use addresse as value (identity function) modulo the size
 * of the hash.  This function is called internal in the HasTable_*
 * function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
uint32_t client_id_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  clientid4 clientid;

  clientid = *((clientid4 *) (buffclef->pdata));

  /* Sum upper and lower 32bits fields to build the key */
  hash_func = ((unsigned long)(clientid & 0x00000000FFFFFFFFLL) +
               (unsigned long)(clientid >> 32));

  return hash_func % p_hparam->index_size;
}                               /*  client_id_value_hash_func */

/**
 *
 *  client_id_rbt_hash_func: computes the rbt value for the entry in Client Id cache.
 *
 * Computes the rbt value for the entry in Client Id cache. In fact, it just use the address value
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long client_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  clientid4 clientid;

  clientid = *((clientid4 *) (buffclef->pdata));

  /* Xor upper and lower 32bits fields to build the key */
  hash_func = ((unsigned long)(clientid & 0x00000000FFFFFFFFLL) ^
               (unsigned long)(clientid >> 32));

  return hash_func;
}                               /* client_id_rbt_hash_func */

/**
 *
 * compare_client_id: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in
 * the hashtable storing the client ids.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_client_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  clientid4 cl1 = *((clientid4 *) (buff1->pdata));
  clientid4 cl2 = *((clientid4 *) (buff2->pdata));
  return (cl1 == cl2) ? 0 : 1;
}

/**
 *
 * display_client_id: displays the client_id stored in the buffer.
 *
 * displays the client_id stored in the buffer. This function is to be used as 'key_to_str' field in
 * the hashtable storing the client ids.
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_client_id(hash_buffer_t * pbuff, char *str)
{
  clientid4 clientid;

  clientid = *((clientid4 *) (pbuff->pdata));

  return sprintf(str, "%llu", (unsigned long long)clientid);
}                               /* display_client_id */

int display_client_id_val(hash_buffer_t * pbuff, char *str)
{
  nfs_client_id_t *precord;

  precord = (nfs_client_id_t *) (pbuff->pdata);

  return sprintf(str, "#%s#=>%llu cb_prog=%u r_addr=%s r_netid=%s",
                 precord->client_name,
                 (unsigned long long)precord->clientid,
                 precord->cb.program,
                 precord->cb.client_r_addr,
                 netid_nc_table[precord->cb.addr.nc].netid);
}                               /* display_client_id_val */

/**
 *
 * nfs_client_id_expire: client expires, need to take care of owners
 *
 *
 * @param clientid           [IN]    the client id used as key
 *
 */
void nfs_client_id_expire(nfs_client_id_t *client_record)
{
  struct glist_head    * glist, * glistn;
  struct glist_head    * glist2, * glistn2;
  state_status_t         pstatus;
  int rc;

  P(client_record->clientid_mutex);
  if (client_record->confirmed == EXPIRED_CLIENT_ID)
    {
      V(client_record->clientid_mutex);
      return;
    }

  client_record->confirmed = EXPIRED_CLIENT_ID;

  /* traverse the client's lock owners, and release all locks */
  glist_for_each_safe(glist, glistn, &client_record->clientid_lockowners)
    {
      state_owner_t * plock_owner = glist_entry(glist,
                                                state_owner_t,
                                	        so_owner.so_nfs4_owner.so_perclient);

      glist_for_each_safe(glist2, glistn2, &plock_owner->so_owner.so_nfs4_owner.so_state_list)
        {
          fsal_op_context_t        fsal_context;
          fsal_status_t            fsal_status;

          state_t* plock_state = glist_entry(glist2,
                                             state_t,
					     state_owner_list);

          /* construct the fsal context based on the export and root credential */
	  fsal_status = FSAL_GetClientContext(&fsal_context,
                                      &plock_state->state_pexport->FS_export_context,
                                      0,
                                      0,
                                      NULL,
                                      0);
          if(FSAL_IS_ERROR(fsal_status))
            {
              /* log error here , and continue? */
              LogDebug(COMPONENT_STATE,
                      "FSAL_GetClientConext failed");
              continue;
            }

          state_owner_unlock_all(&fsal_context,
                                 plock_owner,
                                 plock_state,
                                 &pstatus);
        }
    }

  /* traverse the client's lock owners, and release all locks states and owners */
  glist_for_each_safe(glist, glistn, &client_record->clientid_lockowners)
    {
      state_owner_t * plock_owner = glist_entry(glist,
                                          state_owner_t,
                                                so_owner.so_nfs4_owner.so_perclient);
      inc_state_owner_ref(plock_owner);
      release_lockstate(plock_owner);
      dec_state_owner_ref(plock_owner);
    }

  /* release the corresponding open states , close files*/
  glist_for_each_safe(glist, glistn, &client_record->clientid_openowners)
    {
      state_owner_t * popen_owner
           = glist_entry(glist,
                         state_owner_t,
                         so_owner.so_nfs4_owner.so_perclient);
      inc_state_owner_ref(popen_owner);
      release_openstate(popen_owner);
      dec_state_owner_ref(popen_owner);
    }

  dec_state_owner_ref(client_record->clientid_owner);

  if (client_record->recov_dir != NULL)
    {
      nfs4_rm_clid(client_record->recov_dir);
      gsh_free(client_record->recov_dir);
      client_record->recov_dir = NULL;
    }

  V(client_record->clientid_mutex);

  /* need to free client record
   * TEMP COMMENT OUT */
  rc = CLIENT_ID_SUCCESS; // nfs_client_id_remove(client_record->clientid, client_record->clientid_pool);
  if (rc == CLIENT_ID_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "clientid removed\n");
    }
  else
    {
      LogDebug(COMPONENT_STATE,
               "nfs_client_id_remove failed with rc = %d", rc);
    }
}

/**
 *
 * nfs_Init_client_id: Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 *
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_client_id(nfs_client_id_parameter_t param)
{
  return CLIENT_ID_SUCCESS;
}                               /* nfs_Init_client_id */
