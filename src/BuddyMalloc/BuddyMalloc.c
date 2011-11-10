/*
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
 */

/**
 * \file    BuddyMalloc.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/16 12:59:38 $
 * \version $Revision: 1.24 $
 * \brief   Module for Buddy block allocator.
 *
 * BuddyMalloc.c: Module for Buddy block allocator.
 *
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "BuddyMalloc.h"
#include "stuff_alloc.h"
#include <pthread.h>

#include "log_macros.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/* to detect memory corruption */
#define MAGIC_NUMBER_FREE   0xF4EEB10C
#define MAGIC_NUMBER_USED   0x1D0BE1AE

#define P( _mutex_ ) pthread_mutex_lock( &_mutex_ )
#define V( _mutex_ ) pthread_mutex_unlock( &_mutex_ )

/* type to hold address differences in buddy */
#define BUDDY_PTRDIFF_T  ptrdiff_t

/** Default configuration for Buddy. */

buddy_parameter_t default_buddy_parameter = {
  .memory_area_size = 1048576LL, /* Standard pages size: 1MB = 2^20 */
  .on_demand_alloc  = TRUE,      /* On demand allocation */
  .extra_alloc      = TRUE,      /* Extra allocation */
  .free_areas       = TRUE,      /* Free unused areas */
  .keep_factor      = 3,         /* keep at least 3x the number of used pages */
  .keep_minimum     = 5,         /* Never decrease under 5 allocated pages
                                  * if this value is overcome. */
};

/* ------------------------------------------*
 * Internal datatypes for memory management.
 * ------------------------------------------*/

/** Buddy block status */
typedef enum BuddyBlockStatus_t
{
  FREE_BLOCK,
  RESERVED_BLOCK
} BuddyBlockStatus_t;

typedef struct StdBlockInfo_t
{

  /* k size of "mother" area. */
  unsigned int Base_kSize;

  /* This indicates the size (2^k_size) of this block. */
  unsigned int k_size;

#ifdef _DEBUG_MEMLEAKS
  /* how much the user asked ? (...and how much is wasted in this block) */
  size_t user_size;
#endif

} StdBlockInfo_t;

/** Pointer to a buddy block descriptor. */
typedef struct BuddyBlock_t *BuddyBlockPtr_t;

/** Buddy header */
typedef struct BuddyHeader_t
{

  /* Pointer to the base address of "mother" area.
   * >> NULL when it is an extra block (largest
   * than standard memory pages ).
   */
  BUDDY_ADDR_T Base_ptr;

  /* for sanity checks */
  unsigned int MagicNumber;
  pthread_t OwnerThread;

#ifndef _MONOTHREAD_MEMALLOC
  /* Used when blocks are allocated by a thread
   * and freed by another. */
  struct BuddyThreadContext_t *OwnerThreadContext;
#endif

#ifdef _DEBUG_MEMLEAKS

  /* label of this block (for debugging) */
  const char *label_user_defined;
  const char *label_file;
  const char *label_func;
  unsigned int label_line;

  /* pointer to the next allocated block */
  BuddyBlockPtr_t p_next_allocated;

#ifndef _NO_BLOCK_PREALLOC
  struct prealloc_header *pa_entry;
#endif

#endif

  union
  {
    StdBlockInfo_t StdInfo;
    size_t ExtraInfo;
  } BlockInfo;

  /* Indicate the status for this block. */
  BuddyBlockStatus_t status;

} BuddyHeader_t;

/* aliases */
#define StdInfo BlockInfo.StdInfo
#define ExtraInfo BlockInfo.ExtraInfo

/** Content of a free buddy block (without header)  */
typedef struct BuddyFreeBlockInfo_t
{

  BuddyBlockPtr_t NextBlock;
  BuddyBlockPtr_t PrevBlock;

} BuddyFreeBlockInfo_t;

/**
 * Buddy block definition.
 * This definition is actually mapped
 * over a memory area of a bigger size.
 */
typedef struct BuddyBlock_t
{

  BuddyHeader_t Header;
  union
  {
    BuddyFreeBlockInfo_t FreeBlockInfo;
    char UserSpace[1];
#ifndef _MONOTHREAD_MEMALLOC
    /* pointer to the next entry to be freed */
    struct BuddyBlock_t *NextToBeFreed;
#endif
  } Content;

} BuddyBlock_t;

#define BUDDY_MAX_LOG2_SIZE  64
/* allowed buddyMalloc sizes are from 2^0 to 2^63 */

/** Thread context */
typedef struct BuddyThreadContext_t
{
  /* Indicates if buddy has been initialized. */
  int initialized;

  /* Thread this context belongs to */
  pthread_t OwnerThread;

  /* Current thread configuration */
  buddy_parameter_t Config;

  /* Current thread statistics */
  buddy_stats_t Stats;

  /* Standard size for memory areas (2^k_size). */
  unsigned int k_size;

  /* Memory Map for this thread */
  BuddyBlockPtr_t MemDesc[BUDDY_MAX_LOG2_SIZE];

  /* Error code for this thread */
  int Errno;

#ifndef _MONOTHREAD_MEMALLOC
  struct BuddyThreadContext_t *prev, *next;
  pthread_mutex_t       ToBeFreed_mutex;
  BuddyBlock_t         *ToBeFreed_list;
  int                   destroy_pending; /* protected by the same mutex */
#endif

  char label_thread[STR_LEN];

#ifdef _DEBUG_MEMLEAKS

  /* block label (for debugging) */
  const char *label_user_defined;
  const char *label_file;
  const char *label_func;
  unsigned int label_line;

  /* list of allocated blocks */
  BuddyBlockPtr_t p_allocated;

#endif

} BuddyThreadContext_t;

#ifndef _MONOTHREAD_MEMALLOC
pthread_mutex_t ContextListMutex = PTHREAD_MUTEX_INITIALIZER;
BuddyThreadContext_t *first_context = NULL;
BuddyThreadContext_t *last_context  = NULL;
struct prealloc_pool *first_pool = NULL;

void insert_context(BuddyThreadContext_t *context)
{
  P(ContextListMutex);

  if (last_context == NULL)
    {
      first_context = context;
      last_context  = context;
      context->prev = NULL;
      context->next = NULL;
    }
  else
    {
      context->prev = last_context;
      context->next = NULL;
      last_context->next = context;
      last_context = context;
    }

  V(ContextListMutex);
}

void remove_context(BuddyThreadContext_t *context)
{
  P(ContextListMutex);

  if (context->prev == NULL)
    first_context = context->next;
  else
    context->prev->next = context->next;

  if (context->next == NULL)
    last_context = context->prev;
  else
    context->next->prev = context->prev;

  context->prev = NULL;
  context->next = NULL;

  V(ContextListMutex);
}
#endif

void ShowAllContext()
{
#ifndef _MONOTHREAD_MEMALLOC
  BuddyThreadContext_t *context;
  size_t total = 0, used = 0;
  int count = 0;

  P(ContextListMutex);

  for (context = first_context; context != NULL; context = context->next)
    {
      total += context->Stats.TotalMemSpace;
      used += context->Stats.StdUsedSpace + context->Stats.ExtraMemSpace;
      count++;
      LogDebug(COMPONENT_MEMALLOC,
               "Context for thread %s (%p) Total Mem Space: %lld MB Used: %lld MB",
               context->label_thread,
               (caddr_t)context->OwnerThread,
               (unsigned long long) context->Stats.TotalMemSpace / 1024 / 1024,
               (unsigned long long) (context->Stats.StdUsedSpace + context->Stats.ExtraMemSpace) / 1024 / 1024);
    }

  LogDebug(COMPONENT_MEMALLOC,
           "%d threads, Total Mem Space: %lld MB, Total Used: %lld MB",
           count, (unsigned long long) total / 1024 / 1024, (unsigned long long) used / 1024 / 1024);
  V(ContextListMutex);
#endif
  return;
}

/* ------------------------------------------*
 *        Thread safety management.
 * ------------------------------------------*/

/* threads keys */
static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init pthtread_key for current thread */

static void init_keys(void)
{
  if(pthread_key_create(&thread_key, NULL) == -1)
    LogMajor(COMPONENT_MEMALLOC,
             "Error %d creating pthread key for thread %p : %s",
             errno, (BUDDY_ADDR_T) pthread_self(), strerror(errno));

  return;
}                               /* init_keys */

/**
 * GetThreadContext :
 * manages pthread_keys.
 */
static BuddyThreadContext_t *GetThreadContext()
{
  BuddyThreadContext_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      LogMajor(COMPONENT_MEMALLOC,
               "Error %d calling pthread_once for thread %p : %s",
               errno, (BUDDY_ADDR_T) pthread_self(), strerror(errno));
      return NULL;
    }

  p_current_thread_vars = (BuddyThreadContext_t *) pthread_getspecific(thread_key);

  /* we allocate the thread context if this is the first time */
  if(p_current_thread_vars == NULL)
    {
      /* allocates thread structure */
      p_current_thread_vars =
          (BuddyThreadContext_t *) malloc(sizeof(BuddyThreadContext_t));

      /* panic !!! */
      if(p_current_thread_vars == NULL)
        {
          LogMajor(COMPONENT_MEMALLOC,
                   "%p:BuddyMalloc: Not enough memory",
                   (BUDDY_ADDR_T) pthread_self());
          return NULL;
        }

      LogDebug(COMPONENT_MEMALLOC,
               "Allocating pthread key %p for thread %p",
               p_current_thread_vars, (caddr_t)pthread_self());

      /* Clean thread context */

      memset(p_current_thread_vars, 0, sizeof(BuddyThreadContext_t));

      p_current_thread_vars->initialized = FALSE;
      p_current_thread_vars->Errno = 0;

#ifdef _DEBUG_MEMLEAKS
      p_current_thread_vars->label_user_defined = "N/A";
      p_current_thread_vars->label_file = "N/A";
      p_current_thread_vars->label_func = "N/A";
      p_current_thread_vars->label_line = 0;
      p_current_thread_vars->p_allocated = NULL;
      GetNameFunction(p_current_thread_vars->label_thread, STR_LEN);
#endif

#ifndef _MONOTHREAD_MEMALLOC
      insert_context(p_current_thread_vars);
#endif
      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);
    }

  return p_current_thread_vars;

}                               /* GetThreadContext */

/** Return pointer to errno for the current thread. */
int *p_BuddyErrno()
{

  static int ErrMalloc = BUDDY_ERR_MALLOC;

  BuddyThreadContext_t *context;

  context = GetThreadContext();

  /* If there is no context, it means that malloc failed
   * However, we can't store it in the thread context.
   * so we return a pointer to this error code.
   */
  if(!context)
    return &ErrMalloc;
  else
    return &(context->Errno);
}

/* ------------------------------------------*
 *              useful values.
 * ------------------------------------------*/

/* computes a size of header block that makes
 * the user space aligned to 64bits = 8bytes.
 * to do so we round at the next multiple of 8.
 */
#define size_header64  ( (sizeof(BuddyHeader_t) + 7) & ~7 )

/* the minimum size for userspace */
#define MIN_ALLOC_SIZE  ( sizeof(BuddyFreeBlockInfo_t) )

/* ------------------------------------------*
 *            Internal routines.
 * ------------------------------------------*/

/* Log2Ceil :
 * returns the first power of 2
 * that is greater or equal to i_size.
 *
 * \param i_size (size_t) The size to be compared.
 *
 * \return k<64, such that 2^k >= i_size >2^k-1
 *         If k>=64, returns 0.
 */
static unsigned int Log2Ceil(size_t i_size)
{

  unsigned int k = 0;
  size_t local_size = 1;

  while(local_size < i_size)
    {

      local_size <<= 1;
      k++;

      /* the size can't exceed 2^63 */
      if(k >= BUDDY_MAX_LOG2_SIZE)
        return 0;
    }

  return k;

}

/* Used for memleaks detection */

#ifdef _DEBUG_MEMLEAKS

static void add_allocated_block(BuddyThreadContext_t * context, BuddyBlock_t * p_block)
{
  /* insert block as first entry */
  p_block->Header.p_next_allocated = context->p_allocated;
  context->p_allocated = p_block;
}

static void remove_allocated_block(BuddyThreadContext_t * context, BuddyBlock_t * p_block)
{
  BuddyBlockPtr_t p_prev_block;
  BuddyBlockPtr_t p_curr_block;

  p_prev_block = NULL;

  /* browsing allocated blocks list */
  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
    {
      /* the block to be removed */
      if(p_curr_block == p_block)
        {
          /* if the block is the first of the list */
          if(p_prev_block == NULL)
            {
              context->p_allocated = p_curr_block->Header.p_next_allocated;
            }
          else
            {
              p_prev_block->Header.p_next_allocated
                  = p_curr_block->Header.p_next_allocated;
            }
          /* useless, but safer */
          p_curr_block->Header.p_next_allocated = NULL;
          break;
        }
      p_prev_block = p_curr_block;
    }
}

/* find the block that is just before another */
static BuddyBlock_t *find_previous_allocated(BuddyThreadContext_t * context,
                                             BuddyBlock_t * p_block)
{
  BuddyBlockPtr_t p_curr_block;
  BuddyBlockPtr_t p_max_block = NULL;   /* the max block that is smaller that p_block */

  /* browsing allocated blocks list */
  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
    {
      if((p_curr_block > p_max_block) && (p_curr_block < p_block))
        p_max_block = p_curr_block;
    }

  return p_max_block;
}

#endif

#ifdef _DEBUG_MEMLEAKS
void log_bad_block(const char *label, BuddyThreadContext_t *context, BuddyBlock_t *block, int do_label, int do_guilt)
{
  LogDebug(COMPONENT_MEMALLOC,
           "%s block %p invoked by %s:%u:%s:%s",
           label, block,
           context->label_file,
           context->label_line,
           context->label_func,
           context->label_user_defined);

  if(do_label)
    LogDebug(COMPONENT_MEMALLOC,
             "%s block %p had label: %s:%u:%s:%s",
             label, block,
             block->Header.label_file,
             block->Header.label_line,
             block->Header.label_func,
             block->Header.label_user_defined);

  if(do_guilt && isFullDebug(COMPONENT_MEMALLOC))
  {
    BuddyBlock_t *guilt_block;

    if((guilt_block = find_previous_allocated(context, block)) != NULL)
      LogFullDebug(COMPONENT_MEMALLOC,
                   "%s block %p, guilt block is %p->%p, label: %s:%u:%s:%s",
                   label, block,
                   guilt_block,
                   guilt_block + (1 << block->Header.StdInfo.k_size) - 1,
                   guilt_block->Header.label_file,
                   guilt_block->Header.label_line,
                   guilt_block->Header.label_func,
                   guilt_block->Header.label_user_defined);
    else
      LogFullDebug(COMPONENT_MEMALLOC,
                   "%s block %p, previous Block none???",
                   label, block);
  }
}
#else
#define log_bad_block(label, context, block, do_label, do_guilt)
#endif


/*
 * check current magic number
 */
int isBadMagicNumber(const char *tag, BuddyThreadContext_t *context, BuddyBlock_t *block, unsigned int MagicNumber, int do_guilt, const char *label)
{
  if(block->Header.MagicNumber != MagicNumber)
    {
      const char *_label;
      if(label != NULL)
        _label = label;
      else
        {
#ifdef _DEBUG_MEMLEAKS
          _label = context->label_user_defined;
#else
          _label = "";
#endif
        }
      LogMajor(COMPONENT_MEMALLOC,
               "%s %s block %p has been overwritten or is not a buddy block (Magic number %08x<>%08x)",
               tag, _label, block, block->Header.MagicNumber, MagicNumber);
      log_bad_block(tag, context, block, do_guilt, do_guilt);
      return 1;
    }
  else
    return 0;
}

static void Insert_FreeBlock(BuddyThreadContext_t * context, BuddyBlock_t * p_buddyblock)
{

  BuddyBlock_t *next;

  /* check current magic number */
  isBadMagicNumber("Insert_FreeBlock:", context, p_buddyblock, MAGIC_NUMBER_FREE, 0, NULL);

  /* Is there already a free block in the list ? */
  if((next = context->MemDesc[p_buddyblock->Header.StdInfo.k_size]) != NULL)
    {

      /* check current magic number */
      isBadMagicNumber("Insert_FreeBlock: next", context, next, MAGIC_NUMBER_FREE, 0, NULL);

      context->MemDesc[p_buddyblock->Header.StdInfo.k_size] = p_buddyblock;
      p_buddyblock->Content.FreeBlockInfo.NextBlock = next;
      p_buddyblock->Content.FreeBlockInfo.PrevBlock = NULL;
      next->Content.FreeBlockInfo.PrevBlock = p_buddyblock;
    }
  else
    {
      context->MemDesc[p_buddyblock->Header.StdInfo.k_size] = p_buddyblock;
      p_buddyblock->Content.FreeBlockInfo.NextBlock = NULL;
      p_buddyblock->Content.FreeBlockInfo.PrevBlock = NULL;
    }

  LogFullDebug(COMPONENT_MEMALLOC,
               "%p: @%p inserted to tab[%u] (prev=%p, next =%p)",
               (BUDDY_ADDR_T) pthread_self(),
               p_buddyblock, p_buddyblock->Header.StdInfo.k_size,
               p_buddyblock->Content.FreeBlockInfo.PrevBlock,
               p_buddyblock->Content.FreeBlockInfo.NextBlock);

  return;
}

static void Remove_FreeBlock(BuddyThreadContext_t * context, BuddyBlock_t * p_buddyblock)
{

  BuddyBlock_t *prev;
  BuddyBlock_t *next;

  /* check current magic number */
  isBadMagicNumber("Remove_FreeBlock:", context, p_buddyblock, MAGIC_NUMBER_FREE, 0, NULL);

  prev = p_buddyblock->Content.FreeBlockInfo.PrevBlock;
  next = p_buddyblock->Content.FreeBlockInfo.NextBlock;

  if(prev)
    {
      /* check current magic number */
      isBadMagicNumber("Remove_FreeBlock: prev", context, prev, MAGIC_NUMBER_FREE, 0, NULL);

      prev->Content.FreeBlockInfo.NextBlock = next;
    }
  else
    {
      context->MemDesc[p_buddyblock->Header.StdInfo.k_size] = next;
    }

  if(next)
    {
      /* check current magic number */
      isBadMagicNumber("Remove_FreeBlock: next", context, next, MAGIC_NUMBER_FREE, 0, NULL);

      next->Content.FreeBlockInfo.PrevBlock = prev;
    }

  /* We mark the bloc having no previous or next blocks */
  p_buddyblock->Content.FreeBlockInfo.PrevBlock = NULL;
  p_buddyblock->Content.FreeBlockInfo.NextBlock = NULL;

  LogFullDebug(COMPONENT_MEMALLOC,
               "%p: @%p removed from tab[%u] (prev=%p, next =%p)",
               (BUDDY_ADDR_T) pthread_self(),
               p_buddyblock, p_buddyblock->Header.StdInfo.k_size, prev, next);

  return;

}                               /* Remove_FreeBlock */

/**
 * Get_BuddyBlock :
 * Calculates buddy address.
 */
static BuddyBlock_t *Get_BuddyBlock(BuddyThreadContext_t * context,
                                    BuddyBlock_t * p_buddyblock)
{

  BUDDY_PTRDIFF_T Offset_block;
  BUDDY_PTRDIFF_T Offset_buddy;

  BUDDY_ADDR_T BaseAddr = p_buddyblock->Header.Base_ptr;
  unsigned int k = p_buddyblock->Header.StdInfo.k_size;

  Offset_block = (BUDDY_ADDR_T) p_buddyblock - BaseAddr;

  Offset_buddy = Offset_block ^ (1 << k);

  LogFullDebug(COMPONENT_MEMALLOC,
               "buddy(%08tx,%u,%08x)=%08tx",
               Offset_block, k, 1 << k, Offset_buddy);

  return (BuddyBlock_t *) (Offset_buddy + BaseAddr);

}                               /* Get_BuddyBlock */

/**
 * UpdateStats_InsertStdPage:
 * update statistics to remember that there is a new standard page.
 */
static void UpdateStats_InsertStdPage(BuddyThreadContext_t * context)
{

  if(!context)
    return;

  /* total space allocated */

  context->Stats.TotalMemSpace += context->Stats.StdPageSize;

  /* total space allocated watermark */

  if(context->Stats.TotalMemSpace > context->Stats.WM_TotalMemSpace)
    context->Stats.WM_TotalMemSpace = context->Stats.TotalMemSpace;

  /* The same thing only for standard pages. */

  context->Stats.StdMemSpace += context->Stats.StdPageSize;

  if(context->Stats.StdMemSpace > context->Stats.WM_StdMemSpace)
    context->Stats.WM_StdMemSpace = context->Stats.StdMemSpace;

  /* Amount of client alllocated space stays the same */

  /* Number of std pages */
  context->Stats.NbStdPages++;

  return;

}

/**
 * UpdateStats_RemoveStdPage:
 * update statistics to remember that there is a new standard page.
 */
static void UpdateStats_RemoveStdPage(BuddyThreadContext_t * context)
{

  if(!context)
    return;

  /* total space allocated */

  context->Stats.TotalMemSpace -= context->Stats.StdPageSize;

  /* The same thing only for standard pages. */

  context->Stats.StdMemSpace -= context->Stats.StdPageSize;

  /* Number of std pages */
  context->Stats.NbStdPages--;

  return;

}

/**
 * UpdateStats_UseStdPage:
 * update statistics to remember that a standard page becomes used.
 */
static void UpdateStats_UseStdPage(BuddyThreadContext_t * context)
{

  if(!context)
    return;

  context->Stats.NbStdUsed++;

  if(context->Stats.NbStdUsed > context->Stats.WM_NbStdUsed)
    context->Stats.WM_NbStdUsed = context->Stats.NbStdUsed;

  return;

}

/**
 * UpdateStats_FreeStdPage:
 * update statistics to remember that a standard page becomes used.
 */
static void UpdateStats_FreeStdPage(BuddyThreadContext_t * context)
{

  if(!context)
    return;

  context->Stats.NbStdUsed--;

  return;

}

/**
 * UpdateStats_UseStdMemSpace:
 * update statistics to remember that a an amount of memory has been allocated.
 * to a client
 */
static void UpdateStats_UseStdMemSpace(BuddyThreadContext_t * context, size_t amount)
{

  if(!context)
    return;

  context->Stats.StdUsedSpace += amount;

  if(context->Stats.StdUsedSpace > context->Stats.WM_StdUsedSpace)
    context->Stats.WM_StdUsedSpace = context->Stats.StdUsedSpace;

  return;

}

/**
 * UpdateStats_FreeStdMemSpace:
 * update statistics to remember that a an amount of memory has been freed.
 */
static void UpdateStats_FreeStdMemSpace(BuddyThreadContext_t * context, size_t amount)
{

  if(!context)
    return;

  context->Stats.StdUsedSpace -= amount;

  return;

}

/**
 * UpdateStats_AddExtraPage:
 * update statistics to remember that there is a new extra page.
 */
static void UpdateStats_AddExtraPage(BuddyThreadContext_t * context, size_t alloc_size)
{

  if(!context)
    return;

  context->Stats.TotalMemSpace += alloc_size;

  /* total space allocated watermark */

  if(context->Stats.TotalMemSpace > context->Stats.WM_TotalMemSpace)
    context->Stats.WM_TotalMemSpace = context->Stats.TotalMemSpace;

  /* Extra space allocated */

  context->Stats.ExtraMemSpace += alloc_size;

  if(context->Stats.ExtraMemSpace > context->Stats.WM_ExtraMemSpace)
    context->Stats.WM_ExtraMemSpace = context->Stats.ExtraMemSpace;

  /* page sizes */

  if((context->Stats.MinExtraPageSize > alloc_size)
     || (context->Stats.MinExtraPageSize == 0))
    context->Stats.MinExtraPageSize = alloc_size;

  if(context->Stats.MaxExtraPageSize < alloc_size)
    context->Stats.MaxExtraPageSize = alloc_size;

  context->Stats.NbExtraPages++;

  if(context->Stats.NbExtraPages > context->Stats.WM_NbExtraPages)
    context->Stats.WM_NbExtraPages = context->Stats.NbExtraPages;

  return;

}

/**
 * UpdateStats_RemoveExtraPage:
 * update statistics to remember that we remove an page.
 */
static void UpdateStats_RemoveExtraPage(BuddyThreadContext_t * context, size_t alloc_size)
{

  if(!context)
    return;

  context->Stats.TotalMemSpace -= alloc_size;

  /* Extra space allocated */

  context->Stats.ExtraMemSpace -= alloc_size;

  context->Stats.NbExtraPages--;

  return;

}

/**
 *  NewStdPage :
 *  Adds a new page (with standard size) to the pool.
 */
BuddyBlock_t *NewStdPage(BuddyThreadContext_t * context)
{

  BuddyBlock_t *p_block;
  unsigned int k_size;
  size_t allocation;

  if(!context)
    return NULL;

  k_size = context->k_size;
  allocation = 1 << k_size;

  p_block = (BuddyBlock_t *) malloc(allocation);

  LogDebug(COMPONENT_MEMALLOC,
           "Memory area allocation for thread %p : ptr=%p ; size=%llu=2^%u",
           (caddr_t)pthread_self(), p_block, (unsigned long long)allocation, k_size);

  if(!p_block)
    return NULL;

  /* the block is the parent block itself */

  p_block->Header.Base_ptr = (BUDDY_ADDR_T) p_block;
  p_block->Header.StdInfo.Base_kSize = k_size;
  p_block->Header.status = FREE_BLOCK;
  p_block->Header.StdInfo.k_size = k_size;

  p_block->Header.MagicNumber = MAGIC_NUMBER_FREE;

  /* Now inserting the first free block. */
  Insert_FreeBlock(context, p_block);

  /* update stats */
  UpdateStats_InsertStdPage(context);

  return p_block;

}

/**
 * Garbage_StdPages:
 * Garbage free block, using the policy specified
 * in the configuration structure.
 * \param context: Thread context
 * \param p_last_free_block: The last freed block.
 */
static void Garbage_StdPages(BuddyThreadContext_t * context,
                             BuddyBlock_t * p_last_free_block)
{

  /* sanity checks */
  if(!context || !p_last_free_block)
    return;
  if(!context->Config.free_areas)
    return;

  /* We must keep at least 'keep_minimum' standard pages */

  if(context->Stats.NbStdPages <= context->Config.keep_minimum)
    return;

  /* We must keep at least Nb_used x keep_factor */

  if(context->Stats.NbStdPages <=
     (context->Config.keep_factor * context->Stats.NbStdUsed))
    return;

  /* We can free this page */

  Remove_FreeBlock(context, p_last_free_block);

  free(p_last_free_block);

  UpdateStats_RemoveStdPage(context);

  LogDebug(COMPONENT_MEMALLOC,
           "%p: A standard page has been Garbaged",
           (caddr_t)pthread_self());

  return;

}

/**
 * AllocLargeBlock:
 * Allocates blocks that are larger than the standard page size.
 */
BUDDY_ADDR_T AllocLargeBlock(BuddyThreadContext_t * context, size_t Size)
{

  BuddyBlock_t *p_block;
  size_t total_size = Size + size_header64;

  /* sanity checks */
  if(!context)
    return NULL;
  if(!context->Config.extra_alloc)
    {
      context->Errno = BUDDY_ERR_EINVAL;
      return NULL;
    }

  p_block = (BuddyBlock_t *) malloc(total_size);

  LogDebug(COMPONENT_MEMALLOC,
           "Memory EXTRA area allocation for thread %p : ptr=%p ; size=%llu",
           (caddr_t)pthread_self(), p_block, (unsigned long long)total_size);

  if(!p_block)
    {
      context->Errno = BUDDY_ERR_MALLOC;
      return NULL;
    }

  /* We differentiate a extra memory block
   * by setting a base pointer to NULL.
   */

  p_block->Header.Base_ptr = NULL;
  p_block->Header.ExtraInfo = total_size;

  p_block->Header.status = RESERVED_BLOCK;
  p_block->Header.MagicNumber = MAGIC_NUMBER_USED;
  p_block->Header.OwnerThread = pthread_self();
#ifndef _MONOTHREAD_MEMALLOC
  p_block->Header.OwnerThreadContext = context;
#endif

#ifdef _DEBUG_MEMLEAKS
  /* label for debugging */

  p_block->Header.label_user_defined = context->label_user_defined;
  p_block->Header.label_file = context->label_file;
  p_block->Header.label_func = context->label_func;
  p_block->Header.label_line = context->label_line;
#ifndef _NO_BLOCK_PREALLOC
  p_block->Header.pa_entry = NULL;
#endif

  /* add it to the list of allocated blocks */
  add_allocated_block(context, p_block);
#endif

  /* Update statistics about extra blocks */

  UpdateStats_AddExtraPage(context, total_size);

  /* return pointer to the new allocated zone. */

  /* returns the userspace aligned on 64 bits */
  return (BUDDY_ADDR_T) ((BUDDY_PTRDIFF_T) p_block + (BUDDY_PTRDIFF_T) size_header64);

}

/* Macro used to determine if it is a extra block or not (for BuddyFree) */
#define IS_EXTRA_BLOCK( _p_block_ ) ( (_p_block_)->Header.Base_ptr == NULL )

/**
 * FreeLargeBlock:
 * free blocks that are larger than the standard page size.
 */
void FreeLargeBlock(BuddyThreadContext_t * context, BuddyBlock_t * p_block)
{
  size_t page_size;

  /* sanity checks */
  if(!context || !p_block)
    return;
  if(!IS_EXTRA_BLOCK(p_block))
    return;

  page_size = p_block->Header.ExtraInfo;

  free(p_block);

  UpdateStats_RemoveExtraPage(context, page_size);

  LogDebug(COMPONENT_MEMALLOC,
           "%p: An extra page has been freed (size %zu)",
           (caddr_t)pthread_self(), page_size);

  return;

}

#ifndef _MONOTHREAD_MEMALLOC

static void __BuddyFree(BuddyThreadContext_t * context, BuddyBlock_t * p_block);

/** Free owned blocks that have been freed by another thread */
static void CheckBlocksToBeFreed(BuddyThreadContext_t * context, int do_lock)
{
  BuddyBlock_t *p_block_to_free;

  /* take a block in the list a long as there is one */

  do
    {

      if (do_lock)
        P(context->ToBeFreed_mutex);

      p_block_to_free = context->ToBeFreed_list;
      if(p_block_to_free)
        context->ToBeFreed_list = p_block_to_free->Content.NextToBeFreed;

      if (do_lock)
          V(context->ToBeFreed_mutex);

      if(p_block_to_free == NULL)
        break;

      LogFullDebug(COMPONENT_MEMALLOC,
                   "blocks %p has been released by foreign thread",
                   p_block_to_free);

      __BuddyFree(context, p_block_to_free);
    }
  while(1);

}

#endif

/** Try to cleanup a context in 'destroy_pending' state.
 * /!\ must be called under the protection of ToBeFreed_mutex
 * in the case of multithread alloc.
 */
static int TryContextCleanup(BuddyThreadContext_t * context)
{
        BuddyBlock_t *p_block;
        unsigned int i;

#ifndef _MONOTHREAD_MEMALLOC
        /* check if there are some blocks to be freed from other threads */
        CheckBlocksToBeFreed(context, FALSE);
#endif

        /* free pages that has the size of a memory page */
        while ( (p_block = context->MemDesc[context->k_size]) != NULL )
          {
            /* sanity check on block */
            if ( (p_block->Header.Base_ptr != (BUDDY_ADDR_T) p_block)
               || (p_block->Header.StdInfo.Base_kSize
                   != p_block->Header.StdInfo.k_size) )
              {
                LogCrit(COMPONENT_MEMALLOC,
                        "largest free page is not a root page?!" );
                LogEvent(COMPONENT_MEMALLOC,
                         "thread page size=2^%u, block size=2^%u, block base area=%p (size=2^%u), block addr=%p",
                         context->k_size, p_block->Header.StdInfo.k_size,
                         p_block->Header.Base_ptr,
                         p_block->Header.StdInfo.Base_kSize,
                         (BUDDY_ADDR_T) p_block);
                return BUDDY_ERR_EFAULT;
              }

            /* We can free this page */
            LogFullDebug(COMPONENT_MEMALLOC,
                         "Releasing memory page at address %p, size=2^%u",
                         p_block, p_block->Header.StdInfo.k_size );
            Remove_FreeBlock(context, p_block);
            free(p_block);
            UpdateStats_RemoveStdPage(context);
          }

        /* if there are smaller blocks, it means there are still allocated
         * blocks that cannot be merged with them.
         * We can't free those pages...
         */
        for(i = 0; i < BUDDY_MAX_LOG2_SIZE; i++)
          {
            if ( context->MemDesc[i] )
              {
#ifdef _MONOTHREAD_MEMALLOC
                LogCrit(COMPONENT_MEMALLOC,
                        "Can't release thread resources: memory still in use");
                /* The thread itself did not free something */
                return BUDDY_ERR_INUSE;
#else
                /* another thread holds a block:
                 * we must atomically recheck if blocks have been freed
                 * by another thread in the meantime,
                 * if not, mark the context as 'destroy_pending'.
                 * The last free() from another thread will do the cleaning.
                 */
                LogDebug(COMPONENT_MEMALLOC,
                         "Another thread still holds a block: "
                         "deferred cleanup for context=%s (%p), thread=%p",
                         (caddr_t)context->label_thread, context, (caddr_t)context->OwnerThread);
                /* set the context in "destroy_pending" state,
                 * if it was not already */
                context->destroy_pending = TRUE;
                return BUDDY_ERR_INUSE;
#endif
              }
          }

#ifndef _MONOTHREAD_MEMALLOC
        V(context->ToBeFreed_mutex);
        pthread_mutex_destroy(&context->ToBeFreed_mutex);
#endif

        if (pthread_self() == context->OwnerThread)
          LogDebug(COMPONENT_MEMALLOC,
                   "thread (%s) %p successfully released resources for itself",
                   (caddr_t)context->label_thread, (caddr_t)pthread_self());
        else
          LogDebug(COMPONENT_MEMALLOC,
                   "thread %p successfully released resources of thread %s (%p)",
                   (caddr_t)pthread_self(), context->label_thread, (caddr_t)context->OwnerThread);

        /* destroy thread context */
#ifndef _MONOTHREAD_MEMALLOC
        remove_context(context);
#endif
        free( context );
        return BUDDY_SUCCESS;
}


/* ------------------------------------------*
 *           BuddyMalloc API Routines.
 * ------------------------------------------*/

/**
 * Inits the memory descriptor for current thread.
 */
int BuddyInit(buddy_parameter_t * p_buddy_init_info)
{

  unsigned int m, i;
  BuddyBlock_t *p_block;
  BuddyThreadContext_t *context;

  /* Ensure thread safety. */
  context = GetThreadContext();

  if(!context)
    {
      LogCrit(COMPONENT_MEMALLOC,
              "Buddy Malloc thread context could not be allocated for thread %p",
              (caddr_t)pthread_self());
      ShowAllContext();
      return BUDDY_ERR_MALLOC;
    }

  /* Is the memory descriptor already initialized ? */

  if(context->initialized)
    {
      LogCrit(COMPONENT_MEMALLOC,
              "The memory descriptor is already initialized for thread %p.",
              (caddr_t)pthread_self());
      ShowAllContext();
      return BUDDY_ERR_ALREADYINIT;
    }

  /* now, start doing serious things */

  /* First, check configuration. */

  if(p_buddy_init_info)
    context->Config = *p_buddy_init_info;
  else
    context->Config = default_buddy_parameter;

  /* check for minimum size :
   * this must be greater than the size of a Buddy block desc.*/

  if(context->Config.memory_area_size <= (size_header64 + MIN_ALLOC_SIZE))
    {
      LogMajor(COMPONENT_MEMALLOC, "Invalid size %llu (too small).",
               (unsigned long long)context->Config.memory_area_size);
      ShowAllContext();
      return BUDDY_ERR_EINVAL;
    }

  /* computes the log2 of memory area's size */

  if(!(m = Log2Ceil(context->Config.memory_area_size)))
    {
      LogMajor(COMPONENT_MEMALLOC, "Invalid size %llu (too large).",
               (unsigned long long)context->Config.memory_area_size);
      ShowAllContext();
      return BUDDY_ERR_EINVAL;
    }

  /* Sets misc values */

  context->k_size = m;
  context->Errno = 0;

  /* Init memory map */

  for(i = 0; i < BUDDY_MAX_LOG2_SIZE; i++)
    context->MemDesc[i] = NULL;

  /* Init stats */

  context->Stats.TotalMemSpace = 0;
  context->Stats.WM_TotalMemSpace = 0;
  context->Stats.StdMemSpace = 0;
  context->Stats.WM_StdMemSpace = 0;
  context->Stats.StdUsedSpace = 0;
  context->Stats.WM_StdUsedSpace = 0;
  context->Stats.StdPageSize = 1 << m;
  context->Stats.NbStdPages = 0;
  context->Stats.NbStdUsed = 0;
  context->Stats.WM_NbStdUsed = 0;

  context->Stats.ExtraMemSpace = 0;
  context->Stats.WM_ExtraMemSpace = 0;
  context->Stats.MinExtraPageSize = 0;
  context->Stats.MaxExtraPageSize = 0;
  context->Stats.NbExtraPages = 0;
  context->Stats.WM_NbExtraPages = 0;

#ifndef _MONOTHREAD_MEMALLOC
  if(pthread_mutex_init(&context->ToBeFreed_mutex, NULL) != 0)
    {
      LogCrit(COMPONENT_MEMALLOC,
              "BuddyInit could not initialize ToBeFreed_mutex for thread %p",
              (caddr_t)pthread_self());
      ShowAllContext();
      return BUDDY_ERR_EINVAL;
    }
  context->ToBeFreed_list = NULL;
  context->destroy_pending = FALSE;
#endif

  /* structure is initialized */

  context->initialized = TRUE;
  context->OwnerThread = pthread_self();

  /* Now, we allocate a first memory page */

  p_block = NewStdPage(context);

  LogFullDebug(COMPONENT_MEMALLOC,
               "sizeof header = %zu, size_header64 = %zu",
               sizeof(BuddyHeader_t), size_header64);

  if(p_block)
    {
      LogDebug(COMPONENT_MEMALLOC,
               "BuddyInit successful for thread %p",
               (caddr_t)pthread_self());
      return BUDDY_SUCCESS;
    }
  else
    {
      LogCrit(COMPONENT_MEMALLOC,
              "BuddyInit could not allocate a page for thread %p",
              (caddr_t)pthread_self());
      ShowAllContext();
      return BUDDY_ERR_MALLOC;
    }
}                               /* BuddyInit */

/**
 * For pool allocation, the user may know how much entries
 * it must place in a pool block for not wasting memory.
 * E.g. If he wants to allocate <n> entries of size <s>,
 * and if buddy_header + n*s = 2^k + 1, we will need to alloc 2^(k+1) !!!
 * Thus, we give him back a better value for <n> so that it will exactly
 * fit the 2^(k+1) block size, with no waste ;)
 *
 * \param min_count the min count user wants to alloc
 * \param type_size the size of for single entry
 */
unsigned int BuddyPreferedPoolCount(unsigned int min_count, size_t type_size)
{
  BuddyThreadContext_t *context;
  unsigned int sizelog2, prefered_count;
  size_t prefered_size;
  size_t min_size = min_count * type_size;

  context = GetThreadContext();

  if(min_size < MIN_ALLOC_SIZE)
    sizelog2 = Log2Ceil(MIN_ALLOC_SIZE + size_header64);
  else
    sizelog2 = Log2Ceil(min_size + size_header64);

  /* If it is greater that buddy pages, an extra allocation
   * will be necessary. In this case, count doesn't matter */
  if(sizelog2 > context->k_size)
    return min_count;

  /* now, make the opposite computation */
  prefered_size = (1 << sizelog2) - size_header64;
  prefered_count = prefered_size / type_size;

  if(prefered_count == 0)
    return 1;

  return prefered_count;

}

/**
 *  Allocates a memory area of a given size.
 */
static BUDDY_ADDR_T __BuddyMalloc(size_t Size, int do_exit_on_error)
{

  unsigned int sizelog2, actlog2;
  BuddyBlock_t *p_block;
  BuddyThreadContext_t *context;
  size_t allocation;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* sanity checks */
  if(!context)
    return NULL;

  /* Not initialized */
  if(!context->initialized)
    {
      context->Errno = BUDDY_ERR_NOTINIT;
      return NULL;
    }

#ifndef _MONOTHREAD_MEMALLOC
  /* check if there are some blocks to be freed */
  CheckBlocksToBeFreed(context, TRUE);
#endif

  /* No need to alloc something if asked size is 0 !!! */
  if(Size == 0)
    return NULL;

  if(Size < MIN_ALLOC_SIZE)
    sizelog2 = Log2Ceil(MIN_ALLOC_SIZE + size_header64);
  else
    sizelog2 = Log2Ceil(Size + size_header64);

  actlog2 = sizelog2;
  allocation = 1 << sizelog2;

  /* If it is a non-standard block (largest than page size),
   * We handle the request using AllocLargeBlock( context, size ).
   */

  if(allocation > (1ULL << context->k_size))
    {

      if(context->Config.extra_alloc)
        {
          /* extra block are allowed */
          return AllocLargeBlock(context, Size);
        }
      else
        {
          /* Extra blocks are not allowed */

          LogMajor(COMPONENT_MEMALLOC,
                   "%p:BuddyMalloc(%llu) => BUDDY_ERR_OUTOFMEM (extra_alloc disabled).",
                   (BUDDY_ADDR_T) pthread_self(), (unsigned long long)Size);

          context->Errno = BUDDY_ERR_OUTOFMEM;

          if(do_exit_on_error)
            Fatal();

          return NULL;

        }

    }

  /* It is a standard block, we look for a large enough block
   * in the block pool.
   */

  while((actlog2 < BUDDY_MAX_LOG2_SIZE) && (!context->MemDesc[actlog2]))
    {
      actlog2++;
    }

  LogFullDebug(COMPONENT_MEMALLOC,
               "To alloc %llu (2^%u) we have to alloc 2^%u",
               (unsigned long long)Size, sizelog2, actlog2);

  if(actlog2 < BUDDY_MAX_LOG2_SIZE)
    {
      /* 1st case : a block is available */
      p_block = context->MemDesc[actlog2];
    }
  else if(context->Config.on_demand_alloc)
    {
      /* 2nd case :
       * No memory block available.
       * If the on_demand_alloc option has been set,
       * We can allocate a new page.
       */

      /* add a new page */
      p_block = NewStdPage(context);

      if(!p_block)
        {
          context->Errno = BUDDY_ERR_MALLOC;

          LogMajor(COMPONENT_MEMALLOC, "BuddyMalloc: NOT ENOUGH MEMORY !!!");

          if(do_exit_on_error)
            Fatal();

          return NULL;
        }

    }
  else
    {
      /* Out of memory */
      LogMajor(COMPONENT_MEMALLOC,
               "%p:BuddyMalloc(%llu) => BUDDY_ERR_OUTOFMEM (on_demand_alloc disabled).",
               (BUDDY_ADDR_T) pthread_self(), (unsigned long long)Size);

      if(do_exit_on_error)
        Fatal();

      context->Errno = BUDDY_ERR_OUTOFMEM;
      return NULL;

    }

  /* removes the selected block from the pool of free blocks. */

  Remove_FreeBlock(context, p_block);

  /* If it was a whole page, we notice that it becomes used */

  if((p_block->Header.Base_ptr == (BUDDY_ADDR_T) p_block)
     && (p_block->Header.StdInfo.Base_kSize == p_block->Header.StdInfo.k_size))
    {

      UpdateStats_UseStdPage(context);

    }

  /* Iteratively splits the block. */

  while(p_block->Header.StdInfo.k_size > sizelog2)
    {

      BuddyBlock_t *p_buddy;

      /* divides the main block into 2 smaller blocks */
      p_block->Header.StdInfo.k_size--;

      p_buddy = Get_BuddyBlock(context, p_block);

      /* herits from the same parent */

      p_buddy->Header.Base_ptr = p_block->Header.Base_ptr;
      p_buddy->Header.StdInfo.Base_kSize = p_block->Header.StdInfo.Base_kSize;

      /* new block is free */
      p_buddy->Header.status = FREE_BLOCK;
      p_buddy->Header.MagicNumber = MAGIC_NUMBER_FREE;

      p_buddy->Header.StdInfo.k_size = p_block->Header.StdInfo.k_size;

      /* insert block into the free list */
      Insert_FreeBlock(context, p_buddy);

    }

  /* Finally, we have the block to be reserved  */
  p_block->Header.status = RESERVED_BLOCK;

  p_block->Header.MagicNumber = MAGIC_NUMBER_USED;
  p_block->Header.OwnerThread = pthread_self();

#ifndef _MONOTHREAD_MEMALLOC
  p_block->Header.OwnerThreadContext = context;
#endif

#ifdef _DEBUG_MEMLEAKS
  /* sets the label for debugging */
  p_block->Header.label_user_defined = context->label_user_defined;
  p_block->Header.label_file = context->label_file;
  p_block->Header.label_func = context->label_func;
  p_block->Header.label_line = context->label_line;
#ifndef _NO_BLOCK_PREALLOC
  p_block->Header.pa_entry = NULL;
#endif

  p_block->Header.StdInfo.user_size = Size + size_header64;

  /* add it to the list of allocated blocks */
  add_allocated_block(context, p_block);

#endif

  /* update stats to remember we use this amount of memory */
  UpdateStats_UseStdMemSpace(context, allocation);

  LogDebug(COMPONENT_MEMALLOC,
           "BuddyMalloc(%llu) block=%p => %p",
           (unsigned long long)Size,
           p_block,
           p_block->Content.UserSpace);


  /* returns the userspace aligned on 64 bits */
  return (BUDDY_ADDR_T) ((BUDDY_PTRDIFF_T) p_block + (BUDDY_PTRDIFF_T) size_header64);

}                               /* BuddyMalloc */

BUDDY_ADDR_T BuddyMalloc(size_t Size)
{
  return __BuddyMalloc(Size, FALSE);
}

BUDDY_ADDR_T BuddyMallocExit(size_t Size)
{
  return __BuddyMalloc(Size, TRUE);
}

/**
 * BuddyStr_Dup : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures).
 */
char *BuddyStr_Dup(const char * Str)
{
  char *NewStr = (char *) BuddyMalloc(strlen(Str)+1);
  if(NewStr != NULL)
    strcpy(NewStr, Str);
  return NewStr;
}

/**
 * BuddyStr_Dup_Exit : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup_Exit() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures).
 * If no memory is available, it stops current process.
 */
char *BuddyStr_Dup_Exit(const char * Str)
{
  char *NewStr = (char *) BuddyMallocExit(strlen(Str)+1);
  if(NewStr != NULL)
    strcpy(NewStr, Str);
  return NewStr;
}

/**
 *  Free allocated memory (without any owner checking)
 */
static void __BuddyFree(BuddyThreadContext_t * context, BuddyBlock_t * p_block)
{
  BuddyBlock_t *p_block_tmp;

#ifdef _DEBUG_MEMLEAKS
  /* remove from allocated blocks */
  remove_allocated_block(context, p_block);
#endif

  /* If it is an ExtraBlock, we free it using FreeLargeBlock */
  if(IS_EXTRA_BLOCK(p_block))
    {
      FreeLargeBlock(context, p_block);
      return;
    }

  /* Sanity checks for std blocks */
  if(((BUDDY_ADDR_T) p_block < p_block->Header.Base_ptr) ||
     ((BUDDY_ADDR_T) p_block > p_block->Header.Base_ptr +
      (1 << p_block->Header.StdInfo.Base_kSize)))
    {
      context->Errno = BUDDY_ERR_EINVAL;
      return;
    }

  /* mark it free */

  p_block->Header.status = FREE_BLOCK;
  p_block->Header.MagicNumber = MAGIC_NUMBER_FREE;

  UpdateStats_FreeStdMemSpace(context, 1 << p_block->Header.StdInfo.k_size);

  /* merge free blocks. */

  for(p_block_tmp = p_block;
      p_block_tmp->Header.StdInfo.k_size < p_block_tmp->Header.StdInfo.Base_kSize;
      p_block_tmp->Header.StdInfo.k_size++)
    {

      BuddyBlock_t *p_buddy;

      p_buddy = Get_BuddyBlock(context, p_block_tmp);

      LogFullDebug(COMPONENT_MEMALLOC,
                   "%p:Buddy( %p,%u ) = ( %p ,%u )=>%s",
                   (BUDDY_ADDR_T) pthread_self(),
                   p_block_tmp, p_block_tmp->Header.StdInfo.k_size, p_buddy,
                   p_buddy->Header.StdInfo.k_size,
                   (p_buddy->Header.status ? "RESERV" : " FREE "));

      if((p_buddy->Header.status == RESERVED_BLOCK) ||
         (p_buddy->Header.StdInfo.k_size != p_block_tmp->Header.StdInfo.k_size))
        /* stop merging */
        break;

      /* The buddy can be merged */
      Remove_FreeBlock(context, p_buddy);

      LogFullDebug(COMPONENT_MEMALLOC,
                   "%p:Merging %p with %p (sizes 2^%.2u)",
                   (BUDDY_ADDR_T) pthread_self(),
                   p_buddy, p_block_tmp, p_block_tmp->Header.StdInfo.k_size);

      /* the address of the merged blockset is the smallest
       * of its components addresses.
       */
      if(p_buddy < p_block_tmp)
        {
          p_block_tmp = p_buddy;
        }

    }

  /* Add the merged bloc to the free list */

  Insert_FreeBlock(context, p_block_tmp);

  /* update stats */

  if((p_block_tmp->Header.Base_ptr == (BUDDY_ADDR_T) p_block_tmp)
     && (p_block_tmp->Header.StdInfo.Base_kSize == p_block_tmp->Header.StdInfo.k_size))
    {

      UpdateStats_FreeStdPage(context);

      /* if garbage collection is enabled, run it */

      if(context->Config.free_areas)
        {
          Garbage_StdPages(context, p_block_tmp);
        }

    }

  return;

}                               /* __BuddyFree */

/**
 *  Free allocated memory (user call)
 */
void BuddyFree(BUDDY_ADDR_T ptr)
{

  BuddyBlock_t *p_block;

  BuddyThreadContext_t *context;

  LogFullDebug(COMPONENT_MEMALLOC,
               "%p:BuddyFree(%p)",
               (BUDDY_ADDR_T) pthread_self(), ptr);

  /* Nothing appends if ptr is NULL. */
  if(!ptr)
    return;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* Something very wrong occured !! */
  if(!context)
    return;

  /* Not initialized */
  if(!context->initialized)
    {
      context->Errno = BUDDY_ERR_NOTINIT;
      return;
    }

  /* retrieves block address */
  p_block = (BuddyBlock_t *) (ptr - size_header64);

  /* check magic number consistency */
  switch (p_block->Header.status)
    {
    case FREE_BLOCK:
      /* check for magic number */
      if(isBadMagicNumber("BuddyFree (FREE BLOCK):", context, p_block, MAGIC_NUMBER_FREE, 1, NULL))
        {
          /* doing nothing is safer !!! */
          return;
        }
      break;

    case RESERVED_BLOCK:
      /* check for magic number */
      if(isBadMagicNumber("BuddyFree (RESERVED BLOCK):", context, p_block, MAGIC_NUMBER_USED, 1, NULL))
        {
          /* doing nothing is safer !!! */
          return;
        }
      break;

    default:
      /* Invalid Header status : may not be allocated using BuddyMalloc */
      LogMajor(COMPONENT_MEMALLOC,
               "BuddyFree: pointer %p is not a buddy block !!!",
               ptr);
      log_bad_block("BuddyFree:", context, p_block, 0, 0);
      return;
    }

  /* is it already free ? */
  if(p_block->Header.status == FREE_BLOCK)
    {
      LogWarn(COMPONENT_MEMALLOC, "Double free detected for %p", ptr);
      return;
    }

  /* check owner thread */

  if(p_block->Header.OwnerThread != pthread_self())
    {
#ifndef _MONOTHREAD_MEMALLOC

      /* alias */
      BuddyThreadContext_t *owner_context = p_block->Header.OwnerThreadContext;

      LogFullDebug(COMPONENT_MEMALLOC,
                   "This block (%p) belongs to another thread (%p), I put it in its release list",
                   p_block, (BUDDY_ADDR_T) p_block->Header.OwnerThread);

      /* put the block into the ToBeFreed_list of the owner thread */
      P(owner_context->ToBeFreed_mutex);
      p_block->Content.NextToBeFreed = owner_context->ToBeFreed_list;
      owner_context->ToBeFreed_list = p_block;

      /* if the context state is 'destroy_pending', check if
       * all blocks have been released (/!\ under the protection of
       * 'ToBeFreed_mutex'). If so, complete the cleanup.
       */
      if (!owner_context->destroy_pending ||
          (TryContextCleanup(owner_context) != BUDDY_SUCCESS ))
        V(owner_context->ToBeFreed_mutex);
      /* else no need to release the mutex, it has been destroyed */

#else
      /* Dangerous situation ! */

      LogMajor(COMPONENT_MEMALLOC,
               "BuddyFree: block %p has been allocated by another thread !!!! (%p<>%p)",
               p_block, (BUDDY_ADDR_T) p_block->Header.OwnerThread,
               (BUDDY_ADDR_T) pthread_self());
      log_bad_block("BuddyFree:", context, p_block, 1, 0);

#endif

      /* return in any case */
      return;
    }

  __BuddyFree(context, p_block);
  return;

}                               /* BuddyFree */

/**
 * BuddyRealloc :
 * changes  the  size  of the memory block pointed to by ptr to
 * size bytes.
 *
 * If ptr is NULL, the call is equivalent to malloc(size);
 * if size is equal to zero, the  call is equivalent to free(ptr).
 * Unless ptr is NULL, it must have been returned by an
 * earlier call to malloc(), calloc() or realloc().
 */
BUDDY_ADDR_T BuddyRealloc(BUDDY_ADDR_T ptr, size_t Size)
{

  BUDDY_ADDR_T new_ptr;
  BuddyBlock_t *p_block;
  BuddyThreadContext_t *context;

  LogFullDebug(COMPONENT_MEMALLOC,
               "%p:BuddyRealloc(%p,%llu)",
               (BUDDY_ADDR_T) pthread_self(), ptr,
               (unsigned long long)Size);

  /*  If ptr is NULL, the call is equivalent to malloc(size) */
  if(ptr == NULL)
    return BuddyMalloc(Size);

  /* if size is equal to zero, the  call is equivalent to free(ptr) */
  if(Size == 0)
    {
      BuddyFree(ptr);
      return NULL;
    }

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* sanity check */
  if(!context)
    return NULL;

  /* checking arguments */

  /* Not initialized */
  if(!context->initialized)
    {
      context->Errno = BUDDY_ERR_NOTINIT;
      return NULL;
    }

  /* retrieves block address */
  p_block = (BuddyBlock_t *) (ptr - size_header64);

  /* it should not be free */
  if(p_block->Header.status != RESERVED_BLOCK)
    {
      context->Errno = BUDDY_ERR_EINVAL;
      return NULL;
    }

  /* allocating the new memory area */
  new_ptr = BuddyMalloc(Size);

  /* is there enough memory ? */
  if(!new_ptr)
    return NULL;

  /* copying the old memory area to the new one. */
  /* size of user space = total size of block - size of header */

  if(IS_EXTRA_BLOCK(p_block))
    {

      LogFullDebug(COMPONENT_MEMALLOC,
                   "%p:Copying %zu bytes from @%p to @%p->@%p",
                   (BUDDY_ADDR_T) pthread_self(),
                   p_block->Header.ExtraInfo - size_header64, ptr, new_ptr,
                   new_ptr + p_block->Header.ExtraInfo - size_header64);

      memcpy(new_ptr, ptr, p_block->Header.ExtraInfo - size_header64);

    }
  else
    {

      LogFullDebug(COMPONENT_MEMALLOC,
                   "%p:Copying %zu bytes from @%p to @%p->@%p",
                   (BUDDY_ADDR_T) pthread_self(),
                   (1 << p_block->Header.StdInfo.k_size) - size_header64, ptr, new_ptr,
                   new_ptr + (1 << p_block->Header.StdInfo.k_size) - size_header64);

      memcpy(new_ptr, ptr, (1 << p_block->Header.StdInfo.k_size) - size_header64);

    }

  /* freeing the old memory area */
  BuddyFree(ptr);

  /* returning the adress for the new area. */
  return new_ptr;

}

/**
 * BuddyCalloc :
 * allocates memory for an array of nmemb elements of size bytes
 * each and returns a pointer to the allocated memory.  The memory is set
 * to zero.
*/
BUDDY_ADDR_T BuddyCalloc(size_t NumberOfElements, size_t ElementSize)
{

  BUDDY_ADDR_T ptr;
  ptr = BuddyMalloc(NumberOfElements * ElementSize);

  if(!ptr)
    return NULL;

  LogFullDebug(COMPONENT_MEMALLOC,
               "%p:Setting %zu bytes from @%p to 0",
               (BUDDY_ADDR_T) pthread_self(),
               NumberOfElements * ElementSize, ptr);
  memset(ptr, 0, NumberOfElements * ElementSize);

  return ptr;

}                               /* BuddyCalloc */

/**
 *  Release all thread resources.
 */
int BuddyDestroy()
{
  BuddyThreadContext_t *context;
  int rc;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* sanity checks */
  if(!context)
    return BUDDY_ERR_EINVAL;

  /* Not initialized */
  if(!context->initialized)
    return BUDDY_ERR_NOTINIT;

#ifndef _MONOTHREAD_MEMALLOC
  /* Destroying thread resources must be done
   * under the protection of a mutex,
   * to prevent from concurrent thread that would
   * release a block that it owns.
   */
  P( context->ToBeFreed_mutex );
#endif

  rc = TryContextCleanup(context);

#ifndef _MONOTHREAD_MEMALLOC
  /* If context was not cleaned up, release the mutex, otherwise it is destroyed. */
  if ( rc != BUDDY_SUCCESS )
    V( context->ToBeFreed_mutex );
#endif

  return rc;
}


/**
 *  For debugging.
 *  Prints the content of the memory area to an opened file.
 */
void BuddyDumpMem(FILE * output)
{

  int i, exist;
  BuddyThreadContext_t *context;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* sanity check */
  if(!context)
    return;

  /* print statistics */

  fprintf(output, "%p: Total Space in Arena: %lu  (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.TotalMemSpace,
          (unsigned long)context->Stats.WM_TotalMemSpace);
  fprintf(output, "\n");

  fprintf(output, "%p: Total Space for Standard Pages: %lu  (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.StdMemSpace,
          (unsigned long)context->Stats.WM_StdMemSpace);

  fprintf(output, "%p:       Nb Preallocated Standard Pages: %lu\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.NbStdPages);

  fprintf(output, "%p:       Size of Std Pages: %lu\n", (BUDDY_ADDR_T) pthread_self(),
          (unsigned long)context->Stats.StdPageSize);

  fprintf(output, "%p:       Space Used inside Std Pages: %lu  (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.StdUsedSpace,
          (unsigned long)context->Stats.WM_StdUsedSpace);

  fprintf(output, "%p:       Nb of Std Pages Used: %lu  (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.NbStdUsed,
          (unsigned long)context->Stats.WM_NbStdUsed);

  if(context->Stats.NbStdUsed > 0)
    {
      fprintf(output, "%p:       Memory Fragmentation: %.2f %%\n",
              (BUDDY_ADDR_T) pthread_self(),
              100.0 -
              (100.0 * context->Stats.StdUsedSpace /
               (1.0 * context->Stats.NbStdUsed * context->Stats.StdPageSize)));
    }

  fprintf(output, "\n");

  exist = 0;

  for(i = 0; i < BUDDY_MAX_LOG2_SIZE; i++)
    {

      BuddyBlock_t *p_block = context->MemDesc[i];
      while(p_block)
        {

          exist = 1;

          fprintf(output,
                  "%p: block_size=2^%.2d | block_status=%s | block_addr=%8p  | page_addr=%8p | page_size=2^%.2d\n",
                  (BUDDY_ADDR_T) pthread_self(), p_block->Header.StdInfo.k_size,
                  (p_block->Header.status ? "RESERV" : "FREE  "), p_block,
                  p_block->Header.Base_ptr, p_block->Header.StdInfo.Base_kSize);

          if (isFullDebug(COMPONENT_MEMALLOC))
            {

              /* dump memory state to see if there is any gardening from outside... */
              unsigned char *c;   /* the current char to be printed */

              for(c = (unsigned char *)p_block;
                  c < ((unsigned char *)p_block + sizeof(BuddyBlock_t)); c++)
                fprintf(output, "%.2X", (unsigned char)*c);

              fprintf(output, "\n");

              for(c = (unsigned char *)p_block;
                  c < ((unsigned char *)p_block + sizeof(BuddyBlock_t)); c++)
                fprintf(output, "%c.", (unsigned char)*c);

              fprintf(output, "\n");

            }

          p_block = p_block->Content.FreeBlockInfo.NextBlock;

        }
    }

  if(!exist)
    fprintf(output, "%p: No free blocks\n", (BUDDY_ADDR_T) pthread_self());

  fprintf(output, "\n");

  /* stats about extra pages */

  fprintf(output, "%p: Extra Memory Space:     %lu   (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.ExtraMemSpace,
          (unsigned long)context->Stats.WM_ExtraMemSpace);

  fprintf(output, "%p:       Nb Extra Pages:   %lu   (Watermark: %lu)\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.NbExtraPages,
          (unsigned long)context->Stats.WM_NbExtraPages);
  fprintf(output, "%p:       Min Page Size Watermark:  %lu\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.MinExtraPageSize);
  fprintf(output, "%p:       Max Page Size Watermark:  %lu\n",
          (BUDDY_ADDR_T) pthread_self(), (unsigned long)context->Stats.MaxExtraPageSize);

#ifdef _DEBUG_MEMLEAKS

  fprintf(output, "\n");

  {
    BuddyBlockPtr_t p_curr_block;

    /* browsing allocated blocks list */
    for(p_curr_block = context->p_allocated;
        p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
      {

        /* If it is an ExtraBlock, we free it using FreeLargeBlock */
        if(IS_EXTRA_BLOCK(p_curr_block))
          {

            fprintf(output,
                    "%p: type=EXTRA_BLOCK | size=%lu | status=%s | block_addr=%8p | base_ptr=%8p | label=%s:%u:%s:%s\n",
                    (BUDDY_ADDR_T) pthread_self(),
                    (unsigned long)p_curr_block->Header.ExtraInfo,
                    (p_curr_block->Header.status ? "RESERV" : "FREE  "), p_curr_block,
                    p_curr_block->Header.Base_ptr,
                    p_curr_block->Header.label_file,
                    p_curr_block->Header.label_line,
                    p_curr_block->Header.label_func,
                    p_curr_block->Header.label_user_defined);
          }
        else
          {
            fprintf(output,
                    "%p: type=STD_BLOCK   | size=2^%.2d | status=%s | block_addr=%8p | base_ptr=%8p | label=%s:%u:%s:%s\n",
                    (BUDDY_ADDR_T) pthread_self(), p_curr_block->Header.StdInfo.k_size,
                    (p_curr_block->Header.status ? "RESERV" : "FREE  "), p_curr_block,
                    p_curr_block->Header.Base_ptr,
                    p_curr_block->Header.label_file,
                    p_curr_block->Header.label_line,
                    p_curr_block->Header.label_func,
                    p_curr_block->Header.label_user_defined);
          }

      }
  }
#endif

}                               /* BuddyDumpMem */

/**
 *  Get stats for memory use.
 */
void BuddyGetStats(buddy_stats_t * budd_stats)
{

  BuddyThreadContext_t *context;

  /* sanity check */
  if(!budd_stats)
    return;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* sanity check */
  if(!context)
    return;

  *budd_stats = context->Stats;

  return;

}


#ifdef _DEBUG_MEMLEAKS

int BuddySetDebugLabel(const char *file, const char *func, const unsigned int line,
                        const char *label);

#ifndef _NO_BLOCK_PREALLOC
void FillPool(struct prealloc_pool *pool,
              const char           *file,
              const char           *function,
              const unsigned int    line,
              const char           *str)
{
  int size = pool->pa_size + size_prealloc_header64;
  char *mem;
  BuddyBlock_t *p_block;
  int num = pool->pa_num;

  BuddySetDebugLabel(file, function, line, str);
  mem = (char *) BuddyCalloc(pool->pa_num, size);

  if (mem == NULL)
    return;

  /* retrieves block address */
  p_block = (BuddyBlock_t *) (mem - size_header64);
  p_block->Header.pa_entry = NULL;

  pool->pa_allocated += num;
  pool->pa_blocks++;
  while (num > 0)
    {
      prealloc_header *h = (prealloc_header *) mem;

      h->pa_next  = pool->pa_free;
      h->pa_inuse = 0;
      h->pa_pool  = pool;
      h->pa_nextb = p_block->Header.pa_entry;
      p_block->Header.pa_entry = h;
      pool->pa_free = h;
      mem += size;
      if(pool->pa_constructor != NULL)
        pool->pa_constructor(get_prealloc_entry(h, void));
      num--;
    }
}

void _InitPool(struct prealloc_pool *pool,
               int                   num_alloc,
               int                   size_type,
               constructor           ctor,
               constructor           dtor,
               char                 *type)
{
  int size;
  pool->pa_free        = NULL;
  pool->pa_constructor = ctor;
  pool->pa_destructor  = dtor;
  pool->pa_size        = size_type;
  size = (pool)->pa_size + size_prealloc_header64;
  pool->pa_num         = GetPreferedPool(num_alloc, size);
  pool->pa_blocks      = 0;
  pool->pa_allocated   = 0;
  pool->pa_used        = 0;
  pool->pa_high        = 0;
  pool->pa_type        = type;
  pool->pa_name[0]     = '\0';
#ifndef _MONOTHREAD_MEMALLOC
  P(ContextListMutex);
  pool->pa_next_pool = first_pool;
  first_pool = pool;
  V(ContextListMutex);
#endif
}
#endif

/** Set a label for allocated areas, for debugging. */
int BuddySetDebugLabel(const char *file, const char *func, const unsigned int line,
                        const char *label)
{

  BuddyThreadContext_t *context;

  context = GetThreadContext();

  /* If there is no context, it means that malloc failed
   * However, we can't store it in the thread context.
   * so we return a pointer to this error code.
   */
  if(!context)
    return BUDDY_ERR_MALLOC;

  if(!label)
    return BUDDY_ERR_EINVAL;

  context->label_user_defined = label;
  context->label_file = file;
  context->label_func = func;
  context->label_line = line;

  return BUDDY_SUCCESS;

}

/**
 * Those functions allocate memory with a file/function/line label
 */
BUDDY_ADDR_T BuddyMalloc_Autolabel(size_t sz,
                                   const char *file,
                                   const char *function,
                                   const unsigned int line,
                                   const char *str)
{
  BuddySetDebugLabel(file, function, line, str);
  return BuddyMallocExit(sz);
}

/**
 * BuddyStr_Dup : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures).
 */
char *BuddyStr_Dup_Autolabel(const char * OldStr,
                             const char *file,
                             const char *function,
                             const unsigned int line,
                             const char *str)
{
  char *NewStr;
  BuddySetDebugLabel(file, function, line, str);
  NewStr = (char *) BuddyMallocExit(strlen(OldStr)+1);
  if(NewStr != NULL)
    strcpy(NewStr, OldStr);
  return NewStr;
}

BUDDY_ADDR_T BuddyCalloc_Autolabel(size_t NumberOfElements, size_t ElementSize,
                                   const char *file,
                                   const char *function,
                                   const unsigned int line,
                                   const char *str)
{
  BuddySetDebugLabel(file, function, line, str);
  return BuddyCalloc(NumberOfElements, ElementSize);
}

BUDDY_ADDR_T BuddyRealloc_Autolabel(BUDDY_ADDR_T ptr, size_t Size,
                                    const char *file,
                                    const char *function,
                                    const unsigned int line,
                                    const char *str)
{
  BuddySetDebugLabel(file, function, line, str);
  return BuddyRealloc(ptr, Size);
}

void BuddyFree_Autolabel(BUDDY_ADDR_T ptr,
                         const char *file,
                         const char *function,
                         const unsigned int line,
                         const char *str)
{
  BuddySetDebugLabel(file, function, line, str);
  BuddyFree(ptr);
}

int _BuddyCheck_Autolabel(BUDDY_ADDR_T ptr,
                          int other_thread_ok,
                          const char *file,
                          const char *function,
                          const unsigned int line,
                          const char *str)
{
  LogFullDebug(COMPONENT_MEMALLOC,
               "BuddyCheck %p for %s at %s:%s:%u",
               ptr, str, file, function, line);
  BuddySetDebugLabel(file, function, line, str);
  return _BuddyCheck(ptr, other_thread_ok, str);
}

/** Retrieves the label for a given block.  */
const char *BuddyGetDebugLabel(BUDDY_ADDR_T ptr)
{
  BuddyBlock_t *p_block;

  BuddyThreadContext_t *context;

  /* Nothing is returned if ptr is NULL. */
  if(!ptr)
    return NULL;

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* Something very wrong occured !! */
  if(!context)
    return NULL;

  /* Not initialized */
  if(!context->initialized)
    {
      context->Errno = BUDDY_ERR_NOTINIT;
      return NULL;
    }

  /* retrieves block address */
  p_block = (BuddyBlock_t *) ((BUDDY_PTRDIFF_T) ptr - (BUDDY_PTRDIFF_T) size_header64);

  return p_block->Header.label_user_defined;
}

/**
 *  Count the number of blocks that were allocated using the given label.
 */
int BuddyCountDebugLabel(char *label)
{
  int count = 0;

  BuddyBlockPtr_t p_curr_block;
  BuddyThreadContext_t *context;

  context = GetThreadContext();

  /* If there is no context, it means that malloc failed
   * However, we can't store it in the thread context.
   * so we return a pointer to this error code.
   */
  if(!context)
    return -BUDDY_ERR_MALLOC;

  if(!label)
    return -BUDDY_ERR_EINVAL;

  /* browsing allocated blocks list */
  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
    {

      if(!strcmp(p_curr_block->Header.label_user_defined, label))
        {
          count++;
        }

    }

  return count;

}                               /* BuddyCountDebugLabel */

/* used for counting labels of each type */
typedef struct _label_info_list_
{
  const char *user_label;
  const char *file;
  const char *func;
  unsigned int line;

  unsigned int count;

  struct _label_info_list_ *next;
} label_info_list_t;

static unsigned int hash_label(const char *file, const char *func,
                               const unsigned int line, const char *label,
                               unsigned int hash_sz)
{
  unsigned long hash = 5381;
  int c;
  const char *str;

  str = file;
  while((c = *str++))
    hash = ((hash << 5) + hash) + c;

  str = func;
  while((c = *str++))
    hash = ((hash << 5) + hash) + c;

  str = label;
  while((c = *str++))
    hash = ((hash << 5) + hash) + c;

  hash = (hash ^ line) % hash_sz;

  return hash;

}                               /* hash_label */

static void hash_label_add(const char *file, const char *func, const unsigned int line,
                           const char *label, label_info_list_t * label_hash[],
                           unsigned int hash_sz)
{
  unsigned int h;
  label_info_list_t *p_curr;
  label_info_list_t *p_list;

  /* first compute label's hash value */
  h = hash_label(file, func, line, label, hash_sz);

  /* lookup the entry into hash */

  p_list = label_hash[h];

  for(p_curr = p_list; p_curr != NULL; p_curr = p_curr->next)
    {
      if(!strcmp(file, p_curr->file)
         && !strcmp(func, p_curr->func)
         && !strcmp(label, p_curr->user_label) && (line == p_curr->line))
        {
          p_curr->count++;
          return;
        }
    }

  /* not found */
  p_curr = (label_info_list_t *) malloc(sizeof(label_info_list_t));

  p_curr->user_label = label;
  p_curr->file = file;
  p_curr->func = func;
  p_curr->line = line;
  p_curr->count = 1;
  p_curr->next = p_list;
  label_hash[h] = p_curr;

}

static void hash_label_free(label_info_list_t * label_hash[], unsigned int hash_sz)
{
  unsigned int i;
  label_info_list_t *p_next;
  label_info_list_t *p_curr;

  for(i = 0; i < hash_sz; i++)
    {
      for(p_curr = label_hash[i]; p_curr != NULL; p_curr = p_next)
        {
          p_next = p_curr->next;
          free(p_curr);
        }
    }
}

static void hash_label_display(label_info_list_t * label_hash[],
                               unsigned int        hash_sz)
{
  unsigned int i, max_file, max_func, max_descr;
  label_info_list_t *p_curr;

  /* first count max length of each column */

  max_file = strlen("file");
  max_func = strlen("function");
  max_descr = strlen("description");

  for(i = 0; i < hash_sz; i++)
    {
      for(p_curr = label_hash[i]; p_curr != NULL; p_curr = p_curr->next)
        {
          if(strlen(p_curr->file) > max_file)
            max_file = strlen(p_curr->file);
          if(strlen(p_curr->func) > max_func)
            max_func = strlen(p_curr->func);
          if(strlen(p_curr->user_label) > max_descr)
            max_descr = strlen(p_curr->user_label);
        }
    }

  LogFullDebug(COMPONENT_MEMLEAKS,
               "%-*s | %-*s | %5s | %-*s | %s",
               max_file, "file",
               max_func, "function",
               "line", max_descr, "description", "count");

  for(i = 0; i < hash_sz; i++)
    {
      for(p_curr = label_hash[i]; p_curr != NULL; p_curr = p_curr->next)
        {
          LogFullDebug(COMPONENT_MEMLEAKS,
                       "%-*s | %-*s | %5u | %-*s | %u",
                       max_file, p_curr->file, max_func,
                       p_curr->func, p_curr->line, max_descr, p_curr->user_label,
                       p_curr->count);
        }
    }
}

/**
 *  Displays a summary of all allocated blocks
 *  with their labels
 */
void BuddyLabelsSummary(log_components_t component)
{
#define LBL_HASH_SZ 127
  label_info_list_t *label_hash[LBL_HASH_SZ];
  BuddyThreadContext_t *context;
  BuddyBlockPtr_t p_curr_block;
  unsigned int i;

  if(!isFullDebug(component) || !isFullDebug(COMPONENT_MEMLEAKS))
    return;

  context = GetThreadContext();

  if(!context)
    return;

  /* init hash */
  for(i = 0; i < LBL_HASH_SZ; i++)
    label_hash[i] = NULL;

  /* count all allocated blocks */

  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
    {

      hash_label_add(p_curr_block->Header.label_file,
                     p_curr_block->Header.label_func,
                     p_curr_block->Header.label_line,
                     p_curr_block->Header.label_user_defined, label_hash, LBL_HASH_SZ);

    }

  hash_label_display(label_hash, LBL_HASH_SZ);
  hash_label_free(label_hash, LBL_HASH_SZ);
}

void BuddyDumpPools(FILE *output)
{
#ifndef _MONOTHREAD_MEMALLOC
#ifndef _NO_BLOCK_PREALLOC
  struct prealloc_pool *pool;
  P(ContextListMutex);
  pool = first_pool;
  fprintf(output, "Num Blocks  Num/Block  Size of Entry  Num Allocated  Num in Use  Max in Use  Type/Name\n"
                  "----------  ---------  -------------  -------------  ----------  ----------  ------------------------\n");
  while (pool != NULL)
    {
      char *n = pool->pa_type;
      if (pool->pa_name[0] != '\0')
        n = pool->pa_name;
      fprintf(output,
              "%10d  %9d  %13d  %13d  %10d  %10d  %s\n",
              pool->pa_blocks, pool->pa_num, (int) pool->pa_size,
              pool->pa_allocated, pool->pa_used, pool->pa_high,
              n);
      pool = pool->pa_next_pool;
    }
  V(ContextListMutex);
#endif
#endif
}

void BuddyDumpAll(FILE *output)
{
#ifndef _MONOTHREAD_MEMALLOC
  BuddyThreadContext_t *context;
  BuddyBlockPtr_t p_curr_block;
  size_t total = 0, total_used = 0;
  int count = 0;

  P(ContextListMutex);

  fprintf(output, "All Buddy Memory\n");

  for (context = first_context; context != NULL; context = context->next)
    {
      total += context->Stats.TotalMemSpace;
      total_used += context->Stats.StdUsedSpace + context->Stats.ExtraMemSpace;
      count++;

      fprintf(output, "\nMemory Context for thread %s (%p) Total Mem Space: %lld MB Used: %lld MB\n",
              context->label_thread,
              (void *) context->OwnerThread,
              (unsigned long long) context->Stats.TotalMemSpace / 1024 / 1024,
              (unsigned long long) (context->Stats.StdUsedSpace + context->Stats.ExtraMemSpace) / 1024 / 1024);

      fprintf(output, "\n-SIZE-  ---USED--- -------------------LABEL-------------------\n");

      for(p_curr_block = context->p_allocated;
          p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
        {
          size_t size, used;

          if(IS_EXTRA_BLOCK(p_curr_block))
            {
              size = p_curr_block->Header.ExtraInfo;
              used = size - size_header64;
            }
          else
            {
              size = 1 << p_curr_block->Header.StdInfo.k_size;
              used = p_curr_block->Header.StdInfo.user_size - size_header64;
            }

          if (size < 1024)
            fprintf(output, "%6llu", (unsigned long long) size);
          else
            fprintf(output, "%5lluk", (unsigned long long) size / 1024);

          fprintf(output, "%10llu %s:%u:%s:%s\n",
                  (unsigned long long) used,
                  p_curr_block->Header.label_file,
                  p_curr_block->Header.label_line,
                  p_curr_block->Header.label_func,
                  p_curr_block->Header.label_user_defined);;
#ifndef _NO_BLOCK_PREALLOC
          if (p_curr_block->Header.pa_entry != NULL)
            {
              int used = 0;
              prealloc_header *h = p_curr_block->Header.pa_entry;
              prealloc_pool   *p = h->pa_pool;
              while (h != NULL)
                {
                  used += h->pa_inuse;
                  h = h->pa_nextb;
                }
              fprintf(output,
                      "                   Pool=%p Num/Block=%d In Use=%d (Overall Pool Blocks=%d, Allocated=%d, In Use=%d, High=%d)\n",
                      p, p->pa_num, used, p->pa_blocks, p->pa_allocated, p->pa_used, p->pa_high);
            }
#endif
        }
    }

  fprintf(output, "\n%d threads, Total Mem Space: %lld MB, Total Used: %lld MB\n",
          count, (unsigned long long) total / 1024 / 1024, (unsigned long long) total_used / 1024 / 1024);

  V(ContextListMutex);
#endif
}

/* nbr of bytes for 1 char */
#define DISPLAY_SPACE_UNIT (8*1024)

void DisplayMemoryMap(FILE *output)
{
  BuddyBlockPtr_t p_curr_block, p_next_block, p_curr, p_last;
  BuddyBlockPtr_t p_ordered_list = NULL;
  BUDDY_PTRDIFF_T diff;
  unsigned int i;
  unsigned int nb_space, nb_dash;
  int is_first;
  BuddyThreadContext_t *context;

  context = GetThreadContext();

  if(!context)
    return;

  /* first, sort the allocated block list */
  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_next_block)
    {
      /* remember the next block */
      p_next_block = p_curr_block->Header.p_next_allocated;

      p_curr = p_ordered_list;
      p_last = NULL;

      /* insert it at the good location */
      while(p_curr && (p_curr < p_curr_block))
        {
          p_last = p_curr;
          p_curr = p_curr->Header.p_next_allocated;
        }

      if(!p_last)
        {
          /* insert as the first */
          p_curr_block->Header.p_next_allocated = p_ordered_list;
          p_ordered_list = p_curr_block;
        }
      else
        {
          /* insert after p_last */
          p_curr_block->Header.p_next_allocated = p_last->Header.p_next_allocated;
          p_last->Header.p_next_allocated = p_curr_block;
        }

    }

  /* finally, replace the old list */
  context->p_allocated = p_ordered_list;

  is_first = TRUE;              /* indicates that this is the first block of the list */

  /* now print the allocation map */

  for(p_curr_block = context->p_allocated;
      p_curr_block != NULL; p_curr_block = p_curr_block->Header.p_next_allocated)
    {

      /* do not display extra blocks */
      if(IS_EXTRA_BLOCK(p_curr_block))
        {
          fprintf(output, "Extra block: [ size=%lu ]\n",
                    (unsigned long)p_curr_block->Header.ExtraInfo);
          is_first = TRUE;
          continue;
        }

      /* is it the first block of the page ? */
      if(is_first)
        {
          diff =
              (BUDDY_PTRDIFF_T) p_curr_block -
              (BUDDY_PTRDIFF_T) (p_curr_block->Header.Base_ptr);

          /* which size ? */
          nb_space = diff / DISPLAY_SPACE_UNIT;
          if(diff > 0)
            nb_space++;

          for(i = 0; i < nb_space; i++)
            fprintf(output, " ");

          is_first = FALSE;
        }

      /* display block according to its size */

      if((1 << p_curr_block->Header.StdInfo.k_size) < DISPLAY_SPACE_UNIT)
        fprintf(output, "|");
      else
        {
          nb_space =
              ((1 << p_curr_block->Header.StdInfo.k_size) / DISPLAY_SPACE_UNIT) - 1;
          nb_dash =
              ((1 << p_curr_block->Header.StdInfo.k_size) -
               p_curr_block->Header.StdInfo.user_size) / DISPLAY_SPACE_UNIT;

          fprintf(output, "[");
          for(i = 0; i < nb_space - nb_dash; i++)
            fprintf(output, "#");
          for(i = 0; i < nb_dash; i++)
            fprintf(output, ".");

          fprintf(output, "]");
        }

      /* check next block address */

      if(!p_curr_block->Header.p_next_allocated)
        {
          is_first = TRUE;
          fprintf(output, "\n");
          return;
        }
      else if(p_curr_block->Header.Base_ptr !=
              p_curr_block->Header.p_next_allocated->Header.Base_ptr)
        {
          /* another page or extra block */
          is_first = TRUE;
          fprintf(output, "\n");
        }
      else
        {
          diff =
              (char *) p_curr_block->Header.p_next_allocated - (char *) p_curr_block -
              (1 << p_curr_block->Header.StdInfo.k_size);

          /* which size ? */
          nb_space = (diff / DISPLAY_SPACE_UNIT);
          if(diff > 0)
            nb_space++;

          for(i = 0; i < nb_space; i++)
            fprintf(output, " ");
        }
    }
}                               /* DisplayMemoryMap */

#endif

/**
 *  test memory corruption for a block.
 */
int _BuddyCheck(BUDDY_ADDR_T ptr, int other_thread_ok, const char *label)
{
  BuddyBlock_t *p_block;
  BuddyThreadContext_t *context;

  /* return 0 if ptr is NULL. */
  if(!ptr)
    {
      LogWarn(COMPONENT_MEMALLOC,
              "BuddyCheck %s is NULL",
              label);
      return 0;
    }

  /* Ensure thread safety. */
  context = GetThreadContext();

  /* Something very wrong occured !! */
  if(!context)
    {
      LogWarn(COMPONENT_MEMALLOC,
              "BuddyCheck %s %p invalid context",
              label, ptr);
      return 0;
    }

  /* Not initialized */
  if(!context->initialized)
    {
      context->Errno = BUDDY_ERR_NOTINIT;
      return 0;
    }

  /* retrieves block address */
  p_block = (BuddyBlock_t *) (ptr - size_header64);

  /* check magic number consistency */
  switch (p_block->Header.status)
    {
    case FREE_BLOCK:
      /* check for magic number */
      if(isBadMagicNumber("BuddyCheck (FREE BLOCK):", context, p_block, MAGIC_NUMBER_FREE, 1, label))
        {
          /* doing nothing is safer !!! */
          return 0;
        }
      break;

    case RESERVED_BLOCK:
      /* check for magic number */
      if(isBadMagicNumber("BuddyCheck (RESERVED BLOCK):", context, p_block, MAGIC_NUMBER_USED, 1, label))
        {
          /* doing nothing is safer !!! */
          return 0;
        }
      break;

    default:
      /* Invalid Header status : may not be allocated using BuddyMalloc */
      LogMajor(COMPONENT_MEMALLOC,
               "BuddyCheck: %s pointer %p is not a buddy block !!!",
               label, ptr);
      log_bad_block("BuddyCheck:", context, p_block, 0, 0);
      return 0;
    }

  /* is it already free ? */
  if(p_block->Header.status == FREE_BLOCK)
    {
      LogWarn(COMPONENT_MEMALLOC,
              "BuddyCheck: %s Block %p is already free or has been set to 0",
              label, ptr);
      log_bad_block("BuddyCheck:", context, p_block, 1, 1);
      return 0;
    }

  /* Std blocks sanity checks */
  if(!IS_EXTRA_BLOCK(p_block) &&
     (((BUDDY_ADDR_T) p_block < p_block->Header.Base_ptr) ||
     ((BUDDY_ADDR_T) p_block > p_block->Header.Base_ptr +
      (1 << p_block->Header.StdInfo.Base_kSize))))
    {
      context->Errno = BUDDY_ERR_EINVAL;
      LogWarn(COMPONENT_MEMALLOC,
              "BuddyCheck: %s Block %p may be corrupted)",
              label, p_block);
      return 0;
    }

  if(!other_thread_ok && p_block->Header.OwnerThread != pthread_self())
    {
      LogWarn(COMPONENT_MEMALLOC,
              "BuddyCheck: %s Block %p has been allocated by another thread !!!! (%p<>%p)",
              label, p_block, (BUDDY_ADDR_T) p_block->Header.OwnerThread,
              (BUDDY_ADDR_T) pthread_self());
      log_bad_block("BuddyCheck:", context, p_block, 1, 1);
      return 0;
    }

  LogInfo(COMPONENT_MEMALLOC,
          "BuddyCheck %s %p check out ok",
          label, ptr);

  return 1;
}                               /* BuddyCheck */
