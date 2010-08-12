#ifndef _ERR_HASHTABLE_H
#define _ERR_HASHTABLE_H

#include "log_macros.h"
#include "HashTable.h"

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
 * \file    err_HashTable.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.20 $
 * \brief   Definition des erreur des tables de hachage.
 * 
 * err_HashTable.h : Definition des erreur des tables de hachage.
 *
 *
 */
    static family_error_t __attribute__ ((__unused__)) tab_errctx_hash[] =
{
#define ERR_HASHTABLE_NO_ERROR       0
  {
  ERR_HASHTABLE_NO_ERROR, "ERR_HASHTABLE_NO_ERROR", "Success"},
#define ERR_HASHTABLE_GET            1
  {
  ERR_HASHTABLE_GET, "ERR_HASHTABLE_GET", "Error when getting an entry"},
#define ERR_HASHTABLE_SET            2
  {
  ERR_HASHTABLE_SET, "ERR_HASHTABLE_SET", "Error when setting an entry"},
#define ERR_HASHTABLE_DEL            3
  {
  ERR_HASHTABLE_DEL, "ERR_HASHTABLE_DEL", "Error while deleting an entry"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

static family_error_t __attribute__ ((__unused__)) tab_errstatus_hash[] =
{
  {
  HASHTABLE_SUCCESS, "HASHTABLE_SUCCESS", "Success"},
  {
  HASHTABLE_UNKNOWN_HASH_TYPE, "HASHTABLE_UNKNOWN_HASH_TYPE", "Unknown hash type"},
  {
  HASHTABLE_INSERT_MALLOC_ERROR, "HASHTABLE_INSERT_MALLOC_ERROR",
        "Malloc error at insert time"},
  {
  HASHTABLE_ERROR_NO_SUCH_KEY, "HASHTABLE_ERROR_NO_SUCH_KEY", "No such key"},
  {
  HASHTABLE_ERROR_KEY_ALREADY_EXISTS, "HASHTABLE_ERROR_KEY_ALREADY_EXISTS",
        "Entry of that key already exists"},
  {
  HASHTABLE_ERROR_INVALID_ARGUMENT, "HASHTABLE_ERROR_INVALID_ARGUMENT",
        "Invalid argument"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif                          /* _ERR_HASHTABLE_H */
