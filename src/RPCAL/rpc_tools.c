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
 * @brief   Some tools very usefull in the nfs protocol implementation.
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

const char *str_sock_type(int st)
{
	static char buf[16];

	switch (st) {
	case SOCK_STREAM:
		return "SOCK_STREAM";
	case SOCK_DGRAM:
		return "SOCK_DGRAM ";
	case SOCK_RAW:
		return "SOCK_RAW   ";
	}
	sprintf(buf, "%d", st);
	return buf;
}

const char *str_ip_proto(int p)
{
	static char buf[16];

	switch (p) {
	case IPPROTO_IP:
		return "IPPROTO_IP ";
	case IPPROTO_TCP:
		return "IPPROTO_TCP";
	case IPPROTO_UDP:
		return "IPPROTO_UDP";
	}
	sprintf(buf, "%d", p);
	return buf;
}

const char *str_af(int af)
{
	static char buf[16];

	switch (af) {
	case AF_INET:
		return "AF_INET ";
	case AF_INET6:
		return "AF_INET6";
#ifdef RPC_VSOCK
	case AF_VSOCK:
		return "AF_VSOCK";
#endif /* VSOCK */
	}
	sprintf(buf, "%d", af);
	return buf;
}

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

bool copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt)
{
	struct netbuf *phostaddr = svc_getcaller_netbuf(xprt);

	if (phostaddr->len > sizeof(sockaddr_t) || phostaddr->buf == NULL)
		return 0;
	memcpy(addr, phostaddr->buf, phostaddr->len);
	return 1;
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

int display_sockaddr(struct display_buffer *dspbuf, sockaddr_t *addr)
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

	case AF_LOCAL:
		return display_cat(dspbuf,
				   ((struct sockaddr_un *)addr)->sun_path);
	}

	if (name == NULL)
		return display_cat(dspbuf, "<unknown>");
	else
		return display_printf(dspbuf, "%s:%d", name, port);
}

int sprint_sockip(sockaddr_t *addr, char *buf, int len)
{
	const char *name = NULL;

	memset(buf, 0, len);

	switch (addr->ss_family) {
	case AF_INET:
		name =
		    inet_ntop(addr->ss_family,
			      &(((struct sockaddr_in *)addr)->sin_addr), buf,
			      len);
		break;
	case AF_INET6:
		name =
		    inet_ntop(addr->ss_family,
			      &(((struct sockaddr_in6 *)addr)->sin6_addr), buf,
			      len);
		break;
	case AF_LOCAL:
		strlcpy(buf, ((struct sockaddr_un *)addr)->sun_path, len);
		name = buf;
	}

	if (name == NULL) {
		strlcpy(buf, "<unknown>", len);
		return 0;
	}

	return 1;
}

/**
 *
 * @brief Compare 2 sockaddrs, including ports
 *
 * @param[in] addr_1      First address
 * @param[in] addr_2      Second address
 * @param[in] ignore_port Whether to ignore the port
 *
 * @return Comparator trichotomy,
 */
int cmp_sockaddr(sockaddr_t *addr_1, sockaddr_t *addr_2,
		 bool ignore_port)
{
	if (addr_1->ss_family != addr_2->ss_family)
		return 0;

	switch (addr_1->ss_family) {
	case AF_INET:
	{
		struct sockaddr_in *inaddr1 =
			(struct sockaddr_in *)addr_1;
		struct sockaddr_in *inaddr2 =
			(struct sockaddr_in *)addr_2;

		return (inaddr1->sin_addr.s_addr ==
			inaddr2->sin_addr.s_addr
			&& (ignore_port ||
			    inaddr1->sin_port == inaddr2->sin_port));
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *ip6addr1 =
			(struct sockaddr_in6 *)addr_1;
		struct sockaddr_in6 *ip6addr2 =
			(struct sockaddr_in6 *)addr_2;

		return (memcmp
			(ip6addr1->sin6_addr.s6_addr,
			 ip6addr2->sin6_addr.s6_addr,
			 sizeof(ip6addr2->sin6_addr.s6_addr)) == 0) &&
			(ignore_port ||
			 ip6addr1->sin6_port == ip6addr2->sin6_port);
		}
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
int sockaddr_cmpf(sockaddr_t *addr1, sockaddr_t *addr2,
		  bool ignore_port)
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
	default:
		/* unhandled AF */
		return -2;
	}
}

in_addr_t get_in_addr(sockaddr_t *addr)
{
	if (addr->ss_family == AF_INET)
		return ((struct sockaddr_in *)addr)->sin_addr.s_addr;
	else
		return 0;
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

int ipstring_to_sockaddr(const char *str, sockaddr_t *addr)
{
	struct addrinfo *info, hints, *p;
	int rc;
	char ipname[SOCK_NAME_MAX + 1];

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = 0;
	rc = getaddrinfo(str, NULL, &hints, &info);
	if (rc == 0 && info != NULL) {
		p = info;
		if (isFullDebug(COMPONENT_RPC)) {
			while (p != NULL) {
				sprint_sockip((sockaddr_t *) p->ai_addr,
					      ipname, sizeof(ipname));
				LogFullDebug(COMPONENT_RPC,
					     "getaddrinfo %s returned %s family=%s socktype=%s protocol=%s",
					     str, ipname,
					     str_af(p->ai_family),
					     str_sock_type(p->ai_socktype),
					     str_ip_proto(p->ai_protocol));
				p = p->ai_next;
			}
		}
		memcpy(addr, info->ai_addr, info->ai_addrlen);
		freeaddrinfo(info);
	} else {
		switch (rc) {
		case EAI_SYSTEM:
			LogFullDebug(COMPONENT_RPC,
				     "getaddrinfo %s returned %d(%s)", str,
				     errno, strerror(errno));
			break;
		default:
			LogFullDebug(COMPONENT_RPC,
				     "getaddrinfo %s returned %d(%s)", str, rc,
				     gai_strerror(rc));
		}
	}
	return rc;
}
