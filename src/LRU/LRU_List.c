/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Revision 1.18  2005/11/28 17:02:39  deniel
 * Added CeCILL headers
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
#include "BuddyMalloc.h"
#include "LRU_List.h"
#include "stuff_alloc.h"

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

  /* Sanity check */
  if ((plru = (LRU_list_t *) Mem_Alloc(sizeof(LRU_list_t))) == NULL)
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }

  plru->nb_entry = 0;
  plru->nb_invalid = 0;
  plru->nb_call_gc = 0;
  plru->MRU = plru->LRU = NULL;
  plru->parameter = lru_param;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("LRU_entry_t");
#endif

#ifndef _NO_BLOCK_PREALLOC
  /* Pre allocate entries */
  STUFF_PREALLOC(plru->entry_prealloc, lru_param.nb_entry_prealloc, LRU_entry_t, next);
  if (plru->entry_prealloc == NULL)
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }
#endif

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  *pstatus = LRU_LIST_SUCCESS;
  return plru;
}				/* LRU_Init */

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
  if (pentry->valid_state != LRU_ENTRY_INVALID)
    {
      pentry->valid_state = LRU_ENTRY_INVALID;
      plru->nb_invalid += 1;
    }

  return LRU_LIST_SUCCESS;
}				/* LRU_invalidate */

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

#ifdef _DEBUG_LRU
  printf("==> LRU_new_entry: nb_entry = %d nb_entry_prealloc = %d\n", plru->nb_entry,
	 plru->parameter.nb_entry_prealloc);
#endif

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("LRU_entry_t");
#endif

  GET_PREALLOC(new_entry, plru->entry_prealloc, plru->parameter.nb_entry_prealloc,
	       LRU_entry_t, next);
  if (new_entry == NULL)
    {
      *pstatus = LRU_LIST_MALLOC_ERROR;
      return NULL;
    }
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  new_entry->valid_state = LRU_ENTRY_VALID;
  new_entry->next = NULL;	/* Entry is added as the MRU entry */

  if (plru->MRU == NULL)
    {
      new_entry->prev = NULL;
      plru->LRU = new_entry;
    } else
    {
      new_entry->prev = plru->MRU;
      plru->MRU->next = new_entry;
    }

  plru->nb_entry += 1;
  plru->nb_call_gc += 1;
  plru->MRU = new_entry;

  *pstatus = LRU_LIST_SUCCESS;
  return new_entry;
}				/* LRU_new_entry */

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

  if (plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->nb_invalid == 0)
    return LRU_LIST_SUCCESS;	/* Nothing to be done in this case */

  if (plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->MRU->prev == NULL)	/* One entry only, returns success (the MRU cannot be invalid) */
    return LRU_LIST_SUCCESS;

  /* Do nothing if not enough calls were done */
  if (plru->nb_call_gc < plru->parameter.nb_call_gc_invalid)
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;

  for (pentry = plru->LRU; pentry != plru->MRU; pentry = pentrynext)
    {
      pentrynext = pentry->next;

      if (pentry->valid_state == LRU_ENTRY_INVALID)
	{
	  if (plru->parameter.clean_entry(pentry, cleanparam) != 0)
	    {
#ifdef _DEBUG_LRU
	      printf("Error cleaning pentry %p\n", pentry);
#endif
	      rc = LRU_LIST_BAD_RELEASE_ENTRY;
	    }

	  if (pentry->prev != NULL)
	    pentry->prev->next = pentry->next;
	    else
	    plru->LRU = pentry->next;

	  if (pentry->next != NULL)
	    pentry->next->prev = pentry->prev;
#ifdef _DEBUG_LRU
	    else
	    printf("SHOULD Never appear  !!!! line %d file %s\n", __LINE__, __FILE__);
#endif
	  plru->nb_entry -= 1;
	  plru->nb_invalid -= 1;

	  /* Put it back to pre-allocated pool */
	  RELEASE_PREALLOC(pentry, plru->entry_prealloc, next);
	}
    }

  return rc;
}				/* LRU_gc_invalid */

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

  if (plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->nb_entry == 0)
    return LRU_LIST_SUCCESS;	/* Nothing to be done in this case */

  if (plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->MRU->prev == NULL)	/* One entry only, returns success (the MRU cannot be set invalid) */
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;

  for (pentry = plru->LRU; pentry != plru->MRU; pentry = pentry_next)
    {
      pentry_next = pentry->next;
      if (pentry->valid_state != LRU_ENTRY_INVALID)
	{
	  /* Use test function on the entry to know if it should be set invalid or not */
	  if (testfunc(pentry, addparam) == LRU_LIST_SET_INVALID)
	    {
	      if ((rc = LRU_invalidate(plru, pentry)) != LRU_LIST_SUCCESS)
		break;		/* end of loop, error will be returned */
	    }
	}
    }

  return rc;
}				/* LRU_invalidate_by_function */

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

  if (plru == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->nb_entry == 0)
    return LRU_LIST_SUCCESS;	/* Nothing to be done in this case */

  if (plru->MRU == NULL)
    return LRU_LIST_EMPTY_LIST;

  if (plru->MRU->prev == NULL)	/* One entry only, returns success (the MRU cannot be set invalid) */
    return LRU_LIST_SUCCESS;

  /* From the LRU to the entry BEFORE the MRU */
  rc = LRU_LIST_SUCCESS;
  for (pentry = plru->MRU->prev; pentry != NULL; pentry = pentry->prev)
    {
      if (pentry->valid_state != LRU_ENTRY_INVALID)
	{
	  /* Use test function on the entry to know if it should be set invalid or not */
	  if (myfunc(pentry, addparam) == FALSE)
	    {
	      break;		/* end of loop */
	    }
	}
    }				/* for */

  return rc;

}				/* LRU_apply_function */

/**
 * 
 * HashTable_Print: Print information about the LRU (mostly for debugging purpose).
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

  for (pentry = plru->LRU; pentry != NULL; pentry = pentry->next)
    {
      plru->parameter.entry_to_str(pentry->buffdata, dispdata);
      printf("Entry value = %s, valid_state = %d\n", dispdata, pentry->valid_state);
    }
  printf("-----------------------------------------\n");
}				/* LRU_Print */

/* @} */
