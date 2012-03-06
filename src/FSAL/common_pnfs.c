/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2011 The Linux Box Corporation
 * Author: Adam C. Emerson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/quota.h>
#include "log.h"
#include "fsal.h"
#include "fsal_pnfs.h"
#include "pnfs_common.h"
#include "fsal_pnfs_files.h"
#include "nfs4.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_proto_functions.h"

/**
 * \file    common_pnfs.c
 * \brief   Utility functions for pNFS
 *
 * common_pnfs.c: Utility functions expected to be used by more than
 *                one FSAL implementing pNFS.
 *
 */

#ifdef _PNFS_MDS

/*
 * Functions potentially useful to all MDSs of layout types
 */

/**
 *
 * \brief Encode an fsal_deviceid_t
 *
 * The difference between this and xdr_deviceid4 is that this function
 * treats the deviceid as two 64-bit integers (putting them in network
 * byte order) while xdr_deviceid4 treats the deviceid as an opaque
 * string of 16 bytes.  This function may be more convenient given
 * that we expect the high quad to be in network byte order and assign
 * significance to it in nfs4_op_getdeviceinfo.
 *
 * \param xdrs     [IN/OUT] The XDR stream
 * \param deviceid [IN/OUT] The deviceid to encode/decode
 *
 * \return True on success, false on failure.
 */

bool_t
xdr_fsal_deviceid(XDR *xdrs, struct pnfs_deviceid *deviceid)
{
     if(!xdr_uint64_t(xdrs, &deviceid->export_id)) {
          return FALSE;
     }
     if(!xdr_uint64_t(xdrs, &deviceid->devid)) {
          return FALSE;
     }
     return TRUE;
}

/**
 *
 * \brief Encode most IPv4 netaddrs
 *
 * This convenience function writes an encoded netaddr4 to an XDR
 * stream given a protocol, IP address, and port.
 *
 * \param xdrs   [IN/OUT] The XDR stream
 * \param proto  [IN]     The protocol identifier.  Currently this most
 *                        be one of 6 (TCP), 17 (UDP), or 132 (SCTP)
 *                        in host byte order
 * \param addr   [IN]     The IPv4 address in host byte order
 * \param port   [IN]     The port address in host byte order
 *
 * \return NFSv4 status codes.
 */

nfsstat4
FSAL_encode_ipv4_netaddr(XDR *xdrs,
                         uint16_t proto,
                         uint32_t addr,
                         uint16_t port)
{
     char* mark = NULL;
     /* Six groups of up to three digits each, five dots, and a null */
#define V4_ADDRBUFF_LEN 24
     /* The buffer to which we output the string form of the address */
     char addrbuff[24];
     /* Pointer to the beginning of the buffer, the reference of which
        is passed to xdr_string */
     char *buffptr = &addrbuff[0];
     /* Return value from snprintf to check for overflow or error */
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

     default:
          LogCrit(COMPONENT_FSAL,
                  "Caller supplied invalid protocol %u",
                  proto);
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_string(xdrs, &mark, 5)) {
          LogCrit(COMPONENT_FSAL,
                  "Unable to encode protocol mark.");
          return NFS4ERR_SERVERFAULT;
     }

     /* Then we convert the address and port to a string and encode it. */

     written_length
          = snprintf(addrbuff,
                     V4_ADDRBUFF_LEN,
                     "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
                     ((addr & 0xff000000) >> 0x18),
                     ((addr & 0x00ff0000) >> 0x10),
                     ((addr & 0x0000ff00) >> 0x08),
                     (addr & 0x000000ff),
                     ((port & 0xff00) >> 0x08),
                     (port & 0x00ff));
     if (written_length >= V4_ADDRBUFF_LEN) {
          LogCrit(COMPONENT_FSAL,
                  "Programming error in FSAL_encode_ipv4_netaddr "
                  "defined in src/FSAL/common_pnfs.c causing "
                  "snprintf to overflow address buffer.");
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_string(xdrs, &buffptr, V4_ADDRBUFF_LEN)) {
          LogCrit(COMPONENT_FSAL,
                  "Unable to encode address.");
          return NFS4ERR_SERVERFAULT;
     }

     return NFS4_OK;
}
#endif /* _PNFS_MDS */

#ifdef _PNFS_MDS

/*
 * Functions specific to NFSV4_1_FILES layouts
 */

/**
 *
 * \brief Internal function to convert file handles
 *
 * This function convers an fsal_handle_t to Ganesha's representation
 * of an external filehandle.  (The struct that is copied to the body
 * of an nfs_fh4.)
 *
 * \param fsal_handle [IN]  The FSAL file handle
 * \param export_id   [IN]  The export ID
 * \param export      [IN]  The FSAL export context
 * \param v4_handle   [OUT] Pointer to an nfs_fh4 descriptor for the handle.
 *
 * \return NFSv4 error codes
 *
 */
static nfsstat4
make_file_handle_v4(fsal_handle_t *fsal_handle,
                    unsigned short export_id,
                    fsal_export_context_t *export,
                    nfs_fh4 *wirehandle)
{
     /* Return code from FSAL functions */
     fsal_status_t fsal_status = {0, 0};
     /* We only use this to convert the FSAL error code */
     cache_inode_status_t cache_status = 0;
     struct fsal_handle_desc fh_desc;
     file_handle_v4_t *v4_handle = (file_handle_v4_t *)wirehandle->nfs_fh4_val;

     wirehandle->wirehandle->nfs_fh4_len = sizeof(struct alloc_file_handle_v4);
     memset(wirehandle->wirehandle->nfs_fh4_val,
	    0,
	    wirehandle->wirehandle->nfs_fh4_len);
     fh_desc.start = wirehandle->nfs_fh4_val;
     fh_desc.len = wirehandle->nfs_fh4_len - offsetof(file_handle_v4_t, fsopaque);

     fsal_status =
          FSAL_DigestHandle(export, FSAL_DIGEST_NFSV4, fsal_handle,
                            &fh_desc);

     if (FSAL_IS_ERROR(fsal_status)) {
          cache_status = cache_inode_error_convert(fsal_status);
          return nfs4_Errno(cache_status);
     }

     v4_handle->fhversion = GANESHA_FH_VERSION;
     v4_handle->fs_len = fh_desc.len;
     v4_handle->exportid = export_id;
     v4_handle->ds_flag = 1;

     return NFS4_OK;
}

/**
 *
 * \brief Convert an FSAL handle to an nfs_fh4 DS handle
 *
 * This function converts an FSAL handle to an nfs_fh4 that, when
 * received by Ganesha will be marked as a pNFS DS file handle.
 *
 * \param fsal_handle [IN]  The FSAL file handle
 * \param export_id   [IN]  The export ID
 * \param export      [IN]  The FSAL export context
 * \param wirehandle  [OUT] Pointer to an nfs_fh4 to which to write
 *                          the handle.  nfs_fh4.nfs_fh4_val MUST
 *                          point to allocated memory of at least
 *                          NFS4_FHSIZE bytes.
 *
 * \return NFSv4 error codes
 *
 */
nfsstat4
FSAL_fh4_dshandle(fsal_handle_t *fsal_handle,
                  unsigned short export_id,
                  fsal_export_context_t *export,
                  nfs_fh4 *wirehandle)
{
     return make_file_handle_v4(
          fsal_handle,
          export_id,
          export,
          wirehandle);
}

/**
 *
 * \brief Convenience function to encode loc_body
 *
 * This function allows the FSAL to encode an nfsv4_1_files_layout4
 * without having to allocate and construct all the components of the
 * structure, including file handles.
 *
 * To encode a completed nfsv4_1_file_layout4 structure, call
 * xdr_nfsv4_1_file_layout4.
 *
 * \param xdrs      [IN/OUT] The XDR stream
 * \param context   [IN]     Operation context
 * \param deviceid  [IN]     The deviceid for the layout
 * \param util      [IN]     Stripe width and flags for the layout
 * \param first_idx [IN]     First stripe index
 * \param ptrn_ofst [IN]     Pattern offset
 * \param num_fhs   [IN]     Number of file handles in array
 * \param fhs       [IN]     Array of file handles
 *
 * \return NFSv4 Status code
 *
 */
nfsstat4
FSAL_encode_file_layout(XDR *xdrs,
                        fsal_op_context_t *context,
                        const struct pnfs_deviceid *deviceid,
                        nfl_util4 util,
                        uint32_t first_idx,
                        offset4 ptrn_ofst,
                        uint32_t num_fhs,
                        fsal_handle_t *fhs)
{
     /* Index for traversing FH array */
     size_t i = 0;
     /* NFS status code */
     nfsstat4 nfs_status = 0;

     if (!xdr_fsal_deviceid(xdrs, (struct pnfs_deviceid *)deviceid)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding deviceid.");
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_nfl_util4(xdrs, &util)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding nfl_util4.");
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_uint32_t(xdrs, &first_idx)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding first_stripe_index.");
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_offset4(xdrs, &ptrn_ofst)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding pattern_offset.");
          return NFS4ERR_SERVERFAULT;
     }

     if (!xdr_uint32_t(xdrs, &num_fhs)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding length of FH array.");
          return NFS4ERR_SERVERFAULT;
     }

     for (i = 0; i < num_fhs; i++) {
          /* Temporary external handle that hodls the converted handle before
             encoding */
          struct alloc_file_handle_v4 temphandle;
	  nfs_fh4 handle;

	  handle.nfs_fh4_val = (caddr_t) &temphandle;
	  handle.nfs_fh4_len = sizeof(temphandle);
          memset(handle.nfs_fh4_val, 0, handle.nfs_fh4_len);

          if ((nfs_status
               = make_file_handle_v4(fhs + i,
                                     deviceid->export_id,
                                     context->export_context,
                                     &handle)) != NFS4_OK) {
               LogMajor(COMPONENT_PNFS,
                        "Failed converting FH %lu.", i);
               return nfs_status;
          }

          if (!xdr_bytes(xdrs, (char **)&handle.nfs_fh4_val,
                         &handle.nfs_fh4_len,
                         handle.nfs_fh4_len)) {
               LogMajor(COMPONENT_PNFS,
                        "Failed encoding FH %lu.", i);
               return NFS4ERR_SERVERFAULT;
          }
     }

     return NFS4_OK;
}

/**
 *
 * \brief Convenience function to encode one multipath_list
 *
 * This function writes a multipath list representation of an array of
 * hosts accessed through most IPv4 protocols.
 *
 * \param xdrs      [IN/OUT] The XDR stream
 * \param num_hosts [IN]     Number of hosts in array
 * \param hosts     [IN]     Array of hosts
 *
 * \return NFSv4 Status code
 *
 */
nfsstat4
FSAL_encode_v4_multipath(XDR *xdrs,
                         uint32_t num_hosts,
                         fsal_multipath_member_t *hosts)
{
     /* Index for traversing host array */
     size_t i = 0;
     /* NFS status */
     nfsstat4 nfs_status = 0;

     if (!xdr_uint32_t(xdrs, &num_hosts)) {
          LogMajor(COMPONENT_PNFS, "Failed encoding length of FH array.");
          return NFS4ERR_SERVERFAULT;
     }

     for (i = 0; i < num_hosts; i++) {
          if ((nfs_status
               = FSAL_encode_ipv4_netaddr(xdrs,
                                          hosts[i].proto,
                                          hosts[i].addr,
                                          hosts[i].port))
              != NFS4_OK) {
               return nfs_status;
          }
     }

     return NFS4_OK;
}
#endif /* _PNFS_MDS */

/**
 * Convert POSIX error codes to NFS 4 error codes
 *
 * \param posix_errorcode (input):
 *        The error code returned from POSIX.
 *
 * \return The NFSv4 error code associated
 *         to posix_errorcode.
 *
 */
nfsstat4
posix2nfs4_error(int posix_errorcode)
{
     switch (posix_errorcode) {
     case EPERM:
          return NFS4ERR_PERM;

     case ENOENT:
          return NFS4ERR_NOENT;

          /* connection error */
#ifdef _AIX_5
     case ENOCONNECT:
#elif defined _LINUX
     case ECONNREFUSED:
     case ECONNABORTED:
     case ECONNRESET:
#endif

          /* IO error */
     case EIO:

          /* too many open files */
     case ENFILE:
     case EMFILE:

          /* broken pipe */
     case EPIPE:

          /* all shown as IO errors */
          return NFS4ERR_IO;

          /* no such device */
     case ENODEV:
     case ENXIO:
          return NFS4ERR_NXIO;

          /* invalid file descriptor : */
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

/**
 * \warning
 * AIX returns EEXIST where BSD uses ENOTEMPTY;
 * We want ENOTEMPTY to be interpreted anyway on AIX plateforms.
 * Thus, we explicitely write its value (87).
 */
#ifdef _AIX
     case 87:
#else
     case ENOTEMPTY:
#endif
          return NFS4ERR_NOTEMPTY;

     case ESTALE:
          return NFS4ERR_STALE;

      /* Error code that needs a retry */
     case EAGAIN:
     case EBUSY:
          return NFS4ERR_DELAY;

     case ENOTSUP:
          return NFS4ERR_NOTSUPP;

     default:

          /* other unexpected errors */
          return NFS4ERR_SERVERFAULT;
     }
}

/* Glue */

#ifdef _PNFS_MDS
fsal_mdsfunctions_t fsal_mdsfunctions;
#endif /* _PNFS_MDS */
#ifdef _PNFS_DS
fsal_dsfunctions_t fsal_dsfunctions;
#endif /* _PNFS_DS */

#ifdef _PNFS_MDS
void FSAL_LoadMDSFunctions(void)
{
     fsal_mdsfunctions = FSAL_GetMDSFunctions();
}
#endif /* _PNFS_MDS */
#ifdef _PNFS_DS
void FSAL_LoadDSFunctions(void)
{
     fsal_dsfunctions = FSAL_GetDSFunctions();
}
#endif /* _PNFS_DS */
