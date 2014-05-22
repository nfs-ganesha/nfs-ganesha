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
	}
	sprintf(buf, "%d", af);
	return buf;
}

const char *xprt_type_to_str(xprt_type_t type)
{
	switch (type) {
	case XPRT_UDP:
		return "udp";
	case XPRT_TCP:
		return "tcp";
	case XPRT_TCP_RENDEZVOUS:
		return "tcp rendezvous";
	case XPRT_UNKNOWN:
		return "UNKNOWN";
	case XPRT_SCTP:
		return "sctp";
	case XPRT_RDMA:
		return "rdma";
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
	default:
		break;
	}

	return addr_hash;
}

int sprint_sockaddr(sockaddr_t *addr, char *buf, int len)
{
	const char *name = NULL;
	int port, alen;

	buf[0] = '\0';

	switch (addr->ss_family) {
	case AF_INET:
		name =
		    inet_ntop(addr->ss_family,
			      &(((struct sockaddr_in *)addr)->sin_addr), buf,
			      len);
		port = ntohs(((struct sockaddr_in *)addr)->sin_port);
		break;
	case AF_INET6:
		name =
		    inet_ntop(addr->ss_family,
			      &(((struct sockaddr_in6 *)addr)->sin6_addr), buf,
			      len);
		port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
		break;
	case AF_LOCAL:
		strncpy(buf, ((struct sockaddr_un *)addr)->sun_path, len);
		name = buf;
		port = -1;
		break;
	default:
		port = -1;
	}

	alen = strlen(buf);

	if (name == NULL) {
		strncpy(buf, "<unknown>", len);
		return 0;
	}

	if (port >= 0 && alen < len)
		snprintf(buf + alen, len - alen, ":%d", port);
	return 1;
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
		strncpy(buf, ((struct sockaddr_un *)addr)->sun_path, len);
		name = buf;
	}

	if (name == NULL) {
		strncpy(buf, "<unknown>", len);
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
	default:
		return -1;
	}
}

void socket_setoptions(int socketFd)
{
	unsigned int SbMax = (1 << 30);	/* 1GB */

	while (SbMax > 1048576) {
		if ((setsockopt
		     (socketFd, SOL_SOCKET, SO_SNDBUF, (char *)&SbMax,
		      sizeof(SbMax)) < 0)
		    ||
		    (setsockopt
		     (socketFd, SOL_SOCKET, SO_RCVBUF, (char *)&SbMax,
		      sizeof(SbMax)) < 0)) {
			SbMax >>= 1;	/* SbMax = SbMax/2 */
			continue;
		}

		break;
	}

	return;
}				/* socket_setoptions_ctrl */

#define SIZE_AI_ADDR sizeof(struct sockaddr)

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
				sprint_sockaddr((sockaddr_t *) p->ai_addr,
						ipname, sizeof(ipname));
				LogFullDebug(COMPONENT_RPC,
					     "getaddrinfo %s returned %s family=%s socktype=%s "
					     "protocol=%s", str, ipname,
					     str_af(p->ai_family),
					     str_sock_type(p->ai_socktype),
					     str_ip_proto(p->ai_protocol));
				p = p->ai_next;
			}
		}
		memcpy(addr, info->ai_addr, SIZE_AI_ADDR);
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

pthread_mutex_t clnt_create_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * TI-RPC's clnt_create hierarchy probably isn't re-entrant.  While we
 * -will- make it so, serialize these for now.
 */
CLIENT *gsh_clnt_create(char *host, unsigned long prog, unsigned long vers,
			char *proto)
{
	CLIENT *clnt;
	pthread_mutex_lock(&clnt_create_mutex);
	clnt = clnt_create(host, prog, vers, proto);
	if (clnt == NULL) {
		const char *err = clnt_spcreateerror("clnt_create failed");
		LogDebug(COMPONENT_RPC, "%s", err);
	}
	pthread_mutex_unlock(&clnt_create_mutex);
	return clnt;
}

void gsh_clnt_destroy(CLIENT *clnt)
{
	pthread_mutex_lock(&clnt_create_mutex);
	clnt_destroy(clnt);
	pthread_mutex_unlock(&clnt_create_mutex);
}
