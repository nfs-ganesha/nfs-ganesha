/*
 * DirectFlow IOCTL API for pNFS
 *
 * Copyright (C) from 2012 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 */
#ifndef __PAN_FS_PNFS_API_H__
#define __PAN_FS_PNFS_API_H__

#include "fsal_pnfs.h"

#ifndef KERNEL

#if (__FreeBSD__ > 0)
#include <sys/ioccom.h>
#endif				/* (__FreeBSD__ > 0) */

#if (__linux__ > 0)
#include <sys/ioctl.h>
#endif				/* (__linux__ > 0) */

#endif				/* !defined(KERNEL) */

/* Taken from pan_fs_client_sdk.h */
#define PAN_FS_CLIENT_SDK_IOCTL                ((unsigned int)0x24)

struct pan_ioctl_hdr {
	uint32_t size;		/* unused */
	uint32_t nfsstat;	/* HOST ORDER nfsstat4 enum */
};

struct pan_ioctl_xdr {
	void *xdr_buff;
	uint32_t xdr_alloc_len;
	uint32_t xdr_len;
};

/**
 * @brief Grant a layout segment.
 *
 * This IOCTL is called by nfs41_op_layoutget.
 *
 * @param[out]    loc_body An XDR stream to which the layout specific portion
 *                         of the granted layout segment is encoded.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * Valid error codes in RFC 5661, pp. 366-7.
 */
struct pan_layoutget_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	struct pan_ioctl_xdr loc_body;	/* IN/OUT */
	uint64_t clientid;	/*   IN   */
	void *recall_file_info;	/*   IN   */
	const struct fsal_layoutget_arg *arg;	/*   IN   */
	struct fsal_layoutget_res *res;	/* IN/OUT */
};
#define PAN_FS_CLIENT_PNFS_LAYOUTGET    \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 100, struct pan_layoutget_ioctl)

/**
 * @brief Get information about a pNFS device
 *
 * This IOCTL returns device information at the @c da_addr_body stream.
 *
 * @param[out] da_addr_body An XDR stream to which recieves
 *                          the layout type-specific information
 *                          corresponding to the deviceid.
 * @param[in]  type         The type of layout that specified the
 *                          device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */
struct pan_getdeviceinfo_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	struct pan_ioctl_xdr da_addr_body;	/* IN/OUT */
	const layouttype4 type;	/*   IN   */
	const struct pnfs_deviceid deviceid;	/*   IN   */
};
#define PAN_FS_CLIENT_PNFS_DEVICEINFO   \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 101, struct pan_getdeviceinfo_ioctl)

/**
 * @brief Potentially return one layout segment
 *
 * This function is called once on each segment matching the IO mode
 * and intersecting the range specified in a LAYOUTRETURN operation or
 * for all layouts corresponding to a given stateid on last close,
 * leas expiry, or a layoutreturn with a return-type of FSID or ALL.
 * Whther it is called in the former or latter case is indicated by
 * the synthetic flag in the arg structure, with synthetic being true
 * in the case of last-close or lease expiry.
 *
 * If arg->dispose is true, all resources associated with the
 * layout must be freed.
 *
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */
struct pan_layoutreturn_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	struct pan_ioctl_xdr lrf_body;	/*   IN   */
	const struct fsal_layoutreturn_arg *arg;	/*   IN   */
};

#define PAN_FS_CLIENT_PNFS_LAYOUTRETURN \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 102, struct pan_layoutreturn_ioctl)

/**
 * @brief Commit on a writable layout
 *
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
struct pan_layoutcommit_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	struct pan_ioctl_xdr lou_body;	/*   IN   */
	const struct fsal_layoutcommit_arg *arg;	/*   IN   */
	struct fsal_layoutcommit_res *res;	/*  OUT   */
};

#define PAN_FS_CLIENT_PNFS_LAYOUTCOMMIT \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 103, struct pan_layoutcommit_ioctl)

/**
 * @brief Retrieve next layout Recall
 *
 * @param[out] events      An array of recall events to recieve one
 *                         or more events to recall.
 * @param[in]  max_events  Max elements at events array
 * @param[out] num_events  Num of valid events returned
 *
 */
struct pan_cb_layoutrecall_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	struct pan_cb_layoutrecall_event {
		struct pnfs_segment seg;
		void *recall_file_info;
		void *cookie;
		uint64_t clientid;
		uint32_t flags;
	} *events;		/* OUT   */
	uint32_t max_events;	/* IN    */
	uint32_t num_events;	/* OUT   */
};

#define PAN_FS_CLIENT_PNFS_LAYOUTRECALL \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 104, struct pan_cb_layoutrecall_ioctl)

/**
 * @brief Tell Kernel to release any callback threads
 *
 */
struct pan_cancel_recalls_ioctl {
	struct pan_ioctl_hdr hdr;	/* IN/OUT */
	/* debug_magic must be zero or else ... */
	uint32_t debug_magic;	/* IN */
};

#define PAN_FS_CLIENT_PNFS_CANCEL_RECALLS \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 105, struct pan_cancel_recalls_ioctl)

#endif				/* __PAN_FS_PNFS_API_H__ */
