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
#include "cache_inode.h"

bool_t nlm_block_data_to_fsal_context(cache_inode_nlm_block_data_t * nlm_block_data,
                                      fsal_op_context_t            * fsal_context);

extern const char *lock_result_str(int rc);
extern netobj *copy_netobj(netobj * dst, netobj * src);
extern void netobj_free(netobj * obj);
extern void netobj_to_string(netobj *obj, char *buffer, int maxlen);
extern int in_nlm_grace_period(void);
extern int nlm_monitor_host(char *name);
extern int nlm_unmonitor_host(char *name);

void inc_nlm_client_ref(cache_inode_nlm_client_t *pclient);
void dec_nlm_client_ref(cache_inode_nlm_client_t *pclient);
int display_nlm_client(cache_inode_nlm_client_t *pkey, char *str);
int display_nlm_client_val(hash_buffer_t * pbuff, char *str);
int display_nlm_client_key(hash_buffer_t * pbuff, char *str);
int compare_nlm_client(cache_inode_nlm_client_t *pkey1,
                       cache_inode_nlm_client_t *pkey2);
int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2);
unsigned long nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t    * buffclef);
unsigned long nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t    * buffclef);
cache_inode_nlm_client_t *get_nlm_client(bool_t care, const char * caller_name);
void nlm_client_PrintAll(void);

void inc_nlm_owner_ref(cache_lock_owner_t *powner);
void dec_nlm_owner_ref(cache_lock_owner_t *powner);
int display_nlm_owner(cache_lock_owner_t *pkey, char *str);
int display_nlm_owner_val(hash_buffer_t * pbuff, char *str);
int display_nlm_owner_key(hash_buffer_t * pbuff, char *str);
int compare_nlm_owner(cache_lock_owner_t *pkey1,
                      cache_lock_owner_t *pkey2);
int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2);
unsigned long nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t    * buffclef);
unsigned long nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t    * buffclef);
void make_nlm_special_owner(cache_inode_nlm_client_t * pclient,
                            cache_lock_owner_t       * pnlm_owner);
cache_lock_owner_t *get_nlm_owner(bool_t                     care,
                                  cache_inode_nlm_client_t * pclient, 
                                  netobj                   * oh,
                                  uint32_t                   svid);
void nlm_owner_PrintAll(void);

int Init_nlm_hash(hash_parameter_t client_param, hash_parameter_t owner_param);

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
int nlm_process_parameters(struct svc_req            * preq,
                           bool_t                      exclusive,
                           nlm4_lock                 * alock,
                           cache_lock_desc_t         * plock,
                           hash_table_t              * ht,
                           cache_entry_t            ** ppentry,
                           fsal_op_context_t         * pcontext,
                           cache_inode_client_t      * pclient,
                           bool_t                      care,
                           cache_inode_nlm_client_t ** ppnlm_client,
                           cache_lock_owner_t       ** ppowner,
                           cache_inode_block_data_t ** ppblock_data);

void nlm_process_conflict(nlm4_holder        * nlm_holder,
                          cache_lock_owner_t * holder,
                          cache_lock_desc_t  * conflict);

nlm4_stats nlm_convert_cache_inode_error(cache_inode_status_t status);

cache_inode_status_t nlm_granted_callback(cache_entry_t        * pentry,
                                          cache_lock_entry_t   * lock_entry,
                                          cache_inode_client_t * pclient,
                                          cache_inode_status_t * pstatus);

#endif                          /* _NLM_UTIL_H */
