/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Author: Adam C. Emerson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    pnfs_utils.h
 * @brief   Common utility functions for pNFS
 *
 * pNFS utility functions used all over Ganesha.
 */

#ifndef PNFS_UTILS_H
#define PNFS_UTILS_H

#include <stdint.h>
#include "nfs4.h"
#include "fsal_pnfs.h"
#include "fsal_api.h"

/* The next 3 line are mandatory for proper autotools based management */
#include "config.h"

/******************************************************
 *		 Utility functions for ranges
 ******************************************************/

/**
 * @brief Test for overlap and compatible io_mode of segments
 *
 * @param segment1 [IN] A layout segment
 * @param segmenta [IN] A layout segment
 *
 * @return True if there is one or more byte contained in both
 *	   segments and the io_modes are compatible.
 */

static inline bool pnfs_segments_overlap(const struct pnfs_segment *segment1,
					 const struct pnfs_segment *segmenta)
{
	if (!(segment1->io_mode & segmenta->io_mode)) {
		return false;
	} else if ((segment1->length == 0) || (segmenta->length == 0)) {
		return false;
	} else if (segment1->offset < segmenta->offset) {
		if (segment1->length == NFS4_UINT64_MAX) {
			return true;
		} else if (segment1->offset + segment1->length <
			   segmenta->offset) {
			return false;
		} else {
			return true;
		}
	} else if (segmenta->offset < segment1->offset) {
		if (segmenta->length == NFS4_UINT64_MAX) {
			return true;
		} else if ((segmenta->offset + segmenta->length)
			   < segment1->offset) {
			return false;
		} else {
			return true;
		}
	} else {
		return true;
	}
}

/**
 * @brief Check if one segment contains the other
 *
 * This function checks whether segment2 is subsegment (not
 * necessarily proper) of segment1.
 *
 * @param segment1 [IN] The putative supersegment
 * @param segment2 [IN] The putative subsugment
 *
 * @return True if segment2 is completely contained within segment1
 */

static inline bool pnfs_segment_contains(const struct pnfs_segment *segment1,
					 const struct pnfs_segment *segment2)
{
	if (!(segment1->io_mode & segment2->io_mode)) {
		return false;
	} else if (segment1->length == 0) {
		return false;
	} else if (segment1->offset <= segment2->offset) {
		if (segment1->length == NFS4_UINT64_MAX) {
			return true;
		} else if (segment2->length == NFS4_UINT64_MAX) {
			return false;
		} else if ((segment2->offset + segment2->length) <=
			   (segment1->offset + segment1->length)) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

/**
 * @brief Subtract the second segment from the first
 *
 * In the case that the subtrahend completely contains the minuend,
 * the return value has a length and offset of 0.  If the IO modes of
 * the two arguments are incompatible, the minuend is returned
 * unchanged.  If the subtrahend is a proper subset of the minuend,
 * the minuend is returned unchanged.  This is incorrect, but to
 * handle splitting a segment, we need to add split and merge support
 * to FSALs.
 *
 * @param minuend [IN] The putative supersegment
 * @param subtrahend [IN] The putative subsugment
 *
 * @return A layout segment that is the difference between the two
 *	   segments.
 */

static inline struct pnfs_segment
pnfs_segment_difference(const struct pnfs_segment *minuend,
			const struct pnfs_segment *subtrahend)
{
	if (!(minuend->io_mode & subtrahend->io_mode)) {
		return *minuend;
	} else if (pnfs_segment_contains(subtrahend, minuend)) {
		struct pnfs_segment null = {
			.io_mode = minuend->io_mode,
			.offset = 0,
			.length = 0
		};
		return null;
	} else if (!(pnfs_segments_overlap(minuend, subtrahend))) {
		return *minuend;
	} else if (minuend->offset <= subtrahend->offset) {
		if (minuend->length == NFS4_UINT64_MAX) {
			if (subtrahend->length == NFS4_UINT64_MAX) {
				struct pnfs_segment difference = {
					.io_mode = minuend->io_mode,
					.offset = minuend->offset,
					.length =
					    (subtrahend->offset -
					     minuend->offset)
				};
				return difference;
			} else {
				return *minuend;
			}
		} else {
			if ((minuend->length + minuend->offset) >
			    (subtrahend->length + subtrahend->offset)) {
				return *minuend;
			} else {
				struct pnfs_segment difference = {
					.io_mode = minuend->io_mode,
					.offset = minuend->offset,
					.length =
					    (minuend->offset -
					     subtrahend->offset)
				};
				return difference;
			}
		}
	} else {
		struct pnfs_segment difference = {
			.io_mode = minuend->io_mode,
			.offset = subtrahend->offset + subtrahend->length - 1,
			.length = minuend->length
		};
		return difference;
	}
}

/******************************************************
 *    Common functions for every pNFS implementation
 ******************************************************/

/*
** in FSAL/common_pnfs.c
*/

bool xdr_fsal_deviceid(XDR *xdrs, struct pnfs_deviceid *deviceid);

nfsstat4 FSAL_encode_ipv4_netaddr(XDR *xdrs, uint16_t proto, uint32_t addr,
				  uint16_t port);

/**
 * This type exists soleley so arrays of hosts can be passed to
 * FSAL_encode_multipath_list.
 */

typedef struct fsal_multipath_member {
	uint16_t proto;		/*< Protocool number */
	uint32_t addr;		/*< IPv4 address */
	uint16_t port;		/*< Port */
} fsal_multipath_member_t;

nfsstat4 FSAL_encode_file_layout(XDR *xdrs,
				 const struct pnfs_deviceid *deviceid,
				 nfl_util4 util, const uint32_t first_idx,
				 const offset4 ptrn_ofst,
				 const uint16_t *ds_ids,
				 const uint32_t num_fhs,
				 const struct gsh_buffdesc *fhs);

nfsstat4 FSAL_encode_v4_multipath(XDR *xdrs, const uint32_t num_hosts,
				  const fsal_multipath_member_t *hosts);

nfsstat4 FSAL_encode_flex_file_layout(XDR *xdrs,
				 const struct pnfs_deviceid *deviceid,
				 const uint64_t ffl_stripe_unit,
				 const uint32_t	ffl_mirrors_len,
				 u_int stripes,
				 const uint32_t num_fhs,
				 const uint16_t *ds_ids,
				 const struct gsh_buffdesc *fhs,
				 const uint32_t ffds_efficiency,
				 const fattr4_owner ffds_user,
				 const fattr4_owner_group ffds_group,
				 const ff_flags4 ffl_flags,
				 const uint32_t ffl_stats_collect_hint);

nfsstat4 FSAL_encode_ff_device_versions4(XDR *xdrs,
				const u_int multipath_list4_len,
				const u_int ffda_versions_len,
				const fsal_multipath_member_t *hosts,
				const uint32_t ffdv_version,
				const uint32_t ffdv_minorversion,
				const uint32_t ffdv_rsize,
				const uint32_t ffdv_wsize,
				const bool_t ffdv_tightly_coupled);

nfsstat4 posix2nfs4_error(int posix_errorcode);

/*
** in support/ds.c
*/

struct fsal_pnfs_ds *pnfs_ds_alloc(void);
void pnfs_ds_free(struct fsal_pnfs_ds *pds);

bool pnfs_ds_insert(struct fsal_pnfs_ds *pds);
struct fsal_pnfs_ds *pnfs_ds_get(uint16_t id_servers);

static inline void pnfs_ds_get_ref(struct fsal_pnfs_ds *pds)
{
	(void) atomic_inc_int32_t(&pds->ds_refcount);
}

void pnfs_ds_put(struct fsal_pnfs_ds *pds);
void pnfs_ds_remove(uint16_t id_servers);

int ReadDataServers(config_file_t in_config,
		    struct config_error_type *err_type);
void remove_all_dss(void);
void server_pkginit(void);

#endif				/* PNFS_UTILS_H */
