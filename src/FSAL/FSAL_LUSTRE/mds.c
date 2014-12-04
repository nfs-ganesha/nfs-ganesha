/*
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright © 2012 CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
#include "gsh_rpc.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "lustre_methods.h"
#include "fsal_handle.h"

#include "pnfs_utils.h"
#include "nfs_exports.h"
#include "export_mgr.h"


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
lustre_fs_layouttypes(struct fsal_export *export_hdl,
		      int32_t *count,
		      const layouttype4 **types)
{
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;

	/* FSAL_LUSTRE currently supports only LAYOUT4_NFSV4_1_FILES */
	/** @todo: do a switch that cheks which layout is OK */
	*types = &supported_layout_type;
	*count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just returns the LUSTRE default.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 4 MB.
 */
static uint32_t
lustre_fs_layout_blocksize(struct fsal_export *export_pub)
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
lustre_fs_maximum_segments(struct fsal_export *export_pub)
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
lustre_fs_loc_body_size(struct fsal_export *export_pub)
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
size_t lustre_fs_da_addr_size(struct fsal_module *fsal_hdl)
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


nfsstat4 lustre_getdeviceinfo(struct fsal_module *fsal_hdl,
			      XDR *da_addr_body,
			      const layouttype4 type,
			      const struct pnfs_deviceid *deviceid)
{
	/* The number of DSs  */
	unsigned num_ds = 0; /** @todo To be set via a call to llapi */

	/* Currently, all layouts have the same number of stripes */
	uint32_t stripe_count = 0;
	uint32_t stripe = 0;

	/* NFSv4 status code */
	nfsstat4 nfs_status = 0;
	/* ds list iterator */
	struct glist_head *entry;
	struct lustre_pnfs_ds_parameter *ds;
	struct lustre_pnfs_parameter *pnfs_param;
	struct lustre_fsal_module *lustre_me =
		container_of(fsal_hdl, struct lustre_fsal_module, fsal);

	/* Sanity check on type */
	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}
	pnfs_param = &lustre_me->pnfs_param;

	/* Retrieve and calculate storage parameters of layout */
	stripe_count = glist_length(&pnfs_param->ds_list);

	LogDebug(COMPONENT_PNFS, "device_id %u/%u/%u %lu",
		 deviceid->device_id1, deviceid->device_id2,
		 deviceid->device_id4, deviceid->devid);

	if (!inline_xdr_u_int32_t(da_addr_body, &stripe_count)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of "
			"stripe_indices array: %" PRIu32 ".",
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
	glist_for_each(entry, &pnfs_param->ds_list) {
		fsal_multipath_member_t host;

		ds = glist_entry(entry,
				 struct lustre_pnfs_ds_parameter,
				 ds_list);

		LogDebug(COMPONENT_PNFS,
			"advertises DS addr=%u.%u.%u.%u port=%u id=%u",
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0xFF000000) >> 24,
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0x00FF0000) >> 16,
			(ntohl(ds->ipaddr.sin_addr.s_addr) & 0x0000FF00) >> 8,
			ntohl(ds->ipaddr.sin_addr.s_addr) & 0x000000FF,
			ntohs(ds->ipport), ds->id);

		host.proto = IPPROTO_TCP;
		host.addr = ntohl(ds->ipaddr.sin_addr.s_addr);
		host.port = ntohs(ds->ipport);
		nfs_status = FSAL_encode_v4_multipath(
				da_addr_body,
				1,
				&host);
		if (nfs_status != NFS4_OK)
			return nfs_status;
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
lustre_getdevicelist(struct fsal_export *export_pub,
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
	ops->getdevicelist = lustre_getdevicelist;
	ops->fs_layouttypes = lustre_fs_layouttypes;
	ops->fs_layout_blocksize = lustre_fs_layout_blocksize;
	ops->fs_maximum_segments = lustre_fs_maximum_segments;
	ops->fs_loc_body_size = lustre_fs_loc_body_size;
}

/**
 * @brief Grant a layout segment.
 *
 * Grant a layout on a subset of a file requested.  As a special case,
 * lie and grant a whole-file layout if requested, because Linux will
 * ignore it otherwise.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */


static nfsstat4
lustre_layoutget(struct fsal_obj_handle *obj_hdl,
		 struct req_op_context *req_ctx,
		 XDR *loc_body,
		 const struct fsal_layoutget_arg *arg,
		 struct fsal_layoutget_res *res)
{
	struct lustre_fsal_obj_handle *myself;
	struct lustre_exp_pnfs_parameter *pnfs_exp_param;
	struct lustre_pnfs_parameter *pnfs_param;
	struct lustre_pnfs_ds_parameter *ds;
	struct lustre_fsal_export *myexport;
	struct lustre_file_handle lustre_ds_handle;
	uint32_t stripe_unit = 0;
	nfl_util4 util = 0;
	struct pnfs_deviceid deviceid = DEVICE_ID_INIT_ZERO(FSAL_ID_LUSTRE);
	nfsstat4 nfs_status = 0;
	struct gsh_buffdesc ds_desc;
	struct lustre_fsal_module *lustre_me;

	/* Get pnfs parameters */
	lustre_me = container_of(obj_hdl->fsal,
				 struct lustre_fsal_module,
				 fsal);
	pnfs_param = &lustre_me->pnfs_param;

	myexport = container_of(req_ctx->fsal_export,
				struct lustre_fsal_export,
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
			      struct lustre_fsal_obj_handle,
			      obj_handle);
	memcpy(&lustre_ds_handle,
	       myself->handle, sizeof(struct lustre_file_handle));


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
	/* deviceid.devid =  pnfs_param.ds_param[0].id; */
	ds = glist_first_entry(&pnfs_param->ds_list,
			       struct lustre_pnfs_ds_parameter,
			       ds_list);
	deviceid.devid = ds->id;

	/* last_possible_byte = NFS4_UINT64_MAX; strict. set but unused */

	LogDebug(COMPONENT_PNFS,
		 "devid nodeAddr %016"PRIx64,
		 deviceid.devid);

	ds_desc.addr = &lustre_ds_handle;
	ds_desc.len = sizeof(struct lustre_file_handle);

	nfs_status = FSAL_encode_file_layout(
			loc_body,
			&deviceid,
			util,
			0,
			0,
			req_ctx->export->export_id,
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
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body Nothing for us
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */

static nfsstat4
lustre_layoutreturn(struct fsal_obj_handle *obj_hdl,
		    struct req_op_context *req_ctx,
		    XDR *lrf_body,
		    const struct fsal_layoutreturn_arg *arg)
{
	struct lustre_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct lustre_file_handle *lustre_handle __attribute__((unused));

	/* Sanity check on type */
	if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->lo_type);
	return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself = container_of(obj_hdl,
			      struct lustre_fsal_obj_handle,
			      obj_handle);
	lustre_handle = myself->handle;

	return NFS4_OK;
}

/**
 * @brief Commit a segment of a layout
 *
 * Update the size and time for a file accessed through a layout.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */

static nfsstat4
lustre_layoutcommit(struct fsal_obj_handle *obj_hdl,
		    struct req_op_context *req_ctx,
		    XDR *lou_body,
		    const struct fsal_layoutcommit_arg *arg,
		    struct fsal_layoutcommit_res *res)
{
	struct lustre_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct lustre_file_handle *lustre_handle __attribute__((unused));

	/* Sanity check on type */
	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself =
		container_of(obj_hdl,
			     struct lustre_fsal_obj_handle,
			     obj_handle);
	lustre_handle = myself->handle;

	/** @todo: here, add code to actually commit the layout */
	res->size_supplied = false;
	res->commit_done = true;

	return NFS4_OK;
}


void
handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = lustre_layoutget;
	ops->layoutreturn = lustre_layoutreturn;
	ops->layoutcommit = lustre_layoutcommit;
}
