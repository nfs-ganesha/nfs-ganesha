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

struct nlm_lock_entry
{
  struct nlm4_lockargs arg;
  int state;
  int ref_count;
  pthread_mutex_t lock;
  struct glist_head lock_list;
#ifdef _DEBUG_MEMLEAKS
  struct glist_head all_locks;
#endif
  cache_entry_t *pentry;
  cache_inode_client_t *pclient;
  hash_table_t *ht;
};

typedef struct nlm_lock_entry nlm_lock_entry_t;

extern void dump_lock_list(void);
extern void dump_all_locks(void);
extern void nlm_init_locklist(void);
extern const char *lock_result_str(int rc);
extern netobj *copy_netobj(netobj * dst, netobj * src);
extern void netobj_free(netobj * obj);
extern void netobj_to_string(netobj *obj, char *buffer, int maxlen);
extern void nlm_lock_entry_to_nlm_holder(nlm_lock_entry_t * nlm_entry,
                                         struct nlm4_holder *holder);
extern int nlm_lock_entry_get_state(nlm_lock_entry_t * nlm_entry);
extern nlm_lock_entry_t *nlm_overlapping_entry(struct nlm4_lock *nlm_lock, int exclusive);
extern nlm_lock_entry_t *nlm_add_to_locklist(struct nlm4_lockargs *args,
                                      cache_entry_t * pentry,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext);
extern void nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry);
extern void nlm_delete_lock_entry(struct nlm4_lock *nlm_lock);
extern void nlm_init(void);
extern nlm_lock_entry_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock,
                                             int exclusive, int state);
extern void nlm_lock_entry_dec_ref(nlm_lock_entry_t * nlm_entry);
extern int start_nlm_grace_period(void);
extern int in_nlm_grace_period(void);
extern void nlm_node_recovery(char *name,
                              fsal_op_context_t * pcontext,
                              cache_inode_client_t * pclient, hash_table_t * ht);
extern int nlm_monitor_host(char *name);
extern int nlm_unmonitor_host(char *name);
extern void nlm_grant_blocked_locks(netobj * orig_fh);

extern nlm_lock_entry_t *nlm_find_lock_entry_by_cookie(netobj * cookie);
extern void nlm_resend_grant_msg(void *arg);

/**
 * process_nlm_parameters: Process NLM parameters
 *
 * Returns -1 for a request that needs processing, otherwise returns an NLM status
 *
 * preq:         passed in so interface doesn't need to change when NLM Client uses IP address
 * exclusive:    TRUE if lock is a write lock
 * alock:        nlm4_lock request structure
 * plock:        cache_lock_desc_t to fill in from alock
 * ppentry:      cache inode entry pointer to fill in
 * pcontext:     FSAL op context
 * pclient:      cache inode client
 * care:         TRUE if this caller cares if an owner is found (otherwise return NLM4_GRANTED
 *               because the caller will have nothing to do)
 * ppnlm_client: NLM Client to fill in, returns a reference to the client
 * ppowner:      NLM Owner to fill in, returns a reference to the owner
 */
int process_nlm_parameters(struct svc_req            * preq,
                           bool_t                      exclusive,
                           nlm4_lock                 * alock,
                           cache_lock_desc_t         * plock,
                           hash_table_t              * ht,
                           cache_entry_t            ** ppentry,
                           fsal_op_context_t         * pcontext,
                           cache_inode_client_t      * pclient,
                           bool_t                      care,
                           cache_inode_nlm_client_t ** ppnlm_client,
                           cache_lock_owner_t       ** ppowner);

#endif                          /* _NLM_UTIL_H */
