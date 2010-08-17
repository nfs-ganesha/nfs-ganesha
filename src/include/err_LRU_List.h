#ifndef _ERR_LRU_H
#define _ERR_LRU_H

#include "log_macros.h"

/**
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

 /**/
/**
 * \file    err_LRU_List.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.19 $
 * \brief   Definition des erreur de la liste LRU
 * 
 * err_HashTable.h : Definition des erreur de la liste LRU
 *
 *
 */
    static family_error_t __attribute__ ((__unused__)) tab_errctx_LRU[] =
{
#define ERR_LRU_LIST_NO_ERROR 0
  {
  ERR_LRU_LIST_NO_ERROR, "ERR_LRU_LIST_NO_ERROR", "Pas d'erreur"},
#define ERR_LRU_LIST_INIT     1
  {
  ERR_LRU_LIST_INIT, "ERR_LRU_LIST_INIT", "Erreur a l'initialisation"},
#define ERR_LRU_LIST_GC_INVALID 2
  {
  ERR_LRU_LIST_GC_INVALID, "ERR_LRU_LIST_GC_INVALID", "Erreur dans le gc des invalides"},
#define ERR_LRU_LIST_INVALIDATE 3
  {
  ERR_LRU_LIST_INVALIDATE, "ERR_LRU_LIST_INVALIDATE",
        "Invalidation impossible de l'entree"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

static family_error_t __attribute__ ((__unused__)) tab_errstatus_LRU[] =
{
  {
  LRU_LIST_SUCCESS, "LRU_LISTSUCCESS", "Succes"},
  {
  LRU_LIST_MALLOC_ERROR, "LRU_LIST_MALLOC_ERROR", "Erreur de malloc dans la couche LRU"},
  {
  LRU_LIST_EMPTY_LIST, "LRU_LIST_EMPTY_LIST", "La liste est vide"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif                          /* _ERR_LRU_H */
