// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "fsal.h"
#include "fsal_api.h"
#include "fsal_types.h"
#include "fsal_up.h"
#include "FSAL/fsal_commonlib.h"
#include "gsh_config.h"
#include "pnfs_utils.h"

#include "context_wrap.h"
#include "lzfs_internal.h"
#include "lizardfs/lizardfs_c_api.h"

static int cmp_func(const void *a, const void *b)
{
	if (((const liz_chunkserver_info_t *)a)->ip <
	    ((const liz_chunkserver_info_t *)b)->ip) {
		return -1;
	}
	if (((const liz_chunkserver_info_t *)a)->ip >
	    ((const liz_chunkserver_info_t *)b)->ip) {
		return 1;
	}
	return 0;
}

static int is_disconnected(const void *a, void *unused)
{
	return ((const liz_chunkserver_info_t *)a)->version ==
			kDisconnectedChunkserverVersion;
}

static int is_same_ip(const void *a, void *base)
{
	if (a == base) {
		return 0;
	}

	return ((const liz_chunkserver_info_t *)a)->ip ==
		((const liz_chunkserver_info_t *)a - 1)->ip;
}

static size_t remove_if(void *base,
			size_t num,
			size_t size,
			int (*predicate)(const void *a, void *work_data),
			void *work_data)
{
	size_t i, j;

	j = 0;
	for (i = 0; i < num; ++i) {
		if (!predicate((uint8_t *)base + i * size, work_data)) {
			memcpy((uint8_t *)base + i * size,
			       (uint8_t *)base + j * size,
			       size);
			j++;
		}
	}
	return j;
}

static void shuffle(void *base, size_t num, size_t size)
{
	uint8_t temp[size];
	size_t i, j;

	if (num == 0) {
		return;
	}

	for (i = 0; i < num - 1; ++i) {
		j = i + rand() % (num - i);

		memcpy(temp, (uint8_t *)base + i * size, size);
		memcpy((uint8_t *)base + i * size,
		       (uint8_t *)base + j * size,
		       size);
		memcpy((uint8_t *)base + j * size, temp, size);
	}
}

static liz_chunkserver_info_t *lzfs_int_get_randomized_chunkserver_list(
	struct lzfs_fsal_export *lzfs_export, uint32_t *chunkserver_count)
{
	liz_chunkserver_info_t *chunkserver_info = NULL;
	int rc;

	chunkserver_info = gsh_malloc(LZFS_BIGGEST_STRIPE_COUNT *
					sizeof(liz_chunkserver_info_t));

	rc = liz_get_chunkservers_info(lzfs_export->lzfs_instance,
				       chunkserver_info,
				       LZFS_BIGGEST_STRIPE_COUNT,
				       chunkserver_count);
	if (rc < 0) {
		*chunkserver_count = 0;
		gsh_free(chunkserver_info);
		return NULL;
	}

	// Free labels, we don't need them.
	liz_destroy_chunkservers_info(chunkserver_info);

	// remove disconnected
	*chunkserver_count = remove_if(chunkserver_info, *chunkserver_count,
				       sizeof(liz_chunkserver_info_t),
				       is_disconnected, NULL);

	// remove entries with the same ip
	qsort(chunkserver_info, *chunkserver_count,
	      sizeof(liz_chunkserver_info_t), cmp_func);
	*chunkserver_count = remove_if(chunkserver_info, *chunkserver_count,
				       sizeof(liz_chunkserver_info_t),
				       is_same_ip, chunkserver_info);

	// randomize
	shuffle(chunkserver_info, *chunkserver_count,
		sizeof(liz_chunkserver_info_t));

	return chunkserver_info;
}

/*! \brief Fill DS list with entries corresponding to chunks */
static int lzfs_int_fill_chunk_ds_list(
				XDR *da_addr_body,
				liz_chunk_info_t *chunk_info,
				liz_chunkserver_info_t *chunkserver_info,
				uint32_t chunk_count,
				uint32_t stripe_count,
				uint32_t chunkserver_count,
				uint32_t *chunkserver_index)
{
	fsal_multipath_member_t host[LZFS_EXPECTED_BACKUP_DS_COUNT];

	for (uint32_t chunk_index = 0;
	     chunk_index < MIN(chunk_count, stripe_count);
	     ++chunk_index) {
		liz_chunk_info_t *chunk = &chunk_info[chunk_index];
		int server_count = 0;

		memset(host,
		       0,
		       LZFS_EXPECTED_BACKUP_DS_COUNT *
				sizeof(fsal_multipath_member_t));

		// prefer std chunk part type
		for (int i = 0; i < chunk->parts_size &&
		     server_count < LZFS_EXPECTED_BACKUP_DS_COUNT; ++i) {
			if (chunk->parts[i].part_type_id !=
						LZFS_STD_CHUNK_PART_TYPE) {
				continue;
			}
			host[server_count].proto = TCP_PROTO_NUMBER;
			host[server_count].addr = chunk->parts[i].addr;
			host[server_count].port = NFS_PORT;
			++server_count;
		}

		for (int i = 0; i < chunk->parts_size &&
			server_count < LZFS_EXPECTED_BACKUP_DS_COUNT; ++i) {
			if (chunk->parts[i].part_type_id ==
					LZFS_STD_CHUNK_PART_TYPE) {
				continue;
			}
			host[server_count].proto = TCP_PROTO_NUMBER;
			host[server_count].addr = chunk->parts[i].addr;
			host[server_count].port = NFS_PORT;
			++server_count;
		}

		// fill unused entries with the servers from randomized
		// chunkserver list
		while (server_count < LZFS_EXPECTED_BACKUP_DS_COUNT) {
			host[server_count].proto = TCP_PROTO_NUMBER;
			host[server_count].addr =
				chunkserver_info[*chunkserver_index].ip;
			host[server_count].port = NFS_PORT;
			++server_count;
			*chunkserver_index = (*chunkserver_index + 1) %
							chunkserver_count;
		}

		// encode ds entry
		nfsstat4 nfs_status = FSAL_encode_v4_multipath(da_addr_body,
							       server_count,
							       host);
		if (nfs_status != NFS4_OK) {
			return -1;
		}
	}

	return 0;
}

/*! \brief Fill unused part of DS list with servers from randomized chunkserver
 * list */
static int lzfs_int_fill_unused_ds_list(
				XDR *da_addr_body,
				liz_chunkserver_info_t *chunkserver_info,
				uint32_t chunk_count,
				uint32_t stripe_count,
				uint32_t chunkserver_count,
				uint32_t *chunkserver_index)
{
	fsal_multipath_member_t host[LZFS_EXPECTED_BACKUP_DS_COUNT];

	for (uint32_t chunk_index = MIN(chunk_count, stripe_count);
	     chunk_index < stripe_count; ++chunk_index) {
		int server_count = 0, index;

		memset(host, 0, LZFS_EXPECTED_BACKUP_DS_COUNT *
					sizeof(fsal_multipath_member_t));

		while (server_count < LZFS_EXPECTED_BACKUP_DS_COUNT) {
			index = (*chunkserver_index + server_count) %
							chunkserver_count;
			host[server_count].proto = TCP_PROTO_NUMBER;
			host[server_count].addr = chunkserver_info[index].ip;
			host[server_count].port = NFS_PORT;
			++server_count;
		}
		*chunkserver_index = (*chunkserver_index + 1) %
							chunkserver_count;

		nfsstat4 nfs_status = FSAL_encode_v4_multipath(da_addr_body,
							       server_count,
							       host);
		if (nfs_status != NFS4_OK) {
			return -1;
		}
	}

	return 0;
}

/*! \brief Get information about a pNFS device
 *
 * The function converts LizardFS file's chunk information to pNFS device info.
 *
 * Linux pNFS client imposes limit on stripe size (LZFS_BIGGEST_STRIPE_COUNT =
 * 4096).  If we would use straight forward approach of converting each chunk
 * to stripe entry, we would be limited to file size of 256 GB (4096 * 64MB).
 *
 * To avoid this problem each DS can read/write data from any chunk (Remember
 * that pNFS client takes DS address from DS list in round robin fashion). Of
 * course it's more efficient if DS is answering queries about chunks residing
 * locally.
 *
 * To achieve the best performance we fill the DS list in a following way:
 *
 * First we prepare randomized list of all chunkservers (RCSL). Then for each
 * chunk we fill multipath DS list entry with addresses of chunkservers storing
 * this chunk. If there is less chunkservers than LZFS_EXPECTED_BACKUP_DS_COUNT
 * then we use chunkservers from RCSL.
 *
 * If we didn't use all the possible space in DS list
 * (LZFS_BIGGEST_STRIPE_COUNT), then we fill rest of the stripe entries with
 * addresses from RCSL (again LZFS_EXPECTED_BACKUP_DS_COUNT addresses for each
 * stripe entry).
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_getdeviceinfo(struct fsal_module *fsal_hdl,
					XDR *da_addr_body,
					const layouttype4 type,
					const struct pnfs_deviceid *deviceid)
{
	struct fsal_export *export_hdl;
	struct lzfs_fsal_export *lzfs_export = NULL;
	liz_chunk_info_t *chunk_info = NULL;
	liz_chunkserver_info_t *chunkserver_info = NULL;
	uint32_t chunk_count,
		 chunkserver_count,
		 stripe_count,
		 chunkserver_index;
	struct glist_head *glist,
			  *glistn;
	int rc;

	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x", type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	uint16_t export_id = deviceid->device_id2;

	glist_for_each_safe(glist, glistn, &fsal_hdl->exports) {
		export_hdl = glist_entry(glist, struct fsal_export, exports);
		if (export_hdl->export_id == export_id) {
			lzfs_export = container_of(export_hdl,
						   struct lzfs_fsal_export,
						   export);
			break;
		}
	}

	if (!lzfs_export) {
		LogCrit(COMPONENT_PNFS, "Couldn't find export with id: %"
			PRIu16, export_id);
		return NFS4ERR_SERVERFAULT;
	}

	// get the chunk list for file
	chunk_info = gsh_malloc(LZFS_BIGGEST_STRIPE_COUNT *
					sizeof(liz_chunk_info_t));
	rc = liz_cred_get_chunks_info(lzfs_export->lzfs_instance,
				      &op_ctx->creds, deviceid->devid, 0,
				      chunk_info, LZFS_BIGGEST_STRIPE_COUNT,
				      &chunk_count);
	if (rc < 0) {
		LogCrit(COMPONENT_PNFS,
				"Failed to get LizardFS layout for export=%"
				PRIu16 " inode=%" PRIu64, export_id,
				deviceid->devid);
		goto generic_err;
	}

	chunkserver_info = lzfs_int_get_randomized_chunkserver_list(
							lzfs_export,
							&chunkserver_count);
	if (chunkserver_info == NULL || chunkserver_count == 0) {
		LogCrit(COMPONENT_PNFS,
			"Failed to get LizardFS layout for export=%" PRIu16
			" inode=%" PRIu64, export_id, deviceid->devid);
		goto generic_err;
	}

	chunkserver_index = 0;
	stripe_count = MIN(chunk_count + chunkserver_count,
			   LZFS_BIGGEST_STRIPE_COUNT);
	if (!inline_xdr_u_int32_t(da_addr_body, &stripe_count)) {
		goto encode_err;
	}

	for (uint32_t chunk_index = 0; chunk_index < stripe_count;
	     ++chunk_index) {
		if (!inline_xdr_u_int32_t(da_addr_body, &chunk_index)) {
			goto encode_err;
		}
	}

	if (!inline_xdr_u_int32_t(da_addr_body, &stripe_count)) {
		goto encode_err;
	}

	rc = lzfs_int_fill_chunk_ds_list(da_addr_body, chunk_info,
					 chunkserver_info, chunk_count,
					 stripe_count, chunkserver_count,
					 &chunkserver_index);
	if (rc < 0) {
		goto encode_err;
	}

	rc = lzfs_int_fill_unused_ds_list(da_addr_body, chunkserver_info,
					  chunk_count, stripe_count,
					  chunkserver_count,
					  &chunkserver_index);
	if (rc < 0) {
		goto encode_err;
	}

	liz_destroy_chunks_info(chunk_info);
	gsh_free(chunk_info);
	gsh_free(chunkserver_info);

	return NFS4_OK;

encode_err:
	LogCrit(COMPONENT_PNFS,
		"Failed to encode device information for export=%" PRIu16
		" inode=%" PRIu64, export_id, deviceid->devid);

generic_err:
	if (chunk_info) {
		liz_destroy_chunks_info(chunk_info);
		gsh_free(chunk_info);
	}

	if (chunkserver_info) {
		gsh_free(chunkserver_info);
	}

	return NFS4ERR_SERVERFAULT;
}

/*! \brief Get list of available devices
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_getdevicelist(struct fsal_export *export_hdl,
					layouttype4 type,
					void *opaque,
					bool (*cb)(void *opaque,
					const uint64_t id),
					struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	return NFS4_OK;
}

/*! \brief Get layout types supported by export
 *
 * \see fsal_api.h for more information
 */
static void lzfs_fsal_fs_layouttypes(struct fsal_export *export_hdl,
				     int32_t *count,
				     const layouttype4 **types)
{
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;
	*types = &supported_layout_type;
	*count = 1;
}

/* \brief Get layout block size for export
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_layout_blocksize(struct fsal_export *export_hdl)
{
	return MFSCHUNKSIZE;
}

/*! \brief Maximum number of segments we will use
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maximum_segments(struct fsal_export *export_hdl)
{
	return 1;
}

/*! \brief Size of the buffer needed for loc_body at layoutget
 *
 * \see fsal_api.h for more information
 */
static size_t lzfs_fsal_fs_loc_body_size(struct fsal_export *export_hdl)
{
	return 0x100;  // typical value in NFS FSAL plugins
}

/*! \brief Max Size of the buffer needed for da_addr_body in getdeviceinfo
 *
 * \see fsal_api.h for more information
 */
static size_t lzfs_fsal_fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	// one stripe index + number of addresses +
	// LZFS_EXPECTED_BACKUP_DS_COUNT addresses per chunk each address takes
	// 37 bytes (we use 40 for safety) we add 32 bytes of overhead
	// (includes stripe count and DS count)
	return LZFS_BIGGEST_STRIPE_COUNT *
		(4 + (4 + LZFS_EXPECTED_BACKUP_DS_COUNT * 40)) + 32;
}

void lzfs_fsal_export_ops_pnfs(struct export_ops *ops)
{
	ops->getdevicelist = lzfs_fsal_getdevicelist;
	ops->fs_layouttypes = lzfs_fsal_fs_layouttypes;
	ops->fs_layout_blocksize = lzfs_fsal_fs_layout_blocksize;
	ops->fs_maximum_segments = lzfs_fsal_fs_maximum_segments;
	ops->fs_loc_body_size = lzfs_fsal_fs_loc_body_size;
}

void lzfs_fsal_ops_pnfs(struct fsal_ops *ops)
{
	ops->getdeviceinfo = lzfs_fsal_getdeviceinfo;
	ops->fs_da_addr_size = lzfs_fsal_fs_da_addr_size;
}
