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
 * \file    LRU_List.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:21 $
 * \version $Revision: 1.28 $
 * \brief   Management of the thread safe LRU lists.
 *
 * LRU_List.h :Management of the thread safe LRU lists.
 *
 *
 */

#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include "stuff_alloc.h"

typedef enum LRU_List_state__
{ LRU_ENTRY_BLANK = 0,
  LRU_ENTRY_VALID = 1,
  LRU_ENTRY_INVALID = 2
} LRU_List_state_t;

typedef struct LRU_data__
{
  caddr_t pdata;
  size_t len;
} LRU_data_t;

typedef struct lru_entry__
{
  struct lru_entry__ *next;
  struct lru_entry__ *prev;
  LRU_List_state_t valid_state;
  LRU_data_t buffdata;
} LRU_entry_t;

typedef struct lru_param__
{
  unsigned int nb_entry_prealloc;                  /**< Number of node to allocated when new nodes are necessary. */
  unsigned int nb_call_gc_invalid;                 /**< How many call before garbagging invalid entries           */
  int (*entry_to_str) (LRU_data_t, char *);        /**< Function used to convert an entry to a string. */
  int (*clean_entry) (LRU_entry_t *, void *);      /**< Function used for cleaning an entry while released */
  char *name;                                      /**< Name for LRU list */
} LRU_parameter_t;

typedef struct lru_list__
{
  LRU_entry_t *LRU;
  LRU_entry_t *MRU;
  unsigned int nb_entry;
  unsigned int nb_invalid;
  unsigned int nb_call_gc;
  LRU_parameter_t parameter;
  struct prealloc_pool lru_entry_pool;
} LRU_list_t;

typedef int LRU_status_t;

LRU_entry_t *LRU_new_entry(LRU_list_t * plru, LRU_status_t * pstatus);
LRU_list_t *LRU_Init(LRU_parameter_t lru_param, LRU_status_t * pstatus);
int LRU_gc_invalid(LRU_list_t * plru, void *cleanparam);
int LRU_invalidate(LRU_list_t * plru, LRU_entry_t * pentry);
int LRU_invalidate_by_function(LRU_list_t * plru,
                               int (*testfunc) (LRU_entry_t *, void *addparam),
                               void *addparam);
int LRU_apply_function(LRU_list_t * plru, int (*myfunc) (LRU_entry_t *, void *addparam),
                       void *addparam);
void LRU_Print(LRU_list_t * plru);

/* How many character used to display a key or value */
#define LRU_DISPLAY_STRLEN 1024

/* Possible errors */
#define LRU_LIST_SUCCESS           0
#define LRU_LIST_MALLOC_ERROR      1
#define LRU_LIST_EMPTY_LIST        2
#define LRU_LIST_BAD_RELEASE_ENTRY 3

#define LRU_LIST_SET_INVALID        0
#define LRU_LIST_DO_NOT_SET_INVALID 1
#endif                          /* _LRU_LIST_H */
