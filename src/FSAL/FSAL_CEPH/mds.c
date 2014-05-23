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

#include "ganesha_rpc.h"
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_up.h"
#include "pnfs_utils.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

#ifdef CEPH_PNFS

/**
 * @file   FSAL_CEPH/mds.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Wed Aug 22 14:13:16 2012
 *
 * @brief pNFS Metadata Server Operations for the Ceph FSAL
 *
 * This file implements the layoutget, layoutreturn, layoutcommit,
 * getdeviceinfo, and getdevicelist operations and export query
 * support for the Ceph FSAL.
 */

static bool initiate_recall(vinodeno_t vi, bool write, void *opaque)
{
	/* The private 'full' object handle */
	struct handle *handle = (struct handle *)opaque;
	/* Return code from upcall operation */
	state_status_t status = STATE_SUCCESS;
	struct gsh_buffdesc key = {
		.addr = &handle->vi,
		.len = sizeof(vinodeno_t)
	};
	struct pnfs_segment segment = {
		.offset = 0,
		.length = UINT64_MAX,
		.io_mode = (write ? LAYOUTIOMODE4_RW : LAYOUTIOMODE4_READ)
	};

	status = handle->up_ops->layoutrecall(&key, LAYOUT4_NFSV4_1_FILES,
					      false, &segment, NULL, NULL);

	if (status != STATE_SUCCESS)
		return false;

	return true;
}

/**
 * @brief Describe a Ceph striping pattern
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

static nfsstat4 getdeviceinfo(struct fsal_export *export_pub,
			      XDR *da_addr_body, const layouttype4 type,
			      const struct pnfs_deviceid *deviceid)
{
	/* Full 'private' export */
	struct export *export = container_of(export_pub, struct export, export);
	/* The number of Ceph OSDs in the cluster */
	unsigned num_osds = ceph_ll_num_osds(export->cmount);
	/* Minimal information needed to get layout info */
	vinodeno_t vinode;
	/* Structure containing the storage parameters of the file within
	   the Ceph cluster. */
	struct ceph_file_layout file_layout;
	/* Currently, all layouts have the same number of stripes */
	uint32_t stripes = BIGGEST_PATTERN;
	/* Index for iterating over stripes */
	size_t stripe = 0;
	/* Index for iterating over OSDs */
	size_t osd = 0;
	/* NFSv4 status code */
	nfsstat4 nfs_status = 0;

	vinode.ino.val = deviceid->devid;
	vinode.snapid.val = CEPH_NOSNAP;

	/* Sanity check on type */
	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x", type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Retrieve and calculate storage parameters of layout */
	memset(&file_layout, 0, sizeof(struct ceph_file_layout));
	if (ceph_ll_file_layout(export->cmount, vinode, &file_layout) != 0) {
		LogCrit(COMPONENT_PNFS, "Failed to get Ceph layout for inode");
		return NFS4ERR_SERVERFAULT;
	}

	/* As this is large, we encode as we go rather than building a
	   structure and encoding it all at once. */

	/* The first entry in the nfsv4_1_file_ds_addr4 is the array
	   of stripe indices. First we encode the count of stripes.
	   Since our pattern doesn't repeat, we have as many indices
	   as we do stripes. */

	if (!inline_xdr_u_int32_t(da_addr_body, &stripes)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of " "stripe_indices array: %"
			PRIu32 ".", stripes);
		return NFS4ERR_SERVERFAULT;
	}

	for (stripe = 0; stripe < stripes; stripe++) {
		uint32_t stripe_osd = stripe_osd =
		    ceph_ll_get_stripe_osd(export->cmount,
					   vinode,
					   stripe,
					   &file_layout);
		if (stripe_osd < 0) {
			LogCrit(COMPONENT_PNFS,
				"Failed to retrieve OSD for "
				"stripe %lu of file %" PRIu64 ".  Error: %u",
				stripe, deviceid->devid, -stripe_osd);
			return NFS4ERR_SERVERFAULT;
		}
		if (!inline_xdr_u_int32_t(da_addr_body, &stripe_osd)) {
			LogCrit(COMPONENT_PNFS,
				"Failed to encode OSD for stripe %lu.", stripe);
			return NFS4ERR_SERVERFAULT;
		}
	}

	/* The number of OSDs in our cluster is the length of our
	   array of multipath_lists */

	if (!inline_xdr_u_int32_t(da_addr_body, &num_osds)) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode length of "
			"multipath_ds_list array: %u", num_osds);
		return NFS4ERR_SERVERFAULT;
	}

	/* Since our index is the OSD number itself, we have only one
	   host per multipath_list. */

	for (osd = 0; osd < num_osds; osd++) {
		fsal_multipath_member_t host;
		memset(&host, 0, sizeof(fsal_multipath_member_t));
		host.proto = 6;
		if (ceph_ll_osdaddr(export->cmount, osd, &host.addr) < 0) {
			LogCrit(COMPONENT_PNFS,
				"Unable to get IP address for OSD %lu.", osd);
			return NFS4ERR_SERVERFAULT;
		}
		host.port = 2049;
		nfs_status = FSAL_encode_v4_multipath(da_addr_body, 1, &host);
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

static nfsstat4 getdevicelist(struct fsal_export *export_pub, layouttype4 type,
			      void *opaque, bool(*cb) (void *opaque,
						       const uint64_t id),
			      struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	return NFS4_OK;
}

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

static void fs_layouttypes(struct fsal_export *export_pub, int32_t *count,
			   const layouttype4 **types)
{
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;
	*types = &supported_layout_type;
	*count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just returns the Ceph default.
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
static size_t fs_da_addr_size(struct fsal_export *export_pub)
{
	return 0x1400;
}

void export_ops_pnfs(struct export_ops *ops)
{
	ops->getdeviceinfo = getdeviceinfo;
	ops->getdevicelist = getdevicelist;
	ops->fs_layouttypes = fs_layouttypes;
	ops->fs_layout_blocksize = fs_layout_blocksize;
	ops->fs_maximum_segments = fs_maximum_segments;
	ops->fs_loc_body_size = fs_loc_body_size;
	ops->fs_da_addr_size = fs_da_addr_size;
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

static nfsstat4 layoutget(struct fsal_obj_handle *obj_pub,
			  struct req_op_context *req_ctx, XDR *loc_body,
			  const struct fsal_layoutget_arg *arg,
			  struct fsal_layoutget_res *res)
{
	/* The private 'full' export */
	struct export *export =
	    container_of(req_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(obj_pub, struct handle, handle);
	/* Structure containing the storage parameters of the file within
	   the Ceph cluster. */
	struct ceph_file_layout file_layout;
	/* Width of each stripe on the file */
	uint32_t stripe_width = 0;
	/* Utility parameter */
	nfl_util4 util = 0;
	/* The last byte that can be accessed through pNFS */
	uint64_t last_possible_byte = 0;
	/* The deviceid for this layout */
	struct pnfs_deviceid deviceid = DEVICE_ID_INIT_ZERO(FSAL_ID_CEPH);
	/* NFS Status */
	nfsstat4 nfs_status = 0;
	/* DS wire handle */
	struct ds_wire ds_wire;
	/* Descriptor for DS handle */
	struct gsh_buffdesc ds_desc = {.addr = &ds_wire,
		.len = sizeof(struct ds_wire)
	};
	/* The smallest layout the client will accept */
	struct pnfs_segment smallest_acceptable = {
		.io_mode = res->segment.io_mode,
		.offset = res->segment.offset,
		.length = arg->minlength
	};
	struct pnfs_segment forbidden_area = {
		.io_mode = res->segment.io_mode,
		.length = NFS4_UINT64_MAX
	};

	/* We support only LAYOUT4_NFSV4_1_FILES layouts */

	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Get basic information on the file and calculate the dimensions
	   of the layout we can support. */

	memset(&file_layout, 0, sizeof(struct ceph_file_layout));

	ceph_ll_file_layout(export->cmount, handle->wire.vi, &file_layout);
	stripe_width = file_layout.fl_stripe_unit;
	last_possible_byte = (BIGGEST_PATTERN * stripe_width) - 1;
	forbidden_area.offset = last_possible_byte + 1;

	/* Since the Linux kernel refuses to work with any layout that
	   doesn't cover the whole file, if a whole file layout is
	   requested, lie.

	   Otherwise, make sure the required layout doesn't go beyond
	   what can be accessed through pNFS. This is a preliminary
	   check before even talking to Ceph. */
	if (!
	    ((res->segment.offset == 0)
	     && (res->segment.length == NFS4_UINT64_MAX))) {
		if (pnfs_segments_overlap
		    (&smallest_acceptable, &forbidden_area)) {
			LogCrit(COMPONENT_PNFS,
				"Required layout extends beyond allowed "
				"region. offset: %" PRIu64 ", minlength: %"
				PRIu64 ".", res->segment.offset,
				arg->minlength);
			return NFS4ERR_BADLAYOUT;
		}
		res->segment.offset = 0;
		res->segment.length = stripe_width * BIGGEST_PATTERN;
	}

	LogFullDebug(COMPONENT_PNFS,
		     "will issue layout offset: %" PRIu64 " length: %" PRIu64,
		     res->segment.offset, res->segment.length);

	/* We are using sparse layouts with commit-through-DS, so our
	   utility word contains only the stripe width, our first
	   stripe is always at the beginning of the layout, and there
	   is no pattern offset. */

	if ((stripe_width & ~NFL4_UFLG_STRIPE_UNIT_SIZE_MASK) != 0) {
		LogCrit(COMPONENT_PNFS,
			"Ceph returned stripe width that is disallowed by "
			"NFS: %" PRIu32 ".", stripe_width);
		return NFS4ERR_SERVERFAULT;
	}
	util = stripe_width;

	/* If we have a cached capbility, use that.  Otherwise, call
	   in to Ceph. */

	PTHREAD_RWLOCK_wrlock(&handle->handle.lock);
	if (res->segment.io_mode == LAYOUTIOMODE4_READ) {
		uint32_t r = 0;
		if (handle->rd_issued == 0) {
			r = ceph_ll_hold_rw(export->cmount, handle->wire.vi,
					    false, initiate_recall, handle,
					    &handle->rd_serial, NULL);
			if (r < 0) {
				PTHREAD_RWLOCK_unlock(&handle->handle.lock);
				return posix2nfs4_error(-r);
			}
		}
		++handle->rd_issued;
	} else {
		uint32_t r = 0;
		if (handle->rw_issued == 0) {
			r = ceph_ll_hold_rw(export->cmount, handle->wire.vi,
					    true, initiate_recall, handle,
					    &handle->rw_serial,
					    &handle->rw_max_len);
			if (r < 0) {
				PTHREAD_RWLOCK_unlock(&handle->handle.lock);
				return posix2nfs4_error(-r);
			}
		}
		forbidden_area.offset = handle->rw_max_len;
		if (pnfs_segments_overlap
		    (&smallest_acceptable, &forbidden_area)) {
			PTHREAD_RWLOCK_unlock(&handle->handle.lock);
			return NFS4ERR_BADLAYOUT;
		}
#if CLIENTS_WILL_ACCEPT_SEGMENTED_LAYOUTS	/* sigh */
		res->segment.length =
		    (handle->rw_max_len - res->segment.offset);
#endif
		++handle->rw_issued;
	}
	PTHREAD_RWLOCK_unlock(&handle->handle.lock);

	/* For now, just make the low quad of the deviceid be the
	   inode number.  With the span of the layouts constrained
	   above, this lets us generate the device address on the fly
	   from the deviceid rather than storing it. */

	deviceid.devid = handle->wire.vi.ino.val;

	/* We return exactly one filehandle, filling in the necessary
	   information for the DS server to speak to the Ceph OSD
	   directly. */

	ds_wire.wire = handle->wire;
	ds_wire.layout = file_layout;
	ds_wire.snapseq = ceph_ll_snap_seq(export->cmount, handle->wire.vi);

	nfs_status = FSAL_encode_file_layout(loc_body, &deviceid, util, 0, 0,
					     req_ctx->export->export_id, 1,
					     &ds_desc);
	if (nfs_status != NFS4_OK) {
		LogCrit(COMPONENT_PNFS,
			"Failed to encode nfsv4_1_file_layout.");
		goto relinquish;
	}

	/* We grant only one segment, and we want it back when the file
	   is closed. */

	res->return_on_close = true;
	res->last_segment = true;

	return NFS4_OK;

 relinquish:

	/* If we failed in encoding the lo_content, relinquish what we
	   reserved for it. */

	PTHREAD_RWLOCK_wrlock(&handle->handle.lock);
	if (res->segment.io_mode == LAYOUTIOMODE4_READ) {
		if (--handle->rd_issued != 0) {
			PTHREAD_RWLOCK_unlock(&handle->handle.lock);
			return nfs_status;
		}
	} else {
		if (--handle->rd_issued != 0) {
			PTHREAD_RWLOCK_unlock(&handle->handle.lock);
			return nfs_status;
		}
	}

	ceph_ll_return_rw(export->cmount, handle->wire.vi,
			  res->segment.io_mode ==
			  LAYOUTIOMODE4_READ ? handle->rd_serial : handle->
			  rw_serial);

	PTHREAD_RWLOCK_unlock(&handle->handle.lock);

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

static nfsstat4 layoutreturn(struct fsal_obj_handle *obj_pub,
			     struct req_op_context *req_ctx, XDR *lrf_body,
			     const struct fsal_layoutreturn_arg *arg)
{
	/* The private 'full' export */
	struct export *export =
	    container_of(req_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(obj_pub, struct handle, handle);

	/* Sanity check on type */
	if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->lo_type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	if (arg->dispose) {
		PTHREAD_RWLOCK_wrlock(&handle->handle.lock);
		if (arg->cur_segment.io_mode == LAYOUTIOMODE4_READ) {
			if (--handle->rd_issued != 0) {
				PTHREAD_RWLOCK_unlock(&handle->handle.lock);
				return NFS4_OK;
			}
		} else {
			if (--handle->rd_issued != 0) {
				PTHREAD_RWLOCK_unlock(&handle->handle.lock);
				return NFS4_OK;
			}
		}

		ceph_ll_return_rw(export->cmount, handle->wire.vi,
				  arg->cur_segment.io_mode ==
				  LAYOUTIOMODE4_READ ? handle->
				  rd_serial : handle->rw_serial);

		PTHREAD_RWLOCK_unlock(&handle->handle.lock);
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

static nfsstat4 layoutcommit(struct fsal_obj_handle *obj_pub,
			     struct req_op_context *req_ctx, XDR *lou_body,
			     const struct fsal_layoutcommit_arg *arg,
			     struct fsal_layoutcommit_res *res)
{
	/* The private 'full' export */
	struct export *export =
	    container_of(req_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(obj_pub, struct handle, handle);
	/* Old stat, so we don't truncate file or reverse time */
	struct stat stold;
	/* new stat to set time and size */
	struct stat stnew;
	/* Mask to determine exactly what gets set */
	int attrmask = 0;
	/* Error returns from Ceph */
	int ceph_status = 0;

	/* Sanity check on type */
	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogCrit(COMPONENT_PNFS, "Unsupported layout type: %x",
			arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* A more proper and robust implementation of this would use
	   Ceph caps, but we need to hack at the client to expose
	   those before it can work. */

	memset(&stold, 0, sizeof(struct stat));
	ceph_status = ceph_ll_getattr(export->cmount, handle->wire.vi,
				      &stold, 0, 0);
	if (ceph_status < 0) {
		LogCrit(COMPONENT_PNFS,
			"Error %d in attempt to get attributes of " "file %"
			PRIu64 ".", -ceph_status, handle->wire.vi.ino.val);
		return posix2nfs4_error(-ceph_status);
	}

	memset(&stnew, 0, sizeof(struct stat));
	if (arg->new_offset) {
		if (stold.st_size < arg->last_write + 1) {
			attrmask |= CEPH_SETATTR_SIZE;
			stnew.st_size = arg->last_write + 1;
			res->size_supplied = true;
			res->new_size = arg->last_write + 1;
		}
	}

	if ((arg->time_changed) &&
	    (arg->new_time.seconds > stold.st_mtime))
		stnew.st_mtime = arg->new_time.seconds;
	else
		stnew.st_mtime = time(NULL);

	attrmask |= CEPH_SETATTR_MTIME;

	ceph_status = ceph_ll_setattr(export->cmount, handle->wire.vi,
				      &stnew, attrmask, 0, 0);
	if (ceph_status < 0) {
		LogCrit(COMPONENT_PNFS,
			"Error %d in attempt to get attributes of " "file %"
			PRIu64 ".", -ceph_status, handle->wire.vi.ino.val);
		return posix2nfs4_error(-ceph_status);
	}

	/* This is likely universal for files. */

	res->commit_done = true;

	return NFS4_OK;
}

void handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = layoutget;
	ops->layoutreturn = layoutreturn;
	ops->layoutcommit = layoutcommit;
}

#endif				/* CEPH_PNFS */
