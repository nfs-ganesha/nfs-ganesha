/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 * * --------------------------
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include <sys/time.h>
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"
#include "nsm.h"
#include "nlm_async.h"
#include "nlm_send_reply.h"

/*
 * nlm_lock_entry_t locking rule:
 * The value is always updated/read with nlm_lock_entry->lock held
 * If we have nlm_lock_list mutex held we can read it safely, because the
 * value is always updated while walking the list with nlm_lock_list_mutex held.
 * The updation happens as below:
 *  pthread_mutex_lock(nlm_lock_list_mutex)
 *  pthread_mutex_lock(nlm_entry->lock)
 *    update the nlm_entry value
 *  ........
 * The value is ref counted with nlm_lock_entry->ref_count so that a
 * parallel cancel/unlock won't endup freeing the datastructure. The last
 * release on the data structure ensure that it is freed.
 */
static struct glist_head nlm_lock_list;
static pthread_mutex_t nlm_lock_list_mutex;

/* nlm grace time tracking */
static struct timeval nlm_grace_tv;
#define NLM4_GRACE_PERIOD 10
/*
 * Time after which we should retry the granted
 * message request again
 */
#define NLM4_CLIENT_GRACE_PERIOD 3

static netobj *copy_netobj(netobj * dst, netobj * src)
{
    dst->n_len = 0;
    dst->n_bytes = (char *)Mem_Alloc(src->n_len);
    if(!dst->n_bytes)
        return NULL;
    dst->n_len = src->n_len;
    memcpy(dst->n_bytes, src->n_bytes, src->n_len);
    return dst;
}

static void netobj_free(netobj * obj)
{
    if(obj->n_bytes)
        Mem_Free(obj->n_bytes);
}

static int netobj_compare(netobj * obj1, netobj * obj2)
{
    if(obj1->n_len != obj2->n_len)
        return 1;
    if(memcmp(obj1->n_bytes, obj2->n_bytes, obj1->n_len))
        return 1;
    return 0;
}

static nlm_lock_entry_t *nlm4_lock_to_nlm_lock_entry(struct nlm4_lockargs *args)
{
    nlm_lock_entry_t *nlm_entry;
    int exclusive = args->exclusive;
    nlm4_lock *nlm_lock = &args->alock;

    nlm_entry = (nlm_lock_entry_t *) Mem_Calloc(1, sizeof(nlm_lock_entry_t));
    if(!nlm_entry)
        return NULL;
    nlm_entry->caller_name = strdup(nlm_lock->caller_name);
    if(!copy_netobj(&nlm_entry->fh, &nlm_lock->fh))
        goto err_out;
    if(!copy_netobj(&nlm_entry->oh, &nlm_lock->oh))
        goto err_out;
    if(!copy_netobj(&nlm_entry->cookie, &args->cookie))
        goto err_out;
    nlm_entry->svid = nlm_lock->svid;
    nlm_entry->start = nlm_lock->l_offset;
    nlm_entry->len = nlm_lock->l_len;
    nlm_entry->exclusive = exclusive;
    nlm_entry->ref_count = 0;
    pthread_mutex_init(&nlm_entry->lock, NULL);
    return nlm_entry;
err_out:
    free(nlm_entry->caller_name);
    netobj_free(&nlm_entry->fh);
    netobj_free(&nlm_entry->oh);
    netobj_free(&nlm_entry->cookie);
    Mem_Free(nlm_entry);
    return NULL;
}

void nlm_lock_entry_to_nlm_holder(nlm_lock_entry_t * nlm_entry,
                                  struct nlm4_holder *holder)
{
    /*
     * Take the lock to make sure other threads don't update
     * nlm_entry contents in parallel
     */
    pthread_mutex_lock(&nlm_entry->lock);
    holder->exclusive = nlm_entry->exclusive;
    holder->oh = nlm_entry->oh;
    holder->svid = nlm_entry->svid;
    holder->l_offset = nlm_entry->start;
    holder->l_len = nlm_entry->len;
    pthread_mutex_unlock(&nlm_entry->lock);
}

int nlm_lock_entry_get_state(nlm_lock_entry_t * nlm_entry)
{
    int lck_state;
    pthread_mutex_lock(&nlm_entry->lock);
    lck_state = nlm_entry->state;
    pthread_mutex_unlock(&nlm_entry->lock);
    return lck_state;
}

static void nlm_lock_entry_inc_ref(nlm_lock_entry_t * nlm_entry)
{
    pthread_mutex_lock(&nlm_entry->lock);
    nlm_entry->ref_count++;
    pthread_mutex_unlock(&nlm_entry->lock);
}

void nlm_lock_entry_dec_ref(nlm_lock_entry_t * nlm_entry)
{
    int to_free = 0;
    pthread_mutex_lock(&nlm_entry->lock);
    nlm_entry->ref_count--;
    if(!nlm_entry->ref_count)
        {
            /*
             * We should already be removed from the lock_list
             * So we can free the lock_entry without any locking
             */
            to_free = 1;
        }
    pthread_mutex_unlock(&nlm_entry->lock);
    if(to_free)
        {
            free(nlm_entry->caller_name);
            netobj_free(&nlm_entry->fh);
            netobj_free(&nlm_entry->oh);
            netobj_free(&nlm_entry->cookie);
            Mem_Free(nlm_entry);
        }
}

static nlm_lock_entry_t *get_nlm_overlapping_entry(struct nlm4_lock *nlm_lock,
                                                   int exclusive)
{
    int overlap = 0;
    struct glist_head *glist;
    nlm_lock_entry_t *nlm_entry = NULL;
    uint64_t nlm_entry_end, nlm_lock_end;

    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(netobj_compare(&nlm_entry->fh, &nlm_lock->fh))
                continue;

            if(nlm_entry->state != NLM4_GRANTED)
                continue;

            if(nlm_entry->len)
                nlm_entry_end = nlm_entry->start + nlm_entry->len;
            else
                nlm_entry_end = UINT64_MAX;

            if(nlm_lock->l_len)
                nlm_lock_end = nlm_lock->l_offset + nlm_lock->l_len;
            else
                nlm_lock_end = UINT64_MAX;

            if((nlm_entry_end > nlm_lock->l_offset) &&
               (nlm_lock_end > nlm_entry->start))
                {
                    /* lock overlaps see if we can allow */
                    if(nlm_entry->exclusive || exclusive)
                        {
                            overlap = 1;
                            break;
                        }
                }
        }

    if(!overlap)
        return NULL;

    nlm_lock_entry_inc_ref(nlm_entry);
    return nlm_entry;
}

nlm_lock_entry_t *nlm_overlapping_entry(struct nlm4_lock * nlm_lock, int exclusive)
{
    nlm_lock_entry_t *nlm_entry;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    nlm_entry = get_nlm_overlapping_entry(nlm_lock, exclusive);
    pthread_mutex_unlock(&nlm_lock_list_mutex);

    return nlm_entry;
}

nlm_lock_entry_t *nlm_add_to_locklist(struct nlm4_lockargs * arg)
{
    int allow = 1;
    int exclusive;
    struct glist_head *glist;
    struct nlm4_lock *nlm_lock;
    nlm_lock_entry_t *nlm_entry;
    uint64_t nlm_entry_end, nlm_lock_end;

    nlm_lock = &arg->alock;
    exclusive = arg->exclusive;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    /*
     * First search for a blocked request. Client can ignore the blocked
     * request and keep sending us new lock request again and again. So if
     * we have a mapping blocked request return that
     */
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(netobj_compare(&nlm_entry->fh, &nlm_lock->fh))
                continue;
            if(nlm_entry->state != NLM4_BLOCKED)
                continue;
            if(nlm_entry->start != nlm_lock->l_offset)
                continue;
            if(nlm_entry->len != nlm_lock->l_len)
                continue;
            if(nlm_entry->exclusive != exclusive)
                continue;
            /*
             * We have matched all atribute of the nlm4_lock.
             * Just return the nlm_entry with ref count inc
             */
            nlm_lock_entry_inc_ref(nlm_entry);
            pthread_mutex_unlock(&nlm_lock_list_mutex);
            LogFullDebug(COMPONENT_NFSPROTO,
                         "lock request (but found blocked locks)");
            return nlm_entry;
        }

    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(netobj_compare(&nlm_entry->fh, &nlm_lock->fh))
                continue;

            if(nlm_entry->len)
                nlm_entry_end = nlm_entry->start + nlm_entry->len;
            else
                nlm_entry_end = UINT64_MAX;

            if(nlm_lock->l_len)
                nlm_lock_end = nlm_lock->l_offset + nlm_lock->l_len;
            else
                nlm_lock_end = UINT64_MAX;

            if((nlm_entry_end > nlm_lock->l_offset) &&
               (nlm_lock_end > nlm_entry->start))
                {
                    /* lock overlaps see if we can allow */
                    if(nlm_entry->exclusive || exclusive)
                        {
                            allow = 0;
                            break;
                        }
                }
        }
    nlm_entry = nlm4_lock_to_nlm_lock_entry(arg);
    if(!nlm_entry)
        goto error_out;
    /*
     * Add nlm_entry to the lock list with
     * granted or blocked state. Since we haven't yet added
     * nlm_lock_entry to the lock list, no other threads can
     * find this lock entry. So no need to take the lock.
     */
    if(allow)
        {
            nlm_entry->state = NLM4_GRANTED;
        }
    else
        {
            nlm_entry->state = NLM4_BLOCKED;
        }
    /*
     * +1 for being on the list
     * +1 for the refcount returned
     */
    nlm_entry->ref_count += 2;
    glist_add_tail(&nlm_lock_list, &nlm_entry->lock_list);

error_out:
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    return nlm_entry;
}

static void do_nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry)
{
    /*
     * If some other thread is holding a reference to this nlm_lock_entry
     * don't free the structure. But drop from the lock list
     */
    glist_del(&nlm_entry->lock_list);

    pthread_mutex_lock(&nlm_entry->lock);
    nlm_entry->ref_count--;
    if(nlm_entry->ref_count)
        {
            pthread_mutex_unlock(&nlm_entry->lock);
            return;
        }
    pthread_mutex_unlock(&nlm_entry->lock);
    /*
     * We have dropped ourself from the lock list. So other
     * thread won't be able to find this lock entry. And since
     * ref_count is 0 there is existing reference to the entry.
     * So update without lock held.
     */
    free(nlm_entry->caller_name);
    netobj_free(&nlm_entry->fh);
    netobj_free(&nlm_entry->oh);
    netobj_free(&nlm_entry->cookie);
    Mem_Free(nlm_entry);
}

void nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry)
{
    pthread_mutex_lock(&nlm_lock_list_mutex);
    do_nlm_remove_from_locklist(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
}

static void nlm_init_locklist(void)
{
    init_glist(&nlm_lock_list);
    pthread_mutex_init(&nlm_lock_list_mutex, NULL);
}

nlm_lock_entry_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock,
                                      int exclusive, int state)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(strcmp(nlm_entry->caller_name, nlm_lock->caller_name))
                continue;
            if(netobj_compare(&nlm_entry->fh, &nlm_lock->fh))
                continue;
            if(netobj_compare(&nlm_entry->oh, &nlm_lock->oh))
                continue;
            if(nlm_entry->svid != nlm_lock->svid)
                continue;
            if(state == NLM4_GRANTED)
                {
                    /*
                     * We don't check the below flag when looking for
                     * lock in the lock list with state granted. Lookup
                     * with state granted happens for unlock operation
                     * and RFC says it should only match caller_name, fh,oh
                     * and svid
                     */
                    break;
                }
            if(nlm_entry->start != nlm_lock->l_offset)
                continue;
            if(nlm_entry->len != nlm_lock->l_len)
                continue;
            if(nlm_entry->exclusive != exclusive)
                continue;
            if(nlm_entry->state != state)
                continue;
            /* We have matched all atribute of the nlm4_lock */
            break;
        }
    if(glist == &nlm_lock_list)
        nlm_entry = NULL;
    else
        nlm_lock_entry_inc_ref(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);

    return nlm_entry;
}

static nlm_lock_entry_t *nlm_lock_entry_t_dup(nlm_lock_entry_t * orig_nlm_entry)
{
    nlm_lock_entry_t *nlm_entry;
    nlm_entry = (nlm_lock_entry_t *) Mem_Calloc(1, sizeof(nlm_lock_entry_t));
    if(!nlm_entry)
        return NULL;
    nlm_entry->caller_name = strdup(orig_nlm_entry->caller_name);
    if(!copy_netobj(&nlm_entry->fh, &orig_nlm_entry->fh))
        goto err_out;
    if(!copy_netobj(&nlm_entry->oh, &orig_nlm_entry->oh))
        goto err_out;
    if(!copy_netobj(&nlm_entry->cookie, &orig_nlm_entry->cookie))
        goto err_out;
    nlm_entry->svid = orig_nlm_entry->svid;
    nlm_entry->start = orig_nlm_entry->start;
    nlm_entry->len = orig_nlm_entry->len;
    nlm_entry->state = orig_nlm_entry->state;
    nlm_entry->exclusive = orig_nlm_entry->exclusive;
    nlm_entry->ref_count = 0;
    pthread_mutex_init(&nlm_entry->lock, NULL);
    return nlm_entry;
err_out:
    free(nlm_entry->caller_name);
    netobj_free(&nlm_entry->fh);
    netobj_free(&nlm_entry->oh);
    netobj_free(&nlm_entry->cookie);
    Mem_Free(nlm_entry);
    return NULL;

}

static int do_nlm_delete_lock_entry(nlm_lock_entry_t * nlm_entry,
                                    struct nlm4_lock *nlm_lock,
                                    struct glist_head *split_list)
{
    int delete_lck_cnt = 0;
    uint64_t nlm_entry_end;
    uint64_t nlm_lock_end;
    nlm_lock_entry_t *nlm_entry_left = NULL;
    nlm_lock_entry_t *nlm_entry_right = NULL;

    if(nlm_entry->len)
        nlm_entry_end = nlm_entry->start + nlm_entry->len;
    else
        nlm_entry_end = UINT64_MAX;

    if(nlm_lock->l_len)
        nlm_lock_end = nlm_lock->l_offset + nlm_lock->l_len;
    else
        nlm_lock_end = UINT64_MAX;

    if(nlm_lock_end < nlm_entry->start)
        /* nothing to split */
        goto done;
    if(nlm_entry_end < nlm_lock->l_offset)
        /* nothing to split */
        goto done;
    if((nlm_lock->l_offset <= nlm_entry->start) &&
       nlm_lock_end >= nlm_entry_end)
        /* Fully overlap */
        goto complete_remove;
    /* Delete the old entry and add the two new entries */
    if(nlm_lock->l_offset > nlm_entry->start)
        {
            delete_lck_cnt--;
            nlm_entry_left = nlm_lock_entry_t_dup(nlm_entry);
            /* FIXME handle error */
            nlm_entry_left->len = nlm_lock->l_offset - nlm_entry->start;
        }
    if(nlm_lock_end < nlm_entry_end)
        {
            delete_lck_cnt--;;
            nlm_entry_right = nlm_lock_entry_t_dup(nlm_entry);
            /* FIXME handle error */
            nlm_entry_right->start = nlm_lock_end;
            nlm_entry_right->len = nlm_entry_end - nlm_lock_end;
        }
    if(nlm_entry_left)
        {
            nlm_lock_entry_inc_ref(nlm_entry_left);
            glist_add_tail(split_list, &(nlm_entry_left->lock_list));
        }
    if(nlm_entry_right)
        {
            nlm_lock_entry_inc_ref(nlm_entry_right);
            glist_add_tail(split_list, &(nlm_entry_right->lock_list));
        }
complete_remove:
    delete_lck_cnt++;
    do_nlm_remove_from_locklist(nlm_entry);
done:
    return delete_lck_cnt;
}

/* We need to iterate over the full lock list and remove
 * any mapping entry. And l_offset = 0 and l_len = 0 nlm_lock
 * implies remove all entries
 */
int nlm_delete_lock_entry(struct nlm4_lock *nlm_lock)
{
    int delete_lck_cnt = 0;
    nlm_lock_entry_t *nlm_entry;
    struct glist_head split_lock_list;
    struct glist_head *glist, *glistn;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    init_glist(&split_lock_list);
    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(strcmp(nlm_entry->caller_name, nlm_lock->caller_name))
                continue;
            if(netobj_compare(&nlm_entry->fh, &nlm_lock->fh))
                continue;
            if(netobj_compare(&nlm_entry->oh, &nlm_lock->oh))
                continue;
            if(nlm_entry->svid != nlm_lock->svid)
                continue;
            /*
             * We have matched all atribute of the nlm4_lock
             * Even though we are taking a reference to nlm_entry, we
             * don't inc the ref count because we want to drop the lock entry.
             */
            delete_lck_cnt += do_nlm_delete_lock_entry(nlm_entry,
                                                       nlm_lock,
                                                       &split_lock_list);
        }
    /* now add the split lock list */
    glist_add_list_tail(&nlm_lock_list, &split_lock_list);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    return delete_lck_cnt;
}

nlm_lock_entry_t *nlm_find_lock_entry_by_cookie(netobj * cookie)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(!netobj_compare(&nlm_entry->cookie, cookie))
                break;
        }
    if(glist == &nlm_lock_list)
        nlm_entry = NULL;
    else
        nlm_lock_entry_inc_ref(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    return nlm_entry;
}

int start_nlm_grace_period(void)
{
    return gettimeofday(&nlm_grace_tv, NULL);
}

int in_nlm_grace_period(void)
{
    struct timeval tv;
    if(nlm_grace_tv.tv_sec == 0)
        return 0;

    if(gettimeofday(&tv, NULL) == 0)
        {
            if(tv.tv_sec < (nlm_grace_tv.tv_sec + NLM4_GRACE_PERIOD))
                {
                    return 1;
                }
            else
                {
                    nlm_grace_tv.tv_sec = 0;
                    return 0;
                }
        }
    return 0;
}

void nlm_init(void)
{
    nlm_async_callback_init();
    nlm_init_locklist();
    nsm_unmonitor_all();
    start_nlm_grace_period();
}

void nlm_node_recovery(char *name,
                       fsal_op_context_t * pcontext,
                       cache_inode_client_t * pclient, hash_table_t * ht)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist, *glistn;

    LogFullDebug(COMPONENT_NFSPROTO, "Recovery for host %s\n", name);

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(strcmp(nlm_entry->caller_name, name))
                continue;

            /*
             * inc ref so that we can remove entry from the list
             * and still use the lock entry values
             */
            nlm_lock_entry_inc_ref(nlm_entry);

            /*
             * now remove the from locklist
             */
            do_nlm_remove_from_locklist(nlm_entry);

            /*
             * We don't inc ref count because we want to drop the lock entry
             */
            if(nlm_entry->state == NLM4_GRANTED)
                {
                    /*
                     * Submit the async request to send granted response for
                     * locks that can be granted
                     */
                    nlm_grant_blocked_locks(&nlm_entry->fh);
                }
            nlm_lock_entry_dec_ref(nlm_entry);
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
}

int nlm_monitor_host(char *name)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(!strcmp(nlm_entry->caller_name, name))
                {
                    /* there is already a lock with the same
                     * caller_name. So we should already be monitoring
                     * the host
                     */
                    pthread_mutex_unlock(&nlm_lock_list_mutex);
                    return 0;
                }
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    /* There is nobody monitoring the host */
    LogFullDebug(COMPONENT_NFSPROTO, "Monitoring host %s\n", name);
    return nsm_monitor(name);
}

int nlm_unmonitor_host(char *name)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(!strcmp(nlm_entry->caller_name, name))
                {
                    /* We have locks tracking the same caller_name
                     * we cannot unmonitor the host now. We will do
                     * it for the last unlock from the host
                     */
                    pthread_mutex_unlock(&nlm_lock_list_mutex);
                    return 0;
                }
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    /* There is nobody holding a lock with host */
    LogFullDebug(COMPONENT_NFSPROTO, "Unmonitoring host %s\n", name);
    return nsm_unmonitor(name);
}

static void nlm4_send_grant_msg(void *arg)
{
    int retval;
    struct nlm4_testargs inarg;
    nlm_lock_entry_t *nlm_entry = (nlm_lock_entry_t *) arg;

    /* If we fail allocation the best is to delete the block entry
     * so that client can try again and get the lock. May be
     * by then we are able to allocate objects
     */
    if(!copy_netobj(&inarg.alock.fh, &nlm_entry->fh))
        {
            goto free_nlm_lock_entry;
        }
    if(!copy_netobj(&inarg.alock.oh, &nlm_entry->oh))
        {
            netobj_free(&inarg.alock.fh);
            goto free_nlm_lock_entry;
        }

    if(!copy_netobj(&inarg.cookie, &nlm_entry->cookie))
        {
            netobj_free(&inarg.alock.oh);
            netobj_free(&inarg.alock.fh);
            goto free_nlm_lock_entry;
        }
    inarg.alock.caller_name = strdup(nlm_entry->caller_name);
    if(!inarg.alock.caller_name)
        {
            netobj_free(&inarg.cookie);
            netobj_free(&inarg.alock.oh);
            netobj_free(&inarg.alock.fh);
            goto free_nlm_lock_entry;
        }
    inarg.exclusive = nlm_entry->exclusive;
    inarg.alock.svid = nlm_entry->svid;
    inarg.alock.l_offset = nlm_entry->start;
    inarg.alock.l_len = nlm_entry->len;

    retval = nlm_send_reply(NLMPROC4_GRANTED_MSG,
                            nlm_entry->caller_name,
                            &inarg, NULL);
    free(inarg.alock.caller_name);
    netobj_free(&inarg.alock.fh);
    netobj_free(&inarg.alock.oh);
    netobj_free(&inarg.cookie);
    if(retval != RPC_SUCCESS)
        {
            /*
             * We are not able call granted callback. Some client may retry
             * the lock again. So remove the existing blocked nlm entry
             */
            LogMajor(COMPONENT_NFSPROTO,
                     "%s: GRANTED_MSG RPC call failed. Removing the blocking lock\n",
                     __func__);
            goto free_nlm_lock_entry;
        }
    else
        {
            /*
             * We already have marked the locks granted
             */
            LogMajor(COMPONENT_NFSPROTO,
                     "%s: Granted the blocking lock successfully\n", __func__);
        }
free_nlm_lock_entry:
    nlm_remove_from_locklist(nlm_entry);
    /*
     * Submit the async request to send granted response for
     * locks that can be granted, because of this removal
     * from the lock list. If the client is lucky. It
     * will send the lock request again and before the
     * block locks are granted it gets the lock.
     */
    nlm_grant_blocked_locks(&nlm_entry->fh);
    nlm_lock_entry_dec_ref(nlm_entry);
    return;
}

static void do_nlm_grant_blocked_locks(void *arg)
{
    netobj *fh;
    struct nlm4_lock nlm_lock;
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist, *glistn;
    nlm_lock_entry_t *nlm_entry_overlap;

    fh = (netobj *) arg;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(netobj_compare(&nlm_entry->fh, fh))
                continue;
            if(nlm_entry->state != NLM4_BLOCKED)
                continue;
            /*
             * found a blocked entry for this file handle
             * See if we can place the lock
             */
            /* dummy nlm4_lock */
            if(!copy_netobj(&nlm_lock.fh, &nlm_entry->fh))
                {
                    /* If we fail the best is to delete the block entry
                     * so that client can try again and get the lock. May be
                     * by then we are able to allocate objects
                     */
                    do_nlm_remove_from_locklist(nlm_entry);
                    continue;
                }
            nlm_lock.l_offset = nlm_entry->start;
            nlm_lock.l_len = nlm_entry->len;
            nlm_entry_overlap = get_nlm_overlapping_entry(&nlm_lock, nlm_entry->exclusive);
            netobj_free(&nlm_lock.fh);
            if(nlm_entry_overlap)
                {
                    nlm_lock_entry_dec_ref(nlm_entry_overlap);
                    continue;
                }

            pthread_mutex_lock(&nlm_entry->lock);
            /*
             * Mark the nlm_entry as granted and send a grant msg rpc
             * Some os only support grant msg rpc
             */

            nlm_entry->state = NLM4_GRANTED;
            nlm_entry->ref_count++;
            pthread_mutex_unlock(&nlm_entry->lock);
            /*
             * We don't want to send the granted_msg rpc holding
             * nlm_lock_list_mutex. That will prevent other lock operation
             * at the server. We have incremented nlm_entry ref_count.
             */
            nlm_async_callback(nlm4_send_grant_msg, (void *)nlm_entry);
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    netobj_free(fh);
    Mem_Free(fh);
}

void nlm_grant_blocked_locks(netobj * orig_fh)
{
    netobj *fh;
    fh = (netobj *) Mem_Alloc(sizeof(netobj));
    copy_netobj(fh, orig_fh);
    /*
     * We don't want to block the unlock request to wait
     * for us to grant lock to other host. So create an async
     * task
     */
    nlm_async_callback(do_nlm_grant_blocked_locks, (void *)fh);
}

/*
 * called when server get a response from client
 * saying the grant message is denied due to grace period
 */
void nlm_resend_grant_msg(void *arg)
{
    /*
     * We should wait for client grace period
     */
    sleep(NLM4_CLIENT_GRACE_PERIOD);

    nlm4_send_grant_msg(arg);
}
