// SPDX-License-Identifier: LGPL-3.0-or-later
/* ----------------------------------------------------------------------------
 * Copyright DataDirect Networks, 2025
 * contributeur : Peter Schwenke   pschwenke@ddn.com
 *                Martin Schwenke  mschwenke@ddn.com
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
 * ---------------------------------------
 */

/*
 * Note: In order to build this program remove EXCLUDE_FROM_ALL from
 *       CMakeLists.txt for
 *       add_executable(test_cidr EXCLUDE_FROM_ALL ${test_cidr_SRCS})
 */

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include "ip_utils.h"
#include "abstract_mem.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#define IPv4_MAX_MASK 32
#define IPv6_MAX_MASK 128

struct {
	const char *cidr;
	const char *expected_cidr;
} cidrs[] = {
	{ "192.168.10.1/24x", NULL },
	{ "192.168.10.1/foo", NULL },
	{ "/192.168.10.1", NULL },
	{ "192.168.10.1/", NULL },
	{ "192.168.10.1", "192.168.10.1/32" },
	{ "192.168.10.0", "192.168.10.0/32" },
	{ "192.0.0.0/8", "192.0.0.0/8" },
	{ "192.168.0.0/16", "192.168.0.0/16" },
	{ "192.168.10.0/24", "192.168.10.0/24" },
	{ "192.168.10.0/27", "192.168.10.0/27" },
	{ "192.168.10.6/30", "192.168.10.6/30" },
	{ "192.168.10.6/32", "192.168.10.6/32" },
	{ "192.168.10.666/32", NULL },
	{ "192.168.10.6/33", NULL },
	{ "0.0.0.0/0", "0.0.0.0/0" },
	{ "10.20.30.40", "10.20.30.40/32" },
	{ "::/0", "::/0" },
	{ "::ffff:192.168.10.0/120", NULL },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::5a4d:7416:a595:bd6f/64" },
	{ "fe80::5a4d:7416:a595:bd6f", "fe80::5a4d:7416:a595:bd6f/128" },
	{ "fe80::5a4d:7416:a595:bd6f/120", "fe80::5a4d:7416:a595:bd6f/120" },
	{ "fe80::5a4d:7416:a595:bd6f/128", "fe80::5a4d:7416:a595:bd6f/128" },
	{ "fe80::5a4d:7416:a595:bd6g/128", NULL },
	{ "fe80::5a4d:7416:a595:bd6f/130", NULL },
};

struct {
	const char *cidr;
	char *ip;
	bool contained;
} cidr_matches[] = {
	{ "192.0.0.0", "191.168.10.1", false },
	{ "192.0.0.0/8", "191.168.10.1", false },
	{ "192.0.0.0/8", "192.167.10.1", true },
	{ "192.0.0.0/8", "192.168.10.1", true },
	{ "192.0.0.0/8", "192.168.11.1", true },
	{ "192.168.0.0/16", "191.168.10.1", false },
	{ "192.168.0.0/16", "192.167.10.1", false },
	{ "192.168.0.0/16", "192.168.10.1", true },
	{ "192.168.0.0/16", "192.168.11.1", true },
	{ "192.168.0.0/17", "191.168.10.1", false },
	{ "192.168.10.0/24", "191.168.10.1", false },
	{ "192.168.10.0/24", "191.168.10.254", false },
	{ "192.168.10.0/24", "192.167.10.1", false },
	{ "192.168.10.0/24", "192.168.10.1", true },
	{ "192.168.10.0/24", "192.168.10.254", true },
	{ "192.168.10.0/24", "192.168.11.1", false },
	{ "192.168.10.1", "192.168.10.1", true },
	{ "192.168.10.1", "192.168.10.2", false },
	{ "192.168.1.0/16", "191.168.10.1", false },
	{ "192.168.1.0/16", "192.167.10.1", false },
	{ "192.168.1.0/16", "192.168.10.1", true },
	{ "192.168.1.0/16", "192.168.11.1", true },
	{ "192.168.10.2/31", "192.168.10.1", false },
	{ "192.168.10.2/31", "192.168.10.2", true },
	{ "192.168.10.2/31", "192.168.10.3", true },
	{ "192.168.10.2/32", "192.168.10.1", false },
	{ "2001:0db8::0001:0000", "2001:0db8::0001:0000", true },
	{ "2001:db8:1234:1000::/49", "2001:db8:1234:ffff:ffff:ffff:ffff:fffe",
	  false },
	{ "2001:db8:1234::/48", "2001:db8:1234:0000:0000:0000:0000:0000",
	  true },
	{ "2001:db8:1234::/48", "2001:db8:1234::1", true },
	{ "2001:db8:1234::/48", "2001:db8:1234:ffff:ffff:ffff:ffff:fffe",
	  true },
	{ "2001:db8:1234::/48", "2001:db8:1234:ffff:ffff:ffff:ffff:ffff",
	  true },
	{ "2001:db8:1234::/49", "2001:db8:1234:0:ffff:ffff:ffff:fffe", true },
	{ "2001:db8:1234::/49", "2001:db8:1234:ffff:ffff:ffff:ffff:fffe",
	  false },
	{ "2001:db8:1234:8000::/49", "2001:db8:1234:ffff:ffff:ffff:ffff:fffe",
	  true },
	{ "2001:db8::/48", "2001:db8:0:0:0:0:0:0", true },
	{ "2001:db8::/48", "2001:db8:0:efff:ffff:ffff:ffff:ffff", true },
	{ "2001:db8::/48", "2001:db8:0:ffff:ffff:ffff:ffff:ffff", true },
	{ "2001:db8::/49", "2001:db8:0:efff:ffff:ffff:ffff:ffff", false },
	{ "fe80:0:0:80::/64", "fe80::0080:0:7416:a595:bd6e", true },
	{ "fe80:0:0:80::/65", "fe80::0080:0:7416:a595:bd6e", true },
	{ "fe80:0:0:80::/65", "fe80::1:0:7fff:ffff:ffff:fffe", false },
	{ "fe80:0:0:80::/65", "fe80::1:7416:a595:bd6e", false },
	{ "fe80:0:0:80::/65", "fe80::4a4c:7416:a595:bd6e", false },
	{ "fe80:0:0:80::/65", "fe80::5f4c:7416:a595:bd6e", false },
	{ "fe80:0:0:80::/65", "fe80::8000:7416:a595:bd6e", false },
	{ "fe80:0:0:80::/65", "fe80::fa4c:7416:a595:bd6e", false },
	{ "fe80:0:0:80::/65", "fe80::ff4c:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd00/120", "fe80::5a4d:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd00/120", "fe80::5a4d:7416:a595:be00", false },
	{ "fe80::5a4d:7416:a595:bd00/120", "fe80::5a4d:7416:a595:be6f", false },
	{ "fe80::5a4d:7416:a595:bd6f/120", "fe80::5a4d:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/128", "fe80::5a4d:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/128", "fe80::5a4d:7416:a595:bd6f", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::4a4c:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::5a4c:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::5a4d:7415:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::5a4d:7416:a595:ad6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe80::5a4d:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/64", "fe81::5a4c:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::1:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::4a4c:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::5e81:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::5f4c:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::fa4c:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::fe80:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::fe81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::fe81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe80::ff4c:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/65", "fe81::7e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/66", "fe80::5e81:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/66", "fe80::fe81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/66", "fe81::7e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/67", "fe80::fe81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/68", "fe81::7e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/69", "fe80::5e81:7416:a595:bd6e", true },
	{ "fe80::5a4d:7416:a595:bd6f/70", "fe80::5e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/71", "fe80::5e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/72", "fe80::5e81:7416:a595:bd6e", false },
	{ "fe80::5a4d:7416:a595:bd6f/73", "fe80::5e81:7416:a595:bd6e", false },
	{ "fe80::5c4d:7416:a595:bd6f/70", "fe80::5c81:7416:a595:bd6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe80::4a4c:7416:a595:bd6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe80::5a4c:7416:a595:bd6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe80::5a4d:7415:a595:bd6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe80::5a4d:7416:a595:ad6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe80::5a4d:7416:a595:bd6e", true },
	{ "fe80::7416:a595:bd6f/64", "fe81::5a4c:7416:a595:bd6e", false },
	{ "::ffff:192.168.10.0/120", "192.168.24.1", false },
	{ "::ffff:192.168.10.0/120", "::ffff:192.168.10.1", true },
	{ "0.0.0.0/0", "192.168.10.1", true },
	{ "0.0.0.0/0", "10.0.0.1", true },
	{ "::/0", "fe80::1", true },
	{ "::/0", "2001:db8::1", true },

};

int main(int argc, char **argv)
{
	for (int i = 0; i < ARRAY_SIZE(cidrs); i++) {
		CIDR *cidr = cidr_from_str(COMPONENT_DISPATCH, cidrs[i].cidr,
					   MEM_COMP_GTEST);
		char *ip_str;

		if (!cidr) {
			if (cidrs[i].expected_cidr) {
				printf("Unable to parse %s err=%s\n",
				       cidrs[i].cidr, strerror(errno));
			}
			continue;
		}

		ip_str = cidr_to_str(cidr, MEM_COMP_GTEST);

		if (cidrs[i].expected_cidr &&
		    strcmp(ip_str, cidrs[i].expected_cidr)) {
			printf("IP strings don't match IP str %s Vs %s\n",
			       cidrs[i].cidr, ip_str);
			gsh_free(ip_str, MEM_COMP_GTEST);
			cidr_free(cidr, MEM_COMP_GTEST);
			continue;
		}

		gsh_free(ip_str, MEM_COMP_GTEST);
		cidr_free(cidr, MEM_COMP_GTEST);
	}

	for (int i = 0; i < ARRAY_SIZE(cidr_matches); i++) {
		CIDR *masked_ip;
		sockaddr_t ip;
		bool expected = cidr_matches[i].contained;
		int contains_ret;

		masked_ip = cidr_from_str(COMPONENT_DISPATCH,
					  cidr_matches[i].cidr, MEM_COMP_GTEST);
		if (!masked_ip) {
			printf("Unable to parse masked_ip %s err=%s\n",
			       cidr_matches[i].cidr, strerror(errno));
			continue;
		}

		if (ip_str_to_sockaddr(COMPONENT_DISPATCH, cidr_matches[i].ip,
				       &ip)) {
			printf("Unable to parse ip %s err=%s\n",
			       cidr_matches[i].ip, strerror(errno));
			cidr_free(masked_ip, MEM_COMP_GTEST);
			continue;
		}
		contains_ret = cidr_contains_ip(masked_ip, &ip);

		if (contains_ret && expected) {
			printf("Failed %s Vs %s (%s)\n", cidr_matches[i].cidr,
			       cidr_matches[i].ip, strerror(errno));
		} else if (!contains_ret && !expected) {
			printf("Failed %s Vs %s (%s)\n", cidr_matches[i].cidr,
			       cidr_matches[i].ip, strerror(errno));
		}
		cidr_free(masked_ip, MEM_COMP_GTEST);
	}
}
