/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
   Copyright 2017 Skytechnology sp. z o.o.
   Copyright 2023 Leil Storage OÜ

   This file is part of SaunaFS.

   SaunaFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   SaunaFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SaunaFS. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "saunafs/saunafs_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef sau_fileinfo_t fileinfo_t;

typedef struct FileInfoCache FileInfoCache_t;
typedef struct FileInfoEntry FileInfoEntry_t;

/*!
 * \brief Create fileinfo cache
 * \param maxEntries              Max number of entries to be stored in cache
 * \param minTimeoutMilliseconds  Entries will not be removed until at least
 *                                minTimeoutMilliseconds has passed
 * \return                        pointer to FileInfoCache_t structure
 * \post                          Destroy with destroyFileInfoCache function
 */
FileInfoCache_t *createFileInfoCache(unsigned int maxEntries,
				     int minTimeoutMilliseconds);

/*!
 * \brief Reset cache parameters
 * \param cache                   Cache to be modified
 * \param maxEntries              Maximum number of entries to store in cache
 * \param minTimeoutMilliseconds  Entries will not be removed until at least
 *                                minTimeoutMilliseconds has passed
 */
void resetFileInfoCacheParameters(FileInfoCache_t *cache,
				  unsigned int maxEntries,
				  int minTimeoutMilliseconds);

/*!
 * \brief Destroy fileinfo cache
 * \param cache                   Pointer returned from createFileInfoCache
 */
void destroyFileInfoCache(FileInfoCache_t *cache);

/*!
 * \brief Acquire fileinfo from cache
 * \param cache                    Cache to be modified
 * \param inode                    Inode of a file
 * \return                         Cache entry if succeeded,
 *                                 NULL if cache is full
 * \attention entry->fileinfo will be NULL if file still needs to be open first
 * \post Set fileinfo to a valid pointer after opening a file with
 *       sau_attach_fileinfo
 */
FileInfoEntry_t *acquireFileInfoCache(FileInfoCache_t *cache,
				      sau_inode_t inode);

/*!
 * \brief Release fileinfo from cache
 * \param cache                    Cache to be modified
 * \param entry                    Pointer returned from previous acquire() call
 */
void releaseFileInfoCache(FileInfoCache_t *cache, FileInfoEntry_t *entry);

/*!
 * \brief Erase acquired entry
 * \param cache                    Cache to be modified
 * \param entry                    Pointer returned from previous acquire() call
 * \attention This function should be used if entry should not reside in cache
 * (i.e. opening a file failed)
 */
void eraseFileInfoCache(FileInfoCache_t *cache, FileInfoEntry_t *entry);

/*!
* \brief Get expired fileinfo from cache
* \param cache                     Cache to be modified
* \return entry removed from cache
* \post use this entry to call release() on entry->fileinfo and free entry
* afterwards with fileInfoEntryFree
*/
FileInfoEntry_t *popExpiredFileInfoCache(FileInfoCache_t *cache);

/*!
 * \brief Free unused fileinfo cache entry
 * \param entry                    Entry to be freed
 */
void fileInfoEntryFree(FileInfoEntry_t *entry);

/*!
* \brief Get fileinfo from cache entry
* \param cache                     Cache to be modified
* \return fileinfo extracted from entry
*/
fileinfo_t *extractFileInfo(FileInfoEntry_t *entry);

/*!
* \brief Attach fileinfo to an existing cache entry
* \param entry                     Entry to be modified
* \param fileinfo                  Fileinfo to be attached to entry
*/
void attachFileInfo(FileInfoEntry_t *entry, fileinfo_t *fileinfo);

#ifdef __cplusplus
}
#endif
