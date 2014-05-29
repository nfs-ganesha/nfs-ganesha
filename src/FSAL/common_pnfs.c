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
#include "cache_inode.h"
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

	written_length =
	    snprintf(addrbuff, v4_addrbuff_len, "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
		     ((addr & 0xff000000) >> 0x18),
		     ((addr & 0x00ff0000) >> 0x10),
		     ((addr & 0x0000ff00) >> 0x08), (addr & 0x000000ff),
		     ((port & 0xff00) >> 0x08), (port & 0x00ff));
	if (written_length >= v4_addrbuff_len) {
		LogCrit(COMPONENT_FSAL,
			"Programming error in FSAL_encode_ipv4_netaddr "
			"defined in %s:%u %s causing"
			"snprintf to overflow address buffer.", __FILE__,
			__LINE__, __func__);
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
				    const unsigned int export_id,
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
	v4_handle->exportid = export_id;
	v4_handle->flags = FILE_HANDLE_V4_FLAG_DS;

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
 * @param[in]  export_id Export ID (export on Data Server)
 * @param[in]  num_fhs   Number of file handles in array
 * @param[in]  fhs       Array if buffer descriptors holding opaque DS
 *                       handles
 * @return NFS status codes.
 */
nfsstat4 FSAL_encode_file_layout(XDR *xdrs,
				 const struct pnfs_deviceid *deviceid,
				 nfl_util4 util, const uint32_t first_idx,
				 const offset4 ptrn_ofst,
				 const unsigned int export_id,
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
		struct alloc_file_handle_v4 buffer;
		handle.nfs_fh4_val = (char *)&buffer;
		handle.nfs_fh4_len = sizeof(buffer);
		memset(&buffer, 0, sizeof(buffer));

		nfs_status = make_file_handle_ds(fhs + i,
						 export_id,
						 &handle);
		if (nfs_status != NFS4_OK) {
			LogMajor(COMPONENT_PNFS, "Failed converting FH %lu.",
				 i);
			return nfs_status;
		}

		if (!xdr_bytes(xdrs,
			       (char **)&handle.nfs_fh4_val,
			       &handle.nfs_fh4_len,
			       handle.nfs_fh4_len)) {
			LogMajor(COMPONENT_PNFS, "Failed encoding FH %lu.", i);
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
