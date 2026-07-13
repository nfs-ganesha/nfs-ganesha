// SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright CEA/DAM/DIF, (2008-2025)
 * Copyright: DataDirect Networks, 2025
 * Copyright (c) Matthew D. Fuller <fullermd@over-yonder.net> 2005, 2006
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *                Matt Benjamin <mbenjamin@redhat.com>
 *                Sean Dague <sdague@linux.vnet.ibm.com>
 *                Adam C. Emerson <aemerson@linuxbox.com>
 *                Frank S. Filz <ffilzlnx@mindspring.com>
 *                Gaurav Gangalwar <gaurav.gangalwar@nutanix.com>
 *                Dipit Grover <dipit@google.com>
 *                Kaleb S KEITHLEY <kkeithle@redhat.com>
 *                Jim Lieb <lieb@sea-troll.net>
 *                Malahal Naineni <malahal@us.ibm.com>
 *                Martin Schwenke  mschwenke@ddn.com
 *                Peter Schwenke   pschwenke@ddn.com
 *                Paul Sheer <paulsheer@gmail.com>
 *                Lior Suliman <liorsu@gmail.com>
 *
 * We thank Matthew D. Fuller for providing the original libcidr used
 * by the NFS Ganesha project for a long time.  The cidr_* functions herein
 * mostly match the signature and functionality of the libcidr versions whilst
 * being completely new implementations.
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
 * @file ip_utils.c
 * @brief Tools for IP Addresses and CIDRs - parsing, converting, comparing...
 */

#include <errno.h>
#include <sys/un.h>

#include "log_common.h"
#include "ip_utils.h"

#ifdef RPC_VSOCK
#include <linux/vm_sockets.h>
#endif /* VSOCK */

#include "log.h"
#include "abstract_mem.h"
#include "common_utils.h"

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
	uint64_t addr_hash = 0;
	uint64_t port_hash = 0;

	switch (addr->ss_family) {
	case AF_INET: {
		/* Hash IPv4 in host order so it matches IPv4-mapped IPv6
		 * (::ffff:a.b.c.d). With ntohl on each IPv6 word, the mapped
		 * form's third word is 0xffff in host order on all endianness,
		 * so:0xffff ^ ntohl(ipv4) == ntohl(va[0]) ^ ... ^ ntohl(va[3])
		 */
		struct sockaddr_in *paddr = (struct sockaddr_in *)addr;

		addr_hash = 0xffff ^ ntohl(paddr->sin_addr.s_addr);
		port_hash = (uint64_t)ntohs(paddr->sin_port) << 16;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *paddr = (struct sockaddr_in6 *)addr;
		uint32_t *va;

		va = (uint32_t *)&paddr->sin6_addr;
		addr_hash = ntohl(va[0]) ^ ntohl(va[1]) ^ ntohl(va[2]) ^
			    ntohl(va[3]);
		port_hash = (uint64_t)ntohs(paddr->sin6_port) << 16;
		break;
	}
#ifdef RPC_VSOCK
	case AF_VSOCK: {
		struct sockaddr_vm *svm; /* XXX checkpatch horror */

		svm = (struct sockaddr_vm *)addr;
		addr_hash = svm->svm_cid;
		port_hash = svm->svm_port;
		break;
	}
#endif /* VSOCK */
	default:
		break;
	}

	if (!ignore_port)
		addr_hash ^= port_hash;

	return addr_hash;
}

int display_sockaddr_port(struct display_buffer *dspbuf, const sockaddr_t *addr,
			  bool ignore_port)
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
				 ipname, sizeof(ipname));
		port = ntohs(((struct sockaddr_in *)addr)->sin_port);
		break;

	case AF_INET6:
		name = inet_ntop(addr->ss_family,
				 &(((struct sockaddr_in6 *)addr)->sin6_addr),
				 ipname, sizeof(ipname));
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
 * @brief Convert an IP address string to a sockaddr
 *
 * @param[in]  component    Log component
 * @param[in]  ip_str       String representation of IP address
 * @param[out] sockaddr     Canonicalised to IPv4 if ip_str is IPv4
 *
 * @return 0 on success. EINVAL if ip_str is invalid
 */
int ip_str_to_sockaddr(log_components_t component, const char *ip_str,
		       sockaddr_t *sp)
{
	int ret = 0;

	if (inet_pton(AF_INET, (char *) ip_str,
	    &(*(struct sockaddr_in *)sp).sin_addr) ==
	    1) {
		sp->ss_family = AF_INET;
	} else if (inet_pton(AF_INET6, (char *) ip_str,
			     &(*(struct sockaddr_in6 *)sp).sin6_addr) == 1) {
		sockaddr_t server_addr_ipv4;
		sockaddr_t *server_addr_conv;

		sp->ss_family = AF_INET;
		/* Canonicalise, does the right thing with IPv4 input */
		server_addr_conv = convert_ipv6_to_ipv4(component, sp,
							&server_addr_ipv4);
		if (server_addr_conv == &server_addr_ipv4)
			memcpy(&((struct sockaddr_in *)sp)->sin_addr,
			       &((struct sockaddr_in *)server_addr_conv)
					->sin_addr,
			       sizeof(struct in_addr));
		else
			sp->ss_family = AF_INET6;
	} else {
		ret = EINVAL;
	}
	return ret;
}

/**
 * @brief Canonically compare 2 sockaddrs
 *
 * @param[in] comp        Log component
 * @param[in] addr1       First address
 * @param[in] addr2       Second address
 * @param[in] ignore_port Whether to ignore the port
 *
 * @return Comparator trichotomy
 */
int sockaddr_cmp(log_components_t comp, sockaddr_t *addr1,
		 sockaddr_t *addr2, bool ignore_port)
{
	sockaddr_t addr_ipv6_1 = {};
	sockaddr_t addr_ipv6_2 = {};

	if (addr1->ss_family == AF_INET) {
		addr1 = ipv4_to_ipv4_mapped_ipv6(comp, addr1, &addr_ipv6_1);
	}
	if (addr2->ss_family == AF_INET) {
		addr2 = ipv4_to_ipv4_mapped_ipv6(comp, addr2, &addr_ipv6_2);
	}

	switch (addr1->ss_family) {
	case AF_INET:
		LogFatal(comp, "AF_INET should have been converted already.");
	case AF_INET6: {
		struct sockaddr_in6 *in1 = (struct sockaddr_in6 *)addr1;
		struct sockaddr_in6 *in2 = (struct sockaddr_in6 *)addr2;
		assert(in1->sin6_family == AF_INET6);
		assert(in2->sin6_family == AF_INET6);

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
	case AF_VSOCK: {
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
	} break;
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
 * @param[in] component  Log component
 * @param[in] ipv6       The input address which may be IPv4, IPV6, or
 *                       encapsulated
 * @param[in] ipv4       sockattr_t buffer to create IPv4 address into
 *
 * @returns ipv6 unless an encapsulated address was converted, then ipv4
 */
sockaddr_t *convert_ipv6_to_ipv4(log_components_t component, sockaddr_t *ipv6,
				 sockaddr_t *ipv4)
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
	if ((ipv6->ss_family == AF_INET6) &&
	    !memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
	    (psockaddr_in6->sin6_addr.s6_addr[10] == 0xFF) &&
	    (psockaddr_in6->sin6_addr.s6_addr[11] == 0xFF)) {
		void *ab;

		memset(ipv4, 0, sizeof(*ipv4));
		ab = &(psockaddr_in6->sin6_addr.s6_addr[12]);

		paddr->sin_port = psockaddr_in6->sin6_port;
		paddr->sin_addr.s_addr = *(in_addr_t *)ab;
		ipv4->ss_family = AF_INET;

		if (isMidDebug(component)) {
			char ipstring4[SOCK_NAME_MAX];
			char ipstring6[SOCK_NAME_MAX];
			struct display_buffer dspbuf4 = { sizeof(ipstring4),
							  ipstring4,
							  ipstring4 };
			struct display_buffer dspbuf6 = { sizeof(ipstring6),
							  ipstring6,
							  ipstring6 };

			display_sockip(&dspbuf4, ipv4);
			display_sockip(&dspbuf6, ipv6);
			LogMidDebug(
				component,
				"Converting IPv6 encapsulated IPv4 address %s to IPv4 %s",
				ipstring6, ipstring4);
		}

		return ipv4;
	} else {
		return ipv6;
	}
}

/**
 * @brief Convert an IPv4 address to an IPv4-mapped IPv6 address and return it.
 *        If the input address is not IPv4, return the input address.
 *
 * @param[in] component  Log component
 * @param[in] ipv4       The input address which may be IPv4 or IPV6
 * @param[in] ipv6       sockattr_t buffer to create a new IPv6 address
 *
 * @returns If the input address is IPv4, the input address mapped to IPv6.
 *          Otherwise the input address.
 */
sockaddr_t *ipv4_to_ipv4_mapped_ipv6(log_components_t component,
				     sockaddr_t *ipv4, sockaddr_t *ipv6)
{
	struct sockaddr_in *paddr = (struct sockaddr_in *)ipv4;
	struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *)ipv6;

	/* An IPv4 address when encapsulated as an IPv6 address looks like:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 */
	if (ipv4->ss_family != AF_INET) {
		return ipv4;
	}
	memset(psockaddr_in6, 0, sizeof(*psockaddr_in6));
	psockaddr_in6->sin6_family = AF_INET6;
	psockaddr_in6->sin6_port = paddr->sin_port;
	psockaddr_in6->sin6_addr.s6_addr[10] = 0xFF;
	psockaddr_in6->sin6_addr.s6_addr[11] = 0xFF;
	memcpy(&psockaddr_in6->sin6_addr.s6_addr[12], &paddr->sin_addr.s_addr,
	       sizeof(struct in_addr));

	if (isMidDebug(component)) {
		char ipstring4[SOCK_NAME_MAX];
		char ipstring6[SOCK_NAME_MAX];
		struct display_buffer dspbuf4 = { sizeof(ipstring4), ipstring4,
						  ipstring4 };
		struct display_buffer dspbuf6 = { sizeof(ipstring6), ipstring6,
						  ipstring6 };

		display_sockip(&dspbuf4, ipv4);
		display_sockip(&dspbuf6, ipv6);
		LogMidDebug(
			component,
			"Converting IPv4 address %s to an IPv6 encapsulated IPv4 address %s ",
			ipstring4, ipstring6);
	}
	return ipv6;
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
	return (!memcmp(ip6addr->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
		(ip6addr->sin6_addr.s6_addr[10] == 0xFF) &&
		(ip6addr->sin6_addr.s6_addr[11] == 0xFF) &&
		(ip6addr->sin6_addr.s6_addr[12] == 0x7F)) ||
	       (memcmp(ip6addr->sin6_addr.s6_addr, &in6addr_loopback,
		       sizeof(in6addr_loopback)) == 0);
}

/**
 *
 * @brief Test if addr is inaddrany
 *
 * @param[in] addr      Address
 *
 * @return Comparator true/false,
 */
bool is_inaddrany(sockaddr_t *addr)
{
	struct sockaddr_in6 *ip6addr = (struct sockaddr_in6 *)addr;

	if (addr->ss_family == AF_INET) {
		struct sockaddr_in *inaddr = (struct sockaddr_in *)addr;

		return inaddr->sin_addr.s_addr == INADDR_ANY;
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
	return (!memcmp(ip6addr->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
		(ip6addr->sin6_addr.s6_addr[10] == 0xFF) &&
		(ip6addr->sin6_addr.s6_addr[11] == 0xFF) &&
		(*(in_addr_t *)&ip6addr->sin6_addr.s6_addr[12] ==
		 INADDR_ANY)) ||
	       IN6_IS_ADDR_UNSPECIFIED(ip6addr->sin6_addr.s6_addr);
}

/**
 * @brief Allocate a CIDR struct
 *
 * @return CIDR struct
*/
CIDR *cidr_alloc(mem_components_t comp)
{
	return gsh_calloc(1, sizeof(CIDR), comp);
}

/**
 * @brief Free a CIDR structure
 *
 * @in cidr      CIDR structure
 */
void cidr_free(CIDR *cidr, mem_components_t comp)
{
	gsh_free(cidr, comp);
}

/**
 * @brief Create a CIDR structure from an IP addr/mask string
 *
 * @param[in] component   Log component
 * @param[in] addr        IP Address/Mask string, examples:
 *                                192.168.10.0/24
 *                                e80::5a4d:7416:a595:bd6f/64
 *
 * @return CIDR structure, NULL on parse failure with errno=EINVAL
 */
CIDR *cidr_from_str(log_components_t component, const char *addr,
		    mem_components_t comp)
{
	CIDR *cidr;
	char addr_str[SOCK_NAME_MAX];
	char *slash = NULL;
	char *mask_str = NULL;
	int ret;

	if (strlcpy(addr_str, addr, sizeof(addr_str)) >= sizeof(addr_str)) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	slash = strchr(addr_str, '/');

	if (slash != NULL) {
		*slash = '\0';
		mask_str = slash + 1;
	}

	cidr = cidr_alloc(comp);
	ret = ip_str_to_sockaddr(component, addr_str, &(cidr->ip_addr));
	if (ret) {
		cidr_free(cidr, comp);
		errno = EINVAL;
		return NULL;
	}

	if (mask_str) {
		unsigned long mask;
		char *end = NULL;

		mask = strtoul(mask_str, &end, 10);

		if (mask_str == end || *end != '\0') {
			cidr_free(cidr, comp);
			errno = EINVAL;
			return NULL;
		}

		switch (cidr->ip_addr.ss_family) {
		case AF_INET:
			if (mask > 32) {
				cidr_free(cidr, comp);
				errno = EINVAL;
				return NULL;
			}
			break;
		case AF_INET6:
			if (mask > 128) {
				cidr_free(cidr, comp);
				errno = EINVAL;
				return NULL;
			}
			break;
		}
		cidr->mask = mask;
	} else {
		switch (cidr->ip_addr.ss_family) {
		case AF_INET:
			cidr->mask = 32;
			break;
		case AF_INET6:
			cidr->mask = 128;
			break;
		}
	}

	return cidr;
}

/**
 * @brief Convert a CIDR structure to a string
 *
 * @in addr      IP Address string
 *
 * @return CIDR structure
 */
char *cidr_to_str(CIDR *cidr, mem_components_t comp)
{
	char *cidr_string;

	cidr_string = gsh_calloc(SOCK_NAME_MAX, sizeof(char), comp);

	struct display_buffer dspbuf = { SOCK_NAME_MAX * sizeof(char),
					 cidr_string, cidr_string };

	display_sockip(&dspbuf, &cidr->ip_addr);
	display_printf(&dspbuf, "/%d", cidr->mask);
	return cidr_string;
}

/**
 * @brief See if an IP address is within a CIDR
 *
 * @in cidr      CIDR containing mask and IP address
 * @in ipaddr    IP Address
 *
 * @return 0 on match, -1 if not matched - errno is set to EINVAL if invalid
 */
int cidr_contains_ip(CIDR *cidr, sockaddr_t *ipaddr)
{
	int mask = cidr->mask;

	errno = 0;
	if (cidr->ip_addr.ss_family != ipaddr->ss_family) {
		errno = EINVAL;
		return -1;
	}

	if (cidr->ip_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *cidr_v6 =
			(struct sockaddr_in6 *)&cidr->ip_addr;
		struct sockaddr_in6 *ipaddr_v6 = (struct sockaddr_in6 *)ipaddr;
		uint8_t mask_bytes = mask / 8;
		uint8_t mask_bits = mask % 8;
		uint8_t cidr_byte;
		uint8_t ipaddr_byte;

		for (int i = 0; i < mask_bytes; i++) {
			if (cidr_v6->sin6_addr.s6_addr[i] !=
			    ipaddr_v6->sin6_addr.s6_addr[i]) {
				errno = 0;
				return -1;
			}
		}

		if (mask_bits == 0) {
			return 0;
		}

		cidr_byte = cidr_v6->sin6_addr.s6_addr[mask_bytes];
		ipaddr_byte = ipaddr_v6->sin6_addr.s6_addr[mask_bytes];
		if ((cidr_byte >> (8 - mask_bits)) ==
		    (ipaddr_byte >> (8 - mask_bits))) {
			return 0;
		} else {
			errno = 0;
			return -1;
		}
	} else if (cidr->ip_addr.ss_family == AF_INET) {
		struct sockaddr_in *cidr_v4 =
			(struct sockaddr_in *)&cidr->ip_addr;
		struct sockaddr_in *ipaddr_v4 = (struct sockaddr_in *)ipaddr;
		int cidr_addr = ntohl(cidr_v4->sin_addr.s_addr);
		int ipaddr_addr = ntohl(ipaddr_v4->sin_addr.s_addr);

		if (mask == 0)
			return 0;

		int b = cidr_addr >> (32 - mask);
		int s = ipaddr_addr >> (32 - mask);

		if (b == s) {
			return 0;
		}
	}

	errno = 0;
	return -1;
}

/**
 * @brief Create a CIDR struct from an in_addr
 *
 * @param[in] addr IPv4 Address
 *
 * @return CIDR structure
*/
CIDR *cidr_from_inaddr(const struct in_addr *addr, mem_components_t comp)
{
	CIDR *cidr;

	cidr = cidr_alloc(comp);
	memcpy(&((struct sockaddr_in *)&cidr->ip_addr)->sin_addr.s_addr, addr,
	       sizeof(struct in_addr));
	cidr->ip_addr.ss_family = AF_INET;
	cidr->mask = 32;

	return cidr;
}

/**
 * @brief Create a CIDR struct from an in6_addr
 *
 * @param[in] addr IPv6 Address
 *
 * @return CIDR structure
 */
CIDR *cidr_from_in6addr(const struct in6_addr *addr, mem_components_t comp)
{
	CIDR *cidr;

	cidr = cidr_alloc(comp);
	cidr->ip_addr.ss_family = AF_INET6;

	memcpy(&((struct sockaddr_in6 *)&cidr->ip_addr)->sin6_addr.s6_addr,
	       addr, sizeof(struct in6_addr));

	cidr->mask = 128;
	return cidr;
}

/**
 * @brief Extra the IP address from a CIDR as an array of 16 bytes
 *
 * @param[in]  cidr    A CIDR
 * @param[out] chars   A 16 byte array should be passed here
 *
 * NFS Ganesha previously used libcidr which stored the address
 * as a series of bytes [192 168 10 0] etc.  There is one
 * spot in the Ganesha code that grabs a byte from an array like
 * that. So this function provides that array for now.
 */
void cidr_ipaddr_to_chars(CIDR *cidr, unsigned char *chars)
{
	struct sockaddr_in6 *ip_v6;
	struct sockaddr_in *ip_v4;
	unsigned char *saddr_bytes;

	memset(chars, 0, 16);
	switch (cidr->ip_addr.ss_family) {
	case AF_INET:
		ip_v4 = (struct sockaddr_in *)&cidr->ip_addr;
		saddr_bytes = (unsigned char *)&ip_v4->sin_addr.s_addr;

		for (int i = 12, z = 0; i < 16; i++, z++) {
			chars[i] = saddr_bytes[z];
		}

		break;
	case AF_INET6:
		ip_v6 = (struct sockaddr_in6 *)&cidr->ip_addr;

		for (int i = 0; i < 16; i++) {
			chars[i] = ip_v6->sin6_addr.s6_addr[i];
		}

		break;
	}
}

/**
 * @brief Extra the numeric mask from a CIDR as an array of 16 bytes
 *
 * @param[in]  cidr    A CIDR
 * @param[out] chars   A 16 byte array should be passed here
 *
 * NFS Ganesha previously used libcidr which stored the mask
 * as a series of bytes [255 255 255] etc.  There is one
 * spot in the Ganesha code that grabs a byte from an array like
 * that. So this function provides that array for now.
 */
void cidr_mask_to_chars(CIDR *cidr, unsigned char *chars)
{
	int mask_bytes = cidr->mask / 8;
	int mask_bits = cidr->mask % 8;
	int i = 0;
	int z;

	memset(chars, 0, 16);
	switch (cidr->ip_addr.ss_family) {
	case AF_INET:
		for (i = 0; i < 12; i++) {
			chars[i] = 0xff;
		}

		for (i = 12, z = 0; z < mask_bytes; i++, z++) {
			chars[i] = 0xff;
		}
		chars[i] = 0xff << (8 - mask_bits);
		break;
	case AF_INET6:
		for (i = 0; i < mask_bytes; i++) {
			chars[i] = 0xff;
		}
		chars[i] = 0xff << (8 - mask_bits);

		break;
	}
}

/**
 * @brief Return the IP family (ss_family) of the CIDR
 *
 * @param[in]  cidr    A CIDR
 *
 */
int cidr_family(CIDR *cidr)
{
	return cidr->ip_addr.ss_family;
}

/**
 * @brief Return the protocol type as from libcidr
 *
 * @param[in]  cidr    A CIDR
 *
 * NFS Ganesha previously used libcidr which stored the
 * protocol as 1 for IPv4 and IPv6.  We store the more useful
 * ss_family.  One spot in the export_mgr sends this on DBUS
 * with some other (not useful) info.  This is provided, for
 * now, in case someone is parsing the ExportMgr/DisplayExport
 * output or something.
 */
int cidr_proto(CIDR *cidr)
{
	return (cidr->ip_addr.ss_family == AF_INET ? 1 : 2);
}

/**
 * @brief Return the CIDR version
 *
 * @param[in]  cidr    A CIDR
 *
 * A hang over from the libcidr days.  This field can probably disappear
 * down the track. One spot in the export_mgr sends this on DBUS.
 * This is provided, for now, in case someone is parsing the
 * ExportMgr/DisplayExport output or something.
 *
 * @return 0 was returned previously, let's return 1 for kicks.
 */
int cidr_version(CIDR *cidr)
{
	return 1;
}

/**
 * @brief Compare two CIDRs for equality
 *
 * @param[in] component   Log component
 * @param[in] a           First CIDR
 * @param[in] b           Second CIDR
 *
 * Determines whether two CIDR entries represent the same network.
 * Two CIDRs are considered equal if they belong to the same address
 * family, have identical network masks, and each CIDR contains the
 * other's network address.
 *
 * This comparison is based on network equivalence rather than pointer
 * equality, ensuring that differently constructed CIDR objects
 * representing the same network are treated as equal.
 *
 * @return true if both CIDRs represent the same network, otherwise false.
 */
bool cidr_equals(log_components_t component, CIDR *a, CIDR *b)
{
	if (a->ip_addr.ss_family != b->ip_addr.ss_family)
		return false;

	if (a->mask != b->mask)
		return false;

	if (sockaddr_cmp(component, &a->ip_addr, &b->ip_addr, false) == 0)
		return true;

	return false;
}

/**
 * @brief Normalize IPv4-mapped IPv6 CIDR addresses
 *
 * This helper normalizes a CIDR that represents an IPv4-mapped IPv6
 * address into its canonical IPv4 form. The function converts the
 * address family to AF_INET and adjusts the mask length to IPv4 semantics.
 *
 * Normalization ensures consistent behavior for CIDR equality,
 * containment, and overlap checks by avoiding mismatches between
 * IPv4 and IPv4-mapped IPv6 representations of the same network.
 *
 * This function must be invoked before performing CIDR comparisons
 * such as cidr_contains_ip(), or cidr_equals().
 *
 * @param[in] component   Log component
 * @param[in,out] cidr    CIDR object to be normalized in-place
 */
void normalize_v4_mapped_cidr(log_components_t component, CIDR *cidr)
{
	sockaddr_t ipv4;
	sockaddr_t *result;

	if (!cidr)
		return;

	result =
		convert_ipv6_to_ipv4(component, &cidr->ip_addr, &ipv4);

	if (result == &ipv4) {
		cidr->ip_addr = ipv4;

		if (cidr->mask >= 96)
			cidr->mask -= 96;
		else
			cidr->mask = 0;
	}
}
