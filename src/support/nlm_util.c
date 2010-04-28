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
#include "log_functions.h"
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

static struct glist_head nlm_lock_list;
static pthread_mutex_t nlm_lock_list_mutex;

fsal_lockdesc_t *nlm_lock_to_fsal_lockdesc(struct nlm4_lock *nlm_lock, bool_t exclusive)
{
  fsal_lockdesc_t *fldesc;

  fldesc = (fsal_lockdesc_t *) Mem_Alloc(sizeof(fsal_lockdesc_t));
  if(!fldesc)
    return NULL;

  if(exclusive)
    fldesc->flock.l_type = F_WRLCK;
  else
    fldesc->flock.l_type = F_RDLCK;
  fldesc->flock.l_whence = SEEK_SET;
  fldesc->flock.l_start = nlm_lock->l_offset;
  fldesc->flock.l_len = nlm_lock->l_len;
  return fldesc;
}

static netobj *copy_netobj(netobj * dst, netobj * src)
{
  dst->n_bytes = (char *)Mem_Alloc(src->n_len);
  if(!dst->n_bytes)
    return NULL;
  dst->n_len = src->n_len;
  memcpy(dst->n_bytes, src->n_bytes, src->n_len);
  return dst;
}

static void net_obj_free(netobj * obj)
{
  if(obj->n_bytes)
    Mem_Free(obj->n_bytes);
}

static int netobj_compare(netobj obj1, netobj obj2)
{
  if(obj1.n_len != obj2.n_len)
    return 1;
  if(memcmp(obj1.n_bytes, obj2.n_bytes, obj1.n_len))
    return 1;
  return 0;
}

static nlm_lock_t *nlm4_lock_to_nlm_lock(struct nlm4_lock *nlm_lock, int exclusive)
{
  nlm_lock_t *nlmb;
  nlmb = (nlm_lock_t *) Mem_Calloc(1, sizeof(nlm_lock_t));
  if(!nlmb)
    return NULL;
  nlmb->caller_name = strdup(nlm_lock->caller_name);
  if(!copy_netobj(&nlmb->fh, &nlm_lock->fh))
    goto err_out;
  if(!copy_netobj(&nlmb->oh, &nlm_lock->oh))
    goto err_out;
  nlmb->svid = nlm_lock->svid;
  nlmb->offset = nlm_lock->l_offset;
  nlmb->len = nlm_lock->l_len;
  nlmb->exclusive = exclusive;
  return nlmb;
 err_out:
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
  return NULL;
}

nlm_lock_t *nlm_add_to_locklist(struct nlm4_lock * nlm_lock, int exclusive)
{
  nlm_lock_t *nlmb;
  nlmb = nlm4_lock_to_nlm_lock(nlm_lock, exclusive);
  if(!nlmb)
    return NULL;
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_add_tail(&nlm_lock_list, &nlmb->lock_list);
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  return nlmb;
}

void nlm_remove_from_locklist(nlm_lock_t * nlmb)
{
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_del(&nlmb->lock_list);
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
}

void nlm_init_locklist(void)
{
  init_glist(&nlm_lock_list);
  pthread_mutex_init(&nlm_lock_list_mutex, NULL);
}

nlm_lock_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock, int exclusive, int state)
{
  nlm_lock_t *nlmb;
  struct glist_head *glist;
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_for_each(glist, &nlm_lock_list)
  {
    nlmb = glist_entry(glist, nlm_lock_t, lock_list);
    if(strcmp(nlmb->caller_name, nlm_lock->caller_name))
      continue;
    if(netobj_compare(nlmb->fh, nlm_lock->fh))
      continue;
    if(netobj_compare(nlmb->oh, nlm_lock->oh))
      continue;
    if(nlmb->svid != nlm_lock->svid)
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
    if(nlmb->offset != nlm_lock->l_offset)
      continue;
    if(nlmb->len != nlm_lock->l_len)
      continue;
    if(nlmb->exclusive != exclusive)
      continue;
    if(nlmb->state != state)
      continue;
    /* We have matched all atribute of the nlm4_lock */
    break;
  }
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  if(glist == &nlm_lock_list)
    return NULL;
  return nlmb;
}

static nlm_lock_t *nlm_lock_t_dup(nlm_lock_t * orig_nlmb)
{
  nlm_lock_t *nlmb;
  nlmb = (nlm_lock_t *) Mem_Calloc(1, sizeof(nlm_lock_t));
  if(!nlmb)
    return NULL;
  nlmb->caller_name = strdup(orig_nlmb->caller_name);
  if(!copy_netobj(&nlmb->fh, &orig_nlmb->fh))
    goto err_out;
  if(!copy_netobj(&nlmb->oh, &orig_nlmb->oh))
    goto err_out;
  nlmb->svid = orig_nlmb->svid;
  nlmb->offset = orig_nlmb->offset;
  nlmb->len = orig_nlmb->len;
  nlmb->state = orig_nlmb->state;
  nlmb->exclusive = orig_nlmb->exclusive;
  return nlmb;
 err_out:
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
  return NULL;

}

void nlm_delete_lock_entry(nlm_lock_t * nlmb, struct nlm4_lock *nlm_lock)
{
  nlm_lock_t *nlmb_left = NULL;
  nlm_lock_t *nlmb_right = NULL;
  if(nlm_lock->l_offset > nlmb->offset)
    {
      nlmb_left = nlm_lock_t_dup(nlmb);
      /* FIXME handle error */
      nlmb_left->len = nlmb->offset - nlm_lock->l_offset;
    }
  if((nlm_lock->l_offset + nlm_lock->l_len) < (nlmb->offset + nlmb->len))
    {
      nlmb_right = nlm_lock_t_dup(nlmb);
      /* FIXME handle error */
      nlmb_right->offset = nlm_lock->l_offset + nlm_lock->l_len;
      nlmb_right->len = (nlmb->offset + nlmb->len) -
          (nlm_lock->l_offset + nlm_lock->l_len);
    }
  /* Delete the old entry and add the two new entries */
  nlm_remove_from_locklist(nlmb);
  pthread_mutex_lock(&nlm_lock_list_mutex);
  if(nlmb_left)
    glist_add_tail(&nlm_lock_list, &(nlmb_left->lock_list));
  if(nlmb_right)
    glist_add_tail(&nlm_lock_list, &(nlmb_right->lock_list));
  pthread_mutex_unlock(&nlm_lock_list_mutex);
}
