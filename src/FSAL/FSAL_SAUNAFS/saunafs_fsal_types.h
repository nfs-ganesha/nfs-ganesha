/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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

#include "fsal_api.h"

#include "fileinfo_cache.h"
#include "saunafs/saunafs_c_api.h"

#define SAUNAFS_VERSION(major, minor, micro) \
		(0x010000 * major + 0x0100 * minor + micro)
#define kDisconnectedChunkServerVersion SAUNAFS_VERSION(256, 0, 0)

#define SFS_NAME_MAX 255

static const int kNFS4_ERROR = -1;

/* Global SaunaFS constants */
#define SFSBLOCKSIZE 65536
#define SFSBLOCKSINCHUNK 1024
#define SFSCHUNKSIZE (SFSBLOCKSIZE * SFSBLOCKSINCHUNK)

#define SPECIAL_INODE_BASE 0xFFFFFFF0U
#define SPECIAL_INODE_ROOT 0x01U
#define MAX_REGULAR_INODE  (SPECIAL_INODE_BASE - 0x01U)

#define SAUNAFS_SUPPORTED_ATTRS                                           \
	(ATTR_TYPE | ATTR_SIZE | ATTR_FSID | ATTR_FILEID | ATTR_MODE |        \
	 ATTR_NUMLINKS | ATTR_OWNER | ATTR_GROUP | ATTR_ATIME | ATTR_CTIME |  \
	 ATTR_MTIME | ATTR_CHANGE | ATTR_SPACEUSED | ATTR_RAWDEV | ATTR_ACL | \
	 ATTR4_XATTR)

#define SAUNAFS_BIGGEST_STRIPE_COUNT 4096
#define SAUNAFS_STD_CHUNK_PART_TYPE  0
#define SAUNAFS_EXPECTED_BACKUP_DS_COUNT 3
#define TCP_PROTO_NUMBER 6

typedef sau_fileinfo_t fileinfo_t;

/**
 * @struct SaunaFSModule saunafs_fsal_types.h [saunafs_fsal_types.h]
 *
 * @brief SaunaFS Main global module object.
 *
 * SaunaFSModule contains the global module object
 * operations vector and parameters of the filesystem info.
 */
struct SaunaFSModule {
	struct fsal_module fsal;
	struct fsal_obj_ops handleOperations;
	fsal_staticfsinfo_t filesystemInfo;
};

extern struct SaunaFSModule SaunaFS;

struct SaunaFSHandle;

/**
 * @struct SaunaFSExport saunafs_fsal_types.h [saunafs_fsal_types.h]
 *
 * @brief SaunaFS private export object.
 *
 * SaunaFSExport contains information related with the export,
 * the filesystem operations, the parameters used to connect
 * to the master server, the cache used and the pNFS support.
 */
struct SaunaFSExport {
	struct fsal_export export; /* Export object */
	struct SaunaFSHandle *root; /* root handle of export */

	sau_t *fsInstance; /* Filesystem instance */
	sau_init_params_t parameters; /* Initial parameters */
	FileInfoCache_t *cache; /* Export cache */

	bool pnfsMdsEnabled; /* pNFS Metadata Server enabled */
	bool pnfsDsEnabled;  /* pNFS Data Server enabled */

	uint32_t cacheTimeout; /* Timeout for entries at cache */
	uint32_t cacheMaximumSize; /* Maximum size of cache */
};

/**
 * @struct SaunaFSFd saunafs_fsal_types.h [saunafs_fsal_types.h]
 *
 * @brief SaunaFS FSAL file descriptor.
 *
 * SaunaFSFd works as a container to manage the information of a SaunaFS
 * file descriptor and its flags associated like open and share mode.
 */
struct SaunaFSFd {
	struct fsal_fd fsalFd; /* The open and share mode plus fd management */
	struct sau_fileinfo *fd; /* SaunaFS file descriptor */
};

/**
 * @struct SaunaFSStateFd saunafs_fsal_types.h [saunafs_fsal_types.h]
 *
 * @brief Associates a single NFSv4 state structure with a file descriptor.
 */
struct SaunaFSStateFd {
	/* state MUST be first to use default free_state */
	struct state_t state; /* Structure representing a single NFSv4 state */
	struct SaunaFSFd saunafsFd; /* SaunaFS file descriptor */
};

struct SaunaFSHandleKey {
	uint16_t moduleId; /* module id */
	uint16_t exportId; /* export id */
	sau_inode_t inode; /* inode */
};

/**
 * @struct SaunaFSHandle saunafs_fsal_types.h [saunafs_fsal_types.h]
 *
 * @brief SaunaFS FSAL handle.
 *
 * SaunaFSHandle contains information related with the public structure of the
 * filesystem and its operations.
 */
struct SaunaFSHandle {
	struct fsal_obj_handle handle; /* Public handle */
	struct SaunaFSFd fd; /* SaunaFS FSAL file descriptor */
	sau_inode_t inode; /* inode of file */
	struct SaunaFSHandleKey key; /* Handle key */
	struct SaunaFSExport *export; /* Export to which the handle belongs */
	struct fsal_share share; /* The ref counted share reservation state */
};

struct DSWire {
	uint32_t inode; /* inode */
};

struct DataServerHandle {
	struct fsal_ds_handle handle; /* Public Data Server handle */
	uint32_t inode; /* inode */
	FileInfoEntry_t *cacheHandle; /* Cache entry for inode */
};
