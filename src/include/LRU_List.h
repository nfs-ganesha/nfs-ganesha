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

typedef enum LRU_List_state__ { LRU_ENTRY_BLANK = 0,
  LRU_ENTRY_VALID = 1,
  LRU_ENTRY_INVALID = 2
} LRU_List_state_t;

typedef struct LRU_data__ {
  caddr_t pdata;
  size_t len;
} LRU_data_t;

typedef struct lru_entry__ {
  struct lru_entry__ *next;
  struct lru_entry__ *prev;
  LRU_List_state_t valid_state;
  LRU_data_t buffdata;
} LRU_entry_t;

typedef struct lru_param__ {
  unsigned int nb_entry_prealloc;		   /**< Number of node to allocated when new nodes are necessary. */
  unsigned int nb_call_gc_invalid;		   /**< How many call before garbagging invalid entries           */
  int (*entry_to_str) (LRU_data_t, char *);	   /**< Function used to convert an entry to a string. */
  int (*clean_entry) (LRU_entry_t *, void *);	   /**< Function used for cleaning an entry while released */
} LRU_parameter_t;

typedef struct lru_list__ {
  LRU_entry_t *LRU;
  LRU_entry_t *MRU;
  unsigned int nb_entry;
  unsigned int nb_invalid;
  unsigned int nb_call_gc;
  LRU_parameter_t parameter;
  LRU_entry_t *entry_prealloc;
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
#endif				/* _LRU_LIST_H */
