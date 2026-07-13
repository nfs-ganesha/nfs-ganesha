/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 *
 * Copyright: DataDirect Networks, 2025
 * contributeur : Peter Schwenke   pschwenke@ddn.com
 *                Martin Schwenke  mschwenke@ddn.com
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file ip_utils.h
 * @brief Common tools for IP Address parsing, converting, comparing...
 */

#ifndef __IPUTILS_H
#define __IPUTILS_H

#include <sys/types.h>
#include <netinet/in.h>
#include <stdbool.h>
#include "display.h"
#include "mem_components.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sockaddr_storage sockaddr_t;

/* Allow much more space than we really need for a sock name. An IPV4 address
 * embedded in IPv6 could use 45 bytes and then if we add a port, that would be
 * an additional 6 bytes (:65535) for a total of 51, and then one more for NUL
 * termination. We could use 64 instead of 128.
 */
#define SOCK_NAME_MAX 128

typedef struct cidr_addr {
	sockaddr_t ip_addr;
	u_int16_t mask;
} CIDR;

CIDR *cidr_alloc(mem_components_t);
CIDR *cidr_dup(const CIDR *);
void cidr_free(CIDR *, mem_components_t);
CIDR *cidr_from_str(log_components_t, const char *, mem_components_t);
char *cidr_to_str(CIDR *, mem_components_t);
CIDR *cidr_from_inaddr(const struct in_addr *, mem_components_t);
CIDR *cidr_from_in6addr(const struct in6_addr *, mem_components_t);
int cidr_contains_ip(CIDR *, sockaddr_t *);
void cidr_ipaddr_to_chars(CIDR *, unsigned char *);
void cidr_mask_to_chars(CIDR *, unsigned char *);
int cidr_family(CIDR *);
int cidr_proto(CIDR *);
int cidr_version(CIDR *);
bool cidr_equals(log_components_t, CIDR *, CIDR *);
void normalize_v4_mapped_cidr(log_components_t, CIDR *cidr);

int sockaddr_cmp(log_components_t, sockaddr_t *, sockaddr_t *, bool);
uint64_t hash_sockaddr(sockaddr_t *, bool);
int ip_str_to_sockaddr(log_components_t, const char *, sockaddr_t *);

int get_port(sockaddr_t *);

sockaddr_t *convert_ipv6_to_ipv4(log_components_t component, sockaddr_t *ipv6,
				 sockaddr_t *ipv4);
sockaddr_t *ipv4_to_ipv4_mapped_ipv6(log_components_t component,
				     sockaddr_t *ipv4, sockaddr_t *ipv6);
bool is_loopback(sockaddr_t *addr);
bool is_inaddrany(sockaddr_t *addr);

static inline bool is_af_local(sockaddr_t *addr)
{
	return addr->ss_family == AF_LOCAL;
}

int display_sockaddr_port(struct display_buffer *dspbuf, const sockaddr_t *addr,
			  bool ignore_port);

static inline int display_sockaddr(struct display_buffer *dspbuf,
				   const sockaddr_t *addr)
{
	return display_sockaddr_port(dspbuf, addr, false);
}

static inline int display_sockip(struct display_buffer *dspbuf,
				 const sockaddr_t *addr)
{
	return display_sockaddr_port(dspbuf, addr, true);
}

static inline void *socket_addr(sockaddr_t *addr)
{
	switch (addr->ss_family) {
	case AF_INET:
		return &(((struct sockaddr_in *)addr)->sin_addr);
	case AF_INET6:
		return &(((struct sockaddr_in6 *)addr)->sin6_addr);
#ifdef RPC_VSOCK
	case AF_VSOCK:
		return &(((struct sockaddr_vm *)addr)->svm_cid);
#endif /* VSOCK */
	default:
		return addr;
	}
}

static inline size_t socket_addr_len(sockaddr_t *addr)
{
	switch (addr->ss_family) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
#ifdef RPC_VSOCK
	case AF_VSOCK:
		return sizeof(((struct sockaddr_vm *)addr)->svm_cid);
#endif /* VSOCK */
	default:
		return sizeof(sockaddr_t);
	}
}

static inline int get_sockport(sockaddr_t *addr)
{
#ifdef RPC_VSOCK
	if (addr->ss_family == AF_VSOCK)
		return ((struct sockaddr_vm *)addr)->svm_port;
#endif /* VSOCK */

	if (addr->ss_family == AF_INET)
		return ntohs(((struct sockaddr_in *)addr)->sin_port);

	if (addr->ss_family == AF_INET6)
		return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);

	return -1;
}

static inline bool sprint_sockip(sockaddr_t *addr, char *buf, int len)
{
#ifdef RPC_VSOCK
	if (addr->ss_family == AF_VSOCK) {
		int rc = snprintf(buf, len, "%d",
				  ((struct sockaddr_vm *)addr)->svm_cid);
		return rc >= 0 && rc < len;
	}
#endif /* VSOCK */

	if (addr->ss_family != AF_INET && addr->ss_family != AF_INET6)
		return false;

	return inet_ntop(addr->ss_family, socket_addr(addr), buf, len) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* __IPUTILS_H */
