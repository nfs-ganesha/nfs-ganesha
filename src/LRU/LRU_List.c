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
 * \file    LRU_List.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/31 09:51:13 $
 * \version $Revision: 1.20 $
 * \brief   Management of the thread safe LRU lists.
 *
 * LRU_List.c :Management of the thread safe LRU lists.
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/LRU/LRU_List.c,v 1.20 2006/01/31 09:51:13 deniel Exp $
 *
 * $Log: LRU_List.c,v $
 * Revision 1.20  2006/01/31 09:51:13  deniel
 * Fixed LRU prev bug
 *
 * Revision 1.19  2006/01/24 08:57:34  leibovic
 * Fixing LRU allocation bug.
 *
 * Revision 1.17  2005/11/10 07:53:24  deniel
 * Corrected some memory leaks
 *
 * Revision 1.16  2005/08/12 07:11:14  deniel
 * Corrected cache_inode_readdir semantics
 *
 * Revision 1.15  2005/07/28 08:25:10  deniel
 * Adding different ifdef statemement for additional debugging
 *
 * Revision 1.14  2005/05/11 15:30:43  deniel
 * Added paramter extended options to LRU for invalid entries gc
 *
 * Revision 1.13  2005/05/10 11:44:02  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.12  2005/02/18 09:35:51  deniel
 * Garbagge collection is ok for file (directory gc is not yet implemented)
 *
 * Revision 1.11  2004/11/23 16:45:00  deniel
 * Plenty of bugs corrected
 *
 * Revision 1.10  2004/11/22 07:49:30  deniel
 * Adding LRU_invalidate_by_function
 *
 * Revision 1.9  2004/10/19 08:41:08  deniel
 * Lots of memory leaks fixed
 *
 * Revision 1.8  2004/10/18 08:42:43  deniel
 * Modifying prototypes for LRU_new_entry
 *
 * Revision 1.7  2004/10/13 13:01:37  deniel
 * Now using the stuff allocator
 *
 * Revision 1.6  2004/10/04 12:51:49  deniel
 * Bad prototypes changed
 *
 * Revision 1.5  2004/09/23 08:19:25  deniel
 * Doxygenisation des sources
 *
 * Revision 1.4  2004/09/22 08:33:43  deniel
 * Utilisation de preallocation dans LRU
 *
 * Revision 1.3  2004/09/21 12:21:04  deniel
 * Differentiation des differents tests configurables
 * Premiere version clean
 *
 * Revision 1.2  2004/09/20 15:36:18  deniel
 * Premiere implementation, sans prealloc
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include "LRU_List.h"
#include "stuff_alloc.h"
#include "log_macros.h"

#ifndef TRUE /* XXX need base header for such defines (curr. BuddyMalloc.h) */
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


/* ------ This group contains all the functions used to manipulate the LRU from outside this module ----- */

/**
 * @defgroup LRUExportedFunctions
 *@{
 */

/**
 * 
 * LRU_Init: Init the LRU list.
 *
 * Init the Hash Table .
 *
 * @param lru_param A structure of type lru_parameter_t which contains the values used to init the LRU.
 * @param pstatus Pointer to an integer to contain the status for the operation. 
 *
 * @return NULL if init failed, the pointeur to the hashtable otherwise.
 *
 * @see PreAllocEntry
 */

LRU_list_t *LRU_Init(LRU_parameter_t lru_param, LRU_status_t * pstatus)
{
  LRU_list_t *plru = NULL;
  char *name = "Unamed";

  if (lru_param.name != NULL)
    name = lru_param.name;

  /* Sanity check */
  if((plru = (LRU_list_t *) Mem_Alloc(sizeof(LRU_list_t))) == NULL)
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }

  plru->nb_entry = 0;
  plru->nb_invalid = 0;
  plru->nb_call_gc = 0;
  plru->MRU = plru->LRU = NULL;
  plru->parameter = lru_param;

  /* Pre allocate entries */
  MakePool(&plru->lru_entry_pool, lru_param.nb_entry_prealloc, LRU_entry_t, NULL, NULL);
  NamePool(&plru->lru_entry_pool, "%s LRU Entry Pool", name);
  if(!IsPoolPreallocated(&plru->lru_entry_pool))
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }

  *pstatus = LRU_LIST_SUCCESS;
  return plru;
}                               /* LRU_Init */

/**
 * 
 * LRU_invalidate: Tag an entry as invalid. 
 *
 * Tag an entry as invalid, this kind of entry will be put off the LRU (and sent back to the pool) when 
 * a garbagge collection will be performed.
 *
 * @param plru Pointer to the list to be managed.
 * @param pentry Pointer to the entry to be tagged.
 *
 * @return LRU_LIST_SUCCESS if successfull, other values show an error. 
 *
 * @see LRU_gc_invalid
 */
int LRU_invalidate(LRU_list_t * plru, LRU_entry_t * pentry)
{
  if(pentry->valid_state != LRU_ENTRY_INVALID)
    {
      pentry->valid_state = LRU_ENTRY_INVALID;
      plru->nb_invalid += 1;
    }

  return LRU_LIST_SUCCESS;
}                               /* LRU_invalidate */

/**
 * 
 * LRU_new_entry : acquire a new entry from the pool. 
 *
 * acquire a new entry from the pool. If pool is empty, a new chunck is added to complete the operation.
 *
 * @param plru Pointer to the list to be managed.
 * @param pstatus Pointer to an integer to contain the status for the operation. 
 *
 * @return NULL if init failed, the pointeur to the hashtable otherwise.
 *
 * @see PreAllocEntry
 */
LRU_entry_t *LRU_new_entry(LRU_list_t * plru, LRU_status_t * pstatus)
{
  LRU_entry_t *new_entry = NULL;

  LogDebug(COMPONENT_LRU,
           "==> LRU_new_entry: nb_entry = %d nb_entry_prealloc = %d",
           plru->nb_entry, plru->parameter.nb_entry_prealloc);

  GetFromPool(new_entry, &plru->lru_entry_pool, LRU_entry_t);
  if(new_entry == NULL)
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }

  new_entry->valid_state = LRU_ENTRY_VALID;
  new_entry->next = NULL;       /* Entry is added as the MRU entry */

  if(plru->MRU == NULL)
    {
      new_entry->prev = NULL;
      plru->LRU = new_entry;
    }
  else
    {
      new_entry->prev = plru->MRU;
      plru->MRU->next = new_entry;
    }

  plru->nb_entry += 1;
  plru->nb_call_gc += 1;
  plru->MRU = new_entry;

  *pstatus = LRU_LIST_SUCCESS;
  return new_entry;
}                               /* LRU_new_entry */

/**
 * 
 * LRU_gc_invalid : garbagge collection for invalid entries.
 *
 * Read the whole LRU list and put the invalid entries back to the pool.
 *
 * @param plru Pointer to the list to be managed.
 * @return An integer to contain the status for the operation. 
 *
 * @see LRU_invalidate
 */
int LRU_gc_invalid(LRU_list_t * plru, void *cleanparam)
{
  LRU_entry_t *pentry = NULL;
  LRU_entry_t *pentrynext = NULL;
  int rc = 0;

  if(plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->nb_invalid == 0)
    return LRU_LIST_SUCCESS;    /* Nothing to be done in this case */

  if(plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->MRU->prev == NULL)   /* One entry only, returns success (the MRU cannot be invalid) */
    return LRU_LIST_SUCCESS;

  /* Do nothing if not enough calls were done */
  if(plru->nb_call_gc < plru->parameter.nb_call_gc_invalid)
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;

  for(pentry = plru->LRU; pentry != plru->MRU; pentry = pentrynext)
    {
      pentrynext = pentry->next;

      if(pentry->valid_state == LRU_ENTRY_INVALID)
        {
          if(plru->parameter.clean_entry(pentry, cleanparam) != 0)
            {
              LogDebug(COMPONENT_LRU, "Error cleaning pentry %p", pentry);
              rc = LRU_LIST_BAD_RELEASE_ENTRY;
            }

          if(pentry->prev != NULL)
            pentry->prev->next = pentry->next;
          else
            plru->LRU = pentry->next;

          if(pentry->next != NULL)
            pentry->next->prev = pentry->prev;
          else
            LogCrit(COMPONENT_LRU,
                    "SHOULD Never appear  !!!! line %d file %s",
                    __LINE__, __FILE__);
          plru->nb_entry -= 1;
          plru->nb_invalid -= 1;

          /* Put it back to pre-allocated pool */
          ReleaseToPool(pentry, &plru->lru_entry_pool);
        }
    }

  return rc;
}                               /* LRU_gc_invalid */

/**
 *
 * LRU_invalidate_by_function: Browse the lru to test if entries should ne invalidated.
 *
 * Browse the lru to test if entries should ne invalidated. This function is used for garbagge collection
 *
 * @param plru [INOUT] LRU list to be managed.
 * @param testfunc [IN] function used to identify an entry to be tagged invalid. This function returns TRUE if entry will be tagged invalid
 * @param addparam [IN] parameter for the input function.
 *
 * @return LRU_LIST_SUCCESS if ok, other values shows an error 
 *
 * @see LRU_invalidate
 * @see LRU_gc_invalid
 *
 */
int LRU_invalidate_by_function(LRU_list_t * plru,
                               int (*testfunc) (LRU_entry_t *, void *addparam),
                               void *addparam)
{
  LRU_entry_t *pentry = NULL;
  LRU_entry_t *pentry_next = NULL;
  int rc = 0;

  if(plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->nb_entry == 0)
    return LRU_LIST_SUCCESS;    /* Nothing to be done in this case */

  if(plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->MRU->prev == NULL)   /* One entry only, returns success (the MRU cannot be set invalid) */
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;

  for(pentry = plru->LRU; pentry != plru->MRU; pentry = pentry_next)
    {
      pentry_next = pentry->next;
      if(pentry->valid_state != LRU_ENTRY_INVALID)
        {
          /* Use test function on the entry to know if it should be set invalid or not */
          if(testfunc(pentry, addparam) == LRU_LIST_SET_INVALID)
            {
              if((rc = LRU_invalidate(plru, pentry)) != LRU_LIST_SUCCESS)
                break;          /* end of loop, error will be returned */
            }
        }
    }

  return rc;
}                               /* LRU_invalidate_by_function */

/**
 *
 * LRU_apply_function: apply the same function to every LRU entry, but do not change their states.
 *
 * apply the same function to every LRU entry, but do not change their states.
 * 
 * @param plru [INOUT] LRU list to be managed.
 * @param myfunc [IN] function used to be runned on every entry. If this function return FALSE, the loop stops.
 * @param addparam [IN] parameter for the input function.
 *
 * @return LRU_LIST_SUCCESS if ok, other values shows an error
 *
 * @see LRU_invalidate
 * @see LRU_gc_invalid
 *
 */

int LRU_apply_function(LRU_list_t * plru, int (*myfunc) (LRU_entry_t *, void *addparam),
                       void *addparam)
{
  LRU_entry_t *pentry = NULL;
  int rc = 0;

  if(plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->nb_entry == 0)
    return LRU_LIST_SUCCESS;    /* Nothing to be done in this case */

  if(plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if(plru->MRU->prev == NULL)   /* One entry only, returns success (the MRU cannot be set invalid) */
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;
  for(pentry = plru->MRU->prev; pentry != NULL; pentry = pentry->prev)
    {
      if(pentry->valid_state != LRU_ENTRY_INVALID)
        {
          /* Use test function on the entry to know if it should be set invalid or not */
          if(myfunc(pentry, addparam) == FALSE)
            {
              break;            /* end of loop */
            }
        }
    }                           /* for */

  return rc;

}                               /* LRU_apply_function */

/**
 * 
 * LRU_Print: Print information about the LRU (mostly for debugging purpose).
 *
 * Print information about the LRU (mostly for debugging purpose).
 *
 * @param plru the LRU to be used.
 *
 * @return none (returns void).
 *
 */
void LRU_Print(LRU_list_t * plru)
{
  LRU_entry_t *pentry = NULL;
  char dispdata[LRU_DISPLAY_STRLEN];

  for(pentry = plru->LRU; pentry != NULL; pentry = pentry->next)
    {
      plru->parameter.entry_to_str(pentry->buffdata, dispdata);
      LogFullDebug(COMPONENT_LRU,
                   "Entry value = %s, valid_state = %d",
                   dispdata, pentry->valid_state);
    }
  LogFullDebug(COMPONENT_LRU, "-----------------------------------------");
}                               /* LRU_Print */

/* @} */
