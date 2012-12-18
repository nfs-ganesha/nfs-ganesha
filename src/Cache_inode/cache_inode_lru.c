/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "abstract_atomic.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"

/**
 *
 * @file cache_inode_lru.c
 * @author Matt Benjamin
 * @brief Constant-time cache inode cache management implementation
 *
 * @section DESCRIPTION
 *
 * This module implements a constant-time cache management strategy
 * based on LRU.  Some ideas are taken from 2Q [Johnson and Shasha 1994]
 * and MQ [Zhou, Chen, Li 2004].  In this system, cache management does
 * interact with cache entry lifecycle, but the lru queue is not a garbage
 * collector. Most imporantly, cache management operations execute in constant
 * time, as expected with LRU (and MQ).
 *
 * Cache entries in use by a currently-active protocol request (or other
 * operation) have a positive refcount, and threfore should not be present
 * at the cold end of an lru queue if the cache is well-sized.
 *
 * Cache entries with lock and open state are not eligible for collection
 * under ordinary circumstances, so are kept on a separate lru_pinned
 * list to retain constant time.
 *
 * The locking discipline for this module is complex, because an entry
 * LRU can either be found through the cache entry in which it is
 * embedded (through a hash table or weakref lookup) or one can be
 * found on the queue.  Thus, we have some sections for which locking
 * the entry then the queue is natural, and others for which locking
 * the queue then the entry is natural.  Because it is the most common
 * case in request processing, we have made the lock the queue case
 * the defined lock order.
 *
 * This introduces some complication, particularly in the case of
 * cache_inode_lru_get and the repaer thread, which access entries
 * through their queue.  Therefore, we introduce the following rules
 * for accessing LRU entries:
 *
 *    - The LRU refcount may be increased by a thread holding either
 *      the entry lock or the lock for the queue holding the entry.
 *    - The entry flags may be set or inspected only by a thread
 *      holding the lock on that entry.
 *    - An entry may only be removed from or inserted into a queue by
 *      a thread holding a lock on both the entry and the queue.
 *    - A thread may only decrement the reference count while holding
 *      the entry lock.
 *    - A thread that wishes to decrement the reference count must
 *      check that the reference count is greater than
 *      LRU_SENTINEL_REFCOUNT and, if not, acquire the queue lock
 *      before decrementing it.  When the reference count falls to 0,
 *      the controlling thread will have already acquired the queue
 *      lock.  It must then set the LRU_ENTRY_CONDEMNED bit on the
 *      entry's flags, and remove it from the queue.  It must then
 *      drop both the entry and queue lock, call pthread_yield, and
 *      reacquire the entry lock before continuing with disposal.
 *    - A thread wishing to operate on an entry picked from a given
 *      queue fragment must lock that queue fragment, find the entry,
 *      and increase its reference count by one.  It must then store a
 *      pointer to the entry and release the queue lock before
 *      acquiring the entry lock.  If the LRU_ENTRY_CONDEMNED bit is
 *      set, it must relinquish its lock on the entry and attempt no
 *      further access to it.  Otherwise, it must examine the flags
 *      and lane stored in the entry to determine the current queue
 *      fragment containing it, rather than assuming that the original
 *      location is still valid.
 */

/* Forward Declaration */

static void *lru_thread(void *arg);
struct lru_state lru_state;

/**
 * A single queue structure.
 */

struct lru_q_base
{
     struct glist_head q; /* LRU is at HEAD, MRU at tail */
     pthread_mutex_t mtx;
     uint64_t size;
};

/* Cache-line padding macro from MCAS */

#define CACHE_LINE_SIZE 64 /* XXX arch-specific define */
#define CACHE_PAD(_n) char __pad ## _n [CACHE_LINE_SIZE]
#define ALIGNED_ALLOC(_s)                                       \
     ((void *)(((unsigned long)malloc((_s)+CACHE_LINE_SIZE*2) + \
                CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE-1)))

/**
 * A single queue lane, holding both movable and pinned entries.
 */

struct lru_q_
{
     struct lru_q_base lru;
     struct lru_q_base lru_pinned; /* uncollectable, due to state */
     CACHE_PAD(0);
};

/**
 * A multi-level LRU algorithm inspired by MQ [Zhou].  Transition from
 * L1 to L2 implies various checks (open files, etc) have been
 * performed, so ensures they are performed only once.  A
 * correspondence to the "scan resistance" property of 2Q and MQ is
 * accomplished by recycling/clean loads onto the LRU of L1.  Async
 * processing onto L2 constrains oscillation in this algorithm.
 */

static struct lru_q_ LRU_1[LRU_N_Q_LANES];
static struct lru_q_ LRU_2[LRU_N_Q_LANES];

/**
 * This is a global counter of files opened by cache_inode.  This is
 * preliminary expected to go away.  Problems with this method are
 * that it overcounts file descriptors for FSALs that don't use them
 * for open files, and, under the Lieb Rearchitecture, FSALs will be
 * responsible for caching their own file descriptors, with
 * interfaces for Cache_Inode to interrogate them as to usage or
 * instruct them to close them.
 */

size_t open_fd_count = 0;

/**
 * The refcount mechanism distinguishes 3 key object states:
 *
 * 1. unreferenced (unreachable)
 * 2. unincremented, but reachable
 * 3. incremented
 *
 * It seems most convenient to make unreferenced correspond to refcount==0.
 * Then refcount==1 is a SENTINEL_REFCOUNT in which the only reference to
 * the entry is the set of functions which can grant new references.  An
 * object with refcount > 1 has been referenced by some thread, which must
 * release its reference at some point.
 *
 * Currently, I propose to distinguish between objects with positive refcount
 * and objects with state.  The latter could be evicted, in the normal case,
 * only with loss of protocol correctness, but may have only the sentinel
 * refcount.  To preserve constant time operation, they are stored in an
 * independent partition of the LRU queue.
 */

static pthread_mutex_t lru_mtx;
static pthread_cond_t lru_cv;

static const uint32_t FD_FALLBACK_LIMIT = 0x400;

/**
 * A package for the ID and state of the LRU thread.
 */

static struct lru_thread_state
{
     pthread_t thread_id;
     uint32_t flags;
} lru_thread_state;

/**
 * @brief Initialize a single base queue.
 *
 * This function initializes a single queue fragment (a half-lane)
 */

static inline void
lru_init_queue(struct lru_q_base *q)
{
     init_glist(&q->q);
     pthread_mutex_init(&q->mtx, NULL);
     q->size = 0;
}

/**
 * @brief Return a pointer to the appropriate queue
 *
 * This function returns a pointer to the appropriate LRU queue
 * fragment corresponding to the given flags and lane.
 *
 * @param[in] flags  May be any combination of 0, LRU_ENTRY_PINNED,
 *                   and LRU_ENTRY_L2 or'd together.
 * @param[in] lane   An integer, must be less than the total number of
 *                   lanes.
 *
 * @return The queue containing entries with the given lane and state.
 */

static inline struct lru_q_base *
lru_select_queue(uint32_t flags, uint32_t lane)
{
     assert(lane < LRU_N_Q_LANES);
     if (flags & LRU_ENTRY_PINNED) {
          if (flags & LRU_ENTRY_L2) {
               return &LRU_2[lane].lru_pinned;
          } else {
               return &LRU_1[lane].lru_pinned;
          }
     } else {
          if (flags & LRU_ENTRY_L2) {
               return &LRU_2[lane].lru;
          } else {
               return &LRU_1[lane].lru;
          }
     }
}

/**
 * @brief Get the appropriate lane for a cache_entry
 *
 * This function gets the LRU lane by taking the modulus of the
 * supplied pointer.
 *
 * @param[in] entry  A pointer to a cache entry
 *
 * @return The LRU lane in which that entry should be stored.
 */

static inline uint32_t
lru_lane_of_entry(cache_entry_t *entry)
{
    return (uint32_t) (((uintptr_t) entry) % LRU_N_Q_LANES);
}

/**
 * @brief Insert an entry into the specified queue fragment
 *
 * This function determines the queue corresponding to the supplied
 * lane and flags, inserts the entry into that queue, and updates the
 * entry to holds the flags and lane.
 *
 * The caller MUST have a lock on the entry and MUST NOT hold a lock
 * on the queue.
 *
 * @param[in] lru   The entry to insert
 * @param[in] flags The flags indicating which subqueue into which it
 *                  is to be inserted.  The same flag combinations are
 *                  valid as for lru_select_queue
 * @param[in] lane  The lane into which this entry should be inserted
 */

static inline void
lru_insert_entry(cache_inode_lru_t *lru, uint32_t flags, uint32_t lane)
{
     /* Destination LRU */
     struct lru_q_base *d = NULL;

     d = lru_select_queue(flags, lane);
     pthread_mutex_lock(&d->mtx);
     glist_add(&d->q, &lru->q);
     ++(d->size);
     pthread_mutex_unlock(&d->mtx);

     /* Set the flags on the entry to exactly the set of LRU_ENTRY_L2
        and LRU_ENTRY_PINNED supplied in the flags argument. */

     lru->flags &= ~(LRU_ENTRY_L2 | LRU_ENTRY_PINNED);
     lru->flags |= (flags & (LRU_ENTRY_L2 | LRU_ENTRY_PINNED));
     lru->lane = lane;
}

/**
 * @brief Remove an entry from its queue
 *
 * This function removes an entry from the queue currently holding it
 * and updates its lane and flags to corresponding to no queue.  If
 * the entry is in no queue, this is a no-op.
 *
 * The caller MUST have a lock on the entry and MUST NOT hold a lock
 * on the queue.
 *
 * @param[in] lru The entry to remove from its queue.
 */

static inline void
lru_remove_entry(cache_inode_lru_t *lru)
{
     if (lru->lane == LRU_NO_LANE) {
          return;
     }

     /* Source LRU */
     struct lru_q_base *s = NULL;
     s = lru_select_queue(lru->flags, lru->lane);
     pthread_mutex_lock(&s->mtx);
     glist_del(&lru->q);
     --(s->size);
     pthread_mutex_unlock(&s->mtx);
     lru->flags &= ~(LRU_ENTRY_L2 | LRU_ENTRY_PINNED);
     /* Anyone interested in this entry should back off immediately. */
     lru->lane = LRU_NO_LANE;
}

/**
 * @brief Move an entry from one queue fragment to another
 *
 * This function moves an entry from the queue containing it to the
 * queue specified by the lane and flags.  The entry MUST be locked
 * and no queue locks may be held.
 *
 * @param[in] lru   The entry to move
 * @param[in] flags As accepted by lru_select_queue
 * @param[in] lane  The lane identifying the fragment
 */

static inline void
lru_move_entry(cache_inode_lru_t *lru,
               uint32_t flags,
               uint32_t lane)
{
     /* Source LRU */
     struct lru_q_base *s = NULL;
     /* Destination LRU */
     struct lru_q_base *d = NULL;

     if ((lru->lane == LRU_NO_LANE) &&
         (lane == LRU_NO_LANE)) {
          /* From nothing, to nothing. */
          return;
     } else if (lru->lane == LRU_NO_LANE) {
          lru_insert_entry(lru, flags, lane);
          return;
     } else if (lane == LRU_NO_LANE) {
          lru_remove_entry(lru);
          return;
     }

     s = lru_select_queue(lru->flags, lru->lane);
     d = lru_select_queue(flags, lane);

     if (s == d) {
          pthread_mutex_lock(&s->mtx);
     } else if (s < d) {
          pthread_mutex_lock(&s->mtx);
          pthread_mutex_lock(&d->mtx);
     } else if (s > d) {
          pthread_mutex_lock(&d->mtx);
          pthread_mutex_lock(&s->mtx);
     }

     glist_del(&lru->q);
     --(s->size);

     /* When moving from L2 to L1, add to the LRU, otherwise add to
        the MRU.  (In general we don't want to promote things except
        on initial reference, but promoting things on move makes more
        sense than demoting them.) */
     if ((lru->flags & LRU_ENTRY_L2) &&
         !(flags & LRU_ENTRY_L2)) {
          glist_add_tail(&d->q, &lru->q);
     } else {
          glist_add(&d->q, &lru->q);
     }
     ++(d->size);

     pthread_mutex_unlock(&s->mtx);
     if (s != d) {
          pthread_mutex_unlock(&d->mtx);
     }

     lru->flags &= ~(LRU_ENTRY_L2 | LRU_ENTRY_PINNED);
     lru->flags |= (flags & (LRU_ENTRY_L2 | LRU_ENTRY_PINNED));
}

/**
 * @brief Clean an entry for recycling.
 *
 * This function cleans an entry up before it's recycled or freed.
 *
 * @param[in] entry  The entry to clean
 */

static inline void
cache_inode_lru_clean(cache_entry_t *entry)
{
     fsal_status_t fsal_status = {0, 0};
     cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

     /* Clean an LRU entry re-use.  */
     assert((entry->lru.refcount == LRU_SENTINEL_REFCOUNT) ||
            (entry->lru.refcount == (LRU_SENTINEL_REFCOUNT - 1)));

     if (cache_inode_fd(entry)) {
          cache_inode_close(entry, CACHE_INODE_FLAG_REALLYCLOSE |
                                   CACHE_INODE_FLAG_NOT_PINNED,
                            &cache_status);
          if (cache_status != CACHE_INODE_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE_LRU,
                       "Error closing file in cleanup: %d.",
                       cache_status);
          }
     }

     if (entry->type == DIRECTORY) {
             cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);
     }

     /* Clean up the associated ressources in the FSAL */
     if (FSAL_IS_ERROR(fsal_status
                       = FSAL_CleanObjectResources(&entry->handle))) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_lru_clean: Couldn't free FSAL ressources "
                  "fsal_status.major=%u", fsal_status.major);
     }

     cache_inode_clean_internal(entry);
     entry->lru.refcount = 0;
     cache_inode_clean_entry(entry);
}

/**
 * @brief Try to pull an entry off the queue
 *
 * This function examines the end of the specified queue and if the
 * entry found there can be re-used, it returns with the entry
 * locked.  Otherwise, it returns NULL.  The caller MUST NOT hold a
 * lock on the queue when this function is called.
 *
 * This function follows the locking discipline detailed above.  it
 * returns an lru entry removed from the queue system and which we are
 * permitted to dispose or recycle.
 *
 * @param[in] q  The queue fragment from which to reap.
 */

static inline cache_inode_lru_t *
lru_try_reap_entry(struct lru_q_base *q)
{
     cache_inode_lru_t *lru = NULL;

     pthread_mutex_lock(&q->mtx);
     lru = glist_first_entry(&q->q, cache_inode_lru_t, q);
     if (!lru) {
          pthread_mutex_unlock(&q->mtx);
          return NULL;
     }

     atomic_inc_int64_t(&lru->refcount);
     pthread_mutex_unlock(&q->mtx);
     pthread_mutex_lock(&lru->mtx);
     if ((lru->flags & LRU_ENTRY_CONDEMNED) ||
         (lru->flags & LRU_ENTRY_KILLED)) {
          atomic_dec_int64_t(&lru->refcount);
          pthread_mutex_unlock(&lru->mtx);
          return NULL;
     }
     if ((lru->refcount > (LRU_SENTINEL_REFCOUNT + 1)) ||
         (lru->flags & LRU_ENTRY_PINNED)) {
          /* Any more than the sentinel and our reference count
             and someone else has a reference.  Plus someone may
             have moved it to the pin queue while we were waiting. */
          atomic_dec_int64_t(&lru->refcount);
          pthread_mutex_unlock(&lru->mtx);
          return NULL;
     }
     /* At this point, we have legitimate access to the entry,
        and we go through the disposal/recycling discipline. */

     /* Make sure the entry is still where we think it is. */
     q = lru_select_queue(lru->flags, lru->lane);
     pthread_mutex_lock(&q->mtx);
     if (lru->refcount > LRU_SENTINEL_REFCOUNT + 1) {
          /* Someone took a reference while we were waiting for the
             queue.  */
          atomic_dec_int64_t(&lru->refcount);
          pthread_mutex_unlock(&lru->mtx);
          pthread_mutex_unlock(&q->mtx);
          return NULL;
     }
     /* Drop the refcount to 0, set the flag to tell other threads to
        stop access immediately. */
     lru->refcount = 0;
     lru->flags = LRU_ENTRY_CONDEMNED;
     glist_del(&lru->q);
     --(q->size);
     lru->lane = LRU_NO_LANE;
     /* Drop all locks and give other threads a chance to abandon the
        entry. */
     pthread_mutex_unlock(&lru->mtx);
     pthread_mutex_unlock(&q->mtx);
     pthread_yield();

     return lru;
}

static const uint32_t S_NSECS = 1000000000UL; /* nsecs in 1s */
static const uint32_t MS_NSECS = 1000000UL; /* nsecs in 1ms */

/**
 * @brief Sleep in the LRU thread for a specified time
 *
 * This function should only be called from the LRU thread.  It sleeps
 * for the specified time or until woken by lru_wake_thread.
 *
 * @param[in] ms The time to sleep, in milliseconds.
 *
 * @retval FALSE if the thread wakes by timeout.
 * @retval TRUE if the thread wakes by signal.
 */

/* Not needed w/ntirpc duplex-9 */
#define timespec_addms(vvp, ms)                                         \
    do {                                                                \
        (vvp)->tv_sec += (ms) / 1000;                                   \
        (vvp)->tv_nsec += (((ms) % 1000) * 1000000);                    \
        if ((vvp)->tv_nsec >= 1000000000) {                             \
            (vvp)->tv_sec++;                                            \
            (vvp)->tv_nsec -= 1000000000;                               \
        }                                                               \
    } while (0)

static bool_t
lru_thread_delay_ms(unsigned long ms)
{
     struct timespec ts;
     bool woke;

     clock_gettime(CLOCK_REALTIME, &ts);
     timespec_addms(&ts, ms);

     pthread_mutex_lock(&lru_mtx);
     lru_thread_state.flags |= LRU_SLEEPING;
     woke = (pthread_cond_timedwait(&lru_cv, &lru_mtx, &ts) != ETIMEDOUT);
     lru_thread_state.flags &= ~LRU_SLEEPING;
     pthread_mutex_unlock(&lru_mtx);
     return (woke);
}

/**
 * @brief Function that executes in the lru thread
 *
 * This function performs long-term reorganization, compaction, and
 * other operations that are not performed in-line with referencing
 * and dereferencing.
 *
 * This function is responsible for cleaning the FD cache.  It works
 * by the following rules:
 *
 *  - If the number of open FDs is below the low water mark, do
 *    nothing.
 *
 *  - If the number of open FDs is between the low and high water
 *    mark, make one pass through the queues, and exit.  Each pass
 *    consists of taking an entry from L1, examining to see if it is a
 *    regular file not bearing state with an open FD, closing the open
 *    FD if it is, and then moving it to L2.  The advantage of the two
 *    level system is twofold: First, seldom used entries congregate
 *    in L2 and the promotion behaviour provides some scan
 *    resistance.  Second, once an entry is examined, it is moved to
 *    L2, so we won't examine the same cache entry repeatedly.
 *
 *  - If the number of open FDs is greater than the high water mark,
 *    we consider ourselves to be in extremis.  In this case we make a
 *    number of passes through the queue not to exceed the number of
 *    passes that would be required to process the number of entries
 *    equal to a biggest_window percent of the system specified
 *    maximum.
 *
 *  - If we are in extremis, and performing the maximum amount of work
 *    allowed has not moved the open FD count required_progress%
 *    toward the high water mark, increment lru_state.futility.  If
 *    lru_state.futility reaches futility_count, temporarily disable
 *    FD caching.
 *
 *  - Every time we wake through timeout, reset futility_count to 0.
 *
 *  - If we fall below the low water mark and FD caching has been
 *    temporarily disabled, re-enable it.
 *
 * This function uses the lock discipline for functions accessing LRU
 * entries through a queue fragment.
 *
 * @param[in] arg A void pointer, currently ignored.
 *
 * @return A void pointer, currently NULL.
 */

static void *
lru_thread(void *arg __attribute__((unused)))
{
     /* Index */
     size_t lane = 0;
     /* Temporary holder for flags */
     uint32_t tmpflags = lru_state.flags;
     /* True if we are taking extreme measures to reclaim FDs. */
     bool_t extremis = FALSE;
     /* True if we were explicitly woke. */
     bool_t woke = FALSE;
     uint32_t fdratepersec=1, fds_avg, fddelta;
     float fdnorm, fdwait_ratio, fdmulti;
     uint64_t threadwait;
     
     SetNameFunction("lru_thread");

     fds_avg = (lru_state.fds_hiwat - lru_state.fds_lowat) / 2;

     while (1) {
          if (lru_thread_state.flags & LRU_SHUTDOWN)
               break;

          extremis = (open_fd_count > lru_state.fds_hiwat);
          LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                       "Reaper awakes.");

          if (!woke) {
               /* If we make it all the way through a timed sleep
                  without being woken, we assume we aren't racing
                  against the impossible. */
               lru_state.futility = 0;
          }

          threadwait = lru_state.threadwait;
          uint64_t t_count = 0;
          uint64_t pinned_t_count = 0;
          /* First, sum the queue counts.  This lets us know where we
             are relative to our watermarks. */

          for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
               pthread_mutex_lock(&LRU_1[lane].lru.mtx);
               t_count += LRU_1[lane].lru.size;
               pthread_mutex_unlock(&LRU_1[lane].lru.mtx);

               pthread_mutex_lock(&LRU_1[lane].lru_pinned.mtx);
               pinned_t_count += LRU_1[lane].lru_pinned.size;
               pthread_mutex_unlock(&LRU_1[lane].lru_pinned.mtx);

               pthread_mutex_lock(&LRU_2[lane].lru.mtx);
               t_count += LRU_2[lane].lru.size;
               pthread_mutex_unlock(&LRU_2[lane].lru.mtx);

               pthread_mutex_lock(&LRU_2[lane].lru_pinned.mtx);
               pinned_t_count += LRU_2[lane].lru_pinned.size;
               pthread_mutex_unlock(&LRU_2[lane].lru_pinned.mtx);
          }
          LogDebug(COMPONENT_CACHE_INODE_LRU,
                   "%zu non-pinned entries. %zu pinned entries.",
                   t_count, pinned_t_count);

          t_count += pinned_t_count;

          LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                       "t_count: %zu   inode_has_size: %zu entries in cache.",
                       t_count, HashTable_GetSize(fh_to_cache_entry_ht));

          if (tmpflags & LRU_STATE_RECLAIMING) {
              if (t_count < lru_state.entries_lowat) {
                  tmpflags &= ~LRU_STATE_RECLAIMING;
                  LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                               "Entry count below low water mark.  "
                               "Disabling reclaim.");
               }
          } else {
              if (t_count > lru_state.entries_hiwat) {
                  tmpflags |= LRU_STATE_RECLAIMING;
                  LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                               "Entry count above high water mark.  "
                               "Enabling reclaim.");
               }
          }

          /* Update global state */
          pthread_mutex_lock(&lru_mtx);
          lru_state.flags = tmpflags;

          pthread_mutex_unlock(&lru_mtx);

          /* Total work done in all passes so far.  If this
             exceeds the window, stop. */
          size_t totalwork = 0;
          uint64_t totalclosed=0;
          /* The current count (after reaping) of open FDs */
          size_t currentopen = 0;

          /* Reap file descriptors.  This is a preliminary example of
             the L2 functionality rather than something we expect to
             be permanent.  (It will have to adapt heavily to the new
             FSAL API, for example.) */
          if (atomic_fetch_size_t(&open_fd_count)
              < lru_state.fds_lowat) {
               LogDebug(COMPONENT_CACHE_INODE_LRU,
                        "FD count is %zd and low water mark is "
                        "%d: not reaping.",
                        open_fd_count,
                        lru_state.fds_lowat);
               if (cache_inode_gc_policy.use_fd_cache &&
                   !lru_state.caching_fds) {
                    lru_state.caching_fds = TRUE;
                    LogEvent(COMPONENT_CACHE_INODE_LRU,
                            "Re-enabling FD cache.");
               }
          } else {
               /* The count of open file descriptors before this run
                  of the reaper. */
               size_t formeropen = open_fd_count;
               /* Work done in the most recent pass of all queues.  if
                  value is less than the work to do in a single queue,
                  don't spin through more passes. */
               size_t workpass = 0;
               time_t curr_time = time(NULL);

               fdratepersec = (curr_time <= lru_state.prev_time) ?
                 1 : (open_fd_count - lru_state.prev_fd_count)/
                 (curr_time - lru_state.prev_time);

               LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                            "fdrate:%u = fdcount:%zd - prev:%"PRIu64" / "
                            "curr:%"PRIu64" prev:%lu", fdratepersec, formeropen,
                            lru_state.prev_fd_count, curr_time,
                            lru_state.prev_time);

               if (extremis) {
                    LogDebug(COMPONENT_CACHE_INODE_LRU,
                                 "Open FDs over high water mark, "
                                 "reapring aggressively.");
               }

               /* Total fds closed between all lanes and all current runs. */
               do {
                    workpass = 0;
                    for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
                         /* The amount of work done on this lane on
                            this pass. */
                         size_t workdone = 0;
                         /* The current entry being examined. */
                         cache_inode_lru_t *lru = NULL;
                         /* Number of entries closed in this run. */
                         size_t closed = 0;

                         LogDebug(COMPONENT_CACHE_INODE_LRU,
                                  "Reaping up to %d entries from lane %zd",
                                  lru_state.per_lane_work, lane);
                         LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                                      "formeropen=%zd workpass=%zd lowat=%d "
                                      "workdone=%zd"
                                      "closed:%zd totalclosed:%"PRIu64,
                                      formeropen, workpass, lru_state.fds_lowat,
                                      workdone, closed, totalclosed);
                         pthread_mutex_lock(&LRU_1[lane].lru.mtx);
                         while ((workdone < lru_state.per_lane_work) &&
                                (lru = glist_first_entry(&LRU_1[lane].lru.q,
                                                         cache_inode_lru_t,
                                                         q))) {

                              cache_inode_status_t cache_status
                                   = CACHE_INODE_SUCCESS;
                              cache_entry_t *entry
                                   = container_of(lru, cache_entry_t, lru);

                              /* We currently hold the lane queue
                                 fragment mutex.  Due to lock
                                 ordering, we are forbidden from
                                 acquiring the LRU mutex directly.
                                 therefore, we increase the reference
                                 count of the entry and drop the
                                 queue fragment mutex. */

                              atomic_inc_int64_t(&lru->refcount);
                              pthread_mutex_unlock(&LRU_1[lane].lru.mtx);

                              /* Acquire the content lock first; we may
                               * need to look at fds and clsoe it.
                               */
                              pthread_rwlock_wrlock(&entry->content_lock);

                              /* Acquire the entry mutex.  If the entry
                                 is condemned, removed, pinned, or in
                                 L2, we have no interest in it. Also
                                 decrement the refcount (since we just
                                 incremented it.) */

                              pthread_mutex_lock(&lru->mtx);
                              atomic_dec_int64_t(&lru->refcount);
                              if ((lru->flags & LRU_ENTRY_CONDEMNED) ||
                                  (lru->flags & LRU_ENTRY_PINNED) ||
                                  (lru->flags & LRU_ENTRY_L2) ||
                                  (lru->flags & LRU_ENTRY_KILLED) ||
                                  (lru->flags & LRU_ENTRY_UNINIT) ||
                                  (lru->lane == LRU_NO_LANE)) {
                                   /* Drop the entry lock, then
                                      reacquire the queue lock so we
                                      can make another trip through
                                      the loop. */
                                   pthread_rwlock_unlock(&entry->content_lock);
                                   pthread_mutex_unlock(&lru->mtx);
                                   pthread_mutex_lock(&LRU_1[lane].lru.mtx);
                                   /* By definition, if any of these
                                      flags are set, the entry isn't
                                      in this queue fragment any more. */
                                   /* Instead of repeating on the same entry of the same lane,
                                      process the next lane */
                                   break;
                              }

                              if (cache_inode_fd(entry)) {
                                   cache_inode_close(
                                        entry,
                                        CACHE_INODE_FLAG_REALLYCLOSE |
                                        CACHE_INODE_FLAG_NOT_PINNED |
                                        CACHE_INODE_FLAG_CONTENT_HAVE |
                                        CACHE_INODE_FLAG_CONTENT_HOLD,
                                        &cache_status);
                                   if (cache_status != CACHE_INODE_SUCCESS) {
                                        LogCrit(COMPONENT_CACHE_INODE_LRU,
                                                "Error closing file in "
                                                "LRU thread.");
                                   } else {
                                     ++totalclosed;
                                     ++closed;
                                   }
                              }
                              pthread_rwlock_unlock(&entry->content_lock);

                              /* Move the entry to L2 whatever the
                                 result of examining it.*/
                              lru_move_entry(lru, LRU_ENTRY_L2,
                                             lru->lane);
                              pthread_mutex_unlock(&lru->mtx);
                              ++workdone;
                              /* Reacquire the lock on the queue
                                 fragment for the next run through
                                 the loop. */
                              pthread_mutex_lock(&LRU_1[lane].lru.mtx);
                         }
                         pthread_mutex_unlock(&LRU_1[lane].lru.mtx);
                         LogDebug(COMPONENT_CACHE_INODE_LRU,
                                  "Actually processed %zd entries on lane %zd "
                                  "closing %zd descriptors",
                                  workdone,
                                  lane,
                                  closed);
                         workpass += workdone;
                    }
                    totalwork += workpass;
               } while (extremis &&
                        (workpass >= lru_state.per_lane_work) &&
                        (totalwork < lru_state.biggest_window));

               currentopen = open_fd_count;
               LogFullDebug(COMPONENT_CACHE_INODE_LRU, "formeropen=%zd"
                            " currentopen=%zd futility=%d totalwork=%zd"
                            " biggest_window=%d extremis=%d lanes=%d"
                            " lowat=%d",
                            formeropen, currentopen, lru_state.futility,
                            totalwork, lru_state.biggest_window, extremis,
                            LRU_N_Q_LANES, lru_state.fds_lowat);
               if (extremis &&
                   ((currentopen > formeropen) ||
                    (formeropen - currentopen <
                     (((formeropen - lru_state.fds_hiwat) *
                       cache_inode_gc_policy.required_progress) /
                      100)))) {
                    if (++lru_state.futility >
                        cache_inode_gc_policy.futility_count) {
                         LogCrit(COMPONENT_CACHE_INODE_LRU,
                                 "Futility count exceeded.  The LRU thread is "
                                 "unable to make progress in reclaiming FDs."
                                 "Disabling FD cache.");
                         lru_state.caching_fds = FALSE;
                    }
               }
          }

          /* The following calculation will progressively garbage collect
           * more frequently as these two factors increase:
           * 1. current number of open file descriptors
           * 2. rate at which file descriptors are being used.
           *
           * When there is little activity, this thread will sleep at the
           * "LRU_Run_Interval" from the config.
           *
           * When there is a lot of activity, the thread will sleep for a
           * much shorter time.
           */
          lru_state.prev_fd_count = currentopen;
          lru_state.prev_time = time(NULL);

          fdnorm = (fdratepersec + fds_avg)/fds_avg;
          fddelta = (currentopen > lru_state.fds_lowat) ?
                   (currentopen - lru_state.fds_lowat) : 0;
          fdmulti = (fddelta*10)/fds_avg;
          fdmulti = fdmulti ? fdmulti : 1;
          fdwait_ratio = lru_state.fds_hiwat/
                         ((lru_state.fds_hiwat+fdmulti*fddelta)*fdnorm);
          threadwait = lru_state.threadwait * fdwait_ratio;

          LogDebug(COMPONENT_CACHE_INODE_LRU,
                  "open_fd_count: %zd  t_count:%"PRIu64" fdrate:%u threadwait=%"PRIu64"\n",
                   open_fd_count, t_count - totalwork, fdratepersec, threadwait);
          woke = lru_thread_delay_ms(threadwait);
     }

     LogEvent(COMPONENT_CACHE_INODE_LRU,
              "Shutting down LRU thread.");

     return NULL;
}

/* Public functions */

/**
 * Initialize subsystem
 */

void
cache_inode_lru_pkginit(void)
{
     /* The attributes governing the LRU reaper thread. */
     pthread_attr_t attr_thr;
     /* Index for initializing lanes */
     size_t ix = 0;
     /* Return code from system calls */
     int code = 0;
     /* Rlimit for open file descriptors */
     struct rlimit rlim = {
          .rlim_cur = RLIM_INFINITY,
          .rlim_max = RLIM_INFINITY
     };

     open_fd_count = 0;

     /* Repurpose some GC policy */
     lru_state.flags = LRU_STATE_NONE;

     /* Set high and low watermark for cache entries.  This seems a
        bit fishy, so come back and revisit this. */
     lru_state.entries_hiwat
          = cache_inode_gc_policy.entries_hwmark;
     lru_state.entries_lowat
          = cache_inode_gc_policy.entries_lwmark;

     /* Find out the system-imposed file descriptor limit */
     if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
          code = errno;
          LogCrit(COMPONENT_CACHE_INODE_LRU,
                  "Call to getrlimit failed with error %d.  "
                  "This should not happen.  Assigning default of %d.",
                  code, FD_FALLBACK_LIMIT);
          lru_state.fds_system_imposed = FD_FALLBACK_LIMIT;
     } else {
          if (rlim.rlim_cur < rlim.rlim_max) {
               /* Save the old soft value so we can fall back to it
                  if setrlimit fails. */
               rlim_t old_soft = rlim.rlim_cur;
               LogInfo(COMPONENT_CACHE_INODE_LRU,
                       "Attempting to increase soft limit from %jd "
                       "to hard limit of %jd",
                       rlim.rlim_cur, rlim.rlim_max);
               rlim.rlim_cur = rlim.rlim_max;
               if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
                    code = errno;
                    LogWarn(COMPONENT_CACHE_INODE_LRU,
                            "Attempt to raise soft FD limit to hard FD limit "
                            "failed with error %d.  Sticking to soft limit.",
                            code);
                    rlim.rlim_cur = old_soft;
               }
          }
          if (rlim.rlim_cur == RLIM_INFINITY) {
               FILE *const nr_open = fopen("/proc/sys/fs/nr_open",
                                           "r");
               if (!(nr_open &&
                     (fscanf(nr_open,
                             "%"SCNu32"\n",
                             &lru_state.fds_system_imposed) == 1) &&
                     (fclose(nr_open) == 0))) {
                    code = errno;
                    LogMajor(COMPONENT_CACHE_INODE_LRU,
                             "The rlimit on open file descriptors is infinite "
                             "and the attempt to find the system maximum "
                             "failed with error %d.  "
                             "Assigning the default fallback of %d which is "
                             "almost certainly too small.  If you are on a "
                             "Linux system, this should never happen.  If "
                             "you are running some other system, please set "
                             "an rlimit on file descriptors (for example, "
                             "with ulimit) for this process and consider "
                             "editing " __FILE__ "to add support for finding "
                             "your system's maximum.", code,
                             FD_FALLBACK_LIMIT);
                    lru_state.fds_system_imposed = FD_FALLBACK_LIMIT;
               }
          } else {
               lru_state.fds_system_imposed = rlim.rlim_cur;
          }
          LogInfo(COMPONENT_CACHE_INODE_LRU,
                  "Setting the system-imposed limit on FDs to %d.",
                  lru_state.fds_system_imposed);
     }

     lru_state.fds_hard_limit = (cache_inode_gc_policy.fd_limit_percent *
                                 lru_state.fds_system_imposed) / 100;
     lru_state.fds_hiwat = (cache_inode_gc_policy.fd_hwmark_percent *
                            lru_state.fds_system_imposed) / 100;
     lru_state.fds_lowat = (cache_inode_gc_policy.fd_lwmark_percent *
                            lru_state.fds_system_imposed) / 100;
     lru_state.futility = 0;
     lru_state.per_lane_work
       = (cache_inode_gc_policy.reaper_work / LRU_N_Q_LANES);
     lru_state.biggest_window = (cache_inode_gc_policy.biggest_window *
                                 lru_state.fds_system_imposed) / 100;

     lru_state.prev_fd_count = 0;

     lru_state.threadwait
          = 1000 * cache_inode_gc_policy.lru_run_interval;

     lru_state.caching_fds = cache_inode_gc_policy.use_fd_cache;

     pthread_mutex_init(&lru_mtx, NULL);
     pthread_cond_init(&lru_cv, NULL);

     for (ix = 0; ix < LRU_N_Q_LANES; ++ix) {
          /* L1, unpinned */
          lru_init_queue(&LRU_1[ix].lru);
          /* L1, pinned */
          lru_init_queue(&LRU_1[ix].lru_pinned);
          /* L2, unpinned */
          lru_init_queue(&LRU_2[ix].lru);
          /* L2, pinned */
          lru_init_queue(&LRU_2[ix].lru_pinned);
     }

     if (pthread_attr_init(&attr_thr) != 0) {
          LogCrit(COMPONENT_CACHE_INODE_LRU,
                  "can't init pthread's attributes");
     }

     if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM)
         != 0) {
          LogCrit(COMPONENT_CACHE_INODE_LRU, "can't set pthread's scope");
     }

     if (pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE)
         != 0) {
          LogCrit(COMPONENT_CACHE_INODE_LRU, "can't set pthread's join state");
     }

     if (pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE)
         != 0) {
          LogCrit(COMPONENT_CACHE_INODE_LRU, "can't set pthread's stack size");
     }

     /* spawn LRU background thread */
     code = pthread_create(&lru_thread_state.thread_id, &attr_thr, lru_thread,
                          NULL);
     if (code != 0) {
          code = errno;
          LogFatal(COMPONENT_CACHE_INODE_LRU,
                   "Unable to start lru reaper thread, error code %d.",
                   code);
     }
}

/**
 * Shutdown subsystem
 */

void
cache_inode_lru_pkgshutdown(void)
{
     /* Post and wait for shutdown of LRU background thread */
     pthread_mutex_lock(&lru_mtx);
     lru_thread_state.flags |= LRU_SHUTDOWN;
     lru_wake_thread(LRU_FLAG_NONE);
     pthread_mutex_unlock(&lru_mtx);
}

/**
 * @brief Re-use or allocate an entry
 *
 * This function repurposes a resident entry in the LRU system if the
 * system is above low-water mark, and allocates a new one otherwise.
 * On success, this function always returns an entry with two
 * references (one for the sentinel, one to allow the caller's use.)
 *
 * @param[in] status  Returned status
 * @param[in] flags   Flags governing call
 *
 * @return CACHE_INODE_SUCCESS or error.
 */

cache_entry_t *
cache_inode_lru_get(cache_inode_status_t *status,
                    uint32_t flags)
{
     int rc;
     /* The lane from which we harvest (or into which we store) the
        new entry.  Usually the lane assigned to this thread. */
     uint32_t lane = 0;
     /* The LRU entry */
     cache_inode_lru_t *lru = NULL;
     /* The Cache entry being created */
     cache_entry_t *entry = NULL;

     /* If we are in reclaim state, try to find an entry to recycle. */
     pthread_mutex_lock(&lru_mtx);
     if (lru_state.flags & LRU_STATE_RECLAIMING) {
          pthread_mutex_unlock(&lru_mtx);

          /* Search through logical L2 entry. */
          for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
               lru = lru_try_reap_entry(&LRU_2[lane].lru);
               if (lru)
                    break;
          }

          /* Search through logical L1 if nothing was found in L2
             (fall through, otherwise.) */
          if (!lru) {
               for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
                    lru = lru_try_reap_entry(&LRU_1[lane].lru);
                    if (lru)
                         break;
               }
          }

          /* If we found an entry, we hold a lock on it and it is
             ready to be recycled. */
          if (lru) {
               entry = container_of(lru, cache_entry_t, lru);
               if (entry) {
                    LogFullDebug(COMPONENT_CACHE_INODE_LRU,
                                 "Recycling entry at %p.",
                                 entry);
               }
               cache_inode_lru_clean(entry);
          }
     } else {
          pthread_mutex_unlock(&lru_mtx);
     }

     if (!lru) {
          entry = pool_alloc(cache_inode_entry_pool, NULL);
          if(entry == NULL) {
               LogCrit(COMPONENT_CACHE_INODE_LRU,
                       "can't allocate a new entry from cache pool");
               *status = CACHE_INODE_MALLOC_ERROR;
               goto out;
          }
          if (pthread_mutex_init(&entry->lru.mtx, NULL) != 0) {
               pool_free(cache_inode_entry_pool, entry);
               LogCrit(COMPONENT_CACHE_INODE_LRU,
                       "pthread_mutex_init of lru.mtx returned %d (%s)",
                       errno,
                       strerror(errno));
               entry = NULL;
               *status = CACHE_INODE_INIT_ENTRY_FAILED;
               goto out;
          }
     }

     assert(entry);
     /* Set the sentinel refcount.  Since the entry isn't in a queue,
        nobody can bump the refcount yet. */
     entry->lru.refcount = 2;
     entry->lru.pin_refcnt = 0;
     entry->lru.flags = LRU_ENTRY_UNINIT;
     /* Initialize the entry locks */
     if (((rc = pthread_rwlock_init(&entry->attr_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&entry->content_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&entry->state_lock, NULL)) != 0)) {
          /* Recycle */
          LogCrit(COMPONENT_CACHE_INODE,
                  "pthread_rwlock_init returned %d (%s)",
                  rc, strerror(rc));
          *status = CACHE_INODE_INIT_ENTRY_FAILED;
          pool_free(cache_inode_entry_pool, entry);
          entry = NULL;
          goto out;
     }

     pthread_mutex_lock(&entry->lru.mtx);
     lru_insert_entry(&entry->lru, 0,
                      lru_lane_of_entry(entry));
     pthread_mutex_unlock(&entry->lru.mtx);

     *status = CACHE_INODE_SUCCESS;

out:
     return (entry);
}

/**
 * @brief Function to let the state layer pin an entry
 *
 * This function moves the given entry to the pinned queue fragment
 * for its lane.  If the entry is already pinned, it is a no-op.
 *
 * @param[in] entry  The entry to be moved
 *
 * @retval CACHE_INODE_SUCCESS if the entry was moved.
 * @retval CACHE_INODE_DEAD_ENTRY if the entry is in the process of
 *                                disposal
 */

cache_inode_status_t
cache_inode_inc_pin_ref(cache_entry_t *entry)
{
     cache_inode_status_t rc = CACHE_INODE_SUCCESS;

     pthread_mutex_lock(&entry->lru.mtx);

     if (entry->lru.flags & LRU_ENTRY_UNPINNABLE) {
          pthread_mutex_unlock(&entry->lru.mtx);
          return CACHE_INODE_DEAD_ENTRY;
     }

     if (!entry->lru.pin_refcnt && !(entry->lru.flags & LRU_ENTRY_PINNED)) {
          lru_move_entry(&entry->lru, LRU_ENTRY_PINNED,
                         entry->lru.lane);
     }
     entry->lru.pin_refcnt++;

     /* Also take an LRU reference */
     atomic_inc_int64_t(&entry->lru.refcount);

     pthread_mutex_unlock(&entry->lru.mtx);

     return rc;
}

/**
 * @brief Make it impossible to pin an entry
 *
 * This function makes it impossible to pin an entry, but does not
 * unpin it.
 *
 * @param[in] entry  The entry to be moved
 *
 */

void
cache_inode_unpinnable(cache_entry_t *entry)
{
     pthread_mutex_lock(&entry->lru.mtx);
     entry->lru.flags |= LRU_ENTRY_UNPINNABLE;
     pthread_mutex_unlock(&entry->lru.mtx);
}

/**
 * @brief Function to let the state layer rlease a pin
 *
 * This function moves the given entry out of the pinned queue
 * fragment for its lane.  If the entry is not pinned, it is a
 * no-op.
 *
 * @param[in] entry  The entry to be moved
 *
 * @retval CACHE_INODE_SUCCESS if the entry was moved.
 */

cache_inode_status_t
cache_inode_dec_pin_ref(cache_entry_t *entry, unsigned char closefile)
{
     cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
     pthread_mutex_lock(&entry->lru.mtx);
     assert(entry->lru.pin_refcnt);
     /* Make sure at least one other LRU reference is held,
      * caller should separately hold an LRU reference
      */
     assert(entry->lru.refcount > 1);
     entry->lru.pin_refcnt--;
     if (!entry->lru.pin_refcnt && (entry->lru.flags & LRU_ENTRY_PINNED)) {
          lru_move_entry(&entry->lru, 0, entry->lru.lane);
          if (closefile == TRUE)
          {
              cache_inode_close(entry,
                       CACHE_INODE_FLAG_REALLYCLOSE|CACHE_INODE_FLAG_NOT_PINNED,
                       &cache_status);
          }
     }

     /* Also release an LRU reference */
     atomic_dec_int64_t(&entry->lru.refcount);

     pthread_mutex_unlock(&entry->lru.mtx);

     return CACHE_INODE_SUCCESS;
}

/**
 * @brief Return true if a file is pinned.
 *
 * This function returns true if a file is pinned.
 *
 * @param[in] entry The file to be checked
 *
 * @return TRUE if pinned, FALSE otherwise.
 */
bool_t cache_inode_is_pinned(cache_entry_t *entry)
{
     bool_t rc;

     pthread_mutex_lock(&entry->lru.mtx);

     rc = entry->lru.pin_refcnt > 0;

     pthread_mutex_unlock(&entry->lru.mtx);

     return rc;
}

/**
 * @brief Get a reference
 *
 * This function acquires a reference on the given cache entry, if the
 * entry is still live.  Terrible things will happen if you call this
 * function and don't check its return value.
 *
 * @param[in] entry  The entry on which to get a reference
 * @param[in] flags  Flags indicating the type of reference sought
 *
 * @retval CACHE_INODE_SUCCESS if the reference was acquired
 * @retval CACHE_INODE_DEAD_ENTRY if the object is being disposed
 */

cache_inode_status_t
cache_inode_lru_ref(cache_entry_t *entry,
                    uint32_t flags)
{
     pthread_mutex_lock(&entry->lru.mtx);

     /* Refuse to grant a reference if we're below the sentinel value
        or the entry is being removed or recycled. */
     if ((entry->lru.refcount == 0) ||
         (entry->lru.flags & LRU_ENTRY_CONDEMNED)) {
          pthread_mutex_unlock(&entry->lru.mtx);
          return CACHE_INODE_DEAD_ENTRY;
     }

     /* These shouldn't ever be set */
     flags &= ~(LRU_ENTRY_PINNED | LRU_ENTRY_L2);

     /* Initial and Scan are mutually exclusive. */

     assert(!((flags & LRU_REQ_INITIAL) &&
              (flags & LRU_REQ_SCAN)));

     atomic_inc_int64_t(&entry->lru.refcount);

     /* Move an entry forward if this is an initial reference. */

     if (flags & LRU_REQ_INITIAL) {
          lru_move_entry(&entry->lru,
                         /* Pinned stays pinned */
                         flags | (entry->lru.flags &
                                  LRU_ENTRY_PINNED),
                         entry->lru.lane);
     } else if ((flags & LRU_REQ_SCAN) &&
                (entry->lru.flags & LRU_ENTRY_L2)) {
          lru_move_entry(&entry->lru,
                         /* Pinned stays pinned, L2 stays in L2. A
                            reference got for SCAN must not be used
                            to open an FD. */
                         flags | (entry->lru.flags &
                                  LRU_ENTRY_PINNED) |
                         LRU_ENTRY_L2,
                         entry->lru.lane);
     }

     pthread_mutex_unlock(&entry->lru.mtx);

     return CACHE_INODE_SUCCESS;
}

/**
 * @brief Destroy the sentinel refcount safely
 *
 * This function decrements the refcount by one unless the
 * LRU_FLAG_KILLED bit is set in the flags word.  This is intended to
 * allow a function that needs to remove an extra refcount (the
 * sentinel) to be called multiple times without causing an
 * underflow.
 *
 * @param[in]     entry  The entry to decrement.
 */

void cache_inode_lru_kill(cache_entry_t *entry)
{
     pthread_mutex_lock(&entry->lru.mtx);
     if (entry->lru.flags & LRU_ENTRY_KILLED) {
          pthread_mutex_unlock(&entry->lru.mtx);
     } else {
          entry->lru.flags |= LRU_ENTRY_KILLED;
          /* cache_inode_lru_unref always either unlocks or destroys
             the entry. */
          cache_inode_lru_unref(entry, LRU_FLAG_LOCKED);
     }
}

/**
 * @brief Relinquish a reference
 *
 * This function relinquishes a reference on the given cache entry.
 * It follows the disposal/recycling lock discipline given at the
 * beginning of the file.
 *
 * The supplied entry is always either unlocked or destroyed by the
 * time this function returns.
 *
 * @param[in] entry  The entry on which to release a reference
 * @param[in] flags  Currently significant are and LRU_FLAG_LOCKED
 *                   (indicating that the caller holds the LRU mutex
 *                   lock for this entry.)
 */

void
cache_inode_lru_unref(cache_entry_t *entry,
                      uint32_t flags)
{
     if (!(flags & LRU_FLAG_LOCKED)) {
          pthread_mutex_lock(&entry->lru.mtx);
     }

     assert(entry->lru.refcount >= 1);

     if (entry->lru.refcount == 1) {
          struct lru_q_base *q
               = lru_select_queue(entry->lru.flags,
                                  entry->lru.lane);
          pthread_mutex_lock(&q->mtx);
          atomic_dec_int64_t(&entry->lru.refcount);
          if (entry->lru.refcount == 0) {
               /* Refcount has fallen to zero.  Remove the entry from
                  the queue and mark it as dead. */
               entry->lru.flags = LRU_ENTRY_CONDEMNED;
               glist_del(&entry->lru.q);
               --(q->size);
               entry->lru.lane = LRU_NO_LANE;
               /* Give other threads a chance to see that */
               pthread_mutex_unlock(&entry->lru.mtx);
               pthread_mutex_unlock(&q->mtx);
               pthread_yield();
               /* We should not need to hold the LRU mutex at this
                  point.  The hash table locks will ensure that by
                  the time this function completes successfully,
                  other threads will either have received
                  CACHE_INDOE_DEAD_ENTRY in the attempt to gain a
                  reference, or we will have removed the hash table
                  entry. */
               cache_inode_lru_clean(entry);

               pthread_mutex_destroy(&entry->lru.mtx);
               pool_free(cache_inode_entry_pool, entry);
               return;
          } else {
               pthread_mutex_unlock(&q->mtx);
          }
     } else {
          /* We may decrement the reference count without the queue
             lock, since it cannot go to 0. */
          atomic_dec_int64_t(&entry->lru.refcount);
     }

     pthread_mutex_unlock(&entry->lru.mtx);
}

/**
 *
 * @brief Wake the LRU thread to free FDs.
 *
 * This function wakes the LRU reaper thread to free FDs and should be
 * called when we are over the high water mark.
 *
 * @param[in] flags Flags to affect the wake (currently none)
 */

void lru_wake_thread(uint32_t flags)
{
     if (lru_thread_state.flags & LRU_SLEEPING)
          pthread_cond_signal(&lru_cv);
}
