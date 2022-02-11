// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright © 2020 CEA
 * Author: Philippe DENIEL <philippe.deniel@cea.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "config.h"

#include <assert.h>
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <mntent.h>
#include "gsh_list.h"
#include "fsal.h"
#include "kvsfs_fsal_internal.h"
#include "fsal_convert.h"
#include "../fsal_private.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include "kvsfs_methods.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "pnfs_utils.h"
#include <stdbool.h>
#include <arpa/inet.h>

/**
 * @brief Get layout types supported by export
 *
 * We just return a pointer to the single type and set the count to 1.
 *
 * @param[in]  export_pub Public export handle
 * @param[out] count      Number of layout types in array
 * @param[out] types      Static array of layout types that must not be
 *                        freed or modified and must not be dereferenced
 *                        after export reference is relinquished
 */

static void
kvsfs_fs_layouttypes(struct fsal_export *export_hdl,
		      int32_t *count,
		      const layouttype4 **types)
{
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;

	/* FSAL_KVSFS currently supports only LAYOUT4_NFSV4_1_FILES */
	/** @todo: do a switch that cheks which layout is OK */
	*types = &supported_layout_type;
	*count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just returns the KVSFS default.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 4 MB.
 */
static uint32_t
kvsfs_fs_layout_blocksize(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Maximum number of segments we will use
 *
 * Since current clients only support 1, that's what we'll use.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 1
 */
static uint32_t
kvsfs_fs_maximum_segments(struct fsal_export *export_pub)
{
	return 1;
}

/**
 * @brief Size of the buffer needed for a loc_body
 *
 * Just a handle plus a bit.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a loc_body
 */
static size_t
kvsfs_fs_loc_body_size(struct fsal_export *export_pub)
{
	return 0x100;
}

/**
 * @brief Size of the buffer needed for a ds_addr
 *
 * This one is huge, due to the striping pattern.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a ds_addr
 */
size_t kvsfs_fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	return 0x1400;
}

/**
 * @param[out] da_addr_body Stream we write the result to
 * @param[in]  type         Type of layout that gave the device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */


nfsstat4 kvsfs_getdeviceinfo(struct fsal_module *fsal_hdl,
			      XDR *da_addr_body,
			      const layouttype4 type,
			      const struct pnfs_deviceid *deviceid)
{
	/* The number of DSs  */
	unsigned int num_ds = 0; /** @todo To be set via a call to llapi */

	/* Currently, all layouts have the same number of stripes */
	uint32_t stripe_count = 0;
	uint32_t stripe = 0;

	/* NFSv4 status code */
	nfsstat4 nfs_status = 0;
	/* ds list iterator */
	struct kvsfs_pnfs_ds_parameter *ds;
	struct fsal_export *exp_hdl;
	struct kvsfs_fsal_export *export = NULL;
	struct kvsfs_exp_pnfs_parameter *pnfs_exp_param;
	unsigned int i;

	exp_hdl  = glist_first_entry(&fsal_hdl->exports,
				     struct fsal_export,
				     exports);
	export = container_of(exp_hdl, struct kvsfs_fsal_export, export);
	pnfs_exp_param = &export->pnfs_param;

	/* Sanity check on type */
	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Retrieve and calculate storage parameters of layout */
	stripe_count = pnfs_exp_param->nb_ds;

	LogDebug(COMPONENT_PNFS, "device_id %u/%u/%u %lu",
		 deviceid->device_id1, deviceid->device_id2,
		 deviceid->device_id4, deviceid->devid);

	if (!inline_xdr_u_int32_t(da_addr_body, &stripe_count)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of stripe_indices array: %"
			PRIu32 ".",
			stripe_count);
		return NFS4ERR_SERVERFAULT;
	}

	for (stripe = 0; stripe < stripe_count; stripe++) {
		if (!inline_xdr_u_int32_t(da_addr_body, &stripe)) {
			LogCrit(COMPONENT_PNFS,
				"Failed to encode OSD for stripe %u.", stripe);
			return NFS4ERR_SERVERFAULT;
		}
	}

	num_ds = stripe_count; /* aka glist_length(&pnfs_param->ds_list) */
	if (!inline_xdr_u_int32_t(da_addr_body, &num_ds)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of multipath_ds_list array: %u",
			num_ds);
		return NFS4ERR_SERVERFAULT;
	}

	/* lookup for the right DS in the ds_list */
	for (i = 0; i < pnfs_exp_param->nb_ds ; i++) {
		fsal_multipath_member_t host;

		ds = &pnfs_exp_param->ds_array[i];
		LogDebug(COMPONENT_PNFS,
			"advertises DS addr=%u.%u.%u.%u port=%u",
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0xFF000000) >> 24,
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0x00FF0000) >> 16,
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0x0000FF00) >> 8,
			(unsigned int)ntohl(ds->ipaddr.sin_addr.s_addr)
							   & 0x000000FF,
			(unsigned short)ntohs(ds->ipport));

		host.proto = IPPROTO_TCP;
		host.addr = ntohl(ds->ipaddr.sin_addr.s_addr);
		host.port = ntohs(ds->ipport);
		nfs_status = FSAL_encode_v4_multipath(
				da_addr_body,
				1,
				&host);
		if (nfs_status != NFS4_OK)
			return nfs_status;

		/** @todo TO BE REMOVED ONCE CONFIG IS CLEAN */
	}

	return NFS4_OK;
}

/**
 * @brief Get list of available devices
 *
 * We do not support listing devices and just set EOF without doing
 * anything.
 *
 * @param[in]     export_pub Export handle
 * @param[in]     type      Type of layout to get devices for
 * @param[in]     cb        Function taking device ID halves
 * @param[in,out] res       In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 365-6.
 */

static nfsstat4
kvsfs_getdevicelist(struct fsal_export *export_pub,
		     layouttype4 type,
		     void *opaque,
		     bool (*cb)(void *opaque,
			const uint64_t id),
		     struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	return NFS4_OK;
}

void
export_ops_pnfs(struct export_ops *ops)
{
	ops->getdevicelist = kvsfs_getdevicelist;
	ops->fs_layouttypes = kvsfs_fs_layouttypes;
	ops->fs_layout_blocksize = kvsfs_fs_layout_blocksize;
	ops->fs_maximum_segments = kvsfs_fs_maximum_segments;
	ops->fs_loc_body_size = kvsfs_fs_loc_body_size;
}

/**
 * @brief Grant a layout segment.
 *
 * Grant a layout on a subset of a file requested.  As a special case,
 * lie and grant a whole-file layout if requested, because Linux will
 * ignore it otherwise.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */


static nfsstat4
kvsfs_layoutget(struct fsal_obj_handle *obj_hdl,
		 XDR *loc_body,
		 const struct fsal_layoutget_arg *arg,
		 struct fsal_layoutget_res *res)
{
	struct kvsfs_fsal_obj_handle *myself;
	struct kvsfs_exp_pnfs_parameter *pnfs_exp_param;
	struct kvsfs_fsal_export *myexport;
	struct kvsfs_file_handle kvsfs_ds_handle;
	uint32_t stripe_unit = 0;
	nfl_util4 util = 0;
	struct pnfs_deviceid deviceid = DEVICE_ID_INIT_ZERO(FSAL_ID_KVSFS);
	nfsstat4 nfs_status = 0;
	struct gsh_buffdesc ds_desc;

	myexport = container_of(op_ctx->fsal_export,
				struct kvsfs_fsal_export,
				export);
	pnfs_exp_param = &myexport->pnfs_param;

	/* We support only LAYOUT4_NFSV4_1_FILES layouts */

	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Get basic information on the file and calculate the dimensions
	 *of the layout we can support. */

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);
	memcpy(&kvsfs_ds_handle,
	       myself->handle, sizeof(struct kvsfs_file_handle));


	/** @todo: here, put some code to check if such
	 * a layout is available. If not,
	 * return NFS4ERR_UNKNOWN_LAYOUTTYPE */

	/* We grant only one segment, and we want
	 * it back when the file is closed. */
	res->return_on_close = true;
	res->last_segment = true;
	res->segment.offset = 0;
	res->segment.length = NFS4_UINT64_MAX;

	stripe_unit = pnfs_exp_param->stripe_unit;
	/* util |= stripe_unit | NFL4_UFLG_COMMIT_THRU_MDS; */
	util |= stripe_unit & ~NFL4_UFLG_MASK;

	if (util != stripe_unit)
		LogEvent(COMPONENT_PNFS,
			 "Invalid stripe_unit %u, truncated to %u",
			 stripe_unit, util);

	/** @todo: several DSs not handled yet */
	deviceid.devid =  1;

	/* last_possible_byte = NFS4_UINT64_MAX; strict. set but unused */

	LogDebug(COMPONENT_PNFS,
		 "devid nodeAddr %016"PRIx64,
		 deviceid.devid);

	ds_desc.addr = &kvsfs_ds_handle;
	ds_desc.len = sizeof(struct kvsfs_file_handle);

	nfs_status = FSAL_encode_file_layout(
			loc_body,
			&deviceid,
			util,
			0,
			0,
			&op_ctx->ctx_export->export_id,
			1,
			&ds_desc);
	if (nfs_status) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode nfsv4_1_file_layout.");
		goto relinquish;
	}

	return NFS4_OK;

relinquish:

	return nfs_status;
}

/**
 * @brief Potentially return one layout segment
 *
 * Since we don't make any reservations, in this version, or get any
 * pins to release, always succeed
 *
 * @param[in] obj_pub  Public object handle
 * @param[in] lrf_body Nothing for us
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */

static nfsstat4
kvsfs_layoutreturn(struct fsal_obj_handle *obj_hdl,
		    XDR *lrf_body,
		    const struct fsal_layoutreturn_arg *arg)
{
	struct kvsfs_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct kvsfs_file_handle *kvsfs_handle __attribute__((unused));

	/* Sanity check on type */
	if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->lo_type);
	return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);
	kvsfs_handle = myself->handle;

	return NFS4_OK;
}

/**
 * @brief Commit a segment of a layout
 *
 * Update the size and time for a file accessed through a layout.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */

static nfsstat4
kvsfs_layoutcommit(struct fsal_obj_handle *obj_hdl,
		    XDR *lou_body,
		    const struct fsal_layoutcommit_arg *arg,
		    struct fsal_layoutcommit_res *res)
{
	struct kvsfs_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct kvsfs_file_handle *kvsfs_handle __attribute__((unused));

	/* Sanity check on type */
	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself =
		container_of(obj_hdl,
			     struct kvsfs_fsal_obj_handle,
			     obj_handle);
	kvsfs_handle = myself->handle;

	/** @todo: here, add code to actually commit the layout */
	res->size_supplied = false;
	res->commit_done = true;

	return NFS4_OK;
}


void
handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = kvsfs_layoutget;
	ops->layoutreturn = kvsfs_layoutreturn;
	ops->layoutcommit = kvsfs_layoutcommit;
}
