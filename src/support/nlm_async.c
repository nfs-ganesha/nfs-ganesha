/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * --------------------------
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
#include <pthread.h>
#include <sys/time.h>

#include "stuff_alloc.h"
#include "nlm_list.h"
#include "nlm_async.h"

static pthread_t nlm_async_thread;
static pthread_mutex_t nlm_async_queue_mutex;
static struct glist_head nlm_async_queue;
static pthread_cond_t nlm_async_queue_cond;

typedef struct
{
  nlm_callback_func *func;
  void *arg;
  struct glist_head glist;
} nlm_queue_t;

/* Execute a func from the async queue */
void *nlm_async_func(void *argp)
{
  int rc;
  nlm_queue_t *entry;
  struct timeval now;
  struct timespec timeout;
  struct glist_head nlm_async_tmp_queue;
  struct glist_head *glist, *glistn;

  SetNameFunction("nlm_async_thread");

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogMajor(COMPONENT_NFSPROTO, "NLM async thread: Memory manager could not be initialized, exiting...");
      exit(1);
    }
  LogEvent(COMPONENT_NFSPROTO, "NLM async thread: Memory manager successfully initialized");
#endif
  LogDebug(COMPONENT_NFSPROTO, "NLM async thread: my pthread id is %p",
                  (caddr_t) pthread_self());

  while(1)
    {
      pthread_mutex_lock(&nlm_async_queue_mutex);
 restart:
      while(glist_empty(&nlm_async_queue))
        {
          gettimeofday(&now, NULL);
          timeout.tv_sec = 10 + now.tv_sec;
          timeout.tv_nsec = 0;
          pthread_cond_timedwait(&nlm_async_queue_cond, &nlm_async_queue_mutex, &timeout);
        }
      init_glist(&nlm_async_tmp_queue);
      /* Collect all the work items and add it to the temp
       * list. Later we iterate over tmp list without holding
       * the nlm_async_queue_mutex
       */
      glist_for_each_safe(glist, glistn, &nlm_async_queue)
      {
        glist_del(glist);
        glist_add(&nlm_async_tmp_queue, glist);

      }
      pthread_mutex_unlock(&nlm_async_queue_mutex);
      glist_for_each_safe(glist, glistn, &nlm_async_tmp_queue)
      {
        entry = glist_entry(glist, nlm_queue_t, glist);
        glist_del(&entry->glist);
        /* FIXME should we handle error here ? */
        (*(entry->func)) (entry->arg);
        Mem_Free(entry);
      }
    }

}

/* Insert 'func' to async queue */
void nlm_async_callback(nlm_callback_func * func, void *arg)
{
  nlm_queue_t *q;

  q = (nlm_queue_t *) Mem_Alloc(sizeof(nlm_queue_t));
  q->func = func;
  q->arg = arg;

  pthread_mutex_lock(&nlm_async_queue_mutex);
  glist_add_tail(&nlm_async_queue, &q->glist);
  pthread_mutex_unlock(&nlm_async_queue_mutex);
  pthread_cond_signal(&nlm_async_queue_cond);
}

int nlm_async_callback_init()
{
  int rc;

  pthread_mutex_init(&nlm_async_queue_mutex, NULL);
  pthread_cond_init(&nlm_async_queue_cond, NULL);
  init_glist(&nlm_async_queue);

  rc = pthread_create(&nlm_async_thread, NULL, nlm_async_func, NULL);
  if(rc < 0)
    {
      return -1;
    }

  return 0;
}
