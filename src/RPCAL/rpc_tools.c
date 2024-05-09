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
#ifdef RPC_VSOCK
#include <linux/vm_sockets.h>
#endif /* VSOCK */
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
	struct netbuf *phostaddr = svc_getcaller_netbuf(xprt);

	assert(phostaddr->len <= sizeof(sockaddr_t) && phostaddr->buf != NULL);
	memcpy(addr, phostaddr->buf, phostaddr->len);
}

/**
 * @brief Create a hash value based on the sockaddr_t structure
 *
 * This creates a native pointer size (unsigned long int) hash value
 * from the sockaddr_t structure. It supports both IPv4 and IPv6,
 * other types can be added in time.
 *
 * XXX is this hash...good?
 *
 * @param[in] addr        sockaddr_t address to hash
 * @param[in] ignore_port Whether to ignore the port
 *
 * @return hash value
 *
 */
uint64_t hash_sockaddr(sockaddr_t *addr, bool ignore_port)
{
	unsigned long addr_hash = 0;
	int port;

	switch (addr->ss_family) {
	case AF_INET:
		{
			struct sockaddr_in *paddr = (struct sockaddr_in *)addr;

			addr_hash = paddr->sin_addr.s_addr;
			if (!ignore_port) {
				port = paddr->sin_port;
				addr_hash ^= (port << 16);
			}
			break;
		}
	case AF_INET6:
		{
			struct sockaddr_in6 *paddr =
			    (struct sockaddr_in6 *)addr;
			uint32_t *va;

			va = (uint32_t *)&paddr->sin6_addr;
			addr_hash = va[0] ^ va[1] ^ va[2] ^ va[3];
			if (!ignore_port) {
				port = paddr->sin6_port;
				addr_hash ^= (port << 16);
			}
			break;
		}
#ifdef RPC_VSOCK
	case AF_VSOCK:
	{
		struct sockaddr_vm *svm; /* XXX checkpatch horror */

		svm = (struct sockaddr_vm *) addr;
		addr_hash = svm->svm_cid;
		if (!ignore_port)
			addr_hash ^= svm->svm_port;
	}
#endif /* VSOCK */
	default:
		break;
	}

	return addr_hash;
}

int display_sockaddr_port(struct display_buffer *dspbuf,
			  const sockaddr_t *addr, bool ignore_port)
{
	const char *name = NULL;
	char ipname[SOCK_NAME_MAX];
	int port = 0;
	int b_left = display_start(dspbuf);

	if (b_left <= 0)
		return b_left;

	switch (addr->ss_family) {
	case AF_INET:
		name = inet_ntop(addr->ss_family,
				 &(((struct sockaddr_in *)addr)->sin_addr),
				 ipname,
				 sizeof(ipname));
		port = ntohs(((struct sockaddr_in *)addr)->sin_port);
		break;

	case AF_INET6:
		name = inet_ntop(addr->ss_family,
				 &(((struct sockaddr_in6 *)addr)->sin6_addr),
				 ipname,
				 sizeof(ipname));
		port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
		break;

#ifdef RPC_VSOCK
	case AF_VSOCK:
		return display_printf(dspbuf, "%s:%d",
				      ((struct sockaddr_vm *)addr)->svm_cid,
				      ((struct sockaddr_vm *)addr)->svm_port);
#endif /* VSOCK */

	case AF_LOCAL:
		return display_cat(dspbuf,
				   ((struct sockaddr_un *)addr)->sun_path);
	}

	if (name == NULL)
		return display_cat(dspbuf, "<unknown>");
	else if (ignore_port)
		return display_cat(dspbuf, name);
	else
		return display_printf(dspbuf, "%s:%d", name, port);
}

/**
 *
 * @brief Compare 2 sockaddrs, including ports
 *
 * @param[in] addr_1      First address
 * @param[in] addr_2      Second address
 * @param[in] ignore_port Whether to ignore the port
 *
 * @return Comparator true/false,
 */
int cmp_sockaddr(sockaddr_t *addr_1, sockaddr_t *addr_2, bool ignore_port)
{
	if (addr_1->ss_family != addr_2->ss_family)
		return 0;

	switch (addr_1->ss_family) {
	case AF_INET:
	{
		struct sockaddr_in *inaddr1 = (struct sockaddr_in *)addr_1;
		struct sockaddr_in *inaddr2 = (struct sockaddr_in *)addr_2;

		return (inaddr1->sin_addr.s_addr == inaddr2->sin_addr.s_addr
			&& (ignore_port ||
			    inaddr1->sin_port == inaddr2->sin_port));
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *ip6addr1 = (struct sockaddr_in6 *)addr_1;
		struct sockaddr_in6 *ip6addr2 = (struct sockaddr_in6 *)addr_2;

		return (memcmp(ip6addr1->sin6_addr.s6_addr,
			       ip6addr2->sin6_addr.s6_addr,
			       sizeof(ip6addr2->sin6_addr.s6_addr)) == 0)
			&& (ignore_port ||
			    ip6addr1->sin6_port == ip6addr2->sin6_port);
	}
	break;
#ifdef RPC_VSOCK
	case AF_VSOCK:
	{
		struct sockaddr_vm *svm1 = (struct sockaddr_vm *)addr_1;
		struct sockaddr_vm *svm2 = (struct sockaddr_vm *)addr_2;


		return (svm1->svm_cid == svm2->svm_cid
			&& (ignore_port || svm1->svm_port == svm2->svm_port));
	}
	break;
#endif /* VSOCK */
	default:
		return 0;
	}
}

/**
 * @brief Canonically compare 2 sockaddrs
 *
 * @param[in] addr1       First address
 * @param[in] addr2       Second address
 * @param[in] ignore_port Whether to ignore the port
 *
 * @return Comparator trichotomy
 */
int sockaddr_cmpf(sockaddr_t *addr1, sockaddr_t *addr2, bool ignore_port)
{
	switch (addr1->ss_family) {
	case AF_INET:
	{
		struct sockaddr_in *in1 = (struct sockaddr_in *)addr1;
		struct sockaddr_in *in2 = (struct sockaddr_in *)addr2;

		if (in1->sin_addr.s_addr < in2->sin_addr.s_addr)
			return -1;

		if (in1->sin_addr.s_addr == in2->sin_addr.s_addr) {
			if (ignore_port)
				return 0;
			/* else */
			if (in1->sin_port < in2->sin_port)
				return -1;
			if (in1->sin_port == in2->sin_port)
				return 0;
			return 1;
		}
		return 1;
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *in1 = (struct sockaddr_in6 *)addr1;
		struct sockaddr_in6 *in2 = (struct sockaddr_in6 *)addr2;
		int acmp = memcmp(in1->sin6_addr.s6_addr,
				  in2->sin6_addr.s6_addr,
				  sizeof(struct in6_addr));
		if (acmp == 0) {
			if (ignore_port)
				return 0;
			/* else */
			if (in1->sin6_port < in2->sin6_port)
				return -1;
			if (in1->sin6_port == in2->sin6_port)
				return 0;
			return 1;
		} else
			return acmp < 0 ? -1 : 1;
	}
#ifdef RPC_VSOCK
	case AF_VSOCK:
	{
		struct sockaddr_vm *svm1 = (struct sockaddr_vm *)addr1;
		struct sockaddr_vm *svm2 = (struct sockaddr_vm *)addr2;

		if (svm1->svm_cid < svm2->svm_cid)
			return -1;

		if (svm1->svm_cid == svm2->svm_cid) {
			if (ignore_port)
				return 0;
			/* else */
			if (svm1->svm_port < svm2->svm_port)
				return -1;
			if (svm1->svm_port == svm2->svm_port)
				return 0;
			return 1;
		}
		return 1;
	}
	break;
#endif /* VSOCK */
	default:
		/* unhandled AF */
		return -2;
	}
}

int get_port(sockaddr_t *addr)
{
	switch (addr->ss_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)addr)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
#ifdef RPC_VSOCK
	case AF_VSOCK:
		return ((struct sockaddr_vm *)addr)->svm_port;
#endif /* VSOCK */
	default:
		return -1;
	}
}

static char ten_bytes_all_0[10];

/**
 * @brief Check if address is IPv4 encapsulated into IPv6, if so convert
 *        the address to IPv4 and return that one, otherwise return the
 *        supplied address.
 *
 * @param[in] ipv6  The input address which may be IPv4, IPV6, or encapsulated
 * @param[in] ipv4  sockattr_t buffer to create IPv4 address into
 *
 * @returns ipv6 unless an encapsulated address was converted, then ipv4
 */
sockaddr_t *convert_ipv6_to_ipv4(sockaddr_t *ipv6, sockaddr_t *ipv4)
{
	struct sockaddr_in *paddr = (struct sockaddr_in *)ipv4;
	struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *)ipv6;

	/* If the client socket is IPv4, then it is wrapped into a
	 * ::ffff:a.b.c.d IPv6 address. We check this here.
	 * This kind of address is shaped like this:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 */
	if ((ipv6->ss_family == AF_INET6)
	    && !memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10)
	    && (psockaddr_in6->sin6_addr.s6_addr[10] == 0xFF)
	    && (psockaddr_in6->sin6_addr.s6_addr[11] == 0xFF)) {
		void *ab;

		memset(ipv4, 0, sizeof(*ipv4));
		ab = &(psockaddr_in6->sin6_addr.s6_addr[12]);

		paddr->sin_port = psockaddr_in6->sin6_port;
		paddr->sin_addr.s_addr = *(in_addr_t *) ab;
		ipv4->ss_family = AF_INET;

		if (isMidDebug(COMPONENT_EXPORT)) {
			char ipstring4[SOCK_NAME_MAX];
			char ipstring6[SOCK_NAME_MAX];
			struct display_buffer dspbuf4 = {
				sizeof(ipstring4), ipstring4, ipstring4};
			struct display_buffer dspbuf6 = {
				sizeof(ipstring6), ipstring6, ipstring6};

			display_sockip(&dspbuf4, ipv4);
			display_sockip(&dspbuf6, ipv6);
			LogMidDebug(COMPONENT_EXPORT,
				    "Converting IPv6 encapsulated IPv4 address %s to IPv4 %s",
				    ipstring6, ipstring4);
		}

		return ipv4;
	} else {
		return ipv6;
	}
}

/**
 *
 * @brief Test if addr is loopback
 *
 * @param[in] addr      Address
 *
 * @return Comparator true/false,
 */
bool is_loopback(sockaddr_t *addr)
{
	struct sockaddr_in6 *ip6addr = (struct sockaddr_in6 *)addr;

	if (addr->ss_family == AF_INET) {
		struct sockaddr_in *inaddr = (struct sockaddr_in *)addr;

		return (((char *)&(inaddr->sin_addr.s_addr))[0] == 0x7F);
	} else if (addr->ss_family != AF_INET6) {
		return false;
	}

	/* If the client socket is IPv4, then it is wrapped into a
	 * ::ffff:a.b.c.d IPv6 address. We check this here.
	 * This kind of address is shaped like this:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 *
	 * An IPv4 loop back address is 127.b.c.d, so we only need to examine
	 * the first byte past ::ffff, or s6_addr[12].
	 *
	 * Otherwise we compare to ::1
	 */
	return (!memcmp(ip6addr->sin6_addr.s6_addr, ten_bytes_all_0, 10)
			&& (ip6addr->sin6_addr.s6_addr[10] == 0xFF)
			&& (ip6addr->sin6_addr.s6_addr[11] == 0xFF)
			&& (ip6addr->sin6_addr.s6_addr[12] == 0x7F))
		|| (memcmp(ip6addr->sin6_addr.s6_addr,
			   &in6addr_loopback,
			   sizeof(in6addr_loopback)) == 0);
}

static void xdr_io_data_uio_release(struct xdr_uio *uio, u_int flags)
{
	int ix;
	io_data *io_data = uio->uio_u2;

	LogFullDebug(COMPONENT_DISPATCH,
		     "Releasing %p, references %"PRIi32", count %d",
		     uio, uio->uio_references, (int) uio->uio_count);

	if (--uio->uio_references != 0)
		return;

	if (io_data->release != NULL) {
		/* Handle the case where the io_data comes with its
		 * own release method.
		 *
		 * Note if extra buffer was used to handle RNDUP, the
		 * io_data release function doesn't even know about it so will
		 * not free it.
		 */
		io_data->release(io_data->release_data);
	} else {
		if (uio->uio_u1 != NULL) {
			/* Don't free the last buffer! It was allocated along
			 * with uio..
			 */
			uio->uio_count--;
		}

		/* Free the buffers that had been allocated */
		for (ix = 0; ix < uio->uio_count; ix++)
			gsh_free(uio->uio_vio[ix].vio_base);
	}

	gsh_free(uio);
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
	uio->uio_u2 = objp;

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
			     objp->iov, i,
			     objp->iov[i].iov_base,
			     i_size,
			     totlen, objp->data_len);
	}

	if (size != size2) {
		/* grab the last N bytes of last buffer into extra */
		size_t n = size % BYTES_PER_XDR_UNIT;
		char *p = uio->uio_vio[last].vio_base +
			  uio->uio_vio[last].vio_length - n;
		char *extra = (char *) uio + sizeof(struct xdr_uio) +
					     count * sizeof(struct xdr_vio);

		/* drop those bytes from the last buffer */
		uio->uio_vio[last].vio_tail -= n;
		uio->uio_vio[last].vio_wrap -= n;
		uio->uio_vio[last].vio_length -= n;

		LogFullDebug(COMPONENT_DISPATCH,
			     "Extra trim uio_vio[%d].vio_base %p vio_length %"
			     PRIu32,
			     last,
			     uio->uio_vio[last].vio_base,
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

		LogFullDebug(COMPONENT_DISPATCH,
			     "Extra uio_vio[%d].vio_base %p vio_length %"PRIu32,
			     i,
			     uio->uio_vio[i].vio_base,
			     uio->uio_vio[i].vio_length);

		/* Remember so we don't free... */
		uio->uio_u1 = extra;
	}

	LogFullDebug(COMPONENT_DISPATCH,
		     "Allocated %p, references %"PRIi32", count %d",
		     uio, uio->uio_references, (int) uio->uio_count);

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

	LogFullDebug(COMPONENT_DISPATCH,
		     "data_len = %u",
		     objp->data_len);

	if (objp->data_len == 0) {
		/* Special handling of length 0. */
		objp->iov = gsh_calloc(1, sizeof(*objp->iov));
		i = 0;

		objp->iovcnt = 1;
		objp->iov[i].iov_base = NULL;
		objp->iov[i].iov_len = 0;

		LogFullDebug(COMPONENT_DISPATCH,
			     "iov[%d].iov_base %p iov_len %zu for %zu of %u",
			     i,
			     objp->iov[i].iov_base,
			     objp->iov[i].iov_len,
			     totlen, objp->data_len);
		return true;
	}

	/* Get the current position in the stream */
	start = XDR_GETPOS(xdrs);

	/* Find out how many buffers the data occupies */
	objp->iovcnt = XDR_IOVCOUNT(xdrs, start, objp->data_len);

	LogFullDebug(COMPONENT_DISPATCH,
		     "iovcnt = %u",
		     objp->iovcnt);

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
			     "iov[%d].iov_base %p iov_len %zu for %zu of %u",
			     i,
			     objp->iov[i].iov_base,
			     objp->iov[i].iov_len,
			     totlen, objp->data_len);
	}

	/* We're done with the vio */
	gsh_free(vio);

	/* Now advance the position past the data (rounding up data_len) */
	if (!XDR_SETPOS(xdrs, start + RNDUP(objp->data_len))) {
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

/**
 *
 * @brief  API to get the buffer to fill the IO Payloads,
 *
 * @param[in] req  The svc request structure
 * @param[in] size Requested size of buffer to be allocated.
 *
 * @returns The address of the buffer to fill the payload
 */
void *get_buffer_for_io_response(uint64_t size)
{
#ifdef _USE_NFS_RDMA
	struct nfs_request *nfs_req;
	struct svc_req *req;

	if (op_ctx->req_type != NFS_REQUEST)
		return NULL;

	nfs_req = container_of(op_ctx, struct nfs_request, op_context);
	req = &nfs_req->svc;

	/* Whether it's RDMA enabled xprt and having data chunk */
	if (req->rq_xprt->xp_rdma && req->data_chunk) {
		LogDebug(COMPONENT_TIRPC,
			 "Using data_chunk %p length %d from req %p xprt %p",
			 req->data_chunk, req->data_chunk_length,
			 req, req->rq_xprt);
		assert(size <= req->data_chunk_length);
		op_ctx->is_rdma_buff_used = true;
		return req->data_chunk;
	}
#endif /* _USE_NFS_RDMA */

	/* No special buffer requirements */
	return NULL;
}
