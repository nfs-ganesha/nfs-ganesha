/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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

#include "config_parsing.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define CONF_LABEL_BUDDY          "BUDDY_MALLOC"

/** Returned values for buddy init or for BuddyErrno. */

#define BUDDY_SUCCESS         0
#define BUDDY_ERR_ENOENT       ENOENT
#define BUDDY_ERR_EINVAL      EINVAL
#define BUDDY_ERR_EFAULT      EFAULT

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

typedef struct buddy_paremeter__ {

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

  /* memory error file */
  char buddy_error_file[256];

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
 * If no memory is available, it stops current processus.
 */
BUDDY_ADDR_T BuddyMallocExit(size_t Size);

/**
 *  Free a memory block alloced by BuddyMalloc, BuddyCalloc or BuddyCalloc.
 */
void BuddyFree(BUDDY_ADDR_T ptr);

BUDDY_ADDR_T BuddyRealloc(BUDDY_ADDR_T ptr, size_t Size);

BUDDY_ADDR_T BuddyCalloc(size_t NumberOfElements, size_t ElementSize);

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
typedef struct buddy_stats__ {

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
                                   const char *function, const unsigned int line);

BUDDY_ADDR_T BuddyCalloc_Autolabel(size_t NumberOfElements, size_t ElementSize,
                                   const char *file,
                                   const char *function, const unsigned int line);

BUDDY_ADDR_T BuddyRealloc_Autolabel(BUDDY_ADDR_T ptr, size_t Size,
                                    const char *file,
                                    const char *function, const unsigned int line);

/**
 *  Set a label for allocated areas, for debugging.
 */
int _BuddySetDebugLabel(const char *file, const char *func, const unsigned int line,
                        const char *label);

#define BuddySetDebugLabel( _user_lbl_ )  _BuddySetDebugLabel(  __FILE__, __FUNCTION__, __LINE__, _user_lbl_ )

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
void BuddyLabelsSummary();

/**
 * Display allocation map, and fragmentation info.
 */
void DisplayMemoryMap();

#endif

#ifdef _DETECT_MEMCORRUPT

/**
 *  test memory corruption for a block.
 *  true if the block is OK,
 *  false else.
 */
int BuddyCheck(BUDDY_ADDR_T ptr);

#endif

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
