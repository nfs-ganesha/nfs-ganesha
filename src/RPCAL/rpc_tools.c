// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    rpc_tools.c
 * @brief   Some tools very useful in the nfs protocol implementation.
 *
 */

#include "config.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ctype.h> /* for having isalnum */
#include <stdlib.h> /* for having atoi */
#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h> /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>

#include "hashtable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_dupreq.h"

/* XXX doesn't ntirpc have an equivalent for all of the following?
 */

const char *xprt_type_to_str(xprt_type_t type)
{
	switch (type) {
	case XPRT_UNKNOWN:
		return "UNKNOWN";
	case XPRT_NON_RENDEZVOUS:
		return "UNUSED";
	case XPRT_UDP:
		return "udp";
	case XPRT_UDP_RENDEZVOUS:
		return "udp rendezvous";
	case XPRT_TCP:
		return "tcp";
	case XPRT_TCP_RENDEZVOUS:
		return "tcp rendezvous";
	case XPRT_SCTP:
		return "sctp";
	case XPRT_SCTP_RENDEZVOUS:
		return "sctp rendezvous";
	case XPRT_RDMA:
		return "rdma";
	case XPRT_RDMA_RENDEZVOUS:
		return "rdma rendezvous";
	case XPRT_VSOCK:
		return "vsock";
	case XPRT_VSOCK_RENDEZVOUS:
		return "vsock rendezvous";
	}
	return "INVALID";
}

/**
 * @brief Copy transport address into an address field
 *
 * @param[out] addr Address field to fill in.
 * @param[in]  xprt Transport to get address from.
 *
 * @retval true if okay.
 * @retval false if not.
 */

void copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt)
{
	sockaddr_t *xp_addr = svc_getrpccaller(xprt);

	memcpy(addr, xp_addr, sizeof(*addr));
}

static void xdr_io_data_uio_release(struct xdr_uio *uio, u_int flags)
{
	int ix;
	io_data *io_data = uio->uio_u2;

	LogFullDebug(COMPONENT_DISPATCH,
		     "Releasing %p, references %" PRIi32 ", count %d", uio,
		     uio->uio_references, (int)uio->uio_count);

	if (--uio->uio_references != 0)
		return;

	if (io_data && (io_data->release != NULL)) {
		/* Handle the case where the io_data comes with its
		 * own release method.
		 *
		 * Note if extra buffer was used to handle RNDUP, the
		 * io_data release function doesn't even know about it so will
		 * not free it.
		 */
		io_data->release(io_data->release_data);
	} else {
		if (uio->uio_u3 != NULL) {
			/* Don't free the last buffer! It was allocated along
			 * with uio..
			 */
			uio->uio_count--;
		}

		/* Free the buffers that had been allocated */
		for (ix = 0; ix < uio->uio_count; ix++) {
			if (!(op_ctx && op_ctx->is_rdma_buff_used))
				gsh_free(uio->uio_vio[ix].vio_base);
		}
	}

	gsh_free(uio);
	if (io_data)
		gsh_free(io_data);
}

static inline bool xdr_io_data_encode(XDR *xdrs, io_data *objp)
{
	struct xdr_uio *uio;
	uint32_t size = objp->data_len;
	/* The size to actually be written must be a multiple of
	 * BYTES_PER_XDR_UNIT
	 */
	uint32_t size2 = RNDUP(size);
	int i, extra = 0, last;
	int count = objp->iovcnt;
	uint32_t remain = size;
	size_t totlen = 0;

	if (!inline_xdr_u_int32_t(xdrs, &size))
		return false;

	if (size != size2) {
		/* Add an extra buffer for round up */
		count++;
		extra = BYTES_PER_XDR_UNIT;
		last = objp->iovcnt - 1;
	}

	uio = gsh_calloc(1, sizeof(struct xdr_uio) +
				    count * sizeof(struct xdr_vio) + extra);
	uio->uio_release = xdr_io_data_uio_release;
	uio->uio_count = count;
	if (objp->release && objp->release_data) {
		/* Create a copy of io_data, since send path could be async */
		io_data *objp_copy = gsh_calloc(1, sizeof(io_data));

		objp_copy->release = objp->release;
		objp_copy->release_data = objp->release_data;
		uio->uio_u2 = objp_copy;
	}

	for (i = 0; i < objp->iovcnt; i++) {
		size_t i_size = objp->iov[i].iov_len;

		if (remain < i_size)
			i_size = remain;

		uio->uio_vio[i].vio_base = objp->iov[i].iov_base;
		uio->uio_vio[i].vio_head = objp->iov[i].iov_base;
		uio->uio_vio[i].vio_tail = objp->iov[i].iov_base + i_size;
		uio->uio_vio[i].vio_wrap = objp->iov[i].iov_base + i_size;
		uio->uio_vio[i].vio_length = i_size;
		uio->uio_vio[i].vio_type = VIO_DATA;

		totlen += i_size;
		remain -= i_size;

		LogFullDebug(COMPONENT_DISPATCH,
			     "iov %p [%d].iov_base %p iov_len %zu for %zu of %u",
			     objp->iov, i, objp->iov[i].iov_base, i_size,
			     totlen, objp->data_len);
	}

	if (size != size2) {
		/* grab the last N bytes of last buffer into extra */
		size_t n = size % BYTES_PER_XDR_UNIT;

		/* Check if last buffer has space */
		if (objp->last_iov_buf_size >=
		    (uio->uio_vio[last].vio_length + n)) {
			uio->uio_vio[last].vio_tail =
				uio->uio_vio[last].vio_tail + n;
			uio->uio_vio[last].vio_wrap =
				uio->uio_vio[last].vio_wrap + n;
			uio->uio_vio[last].vio_length =
				uio->uio_vio[last].vio_length + n;
			uio->uio_count--;
			goto putbufs;
		}

		char *p = uio->uio_vio[last].vio_base +
			  uio->uio_vio[last].vio_length - n;
		char *extra = (char *)uio + sizeof(struct xdr_uio) +
			      count * sizeof(struct xdr_vio);

		/* drop those bytes from the last buffer */
		uio->uio_vio[last].vio_tail -= n;
		uio->uio_vio[last].vio_wrap -= n;
		uio->uio_vio[last].vio_length -= n;

		LogFullDebug(
			COMPONENT_DISPATCH,
			"Extra trim uio_vio[%d].vio_base %p vio_length %" PRIu32,
			last, uio->uio_vio[last].vio_base,
			uio->uio_vio[last].vio_length);

		/* move the bytes to the extra buffer and set it up as a
		 * BYTES_PER_XDR_UNIT (4) byte buffer. Because it is part of the
		 * memory we allocated above with calloc, the extra bytes are
		 * already zeroed.
		 */
		memcpy(extra, p, n);

		i = count - 1;
		uio->uio_vio[i].vio_base = extra;
		uio->uio_vio[i].vio_head = extra;
		uio->uio_vio[i].vio_tail = extra + BYTES_PER_XDR_UNIT;
		uio->uio_vio[i].vio_wrap = extra + BYTES_PER_XDR_UNIT;
		uio->uio_vio[i].vio_length = BYTES_PER_XDR_UNIT;
		uio->uio_vio[i].vio_type = VIO_DATA;

		LogFullDebug(
			COMPONENT_DISPATCH,
			"Extra uio_vio[%d].vio_base %p vio_length %" PRIu32, i,
			uio->uio_vio[i].vio_base, uio->uio_vio[i].vio_length);

		/* Remember so we don't free... */
		uio->uio_u3 = extra;
	}

putbufs:
	LogFullDebug(COMPONENT_DISPATCH,
		     "Allocated %p, references %" PRIi32 ", count %d", uio,
		     uio->uio_references, (int)uio->uio_count);

	if (!xdr_putbufs(xdrs, uio, UIO_FLAG_NONE)) {
		uio->uio_release(uio, UIO_FLAG_NONE);
		return false;
	}

	return true;
}

void release_io_data_copy(void *release_data)
{
	io_data *objp = release_data;
	int i;

	for (i = 0; i < objp->iovcnt; i++)
		gsh_free(objp->iov[i].iov_base);
}

static inline bool xdr_io_data_decode(XDR *xdrs, io_data *objp)
{
	uint32_t start;
	struct xdr_vio *vio;
	int i;
	size_t totlen = 0;

	/* Get the data_len */
	if (!inline_xdr_u_int32_t(xdrs, &objp->data_len))
		return false;

	LogFullDebug(COMPONENT_DISPATCH, "data_len = %u", objp->data_len);

	if (objp->data_len == 0) {
		/* Special handling of length 0. */
		objp->iov = gsh_calloc(1, sizeof(*objp->iov));
		i = 0;

		objp->iovcnt = 1;
		objp->iov[i].iov_base = NULL;
		objp->iov[i].iov_len = 0;

		LogFullDebug(COMPONENT_DISPATCH,
			     "iov[%d].iov_base %p iov_len %zu for %zu of %u", i,
			     objp->iov[i].iov_base, objp->iov[i].iov_len,
			     totlen, objp->data_len);
		return true;
	}

	/* Get the current position in the stream */
	start = XDR_GETPOS(xdrs);

	/* For rdma_reads data received separately, so get data buffer */
	start = XDR_GETSTARTDATAPOS(xdrs, start, objp->data_len);

	/* Find out how many buffers the data occupies */
	objp->iovcnt = XDR_IOVCOUNT(xdrs, start, objp->data_len);

	LogFullDebug(COMPONENT_DISPATCH, "iovcnt = %u", objp->iovcnt);

	if (objp->iovcnt > IOV_MAX) {
		char *buf;

		LogInfo(COMPONENT_DISPATCH,
			"bypassing zero-copy, io_data iovcnt %u exceeds IOV_MAX, allocating %u byte buffer",
			objp->iovcnt, objp->data_len);

		/** @todo - Can we do something different? Do we really need to?
		 *          Does anything other than pynfs with large I/O
		 *          trigger this?
		 */
		/* The iovec is too big, we will have to copy, allocate and use
		 * a single buffer.
		 */
		objp->iovcnt = 1;
		objp->iov = gsh_calloc(1, sizeof(*objp->iov));
		buf = gsh_malloc(objp->data_len);
		objp->iov[0].iov_base = buf;
		objp->iov[0].iov_len = objp->data_len;

		if (!xdr_opaque_decode(xdrs, buf, objp->data_len)) {
			gsh_free(buf);
			gsh_free(objp->iov);
			objp->iov = NULL;
			return false;
		}

		objp->release = release_io_data_copy;
		objp->release_data = objp;

		return true;
	}

	/* Allocate a vio to extract the data buffers into */
	vio = gsh_calloc(objp->iovcnt, sizeof(*vio));

	/* Get the data buffers - XDR_FILLBUFS happens to do what we want... */
	if (!XDR_FILLBUFS(xdrs, start, vio, objp->data_len)) {
		gsh_free(vio);
		return false;
	}

	/* Now allocate an iovec to carry the data */
	objp->iov = gsh_calloc(objp->iovcnt, sizeof(*objp->iov));

	/* Convert the xdr_vio to an iovec */
	for (i = 0; i < objp->iovcnt; i++) {
		objp->iov[i].iov_base = vio[i].vio_head;
		objp->iov[i].iov_len = vio[i].vio_length;
		totlen += vio[i].vio_length;
		LogFullDebug(COMPONENT_DISPATCH,
			     "iov[%d].iov_base %p iov_len %zu for %zu of %u", i,
			     objp->iov[i].iov_base, objp->iov[i].iov_len,
			     totlen, objp->data_len);
	}

	/* We're done with the vio */
	gsh_free(vio);

	/* Now advance the position past the data (rounding up data_len) */
	if (!XDR_SETPOS(xdrs, XDR_GETENDDATAPOS(xdrs, start,
						RNDUP(objp->data_len)))) {
		gsh_free(objp->iov);
		objp->iov = NULL;
		return false;
	}

	objp->release = NULL;
	objp->release_data = NULL;

	return true;
}

bool xdr_io_data(XDR *xdrs, io_data *objp)
{
	if (xdrs->x_op == XDR_ENCODE) {
		/* We are going to use putbufs */
		return xdr_io_data_encode(xdrs, objp);
	}

	if (xdrs->x_op == XDR_DECODE) {
		/* We are going to use putbufs */
		return xdr_io_data_decode(xdrs, objp);
	}

	/* All that remains is XDR_FREE */
	if (objp->release != NULL)
		objp->release(objp->release_data);

	gsh_free(objp->iov);
	objp->iov = NULL;

	return true;
}
