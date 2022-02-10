/*
   Copyright 2017 Skytechnology sp. z o.o.

   This file is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS. If not, see <http://www.gnu.org/licenses/>.
*/

#include "lizardfs/lizardfs_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct liz_fileinfo_cache liz_fileinfo_cache_t;
typedef struct liz_fileinfo_entry liz_fileinfo_entry_t;

/*!
 * \brief Create fileinfo cache
 * \param max_entries max number of entries to be stored in cache
 * \param min_timeout_ms entries will not be removed until at least
 * min_timeout_ms ms has passed
 * \return pointer to fileinfo cache structure
 * \post Destroy with liz_destroy_fileinfo_cache function
 */
liz_fileinfo_cache_t *liz_create_fileinfo_cache(unsigned int max_entries,
						int min_timeout_ms);

/*!
 * \brief Reset cache parameters
 * \param cache The cache to be modified
 * \param max_entries max number of entries to be stored in cache
 * \param min_timeout_ms entries will not be removed until at least
 * min_timeout_ms ms has passed
 */
void liz_reset_fileinfo_cache_params(liz_fileinfo_cache_t *cache,
				     unsigned int max_entries,
				     int min_timeout_ms);

/*!
 * \brief Destroy fileinfo cache
 * \param cache pointer returned from liz_create_fileinfo_cache
 */
void liz_destroy_fileinfo_cache(liz_fileinfo_cache_t *cache);

/*!
* \brief Acquire fileinfo from cache
* \param cache The cache to be modified
* \param inode The inode of a file
* \return Cache entry if succeeded, NULL if cache is full
* \attention entry->fileinfo will be NULL if file still needs to be open first
* \post Set fileinfo to a valid pointer after opening a file with
* liz_attach_fileinfo
*/
liz_fileinfo_entry_t *liz_fileinfo_cache_acquire(liz_fileinfo_cache_t *cache,
						 liz_inode_t inode);

/*!
* \brief Release fileinfo from cache
* \param cache The cache to be modified
* \param entry pointer returned from previous acquire() call
*/
void liz_fileinfo_cache_release(liz_fileinfo_cache_t *cache,
				liz_fileinfo_entry_t *entry);

/*!
* \brief Erase acquired entry
* \attention This function should be used if entry should not reside in cache
* (i.e. opening a file
* failed)
* \param cache The cache to be modified
* \param entry pointer returned from previous acquire() call
*/
void liz_fileinfo_cache_erase(liz_fileinfo_cache_t *cache,
			      liz_fileinfo_entry_t *entry);

/*!
* \brief Get expired fileinfo from cache
* \param cache The cache to be modified
* \return entry removed from cache
* \post use this entry to call release() on entry->fileinfo and free entry
* afterwards with
* liz_fileinfo_entry_free
*/
liz_fileinfo_entry_t *liz_fileinfo_cache_pop_expired(
						liz_fileinfo_cache_t *cache);

/*!
 * \brief Free unused fileinfo cache entry
 * \param entry The entry to be freed
 */
void liz_fileinfo_entry_free(liz_fileinfo_entry_t *entry);

/*!
* \brief Get fileinfo from cache entry
* \param cache The cache to be modified
* \return fileinfo extracted from entry
*/
liz_fileinfo_t *liz_extract_fileinfo(liz_fileinfo_entry_t *entry);

/*!
* \brief Attach fileinfo to an existing cache entry
* \param entry The entry to be modified
* \param fileinfoThe  fileinfo to be attached to entry
*/
void liz_attach_fileinfo(liz_fileinfo_entry_t *entry,
			 liz_fileinfo_t *fileinfo);

#ifdef __cplusplus
}
#endif
