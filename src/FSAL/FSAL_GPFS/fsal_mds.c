/*
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_up.h"
#include "pnfs_utils.h"
#include "fsal_internal.h"
#include "gpfs_methods.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"
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

static void fs_layouttypes(struct fsal_export *export_hdl, int32_t *count,
			   const layouttype4 **types)
{
	int rc;
	struct open_arg arg;
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;
	struct gpfs_filesystem *gpfs_fs;
	struct gpfs_fsal_export *myself;
	int errsv = 0;

	/** @todo FSF: needs real getdeviceinfo that gets to the correct
	 * filesystem, this will not work for sub-mounted filesystems.
	 */
	myself = container_of(export_hdl, struct gpfs_fsal_export, export);
	gpfs_fs = myself->root_fs->private;

	arg.mountdirfd = gpfs_fs->root_fd;
	rc = gpfs_ganesha(OPENHANDLE_LAYOUT_TYPE, &arg);
	errsv = errno;
	if (rc < 0 || (rc != LAYOUT4_NFSV4_1_FILES)) {
		LogDebug(COMPONENT_PNFS, "fs_layouttypes rc %d\n", rc);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		*count = 0;
		return;

	}
	*types = &supported_layout_type;
	*count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just returns the GPFS default.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 4 MB.
 */
static uint32_t fs_layout_blocksize(struct fsal_export *export_pub)
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
static uint32_t fs_maximum_segments(struct fsal_export *export_pub)
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
static size_t fs_loc_body_size(struct fsal_export *export_pub)
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
size_t fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	return 0x1400;
}

/**
 * @brief Describe a GPFS striping pattern
 *
 * At present, we support a files based layout only.  The CRUSH
 * striping pattern is a-periodic
 *
 * @param[in]  export_pub   Public export handle
 * @param[out] da_addr_body Stream we write the result to
 * @param[in]  type         Type of layout that gave the device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */

nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl,
		       XDR *da_addr_body, const layouttype4 type,
		       const struct pnfs_deviceid *deviceid)
{
	/* The position before any bytes are sent to the stream */
	size_t da_beginning = 0;
	/* The total length of the XDR-encoded da_addr_body */
	size_t da_length = 0;
	int rc = 0;
	size_t ds_buffer = 0;
	struct deviceinfo_arg darg;
	int errsv = 0;

	darg.mountdirfd = deviceid->device_id4;
	darg.type = LAYOUT4_NFSV4_1_FILES;
	darg.devid.devid = deviceid->devid;
	darg.devid.device_id1 = deviceid->device_id1;
	darg.devid.device_id2 = deviceid->device_id2;
	darg.devid.device_id4 = deviceid->device_id4;
	darg.devid.devid = deviceid->devid;

	ds_buffer = fs_da_addr_size(NULL);

	darg.xdr.p = (int *)da_addr_body->x_base;
	da_beginning = xdr_getpos(da_addr_body);
	darg.xdr.end = (int *)(darg.xdr.p + (ds_buffer - da_beginning));

	LogDebug(COMPONENT_PNFS,
		"getdeviceinfo p %p end %p da_length %ld seq %d fd %d fsid 0x%lx\n",
		darg.xdr.p, darg.xdr.end, da_beginning,
		deviceid->device_id2, deviceid->device_id4,
		deviceid->devid);

	rc = gpfs_ganesha(OPENHANDLE_GET_DEVICEINFO, &darg);
	errsv = errno;
	if (rc < 0) {

		LogDebug(COMPONENT_PNFS, "getdeviceinfo rc %d\n", rc);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return NFS4ERR_RESOURCE;
	}
	xdr_setpos(da_addr_body, rc);
	da_length = xdr_getpos(da_addr_body) - da_beginning;

	LogDebug(COMPONENT_PNFS, "getdeviceinfo rc %d da_length %ld\n", rc,
		 da_length);

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

static nfsstat4 getdevicelist(struct fsal_export *export_pub, layouttype4 type,
			      void *opaque, bool(*cb) (void *opaque,
						       const uint64_t id),
			      struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	return NFS4_OK;
}

void export_ops_pnfs(struct export_ops *ops)
{
	ops->getdevicelist = getdevicelist;
	ops->fs_layouttypes = fs_layouttypes;
	ops->fs_layout_blocksize = fs_layout_blocksize;
	ops->fs_maximum_segments = fs_maximum_segments;
	ops->fs_loc_body_size = fs_loc_body_size;
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

static nfsstat4 layoutget(struct fsal_obj_handle *obj_hdl,
			  struct req_op_context *req_ctx, XDR *loc_body,
			  const struct fsal_layoutget_arg *arg,
			  struct fsal_layoutget_res *res)
{
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_file_handle gpfs_ds_handle;
	struct layoutget_arg larg;
	struct layoutreturn_arg lrarg;
	unsigned int rc, *fh;
	/* Structure containing the storage parameters of the file within
	   the GPFS cluster. */
	struct pnfs_filelayout_layout file_layout;
	/* Width of each stripe on the file */
	uint32_t stripe_width = 0;
	/* Utility parameter */
	nfl_util4 util = 0;
	/* The last byte that can be accessed through pNFS */
	/* uint64_t last_possible_byte = 0; strict. set but unused */
	/* The deviceid for this layout */
	struct pnfs_deviceid deviceid =  DEVICE_ID_INIT_ZERO(FSAL_ID_GPFS);
	/* NFS Status */
	nfsstat4 nfs_status = 0;
	/* Descriptor for DS handle */
	struct gsh_buffdesc ds_desc;
	int errsv = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	/* We support only LAYOUT4_NFSV4_1_FILES layouts */

	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Get basic information on the file and calculate the dimensions
	   of the layout we can support. */

	memset(&file_layout, 0, sizeof(struct pnfs_filelayout_layout));

	memcpy(&gpfs_ds_handle, myself->handle,
	       sizeof(struct gpfs_file_handle));

	larg.fd = myself->u.file.fd;
	larg.args.lg_minlength = arg->minlength;
	larg.args.lg_sbid = arg->export_id;
	larg.args.lg_fh = &gpfs_ds_handle;
	larg.args.lg_iomode = res->segment.io_mode;
	larg.handle = &gpfs_ds_handle;
	larg.file_layout = &file_layout;
	larg.xdr = NULL;

	fh = (int *)&(gpfs_ds_handle.f_handle);
	LogDebug(COMPONENT_PNFS,
		 "fh in len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_ds_handle.handle_size, gpfs_ds_handle.handle_type,
		 gpfs_ds_handle.handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	rc = gpfs_ganesha(OPENHANDLE_LAYOUT_GET, &larg);
	errsv = errno;
	if (rc != 0) {
		LogDebug(COMPONENT_PNFS, "GPFSFSAL_layoutget rc %d\n", rc);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}
	fh = (int *)&(gpfs_ds_handle.f_handle);
	LogDebug(COMPONENT_PNFS,
		 "fh out len %d type %d key %d: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 gpfs_ds_handle.handle_size, gpfs_ds_handle.handle_type,
		 gpfs_ds_handle.handle_key_size, fh[0], fh[1], fh[2], fh[3],
		 fh[4], fh[5], fh[6], fh[7], fh[8], fh[9]);

	/* We grant only one segment, and we want it back when file is closed.*/
	res->return_on_close = true;
	res->last_segment = true;
	res->segment.offset = 0;
	res->segment.length = NFS4_UINT64_MAX;

	stripe_width = file_layout.lg_stripe_unit;
	util |= stripe_width | NFL4_UFLG_COMMIT_THRU_MDS;

	deviceid.fsal_id = file_layout.device_id.fsal_id;
	deviceid.device_id2 = file_layout.device_id.device_id2;
	deviceid.device_id4 = file_layout.device_id.device_id4;
	deviceid.devid = file_layout.device_id.devid;
	/* last_possible_byte = NFS4_UINT64_MAX; strict. set but unused */

	LogDebug(COMPONENT_PNFS, "fsal_id %d seq %d fd %d fsid 0x%lx",
		deviceid.fsal_id, deviceid.device_id2,
		deviceid.device_id4, deviceid.devid);

	ds_desc.addr = &gpfs_ds_handle;
	ds_desc.len = sizeof(struct gpfs_file_handle);

	nfs_status =
	     FSAL_encode_file_layout(loc_body, &deviceid, util, 0, 0,
				     req_ctx->export->export_id, 1,
				     &ds_desc);
	if (nfs_status) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode nfsv4_1_file_layout.");
		goto relinquish;
	}

	return NFS4_OK;

 relinquish:

	/* If we failed in encoding the lo_content, relinquish what we
	   reserved for it. */

	lrarg.mountdirfd = myself->u.file.fd;
	lrarg.handle = &gpfs_ds_handle;
	lrarg.args.lr_return_type = arg->type;
	lrarg.args.lr_reclaim = false;
	lrarg.args.lr_seg.clientid = 0;
	lrarg.args.lr_seg.layout_type = arg->type;
	lrarg.args.lr_seg.iomode = res->segment.io_mode;
	lrarg.args.lr_seg.offset = 0;
	lrarg.args.lr_seg.length = NFS4_UINT64_MAX;

	rc = gpfs_ganesha(OPENHANDLE_LAYOUT_RETURN, &lrarg);
	errsv = errno;
	LogDebug(COMPONENT_PNFS, "GPFSFSAL_layoutreturn rc %d", rc);
	if (rc != 0) {
		LogDebug(COMPONENT_PNFS, "GPFSFSAL_layoutget rc %d\n", rc);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
	}

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

static nfsstat4 layoutreturn(struct fsal_obj_handle *obj_hdl,
			     struct req_op_context *req_ctx, XDR *lrf_body,
			     const struct fsal_layoutreturn_arg *arg)
{
	struct layoutreturn_arg larg;
	struct gpfs_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct gpfs_file_handle *gpfs_handle;
	int errsv = 0;

	int rc = 0;

	/* Sanity check on type */
	if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->lo_type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_handle = myself->handle;

	if (arg->dispose) {
		larg.mountdirfd = myself->u.file.fd;
		larg.handle = gpfs_handle;
		larg.args.lr_return_type = arg->lo_type;
		larg.args.lr_reclaim =
		    (arg->circumstance == circumstance_reclaim);
		larg.args.lr_seg.clientid = 0;
		larg.args.lr_seg.layout_type = arg->lo_type;
		larg.args.lr_seg.iomode = arg->spec_segment.io_mode;
		larg.args.lr_seg.offset = arg->spec_segment.offset;
		larg.args.lr_seg.length = arg->spec_segment.length;

		rc = gpfs_ganesha(OPENHANDLE_LAYOUT_RETURN, &larg);
		errsv = errno;
		if (rc != 0) {
			LogDebug(COMPONENT_PNFS,
				 "GPFSFSAL_layoutreturn rc %d\n", rc);
			if (errsv == EUNATCH)
				LogFatal(COMPONENT_PNFS,
					"GPFS Returned EUNATCH");
			return NFS4ERR_NOMATCHING_LAYOUT;
		}
	}
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

static nfsstat4 layoutcommit(struct fsal_obj_handle *obj_hdl,
			     struct req_op_context *req_ctx, XDR *lou_body,
			     const struct fsal_layoutcommit_arg *arg,
			     struct fsal_layoutcommit_res *res)
{
	struct gpfs_fsal_obj_handle *myself;
	/* The private 'full' object handle */
	struct gpfs_file_handle *gpfs_handle;

	int rc = 0;
	struct layoutcommit_arg targ;
	int errsv = 0;

	/* Sanity check on type */
	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_handle = myself->handle;

	targ.mountdirfd = myself->u.file.fd;
	targ.handle = gpfs_handle;
	targ.xdr = NULL;
	targ.offset = arg->segment.offset;
	targ.length = arg->segment.length;

	targ.reclaim = arg->reclaim;	/* True if this is a reclaim commit */
	targ.new_offset = arg->new_offset; /* True if the client has suggested a
						new offset */
	if (arg->new_offset)
		targ.last_write = arg->last_write; /* The offset of the last
							byte written */
	targ.time_changed = arg->time_changed; /*True if provided a new mtime*/
	if (arg->time_changed) {
		targ.new_time.t_sec = arg->new_time.seconds;
		targ.new_time.t_nsec = arg->new_time.nseconds;
	}
	rc = gpfs_ganesha(OPENHANDLE_LAYOUT_COMMIT, &targ);
	errsv = errno;
	if (rc != 0) {
		LogDebug(COMPONENT_PNFS, "GPFSFSAL_layoutcommit rc %d\n", rc);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_PNFS, "GPFS Returned EUNATCH");
		return posix2nfs4_error(-rc);
	}
	res->size_supplied = false;
	res->commit_done = true;

	return NFS4_OK;
}

void handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = layoutget;
	ops->layoutreturn = layoutreturn;
	ops->layoutcommit = layoutcommit;
}
