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
#include "ganesha_rpc.h"
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
	/* The number of OSSs  */
	unsigned num_osds = 0; /** @todo To be set via a call to llapi */

	/* Currently, all layouts have the same number of stripes */
	uint32_t stripes = 1;

	/* Index for iterating over stripes */
	size_t stripe  = 0;

	/* NFSv4 status code */
	nfsstat4 nfs_status = 0;
	/* ds list iterator */
	struct glist_head *entry;
	struct lustre_pnfs_ds_parameter *ds;

	/* Sanity check on type */
	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Retrieve and calculate storage parameters of layout */
	/** @todo This is to be done by a call to llapi */

	/* As this is large, we encode as we go rather than building a
	 * structure and encoding it all at once. */

	/* The first entry in the nfsv4_1_file_ds_addr4 is the array
	 * of stripe indices. First we encode the count of stripes.
	 * Since our pattern doesn't repeat, we have as many indices
	 * as we do stripes. */

	if (!inline_xdr_u_int32_t(da_addr_body, &stripes)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of "
			"stripe_indices array: %" PRIu32 ".",
			stripes);
		return NFS4ERR_SERVERFAULT;
	}

	for (stripe = 0; stripe < stripes; stripe++) {
		uint32_t stripe_osd = 0;

		/** @todo use llapi or configuration file
		 * to get this information */

		if (stripe_osd < 0) {
			LogCrit(COMPONENT_PNFS,
				"Failed to retrieve OSD for "
				"stripe %lu of file %" PRIu64 ".  Error: %u",
				stripe, deviceid->devid, -stripe_osd);
			return NFS4ERR_SERVERFAULT;
		}

		if (!inline_xdr_u_int32_t(da_addr_body, &stripe_osd)) {
			LogCrit(COMPONENT_PNFS,
				"Failed to encode OSD for stripe %lu.",
				stripe);
			return NFS4ERR_SERVERFAULT;
		}
	}

	/* The number of OSDs in our cluster is the length of our
	 * multipath_lists */
	num_osds = glist_length(&pnfs_param.ds_list);
	if (!inline_xdr_u_int32_t(da_addr_body, &num_osds)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of multipath_ds_list array: %u",
			num_osds);
		return NFS4ERR_SERVERFAULT;
	}

	/* Since our index is the OSD number itself, we have only one
	 * host per multipath_list. */

	glist_for_each(entry, &pnfs_param.ds_list) {
		fsal_multipath_member_t host;
		struct sockaddr_in *sock;

		ds = glist_entry(entry,
				 struct lustre_pnfs_ds_parameter,
				 ds_list);
		memset(&host, 0, sizeof(fsal_multipath_member_t));
		host.proto = 6;
		sock = (struct sockaddr_in *)&ds->ipaddr;
		memcpy(&host.addr,
		       &sock->sin_addr,
		       sizeof(struct in_addr));
		host.port = ds->ipport;
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
	struct lustre_file_handle lustre_ds_handle;
	/* Width of each stripe on the file */
	uint32_t stripe_width = 0;
	/* Utility parameter */
	nfl_util4 util = 0;
	/* The last byte that can be accessed through pNFS */
	/* uint64_t last_possible_byte = 0; strict. set but unused */
	/* The deviceid for this layout */
	struct pnfs_deviceid deviceid = DEVICE_ID_INIT_ZERO(FSAL_ID_LUSTRE);
	/* NFS Status */
	nfsstat4 nfs_status = 0;
	/* Descriptor for DS handle */
	struct gsh_buffdesc ds_desc;
	struct lustre_pnfs_ds_parameter *ds;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle,
			      obj_handle);

	/* We support only LAYOUT4_NFSV4_1_FILES layouts */

	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS,
			"Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Get basic information on the file and calculate the dimensions
	 *of the layout we can support. */

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

	stripe_width = pnfs_param.stripe_width;
	/* util |= stripe_width | NFL4_UFLG_COMMIT_THRU_MDS; */
	util |= stripe_width;

	/** @todo: several DSs not handled yet */
	/* deviceid.devid =  pnfs_param.ds_param[0].id; */
	ds = glist_first_entry(&pnfs_param.ds_list,
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
