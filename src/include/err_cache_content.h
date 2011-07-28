#ifndef _ERR_CACHE_CONTENT_H
#define _ERR_CACHE_CONTENT_H

#include "log_macros.h"
#include "cache_content.h"

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
 * \file    err_cache_content.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.2 $
 * \brief   Cache content error definitions.
 * 
 * err_cache_content.h : Cache content error definitions.
 *
 *
 */
    static family_error_t __attribute__ ((__unused__)) tab_errctx_cache_content[] =
{
#define ERR_CACHE_CONTENT_NEW_ENTRY     CACHE_CONTENT_NEW_ENTRY
  {
  ERR_CACHE_CONTENT_NEW_ENTRY, "ERR_CACHE_CONTENT_NEW_ENTRY",
        "Impossible to create a new entry"},
#define ERR_CACHE_CONTENT_RELEASE_ENTRY CACHE_CONTENT_RELEASE_ENTRY
  {
  ERR_CACHE_CONTENT_RELEASE_ENTRY, "ERR_CACHE_CONTENT_RELEASE_ENTRY",
        "Impossible to release an entry"},
#define ERR_CACHE_CONTENT_READ_ENTRY    CACHE_CONTENT_READ_ENTRY
  {
  ERR_CACHE_CONTENT_READ_ENTRY, "ERR_CACHE_CONTENT_READ_ENTRY",
        "Entry could not be read"},
#define ERR_CACHE_CONTENT_WRITE_ENTRY   CACHE_CONTENT_WRITE_ENTRY
  {
  ERR_CACHE_CONTENT_WRITE_ENTRY, "ERR_CACHE_CONTENT_WRITE_ENTRY",
        "Entry could not be written"},
#define ERR_CACHE_CONTENT_TRUNCATE      CACHE_CONTENT_TRUNCATE
  {
  ERR_CACHE_CONTENT_TRUNCATE, "ERR_CACHE_CONTENT_TRUNCATE",
        "Entry could not be truncated"},
#define ERR_CACHE_CONTENT_FLUSH         CACHE_CONTENT_FLUSH
  {
  ERR_CACHE_CONTENT_FLUSH, "ERR_CACHE_CONTENT_FLUSH", "Entry could not be flush to FSAL"},
#define ERR_CACHE_CONTENT_REFRESH       CACHE_CONTENT_REFRESH
  {
  ERR_CACHE_CONTENT_REFRESH, "ERR_CACHE_CONTENT_REFRESH",
        "Entry could not be updated from FSAL"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

static family_error_t __attribute((__unused__)) tab_errstatus_cache_content[] =
{
#define ERR_CACHE_CONTENT_SUCCESS   CACHE_CONTENT_SUCCESS
#define ERR_CACHE_CONTENT_NO_ERROR  ERR_CACHE_CONTENT_SUCCESS
  {
  ERR_CACHE_CONTENT_NO_ERROR, "ERR_CACHE_CONTENT_NO_ERROR", "No error"},
#define ERR_CACHE_CONTENT_INVALID_ARGUMENT  CACHE_CONTENT_INVALID_ARGUMENT
  {
  ERR_CACHE_CONTENT_INVALID_ARGUMENT, "ERR_CACHE_CONTENT_INVALID_ARGUMENT",
        "Invalid argument"},
#define ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY  CACHE_CONTENT_UNAPPROPRIATED_KEY
  {
  ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY, "ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY",
        "Bad key"},
#define ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY CACHE_CONTENT_BAD_CACHE_INODE_ENTRY
  {
  ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY, "ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY",
        "Bad cache inode entry"},
#define ERR_CACHE_CONTENT_ENTRY_EXISTS CACHE_CONTENT_ENTRY_EXISTS
  {
  ERR_CACHE_CONTENT_ENTRY_EXISTS, "ERR_CACHE_CONTENT_ENTRY_EXISTS",
        "Entry already exists"},
#define ERR_CACHE_CONTENT_FSAL_ERROR     CACHE_CONTENT_FSAL_ERROR
  {
  ERR_CACHE_CONTENT_FSAL_ERROR, "ERR_CACHE_CONTENT_FSAL_ERROR", "Unexpected FSAL error"},
#define ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR  CACHE_CONTENT_LOCAL_CACHE_ERROR
  {
  ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR, "ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR",
        "Unexpected local cache error"},
#define ERR_CACHE_CONTENT_MALLOC_ERROR     CACHE_CONTENT_MALLOC_ERROR
  {
  ERR_CACHE_CONTENT_MALLOC_ERROR, "ERR_CACHE_CONTENT_MALLOC_ERROR",
        "resource allocation error"},
#define ERR_CACHE_CONTENT_LRU_ERROR   CACHE_CONTENT_LRU_ERROR
  {
  ERR_CACHE_CONTENT_LRU_ERROR, "ERR_CACHE_CONTENT_LRU_ERROR", "Unexpected LRU error"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif                          /* _ERR_CACHE_CONTENT_H */
