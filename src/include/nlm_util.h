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
#include "nlm_list.h"

struct nlm_lock_entry
{
  char *caller_name;
  netobj fh;
  netobj oh;
  netobj cookie;
  int32_t svid;
  uint64_t start;
  uint64_t len;
  int state;
  int exclusive;
  int ref_count;
  pthread_mutex_t lock;
  struct glist_head lock_list;
};

typedef struct nlm_lock_entry nlm_lock_entry_t;

extern void nlm_lock_entry_to_nlm_holder(nlm_lock_entry_t * nlm_entry,
                                         struct nlm4_holder *holder);
extern int nlm_lock_entry_get_state(nlm_lock_entry_t * nlm_entry);
extern nlm_lock_entry_t *nlm_overlapping_entry(struct nlm4_lock *nlm_lock, int exclusive);
extern nlm_lock_entry_t *nlm_add_to_locklist(struct nlm4_lockargs *args);
extern void nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry);
extern int nlm_delete_lock_entry(struct nlm4_lock *nlm_lock);
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
