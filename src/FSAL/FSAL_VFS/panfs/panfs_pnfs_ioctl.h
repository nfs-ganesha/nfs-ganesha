/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DirectFlow IOCTL API for pNFS
 *
 * Copyright (C) from 2012 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
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
 */
#ifndef __PAN_FS_PNFS_API_H__
#define __PAN_FS_PNFS_API_H__

#include "panfs_int.h"
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
#define PAN_FS_CLIENT_SDK_IOCTL	((unsigned int)0x24)

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
	struct pan_ioctl_hdr hdr;
	struct pan_ioctl_xdr da_addr_body;
	/* input only params */
	const layouttype4 type;
	const struct pnfs_deviceid deviceid;
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

/**
 * @brief Lookup a file in PanFS
 *
 */
#define PAN_FS_CLIENT_IOC_LOOKUP_NAME_SIZE	256
struct pan_fs_client_ioctl_lookup_args_s {
	char			name[PAN_FS_CLIENT_IOC_LOOKUP_NAME_SIZE];
	pan_bool_t		target_found;
	uint32_t		target_pannode_type;
	pan_stor_obj_id_t	target_obj_id;
};

#define PAN_FS_CLIENT_IOC_LOOKUP \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 11, \
	      struct pan_fs_client_ioctl_lookup_args_s)


/**
 * @brief Get ACL attributes for a file
 */

struct pan_fs_client_ioctl_get_attr_args_s {
	pan_stor_obj_id_t	obj_id;		/**< Key OUT */
	pan_sm_obj_map_hint_t	map_hint;	/**< unused */
	uint32_t		flags;		/**< Key IN */
	uint64_t		storage_length;
	uint64_t		capacity_used;
	pan_timespec_t		data_modified_time;
	pan_timespec_t		attr_modified_time;
	pan_timespec_t		obj_creation_time;
	uint16_t		obj_type;
	uint64_t		obj_flags;
	pan_identity_t		owner;
	pan_identity_t		primary_group;
	pan_fs_client_llapi_access_t  access_item;
	uint64_t		mgr_id;
	uint64_t		link_count;
	pan_agg_layout_hdr_t	agg_layout_hdr;
	union {
		struct  {
			uint16_t	num_components_created;
		} file;
		struct {
			uint32_t	major;
			uint32_t	minor;
		} dev;
		struct  {
			pan_agg_layout_hdr_t	def_layout_hdr;
			pan_stor_obj_id_t	parent_obj_id;
			uint16_t		dir_version;
		} dir;
	} spec_attr;
	/* ACL */
	uint16_t	num_aces;
	pan_fs_ace_t	panfs_acl[PAN_FS_ACL_LEN_MAX];
	uint32_t	acl_version;
};
typedef struct pan_fs_client_ioctl_get_attr_args_s
	pan_fs_client_ioctl_get_attr_args_t;

/* Bits for PAN_FS_CLIENT_IOC_ATTR_GET flags.
 *
 * PAN_FS_CLIENT_IOCTL_GET_F__SORT_V1_ACL
 *   The client should sort a V1 ACL, if witnessed.
 * PAN_FS_CLIENT_IOCTL_GET_F__SORT_V1_ACL
 *   The V2 ACL will be cached in the ACL cache (gateway only).  Caching should
 *   only be requested if sorting is also requested.
 */
#define PAN_FS_CLIENT_IOCTL_GET_F__NONE	0x0000
#define PAN_FS_CLIENT_IOCTL_GET_F__GET_CACHED	0x0001
#define PAN_FS_CLIENT_IOCTL_GET_F__OPT_ATTRS	0x0002
#define PAN_FS_CLIENT_IOCTL_GET_F__SORT_V1_ACL	0x0004
#define PAN_FS_CLIENT_IOCTL_GET_F__CACHE_ACL	0x0008 /* Implies SORT_V1_ACL */

struct pan_fs_client_ioctl_get_attr_args_holder_s {
	pan_fs_client_ioctl_get_attr_args_t  *get_attr_args;
};

#define PAN_FS_CLIENT_IOC_ATTR_GET \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 1, \
	      struct pan_fs_client_ioctl_get_attr_args_holder_s)

/**
 * @brief Set ACL attributes for a file
 */

/**
 * An ioctl command for setting PanFS attributes. PanFS specific ACLs can also
 * be set using this ioctl.
 *
 * @author  ssubbarayan
 * @version 1.0
 *
 * @param data structure for passing data
 * @see #pan_fs_client_ioctl_set_attr_args_t
 *
 * @since 1.0
 */
/* Bits to determine what attributes are being set */
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_LENGTH	(1<<1)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_TIME_DATA_MOD	(1<<2)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_TIME_ATTR_MOD	(1<<3)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_AGG_DIR_DEF_LAYOUT	(1<<4)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_OBJ_FLAGS	(1<<5)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_OBJ_FLAGS_MASK	(1<<6)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_OWNER	(1<<7)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_PRIMARY_GROUP	(1<<8)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_ACL	(1<<9)
#define PAN_FS_CLIENT_IOCTL_SET_ATTR_ACL_REPLACE	(1<<10)

/*
 * Note: acl_version must be provided if ATTR_ACL_REPLACE is being done.  The
 * acl_version should be set to the version of the ACL being provided.  If the
 * ACL being set came from another panfs object, the acl_version can be
 * retrieved by an ioctl getattr on that object. */
typedef struct pan_fs_client_ioctl_set_attr_args_s
	pan_fs_client_ioctl_set_attr_args_t;
struct pan_fs_client_ioctl_set_attr_args_s {
	uint32_t		attr_mask;
	/* Length of the object as it appears on the storage media */
	uint64_t		storage_length;
	pan_timespec_t		data_modified_time;
	pan_timespec_t		attr_modified_time;
	pan_agg_layout_hdr_t	dir_def_layout;
	/* File Manager attributes */
	uint64_t		obj_flags;
	uint64_t		pos_obj_flags;
	uint64_t		neg_obj_flags;
	pan_identity_t		owner;
	pan_identity_t		primary_group;
	/* ACL */
	uint16_t		num_aces;
	pan_fs_ace_t		panfs_acl[PAN_FS_ACL_LEN_MAX];
	uint32_t		acl_version;
};

struct pan_fs_client_ioctl_set_attr_args_holder_s {
	pan_fs_client_ioctl_set_attr_args_t  *set_attr_args;
};

#define PAN_FS_CLIENT_IOC_ATTR_SET \
	_IOWR(PAN_FS_CLIENT_SDK_IOCTL, 3, \
	      struct pan_fs_client_ioctl_set_attr_args_holder_s)

#endif				/* __PAN_FS_PNFS_API_H__ */
