// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "gsh_config.h"
#include "saunafs_fsal_types.h"
#include "pnfs_utils.h"
#include <stdlib.h>

#include "context_wrap.h"
#include "saunafs/saunafs_c_api.h"

const int MaxBufferSize = 0x100;
const int ChunkAddressSizeInBytes = 40;
const int ChunkDataOverhead = 32;

/**
 * @brief Comparison function for based-ip sorting of chunkservers.
 *
 * This function sorts chunkserver in ascending order of their ip attribute.
 *
 * @param[in] chunkserverA      void* pointer to chunkserver A
 * @param[in] chunkserverB      void* pointer to chunkserver B
 *
 * @returns: Integer number representing the natural order of chunkserver
 *
 * @retval value < 0: chunkserverA's ip is less than chunkserverB's ip
 * @retval value > 0: chunkserverA's ip is greater than chunkserverB's ip
 * @retval         0: Both chunkservers have the same ip
 */
static int ascendingIpCompare(const void *chunkserverA,
			      const void *chunkserverB)
{
	uint32_t ipFromChunkserverA =
		((const sau_chunkserver_info_t *)chunkserverA)->ip;
	uint32_t ipFromChunkserverB =
		((const sau_chunkserver_info_t *)chunkserverB)->ip;

	return ipFromChunkserverA - ipFromChunkserverB;
}

/**
 * @brief Check if one chunkserver is disconnected.
 *
 * @param[in] chunkserver      void* pointer to chunkserver instance
 *
 * @returns: Integer number representing if chunkserver is disconnected
 *
 * @retval 0: chunkserver is connected
 * @retval 1: chunkserver is disconnected
 */
static int isChunkserverDisconnected(const void *chunkserver, void *unused)
{
	(void)unused;

	return ((const sau_chunkserver_info_t *)chunkserver)->version ==
	       kDisconnectedChunkServerVersion;
}

/**
 * @brief Check if two adjacent chunkservers have the same ip.
 *
 * @param[in] chunkserver      void* pointer to chunkserver instance
 * @param[in] base             void* pointer to the first chunkserver of the
 *                             collection
 *
 * @returns: Integer number representing if chunkserver instance and
 *           its predecessor have the same ip.
 *
 * @retval 0: chunkserver and its predecessor doesn't have the same ip
 * @retval 1: chunkserver and its predecessor have the same ip
 */
static int adjacentChunkserversWithSameIp(const void *chunkserver, void *base)
{
	if (chunkserver == base)
		return 0;

	return ((const sau_chunkserver_info_t *)chunkserver)->ip ==
	       ((const sau_chunkserver_info_t *)chunkserver - 1)->ip;
}

/**
 * @brief Remove chunkservers if predicate is successfully evaluated.
 *
 * @param[in,out] data       void* pointer to the chunkservers collection
 * @param[in] size           Size of the collection
 * @param[in] itemSize       Size of each item of the collection
 * @param[in] predicate      Predicate to evaluate
 * @param[in] targetData     void* pointer to targetData
 *
 * @returns: Number of removed elements.
 */
static size_t remove_if(void *data, size_t size, size_t itemSize,
			int (*predicate)(const void *chunkserver,
					 void *targetData),
			void *targetData)
{
	size_t step = 0;

	for (size_t i = 0; i < size; ++i) {
		if (!predicate((uint8_t *)data + i * itemSize, targetData)) {
			memcpy((uint8_t *)data + i * itemSize,
			       (uint8_t *)data + step * itemSize, itemSize);
			step++;
		}
	}
	return step;
}

/**
 * @brief Randomly rearrange the elements of a collection.
 *
 * @param[in,out] data       void* pointer to the collection
 * @param[in] size           Size of the collection
 * @param[in] itemSize       Size of each item of the collection
 */
static void shuffle(void *data, size_t size, size_t itemSize)
{
	uint8_t temp[itemSize];

	if (size == 0)
		return;

	srandom(time(NULL));
	for (size_t index = 0; index < size - 1; ++index) {
		size_t start = index + random() % (size - index);

		memcpy(temp, (uint8_t *)data + index * itemSize, itemSize);

		memcpy((uint8_t *)data + index * itemSize,
		       (uint8_t *)data + start * itemSize, itemSize);

		memcpy((uint8_t *)data + start * itemSize, temp, itemSize);
	}
}

/**
 * @brief Randomly rearrange the list of chunkservers.
 *
 * @param[in] export                Export where the chunkservers live on
 * @param[in] chunkserverCount      Pointer to store the number of chunkservers
 *
 * @returns: Randomized chunkservers list.
 */
static sau_chunkserver_info_t *
randomizedChunkserverList(struct SaunaFSExport *export,
			  uint32_t *chunkserverCount)
{
	sau_chunkserver_info_t *chunkserverInfo = NULL;

	chunkserverInfo = gsh_malloc(SAUNAFS_BIGGEST_STRIPE_COUNT *
				     sizeof(sau_chunkserver_info_t));

	int retvalue = sau_get_chunkservers_info(export->fsInstance,
						 chunkserverInfo,
						 SAUNAFS_BIGGEST_STRIPE_COUNT,
						 chunkserverCount);

	if (retvalue < 0) {
		*chunkserverCount = 0;
		gsh_free(chunkserverInfo);
		return NULL;
	}

	/* Free labels, we don't need them. */
	sau_destroy_chunkservers_info(chunkserverInfo);

	/* remove disconnected */
	*chunkserverCount = remove_if(chunkserverInfo, *chunkserverCount,
				      sizeof(sau_chunkserver_info_t),
				      isChunkserverDisconnected, NULL);

	/* sorting chunkservers based on its ip attribute */
	qsort(chunkserverInfo, *chunkserverCount,
	      sizeof(sau_chunkserver_info_t), ascendingIpCompare);

	/* remove entries with the same ip */
	*chunkserverCount = remove_if(chunkserverInfo, *chunkserverCount,
				      sizeof(sau_chunkserver_info_t),
				      adjacentChunkserversWithSameIp,
				      chunkserverInfo);

	/* randomize */
	shuffle(chunkserverInfo, *chunkserverCount,
		sizeof(sau_chunkserver_info_t));

	return chunkserverInfo;
}

/**
 * @brief Fill Data Server list with entries corresponding to chunks.
 *
 * @param[in] export                Export where the chunkservers live on
 * @param[in] chunkserverCount      Pointer to store the number of chunkservers
 *
 * @returns: Operation status.
 *
 * @retval   0: Successful operation
 * @retval  -1: Failing operation
 */
static int fillChunkDataServerList(XDR *da_addr_body,
				   sau_chunk_info_t *chunkInfo,
				   sau_chunkserver_info_t *chunkserverInfo,
				   uint32_t chunkCount, uint32_t stripeCount,
				   uint32_t chunkserverCount,
				   uint32_t *chunkserverIndex)
{
	fsal_multipath_member_t host[SAUNAFS_EXPECTED_BACKUP_DS_COUNT];
	uint32_t size = MIN(chunkCount, stripeCount);
	const int upperBound = SAUNAFS_EXPECTED_BACKUP_DS_COUNT;

	for (uint32_t chunkIndex = 0; chunkIndex < size; ++chunkIndex) {
		sau_chunk_info_t *chunk = &chunkInfo[chunkIndex];
		int serverCount = 0;

		memset(host, 0, upperBound * sizeof(fsal_multipath_member_t));

		/* prefer std chunk part type */
		for (size_t i = 0;
		     i < chunk->parts_size && serverCount < upperBound; ++i) {
			if (chunk->parts[i].part_type_id !=
			    SAUNAFS_STD_CHUNK_PART_TYPE) {
				continue;
			}

			host[serverCount].proto = TCP_PROTO_NUMBER;
			host[serverCount].addr = chunk->parts[i].addr;
			host[serverCount].port = NFS_PORT;
			++serverCount;
		}

		for (size_t i = 0;
		     i < chunk->parts_size && serverCount < upperBound; ++i) {
			if (chunk->parts[i].part_type_id ==
			    SAUNAFS_STD_CHUNK_PART_TYPE) {
				continue;
			}

			host[serverCount].proto = TCP_PROTO_NUMBER;
			host[serverCount].addr = chunk->parts[i].addr;
			host[serverCount].port = NFS_PORT;
			++serverCount;
		}

		/* fill unused entries with the servers from randomized
		 * chunkserver list */
		while (serverCount < upperBound) {
			host[serverCount].proto = TCP_PROTO_NUMBER;
			host[serverCount].addr =
				chunkserverInfo[*chunkserverIndex].ip;
			host[serverCount].port = NFS_PORT;

			++serverCount;
			*chunkserverIndex =
				(*chunkserverIndex + 1) % chunkserverCount;
		}

		/* encode ds entry */
		nfsstat4 status = FSAL_encode_v4_multipath(da_addr_body,
							   serverCount, host);

		if (status != NFS4_OK)
			return kNFS4_ERROR;
	}

	return NFS4_OK;
}

/**
 * @brief Fill unused part of DS list with servers from randomized chunkserver
 *        list.
 *
 * @param[in] xdrStream             XDR stream
 * @param[in] chunkserverInfo       Collection of chunkservers
 * @param[in] chunkCount            Number of chunks
 * @param[in] stripeCount           Number of stripe
 * @param[in] chunkserverCount      Number of chunkservers
 * @param[in] chunkserverIndex      Index of chunkserver
 *
 * @returns: Operation status.
 *
 * @retval   0: Successful operation
 * @retval  -1: Failing operation
 */
static int fillUnusedDataServerList(XDR *xdrStream,
				    sau_chunkserver_info_t *chunkserverInfo,
				    uint32_t chunkCount, uint32_t stripeCount,
				    uint32_t chunkserverCount,
				    uint32_t *chunkserverIndex)
{
	fsal_multipath_member_t host[SAUNAFS_EXPECTED_BACKUP_DS_COUNT];
	uint32_t size = MIN(chunkCount, stripeCount);
	const int upperBound = SAUNAFS_EXPECTED_BACKUP_DS_COUNT;

	for (uint32_t chunkIndex = size; chunkIndex < stripeCount;
	     ++chunkIndex) {
		int serverCount = 0;
		int index = 0;

		memset(host, 0, upperBound * sizeof(fsal_multipath_member_t));

		while (serverCount < SAUNAFS_EXPECTED_BACKUP_DS_COUNT) {
			index = (*chunkserverIndex + serverCount) %
				chunkserverCount;

			host[serverCount].proto = TCP_PROTO_NUMBER;
			host[serverCount].addr = chunkserverInfo[index].ip;
			host[serverCount].port = NFS_PORT;

			++serverCount;
		}

		*chunkserverIndex = (*chunkserverIndex + 1) % chunkserverCount;

		nfsstat4 status =
			FSAL_encode_v4_multipath(xdrStream, serverCount, host);

		if (status != NFS4_OK)
			return -1;
	}

	return NFS4_OK;
}

/**
 * @brief Release resources.
 *
 * @param[in] chunkInfo             Chunk information including id, type and all
 *                                  parts
 * @param[in] chunkserverInfo       Chunkserver information including version,
 *                                  ip, port, used space and total space
 */
static void releaseResources(sau_chunk_info_t *chunkInfo,
			     sau_chunkserver_info_t *chunkserverInfo)
{
	if (chunkInfo) {
		sau_destroy_chunks_info(chunkInfo);
		gsh_free(chunkInfo);
	}

	if (chunkserverInfo)
		gsh_free(chunkserverInfo);
}

/**
 * @brief Get information about a pNFS device.
 *
 * pNFS functions. When this function is called, the FSAL should write device
 * information to the da_addr_body stream.
 *
 * The function converts SaunaFS file's chunk information to pNFS device info.
 *
 * Linux pNFS client imposes limit on stripe size (SAUNAFS_BIGGEST_STRIPE_COUNT
 * = 4096). If we would use straight forward approach of converting each chunk
 * to stripe entry, we would be limited to file size of 256 GB (4096 * 64MB).
 *
 * To avoid this problem each DS can read/write data from any chunk (Remember
 * that pNFS client takes DS address from DS list in round robin fashion). Of
 * course it's more efficient if DS is answering queries about chunks residing
 * locally.
 *
 * To achieve the best performance we fill the DS list in a following way:
 *
 * First we prepare randomized list of all chunkservers (RCSL).
 * Then for each chunk we fill multipath DS list entry with addresses of
 * chunkservers storing this chunk. If there is less chunkservers than
 * SAUNAFS_EXPECTED_BACKUP_DS_COUNT then we use chunkservers from RCSL.
 *
 * If we didn't use all the possible space in DS list
 * (SAUNAFS_BIGGEST_STRIPE_COUNT), then we fill rest of the stripe entries with
 * addresses from RCSL (again SAUNAFS_EXPECTED_BACKUP_DS_COUNT addresses for
 * each stripe entry).
 *
 * @param[in]  module         FSAL module
 * @param[out] xdrStream      XDR stream to which the FSAL is to write the
 *                            layout type-specific information corresponding to
 *                            the deviceid.
 * @param[in]  type           The type of layout that specified the device
 * @param[in]  deviceid       The device to look up
 *
 * @returns: Valid error codes in RFC 5661, p. 365.
 */
static nfsstat4 getdeviceinfo(struct fsal_module *module, XDR *xdrStream,
			      const layouttype4 type,
			      const struct pnfs_deviceid *deviceid)
{
	struct fsal_export *exportHandle = NULL;
	struct SaunaFSExport *export = NULL;

	sau_chunk_info_t *chunkInfo = NULL;
	sau_chunkserver_info_t *chunkserverInfo = NULL;

	uint32_t chunkCount = 0;
	uint32_t chunkserverCount = 0;
	uint32_t stripeCount = 0;
	uint32_t chunkserverIndex = 0;

	struct glist_head *glist = NULL;
	struct glist_head *glistn = NULL;

	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x", type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	uint16_t export_id = deviceid->device_id2;

	glist_for_each_safe(glist, glistn, &module->exports) {
		exportHandle = glist_entry(glist, struct fsal_export, exports);

		if (exportHandle->export_id == export_id) {
			export = container_of(exportHandle,
					      struct SaunaFSExport, export);
			break;
		}
	}

	if (!export) {
		LogCrit(COMPONENT_PNFS,
			"Couldn't find export with id: %" PRIu16, export_id);

		return NFS4ERR_SERVERFAULT;
	}

	/* get the chunk list for file */
	chunkInfo = gsh_malloc(SAUNAFS_BIGGEST_STRIPE_COUNT *
			       sizeof(sau_chunk_info_t));

	int retvalue = saunafs_get_chunks_info(
		export->fsInstance, &op_ctx->creds, deviceid->devid, 0,
		chunkInfo, SAUNAFS_BIGGEST_STRIPE_COUNT, &chunkCount);

	if (retvalue < 0) {
		LogCrit(COMPONENT_PNFS,
			"Failed to get SaunaFS layout for export = %" PRIu16
			" inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	chunkserverInfo = randomizedChunkserverList(export, &chunkserverCount);

	if (chunkserverInfo == NULL || chunkserverCount == 0) {
		LogCrit(COMPONENT_PNFS,
			"Failed to get SaunaFS layout for export = %" PRIu16
			" inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	chunkserverIndex = 0;
	stripeCount = MIN(chunkCount + chunkserverCount,
			  SAUNAFS_BIGGEST_STRIPE_COUNT);

	if (!inline_xdr_u_int32_t(xdrStream, &stripeCount)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode device information for export = %"
			PRIu16
			" inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	for (uint32_t chunkIndex = 0; chunkIndex < stripeCount; ++chunkIndex) {
		if (!inline_xdr_u_int32_t(xdrStream, &chunkIndex)) {
			LogCrit(COMPONENT_PNFS,
				"Failed to encode device information for export = %"
				PRIu16 " inode = %" PRIu64,
				export_id, deviceid->devid);

			releaseResources(chunkInfo, chunkserverInfo);
			return NFS4ERR_SERVERFAULT;
		}
	}

	if (!inline_xdr_u_int32_t(xdrStream, &stripeCount)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode device information for export = %"
			PRIu16 " inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	retvalue = fillChunkDataServerList(xdrStream, chunkInfo,
					   chunkserverInfo, chunkCount,
					   stripeCount, chunkserverCount,
					   &chunkserverIndex);

	if (retvalue < 0) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode device information for export = %"
			PRIu16 " inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	retvalue = fillUnusedDataServerList(xdrStream, chunkserverInfo,
					    chunkCount, stripeCount,
					    chunkserverCount,
					    &chunkserverIndex);

	if (retvalue < 0) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode device information for export = %"
			PRIu16 " inode = %" PRIu64,
			export_id, deviceid->devid);

		releaseResources(chunkInfo, chunkserverInfo);
		return NFS4ERR_SERVERFAULT;
	}

	sau_destroy_chunks_info(chunkInfo);

	gsh_free(chunkInfo);
	gsh_free(chunkserverInfo);

	return NFS4_OK;
}

/**
 * @brief Get list of available devices.
 *
 * This function should populate calls cb values representing the low quad of
 * deviceids it wishes to make the available to the caller. it should continue
 * calling cb until cb returns false or it runs out of deviceids to make
 * available.
 *
 * If cb returns false, it should assume that cb has not stored the most recent
 * deviceid and set res->cookie to a value that will begin with the most
 * recently provided.
 *
 * If it wishes to return no deviceids, it may set res->eof to true without
 * calling cb at all.
 *
 * @param[in] exportHandle     Export handle
 * @param[in] type             Type of layout to get devices for
 * @param[in] opaque           Opaque pointer to be passed to callback
 * @param[in] cb               Function taking device ID halves
 * @param[in] res              In/out and output arguments of the function
 *
 * @returns: Valid error codes in RFC 5661, p. 365-6.
 */
static nfsstat4
getdevicelist(struct fsal_export *exportHandle, layouttype4 type, void *opaque,
	      bool (*callback)(void *opaque, const uint64_t identifier),
	      struct fsal_getdevicelist_res *res)
{
	(void)exportHandle;
	(void)type;
	(void)opaque;
	(void)callback;

	res->eof = true;
	return NFS4_OK;
}

/**
 * @brief Get layout types supported by export.
 *
 * This function is the handler of the NFS4.1 FATTR4_FS_LAYOUT_TYPES file
 * attribute.
 *
 * @param[in]  exportHandle      Filesystem to interrogate
 * @param[out] count             Number of layout types in array
 * @param[out] types             Static array of layout types that must not be
 *                               freed or modified and must not be dereferenced
 *                               after export reference is relinquished.
 */
static void fs_layouttypes(struct fsal_export *exportHandle, int32_t *count,
			   const layouttype4 **types)
{
	(void)exportHandle;

	static const layouttype4 supportedLayoutType = LAYOUT4_NFSV4_1_FILES;
	*types = &supportedLayoutType;
	*count = 1;
}

/**
 * @brief Get layout block size for export.
 *
 * This function is the handler of the NFS4.1 FATTR4_LAYOUT_BLKSIZE f-attribute.
 *
 * This is the preferred read/write block size. Clients are requested (but
 * don't have to) read and write in multiples.
 *
 * NOTE: The Linux client only asks for this in blocks-layout, where this is
 * the filesystem wide block-size. (Minimum write size and alignment).
 *
 * @param[in] export      Filesystem to interrogate
 *
 * @returns: The preferred layout block size.
 */
static uint32_t fs_layout_blocksize(struct fsal_export *export)
{
	(void)export;

	return SFSCHUNKSIZE;
}

/**
 * @brief Maximum number of segments we will use.
 *
 * This function returns the maximum number of segments that will be used to
 * construct the response to any single layoutget call. Bear in mind that
 * current clients only support 1 segment.
 *
 * @param[in] export      Filesystem to interrogate
 *
 * @returns: The Maximum number of layout segments in a compound layoutget.
 */
static uint32_t fs_maximum_segments(struct fsal_export *export)
{
	(void)export;

	return 1;
}

/**
 * @brief Size of the buffer needed for loc_body at layoutget.
 *
 * This function sets policy for XDR buffer allocation in layoutget
 * vector below.
 * If FSAL has a const size, just return it here. If it is dependent on what the
 * client can take return ~0UL. In any case the buffer allocated will not be
 * bigger than client's requested maximum.
 *
 * @param[in] export      Filesystem to interrogate
 *
 * @returns: Max size of the buffer needed for a loc_body.
 */
static size_t fs_loc_body_size(struct fsal_export *export)
{
	(void)export;

	return MaxBufferSize;  /* typical value in NFS FSAL plugins */
}

/**
 * @brief Max Size of the buffer needed for da_addr_body in getdeviceinfo.
 *
 * This function sets policy for XDR buffer allocation in getdeviceinfo.
 * If FSAL has a const size, just return it here. If it is dependent on
 * what the client can take return ~0UL. In any case the buffer allocated
 * will not be bigger than client's requested maximum.
 *
 * @param[in] module      FSAL Module
 *
 * @returns: Max size of the buffer needed for a da_addr_body.
 */
static size_t fs_da_addr_size(struct fsal_module *module)
{
	(void)module;

	/* one stripe index + number of addresses +
	 * SAUNAFS_EXPECTED_BACKUP_DS_COUNT addresses per chunk each address
	 * takes 37 bytes (we use 40 for safety) we add 32 bytes of overhead
	 * (includes stripe count and DS count) */
	return SAUNAFS_BIGGEST_STRIPE_COUNT *
		       (4 + (4 + SAUNAFS_EXPECTED_BACKUP_DS_COUNT *
					 ChunkAddressSizeInBytes)) +
	       ChunkDataOverhead;
}

/**
 * @brief Initialize pNFS export vector operations
 *
 * @param[in] ops      Export operations vector
 */
void exportOperationsPnfs(struct export_ops *ops)
{
	ops->getdevicelist = getdevicelist;
	ops->fs_layouttypes = fs_layouttypes;
	ops->fs_layout_blocksize = fs_layout_blocksize;
	ops->fs_maximum_segments = fs_maximum_segments;
	ops->fs_loc_body_size = fs_loc_body_size;
}

/**
 * @brief Initialize pNFS related operations
 *
 * @param[in] ops      Export operations vector
 */
void pnfsMdsOperationsInit(struct fsal_ops *ops)
{
	ops->getdeviceinfo = getdeviceinfo;
	ops->fs_da_addr_size = fs_da_addr_size;
}
