/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2011 The Linux Box Corporation
 * Author: Adam C. Emerson
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
 * ---------------------------------------
 */

/**
 * @addtogroup FSAL
 * @{
 */

#include "config.h"

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "fsal_pnfs.h"
#include "pnfs_utils.h"
#include "nfs4.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_proto_functions.h"

struct fsal_module *pnfs_fsal[FSAL_ID_COUNT];

/**
 * @file   common_pnfs.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief  Utility functions for pNFS
 *
 * Utility functions expected to be used by more than one FSAL *
 * implementing pNFS.
 */

/*
 * Functions potentially useful to all MDSs of layout types
 */

/**
 *
 * @brief Encode/decode an fsal_deviceid_t
 *
 * The difference between this and xdr_deviceid4 is that this function
 * treats the deviceid as two 64-bit integers (putting them in network
 * byte order) while xdr_deviceid4 treats the deviceid as an opaque
 * string of 16 bytes.  This function may be more convenient given
 * that we expect the high quad to be in network byte order and assign
 * significance to it in nfs4_op_getdeviceinfo.
 *
 * @param[in,out] xdrs     The XDR stream
 * @param[in,out] deviceid The deviceid to encode/decode
 *
 * @retval true on success.
 * @retval false on failure.
 */

bool xdr_fsal_deviceid(XDR *xdrs, struct pnfs_deviceid *deviceid)
{
	if (!xdr_opaque(xdrs, (char *)deviceid, NFS4_DEVICEID4_SIZE))
		return false;
	return true;
}

/**
 * @brief Encode most IPv4 netaddrs
 *
 * This convenience function writes an encoded netaddr4 to an XDR
 * stream given a protocol, IP address, and port.
 *
 * @param[in,out] xdrs  The XDR stream
 * @param[in]     proto The protocol identifier.  Currently this most
 *                      be one of 6 (TCP), 17 (UDP), or 132 (SCTP)
 *                      in host byte order
 * @param[in]     addr  The IPv4 address in host byte order
 * @param[in]     port  The port address in host byte order
 *
 * @return NFSv4 status codes.
 */

nfsstat4 FSAL_encode_ipv4_netaddr(XDR *xdrs, uint16_t proto, uint32_t addr,
				  uint16_t port)
{
	/* The address family mark string */
	const char *mark = NULL;
	/* Six groups of up to three digits each, five dots, and a null */
	const size_t v4_addrbuff_len = 24;
	/* The buffer to which we output the string form of the address */
	char addrbuff[v4_addrbuff_len];
	/* Pointer to the beginning of the buffer, the reference of
	   which is passed to xdr_string */
	char *buffptr = &addrbuff[0];
	/* Return value from snprintf to check for overflow or
	   error */
	size_t written_length = 0;

	/* First, we output the correct netid for the protocol */
	switch (proto) {
	case 6:
		mark = "tcp";
		break;

	case 17:
		mark = "udp";
		break;

	case 123:
		mark = "sctp";
		break;

	default:
		LogCrit(COMPONENT_FSAL, "Caller supplied invalid protocol %u",
			proto);
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_string(xdrs, (char **)&mark, 5)) {
		LogCrit(COMPONENT_FSAL, "Unable to encode protocol mark.");
		return NFS4ERR_SERVERFAULT;
	}

	/* Then we convert the address and port to a string and encode it. */

	written_length = snprintf(addrbuff, v4_addrbuff_len,
				  "%u.%u.%u.%u.%u.%u",
				  (unsigned int) ((addr & 0xff000000) >> 0x18),
				  (unsigned int) ((addr & 0x00ff0000) >> 0x10),
				  (unsigned int) ((addr & 0x0000ff00) >> 0x08),
				  (unsigned int) (addr & 0x000000ff),
				  (unsigned int) ((port & 0xff00) >> 0x08),
				  (unsigned int) (port & 0x00ff));
	if (written_length >= v4_addrbuff_len) {
		LogCrit(COMPONENT_FSAL,
			"Programming error at %s:%u %s causing snprintf to overflow address buffer.",
			__FILE__, __LINE__, __func__);
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_string(xdrs, &buffptr, v4_addrbuff_len)) {
		LogCrit(COMPONENT_FSAL, "Unable to encode address.");
		return NFS4ERR_SERVERFAULT;
	}

	return NFS4_OK;
}

/*
 * Functions specific to NFSV4_1_FILES layouts
 */

/**
 *
 * @brief Internal function to convert file handles
 *
 * This function creates a filehandle (that will be recognizes by
 * Ganesha as a DS filehandle) containing the supplied opaque.
 *
 * @param[in]  fh_desc   FSAL specific DS handle
 * @param[in]  export_id Export ID (So we don't have to require that
 *                       the MDS export and DS export share IDs in all
 *                       cases.)
 * @param[out] v4_handle An nfs_fh4 descriptor for the handle.
 *
 * @return NFSv4 error codes
 */
static nfsstat4 make_file_handle_ds(const struct gsh_buffdesc *fh_desc,
				    const uint16_t server_id,
				    nfs_fh4 *wirehandle)
{
	/* The v4_handle being constructed */
	file_handle_v4_t *v4_handle =
	    (file_handle_v4_t *) wirehandle->nfs_fh4_val;

	if ((offsetof(struct file_handle_v4, fsopaque) + fh_desc->len) >
	    wirehandle->nfs_fh4_len) {
		LogMajor(COMPONENT_PNFS, "DS handle too big to encode!");
		return NFS4ERR_SERVERFAULT;
	}
	wirehandle->nfs_fh4_len =
	    offsetof(struct file_handle_v4, fsopaque)+fh_desc->len;

	v4_handle->fhversion = GANESHA_FH_VERSION;
	v4_handle->fs_len = fh_desc->len;
	memcpy(v4_handle->fsopaque, fh_desc->addr, fh_desc->len);
	v4_handle->id.servers = htons(server_id);
#if (BYTE_ORDER == BIG_ENDIAN)
	v4_handle->fhflags1 = FILE_HANDLE_V4_FLAG_DS | FH_FSAL_BIG_ENDIAN;
#else
	v4_handle->fhflags1 = FILE_HANDLE_V4_FLAG_DS;
#endif

	return NFS4_OK;
}

/**
 * @brief Convenience function to encode loc_body
 *
 * This function allows the FSAL to encode an nfsv4_1_files_layout4
 * without having to allocate and construct all the components of the
 * structure, including file handles.
 *
 * To encode a completed nfsv4_1_file_layout4 structure, call
 * xdr_nfsv4_1_file_layout4.
 *
 * @note This function encodes Ganesha data server handles in the
 * loc_body, it does not use the FSAL's DS handle unadorned.
 *
 * @param[out] xdrs      XDR stream
 * @param[in]  deviceid  The deviceid for the layout
 * @param[in]  util      Stripe width and flags for the layout
 * @param[in]  first_idx First stripe index
 * @param[in]  ptrn_ofst Pattern offset
 * @param[in]  ds_ids	   Server IDs of DSs for each file handle
 * @param[in]  num_fhs   Number of file handles in array
 * @param[in]  fhs       Array if buffer descriptors holding opaque DS
 *                       handles
 * @return NFS status codes.
 */
nfsstat4 FSAL_encode_file_layout(XDR *xdrs,
				 const struct pnfs_deviceid *deviceid,
				 nfl_util4 util, const uint32_t first_idx,
				 const offset4 ptrn_ofst,
				 const uint16_t *ds_ids,
				 const uint32_t num_fhs,
				 const struct gsh_buffdesc *fhs)
{
	/* Index for traversing FH array */
	size_t i = 0;
	/* NFS status code */
	nfsstat4 nfs_status = 0;
	offset4 *p_ofst = (offset4 *) &ptrn_ofst; /* kill compile warnings */

	if (!xdr_fsal_deviceid(xdrs, (struct pnfs_deviceid *)deviceid)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding deviceid.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_nfl_util4(xdrs, &util)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding nfl_util4.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_uint32_t(xdrs, (uint32_t *) &first_idx)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding first_stripe_index.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_offset4(xdrs, p_ofst)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding pattern_offset.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_uint32_t(xdrs, (int32_t *) &num_fhs)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding length of FH array.");
		return NFS4ERR_SERVERFAULT;
	}

	for (i = 0; i < num_fhs; i++) {
		nfs_fh4 handle;
		char buffer[NFS4_FHSIZE];

		handle.nfs_fh4_val = buffer;
		handle.nfs_fh4_len = sizeof(buffer);
		memset(buffer, 0, sizeof(buffer));

		nfs_status = make_file_handle_ds(fhs + i,
						 *(ds_ids + i),
						 &handle);
		if (nfs_status != NFS4_OK) {
			LogMajor(COMPONENT_PNFS, "Failed converting FH %zu.",
				 i);
			return nfs_status;
		}

		if (!xdr_bytes(xdrs,
			       (char **)&handle.nfs_fh4_val,
			       &handle.nfs_fh4_len,
			       handle.nfs_fh4_len)) {
			LogMajor(COMPONENT_PNFS, "Failed encoding FH %zu.", i);
			return NFS4ERR_SERVERFAULT;
		}
	}

	return NFS4_OK;
}

/**
 * @brief Convenience function to encode one multipath_list
 *
 * This function writes a multipath list representation of an array of
 * hosts accessed through most IPv4 protocols.
 *
 * @param[in,out] xdrs      The XDR stream
 * @param[in]     num_hosts Number of hosts in array
 * @param[in]     hosts     Array of hosts
 *
 * @return NFSv4 Status code
 *
 */
nfsstat4 FSAL_encode_v4_multipath(XDR *xdrs, const uint32_t num_hosts,
				  const fsal_multipath_member_t *hosts)
{
	/* Index for traversing host array */
	size_t i = 0;
	/* NFS status */
	nfsstat4 nfs_status = 0;

	if (!xdr_uint32_t(xdrs, (uint32_t *) &num_hosts)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding length of FH array.");
		return NFS4ERR_SERVERFAULT;
	}

	for (i = 0; i < num_hosts; i++) {
		nfs_status = FSAL_encode_ipv4_netaddr(xdrs,
						      hosts[i].proto,
						      hosts[i].addr,
						      hosts[i].port);
		if (nfs_status != NFS4_OK)
			return nfs_status;
	}

	return NFS4_OK;
}

/**
 * @brief Convenience function to encode a single ff_data_server4
 *
 * @param[out] xdrs      XDR stream
 * @param[in]  deviceid  The deviceid for the layout
 * @param[in]  num_fhs   Number of file handles and length of ds_ids array.
 * @param[in]  ds_ids	 Server IDs of DSs for each file handle
 * @param[in]  fhs       Array if buffer descriptors holding opaque DS
 *                       handles
 * @param[in] ffds_efficiency MDS evalution of mirror's effectiveness
 * @param[in] ffds_user Synthetic uid to be used for RPC call to DS
 * @param[in] ffds_group Synthetic gid to be used for RPC call to DS
 * @return NFS status codes.
 */
static nfsstat4 FSAL_encode_data_server(XDR *xdrs,
		const struct pnfs_deviceid *deviceid,
		const uint32_t num_fhs,
		const uint16_t *ds_ids,
		const struct gsh_buffdesc *fhs,
		const uint32_t ffds_efficiency,
		const fattr4_owner ffds_user,
		const fattr4_owner_group ffds_group)
{
	nfsstat4 nfs_status = 0;
	size_t i = 0;

	/* Encode ffds_deviceid */
	if (!xdr_fsal_deviceid(xdrs, (struct pnfs_deviceid *)deviceid)) {
		LogMajor(COMPONENT_PNFS,
				"Failed encoding deviceid.");
		return NFS4ERR_SERVERFAULT;
	}

	/* Encode ffds_efficiency */
	if (!xdr_uint32_t(xdrs, (int32_t *) &ffds_efficiency)) {
		LogMajor(COMPONENT_PNFS,
			"Failed encoding ffds_efficiency.");
			return NFS4ERR_SERVERFAULT;
	}

	/* Encode ffds_stateid
	 * For now, we assume only loosely coupled setup.
	 * Hence set stateid to anonymous.
	*/
	stateid4 ffds_stateid;

	ffds_stateid.seqid = 0;
	memset(&ffds_stateid.other, '\0', sizeof(ffds_stateid.other));

	if (!xdr_stateid4(xdrs, &ffds_stateid)) {
		LogMajor(COMPONENT_PNFS,
			"Failed encoding ffds_stateid.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_uint32_t(xdrs, (int32_t *) &num_fhs)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding length of FH array.");
		return NFS4ERR_SERVERFAULT;
	}

	/* Encode ffds_fh_vers */
	for (i = 0; i < num_fhs; i++) {

		nfs_fh4 handle;
		char buffer[NFS4_FHSIZE];

		handle.nfs_fh4_val = buffer;
		handle.nfs_fh4_len = sizeof(buffer);
		memset(buffer, 0, sizeof(buffer));
		nfs_status = make_file_handle_ds(fhs + i,
						 *(ds_ids + i),
						 &handle);
		if (nfs_status != NFS4_OK) {
			LogMajor(COMPONENT_PNFS,
				"Failed converting FH %zu.", i);
				return nfs_status;
		}
		if (!xdr_bytes(xdrs,
			(char **)&handle.nfs_fh4_val,
			&handle.nfs_fh4_len,
			handle.nfs_fh4_len)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding FH %zu.", i);
				return NFS4ERR_SERVERFAULT;
			}
	}

	if (!xdr_fattr4_owner(xdrs, (fattr4_owner *) &ffds_user)) {
		LogMajor(COMPONENT_PNFS,
				"Failed encoding ffds_user.");
		return NFS4ERR_SERVERFAULT;
	}

	if (!xdr_fattr4_owner (xdrs, (fattr4_owner_group *) &ffds_group)) {
		LogMajor(COMPONENT_PNFS,
				"Failed encoding ffds_group.");
		return NFS4ERR_SERVERFAULT;
	}

	return NFS4_OK;
}


/**
 * @brief Convenience function to encode loc_body
 *
 * This function allows the FSAL to encode ff_layout4
 * without having to allocate and construct all the components of the
 * structure, including file handles.
 *
 * To encode a completed ff_layout4 structure, call
 * xdr_ff_layout4.
 *
 * @param[out] xdrs      XDR stream
 * @param[in]  deviceid  The deviceid for the layout
 * @param[in]  ffl_stripe_unit Stripe unit for current layout segment
 * @param[in]  ffl_mirrors_len Number of mirrored storage servers.
 * @param[in]  stripes Number of stripes in layout
 * @param[in]  num_fhs   Number of file handles and length of ds_ids array.
 * @param[in]  ds_ids	 Server IDs of DSs for each file handle
 * @param[in]  fhs       Array if buffer descriptors holding opaque DS
 *                       handles
 * @param[in] ffds_efficiency MDS evalution of mirror's effectiveness
 * @param[in] ffds_user Synthetic uid to be used for RPC call to DS
 * @param[in] ffds_group Synthetic gid to be used for RPC call to DS
 * @param[in] ffl_flags Bitmap flags
 * @param[in] ffl_stats_collect_hint Hint to client
					on how often the server wants it
 *        to report LAYOUTSTATS for a file. The time is in seconds.
 * @return NFS status codes.
 */
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
				 const uint32_t ffl_stats_collect_hint)
{
	nfsstat4 nfs_status = NFS4_OK;
	size_t i = 0;

	/* Stripe_unit */
	if (!xdr_length4(xdrs, (uint64_t *) &ffl_stripe_unit)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding ffl_stripe_unit.");
		return NFS4ERR_SERVERFAULT;
	}

	/* ffl_mirrors_len */
	if (!xdr_uint32_t(xdrs, (uint32_t *) &ffl_mirrors_len)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding ffl_mirrors_len.");
		return NFS4ERR_SERVERFAULT;
	}

	/* ffl_mirrors_val */
	for (i = 0; i < ffl_mirrors_len; i++) {
		size_t j = 0;

		/* stripes = ffm_data_servers_len */
		if (!xdr_uint32_t(xdrs, (u_int *) &stripes)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding ffm_data_servers_len.");
			return NFS4ERR_SERVERFAULT;
		}

		/* Encode ff_data_server4 elements */
		for (j = 0; j < stripes; j++) {
			nfs_status = FSAL_encode_data_server(xdrs,
						deviceid, num_fhs, ds_ids,
						fhs, ffds_efficiency, ffds_user,
						ffds_group);
		}
	}

	/* FFL_FLAGS */
	if (!xdr_ff_flags(xdrs, (ff_flags4 *) &ffl_flags)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding ffl_flags.");
		return NFS4ERR_SERVERFAULT;
	}

	/* Stats collect hint */
	if (!xdr_uint32_t(xdrs, (uint32_t *) &ffl_stats_collect_hint)) {
		LogMajor(COMPONENT_PNFS,
			"Failed encoding ffl_stats_collect_hint.");
		return NFS4ERR_SERVERFAULT;
	}

	return nfs_status;
}

/**
 * @brief Convenience function to encode ff_device_addr4
 *
 *
 * @param[in,out] xdrs      The XDR stream
 * @param[in]     hosts     Array of hosts
 * @return NFSv4 Status code
 *
 */
nfsstat4 FSAL_encode_ff_device_versions4(XDR *xdrs,
				const u_int multipath_list4_len,
				const u_int ffda_versions_len,
				const fsal_multipath_member_t *hosts,
				const uint32_t ffdv_version,
				const uint32_t ffdv_minorversion,
				const uint32_t ffdv_rsize,
				const uint32_t ffdv_wsize,
				const bool_t ffdv_tightly_coupled)
{
	size_t i = 0;
	nfsstat4 nfs_status = 0;

	/* multipath_list4_len */
	if (!xdr_u_int (xdrs, (u_int *) &multipath_list4_len)) {
		LogMajor(COMPONENT_PNFS,
			"Failed encoding multipath_list4_len.");
		return NFS4ERR_SERVERFAULT;
	}

	for (i = 0; i < multipath_list4_len; i++) {
		nfs_status = FSAL_encode_ipv4_netaddr(xdrs,
								hosts[i].proto,
								hosts[i].addr,
								hosts[i].port);
		if (nfs_status != NFS4_OK)
			return nfs_status;
	}

	if (!xdr_uint32_t (xdrs, (uint32_t *) &ffda_versions_len)) {
		LogMajor(COMPONENT_PNFS, "Failed encoding ffda_versions_len.");
		return NFS4ERR_SERVERFAULT;
	}

	for (i = 0; i < ffda_versions_len; i++) {
		if (!xdr_uint32_t (xdrs, (uint32_t *) &ffdv_version)) {
			LogMajor(COMPONENT_PNFS,
			 "Failed encoding ffdv_version.");
			return NFS4ERR_SERVERFAULT;
		}

		if (!xdr_uint32_t (xdrs, (uint32_t *) &ffdv_minorversion)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding ffdv_minorversion.");
			return NFS4ERR_SERVERFAULT;
		}

		if (!xdr_uint32_t (xdrs, (uint32_t *) &ffdv_rsize)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding ffdv_rsize.");
			return NFS4ERR_SERVERFAULT;
		}

		if (!xdr_uint32_t (xdrs, (uint32_t *) &ffdv_wsize)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding ffdv_wsize.");
			return NFS4ERR_SERVERFAULT;
		}

		if (!xdr_bool (xdrs, (bool_t *) &ffdv_tightly_coupled)) {
			LogMajor(COMPONENT_PNFS,
				"Failed encoding ffdv_tightly_coupled.");
			return NFS4ERR_SERVERFAULT;
		}
	}

	return NFS4_OK;
}

/**
 * @brief Convert POSIX error codes to NFS 4 error codes
 *
 * @param[in] posix_errorcode The error code returned from POSIX.
 *
 * @return The NFSv4 error code associated to posix_errorcode.
 *
 */
nfsstat4 posix2nfs4_error(const int posix_errorcode)
{
	switch (posix_errorcode) {
	case EPERM:
		return NFS4ERR_PERM;

	case ENOENT:
		return NFS4ERR_NOENT;

	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
	case EIO:
	case ENFILE:
	case EMFILE:
	case EPIPE:
		return NFS4ERR_IO;

	case ENODEV:
	case ENXIO:
		return NFS4ERR_NXIO;

	case EBADF:
		return NFS4ERR_OPENMODE;

	case ENOMEM:
		return NFS4ERR_SERVERFAULT;

	case EACCES:
		return NFS4ERR_ACCESS;

	case EFAULT:
		return NFS4ERR_SERVERFAULT;

	case EEXIST:
		return NFS4ERR_EXIST;

	case EXDEV:
		return NFS4ERR_XDEV;

	case ENOTDIR:
		return NFS4ERR_NOTDIR;

	case EISDIR:
		return NFS4ERR_ISDIR;

	case EINVAL:
		return NFS4ERR_INVAL;

	case EFBIG:
		return NFS4ERR_FBIG;

	case ENOSPC:
		return NFS4ERR_NOSPC;

	case EMLINK:
		return NFS4ERR_MLINK;

	case EDQUOT:
		return NFS4ERR_DQUOT;

	case ENAMETOOLONG:
		return NFS4ERR_NAMETOOLONG;

	case ENOTEMPTY:
		return NFS4ERR_NOTEMPTY;

	case ESTALE:
		return NFS4ERR_STALE;

	case ENOTSUP:
		return NFS4ERR_NOTSUPP;

	default:
		return NFS4ERR_SERVERFAULT;
	}
}


/** @} */
