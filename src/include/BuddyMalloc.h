/*
 *
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
 * \file    stuff_alloc.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 12:31:52 $
 * \version $Revision: 1.16 $
 * \brief   Buddy bloc allocator module.
 *
 * BuddyMalloc: Buddy bloc allocator module.
 *
 *
 */

#ifndef _BUDDY_MALLOC_H
#define _BUDDY_MALLOC_H

#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>

#include "config_parsing.h"
#include "log_macros.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define CONF_LABEL_BUDDY          "BUDDY_MALLOC"

/** Returned values for buddy init or for BuddyErrno. */

#define BUDDY_SUCCESS         0
#define BUDDY_ERR_ENOENT      ENOENT
#define BUDDY_ERR_EINVAL      EINVAL
#define BUDDY_ERR_EFAULT      EFAULT

/* trying to destroy a resource that is still in use */
#define BUDDY_ERR_INUSE       EBUSY

/* We may want to differenciate the two error codes:
 * BUDDY_ERR_MALLOC is for system malloc errors.
 * BUDDY_ERR_OUTOFMEM is for buddy malloc errors.
 */
#define BUDDY_ERR_MALLOC      ENOMEM
#define BUDDY_ERR_OUTOFMEM    10001

#define BUDDY_ERR_NOTINIT     20000
#define BUDDY_ERR_ALREADYINIT 20001

/* type to hold addresses in buddy */
#define BUDDY_ADDR_T     caddr_t

/** Return pointer to errno for the current thread. */
int *p_BuddyErrno();

/** The errno variable for current thread. */
#define BuddyErrno ( *p_BuddyErrno() )

/** Configuration for Buddy. */

typedef struct buddy_paremeter__
{

  /* Size of memory areas to manage.
   * This must be large enough compared
   * with the size of asked memory blocks.
   * However, if a BuddyMalloc call is greater
   * that this size, it allocates a memory segment
   * large enough to handle this request
   * (extra_alloc must be set to TRUE).
   */
  size_t memory_area_size;

  /* Indicates if the buddy memory manager can
   * dynamically alloc new pages to meet
   * client's needs.
   */
  int on_demand_alloc;

  /**
   * Indicates if the memory manager can
   * alloc new pages greater than memory_area_size
   * if a client need a block greater than
   * memory_area_size.
   */
  int extra_alloc;

  /* Indicates if the Buddy memory manager
   * can free unused memory areas (garbage collection)
   * (using keep_factor and keep_minimum criterias).
   */
  int free_areas;

  /* Multiplying factor that indicates the number
   * of memory areas to keep even if they are unused.
   * 1 = 1x the number of memory pages currently used.
   * 2 = 2x the number of memory pages currently used.
   * etc...
   * This value applies only to standard pages.
   */
  unsigned int keep_factor;

  /* Indicates the minimum amount of memory areas
   * to be kept.
   * There is no garbage collection if the number
   * of preallocated pages are above this value.
   * This value applies only to standard pages.
   */
  unsigned int keep_minimum;

} buddy_parameter_t;

/**
 * Inits the memory descriptor for current thread.
 * if p_buddy_init_info is NULL, default settings are used.
 */
int BuddyInit(buddy_parameter_t * p_buddy_init_info);

/**
 * BuddyMalloc : memory allocator based on buddy system.
 *
 * The  BuddyMalloc() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures). 
 */
BUDDY_ADDR_T BuddyMalloc(size_t Size);

/**
 * BuddyMallocExit : memory allocator based on buddy system.
 *
 * The  BuddyMallocExit() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures).
 * If no memory is available, it stops current process.
 */
BUDDY_ADDR_T BuddyMallocExit(size_t Size);

/**
 * BuddyStr_Dup : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures). 
 */
char *BuddyStr_Dup(const char * Str);

/**
 * BuddyStr_Dup_Exit : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup_Exit() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures). 
 * If no memory is available, it stops current process.
 */
char *BuddyStr_Dup_Exit(const char * Str);

/**
 *  Free a memory block alloced by BuddyMalloc, BuddyCalloc or BuddyCalloc.
 */
void BuddyFree(BUDDY_ADDR_T ptr);

BUDDY_ADDR_T BuddyRealloc(BUDDY_ADDR_T ptr, size_t Size);

BUDDY_ADDR_T BuddyCalloc(size_t NumberOfElements, size_t ElementSize);

/**
 *  Release all thread resources.
 */
int BuddyDestroy();

/**
 * For pool allocation, the user should know how much entries
 * it can place in a pool, for not wasting memory.
 * E.g. If he wants to allocate <n> entries of size <s>,
 * and if buddy_header + n*s = 2^k + 1, we will need to alloc 2^(k+1) !!!
 * Thus, we give him a value for <n> so that it will fit
 * the 2^(k+1) block size, with no waste ;)
 *
 * \param min_count the min count user wants to alloc
 * \param type_size the size of a single preallocated entry
 */
unsigned int BuddyPreferedPoolCount(unsigned int min_count, size_t type_size);

/**
 *  For debugging.
 *  Print the memory map of the current thread
 *  to a file.
 */
void BuddyDumpMem(FILE * output);

/**
 * Stats structure for a thread.
 */
typedef struct buddy_stats__
{

  /* Total space allocated BuddyMallocExit */

  size_t TotalMemSpace;         /* Current */
  size_t WM_TotalMemSpace;      /* High watermark */

  /* Pages allocated using standard page size */

  size_t StdMemSpace;           /* Total Space used for standard pages */
  size_t WM_StdMemSpace;        /* High watermark for memory used for std pages */

  size_t StdUsedSpace;          /* Total Space really used */
  size_t WM_StdUsedSpace;       /* High watermark for memory used in std pages */

  size_t StdPageSize;           /* Standard Pages size */

  unsigned int NbStdPages;      /* Number of pages (current) */
  unsigned int NbStdUsed;       /* Number of used pages (current) */
  unsigned int WM_NbStdUsed;    /* High watermark for used std pages */

  size_t ExtraMemSpace;         /* Total Space */
  size_t WM_ExtraMemSpace;      /* High watermark for memory used by extra pages */
  size_t MinExtraPageSize;      /* Low water mark size for extra page  */
  size_t MaxExtraPageSize;      /* High water mark size for extra page */
  unsigned int NbExtraPages;    /* Number of extra pages (current) */
  unsigned int WM_NbExtraPages; /* Watermark of extra pages */

} buddy_stats_t;

/**
 *  Get stats for memory use.
 */
void BuddyGetStats(buddy_stats_t * budd_stats);

#ifdef _DEBUG_MEMLEAKS

/**
 * Those functions allocate memory with a file/function/line label
 */
BUDDY_ADDR_T BuddyMalloc_Autolabel(size_t sz,
                                   const char *file,
                                   const char *function,
                                   const unsigned int line,
                                   const char *str);

/**
 * BuddyStr_Dup : string duplicator based on buddy system.
 *
 * The  BuddyStr_Dup() function returns a pointer to a block of at least
 * Size bytes suitably aligned (32 or 64bits depending on architectures). 
 */
inline char *BuddyStr_Dup_Autolabel(const char * OldStr,
                                    const char *file,
                                    const char *function,
                                    const unsigned int line,
                                    const char *str);

BUDDY_ADDR_T BuddyCalloc_Autolabel(size_t NumberOfElements, size_t ElementSize,
                                   const char *file,
                                   const char *function,
                                   const unsigned int line,
                                   const char *str);

BUDDY_ADDR_T BuddyRealloc_Autolabel(BUDDY_ADDR_T ptr, size_t Size,
                                    const char *file,
                                    const char *function,
                                    const unsigned int line,
                                    const char *str);

void BuddyFree_Autolabel(BUDDY_ADDR_T ptr,
                         const char *file,
                         const char *function,
                         const unsigned int line,
                         const char *str);

/**
 *  Retrieves the debugging label for a given block.
 */
const char *BuddyGetDebugLabel(BUDDY_ADDR_T ptr);

/**
 *  Count the number of blocks that were allocated using the given label.
 *  returns a negative error code on error.
 */
int BuddyCountDebugLabel(char *label);

/**
 *  Displays a summary of all allocated blocks
 *  with their labels
 */
void BuddyLabelsSummary(log_components_t component);

/**
 * Display allocation map, and fragmentation info.
 */
void DisplayMemoryMap(FILE *output);
void BuddyDumpAll(FILE *output);
void BuddyDumpPools(FILE *output);

int _BuddyCheck_Autolabel(BUDDY_ADDR_T ptr,
                          int other_thread_ok,
                          const char *file,
                          const char *function,
                          const unsigned int line,
                          const char *str);

#define BuddyCheck(ptr, ok)           _BuddyCheck_Autolabel((BUDDY_ADDR_T) ptr, ok, __FILE__, __FUNCTION__, __LINE__, "BuddyCheck")
#define BuddyCheckLabel(ptr, ok, lbl) _BuddyCheck_Autolabel((BUDDY_ADDR_T) ptr, ok, __FILE__, __FUNCTION__, __LINE__, lbl)
#else
/**
 *  test memory corruption for a block.
 *  true if the block is OK,
 *  false else.
 */

#define BuddyCheck(ptr, ok)           _BuddyCheck((BUDDY_ADDR_T) ptr, ok, "BuddyCheck")
#define BuddyCheckLabel(ptr, ok, lbl) _BuddyCheck((BUDDY_ADDR_T) ptr, ok, lbl)
#endif

/**
 *  test memory corruption for a block.
 *  true if the block is OK,
 *  false else.
 */
int _BuddyCheck(BUDDY_ADDR_T ptr, int other_thread_ok, const char *label);

/**
 * sets default values for buddy configuration structure.
 */
int Buddy_set_default_parameter(buddy_parameter_t * out_parameter);

/**
 * extract values for buddy configuration structure,
 * from configuration file.
 */
int Buddy_load_parameter_from_conf(config_file_t in_config,
                                   buddy_parameter_t * out_parameter);

#endif                          /* _BUDDY_MALLOC_H */
