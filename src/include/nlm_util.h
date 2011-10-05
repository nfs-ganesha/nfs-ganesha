/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
 *
 */

#ifndef _NLM_UTIL_H
#define _NLM_UTIL_H

#include "nlm_list.h"
#include "nlm4.h"
#include "sal_functions.h"

bool_t nlm_block_data_to_fsal_context(state_nlm_block_data_t * nlm_block_data,
                                      fsal_op_context_t      * fsal_context);

extern const char *lock_result_str(int rc);
extern netobj *copy_netobj(netobj * dst, netobj * src);
extern void netobj_free(netobj * obj);
extern void netobj_to_string(netobj *obj, char *buffer, int maxlen);
extern int in_nlm_grace_period(void);

/**
 * process_nlm_parameters: Process NLM parameters
 *
 * Returns -1 for a request that needs processing, otherwise returns an NLM status
 *
 * preq:         passed in so interface doesn't need to change when NLM Client uses IP address
 * exclusive:    TRUE if lock is a write lock
 * alock:        nlm4_lock request structure
 * plock:        cache_lock_desc_t to fill in from alock
 * ht:           The cache inode hash table used to find cache inode entries
 * ppentry:      cache inode entry pointer to fill in
 * pcontext:     FSAL op context
 * pclient:      cache inode client
 * care:         TRUE if this caller cares if an owner is found (otherwise return NLM4_GRANTED
 *               because the caller will have nothing to do)
 * ppnlm_client: NLM Client to fill in, returns a reference to the client
 * ppowner:      NLM Owner to fill in, returns a reference to the owner
 * ppblock_data: Data required to make a call back to the client to grant a blocked lock
 */
int nlm_process_parameters(struct svc_req        * preq,
                           bool_t                  exclusive,
                           nlm4_lock             * alock,
                           state_lock_desc_t     * plock,
                           hash_table_t          * ht,
                           cache_entry_t        ** ppentry,
                           fsal_op_context_t     * pcontext,
                           cache_inode_client_t  * pclient,
                           care_t                  care,
                           state_nsm_client_t   ** ppnsm_client,
                           state_nlm_client_t   ** ppnlm_client,
                           state_owner_t        ** ppowner,
                           state_block_data_t   ** ppblock_data);

void nlm_process_conflict(nlm4_holder          * nlm_holder,
                          state_owner_t        * holder,
                          state_lock_desc_t    * conflict,
                          cache_inode_client_t * pclient);

nlm4_stats nlm_convert_state_error(state_status_t status);

state_status_t nlm_granted_callback(cache_entry_t        * pentry,
                                    state_lock_entry_t   * lock_entry,
                                    cache_inode_client_t * pclient,
                                    state_status_t       * pstatus);

#endif                          /* _NLM_UTIL_H */
