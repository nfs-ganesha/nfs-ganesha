// SPDX-License-Identifier: LGPL-3.0-or-later
/* ----------------------------------------------------------------------------
 * Copyright 2026 Mike Borozdin <nolongemails@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * ----------------------------------------------------------------------------
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <netdb.h>
#include "ip_utils.h"

int main(void)
{
	char full_host[NI_MAXHOST + 1];
	char small_buf[8];
	char single_byte[1];
	char ad_host[NI_MAXHOST + 1];
	int ret;
	size_t i;

	printf("Running gsh_gethostname unit tests...\n");

	/* Test 1: Zero length buffer should safely return -1 */
	ret = gsh_gethostname(full_host, 0);
	assert(ret == -1);
	printf("  [PASS] len = 0 returns -1\n");

	/* Test 2: Standard buffer should return 0 and be non-empty */
	memset(full_host, 0xFF, sizeof(full_host));
	ret = gsh_gethostname(full_host, sizeof(full_host));
	assert(ret == 0);
	assert(full_host[sizeof(full_host) - 1] == '\0');
	assert(strlen(full_host) > 0);
	printf("  [PASS] standard buffer: hostname='%s' (len=%zu)\n",
	       full_host, strlen(full_host));

	/* Test 3: 1-byte buffer should guarantee NUL termination at index 0 */
	single_byte[0] = 'X';
	ret = gsh_gethostname(single_byte, sizeof(single_byte));
	assert(single_byte[0] == '\0');
	printf("  [PASS] 1-byte buffer NUL termination\n");

	/* Test 4: Small buffer truncation with guaranteed NUL termination */
	memset(small_buf, 'A', sizeof(small_buf));
	ret = gsh_gethostname(small_buf, sizeof(small_buf));
	assert(small_buf[sizeof(small_buf) - 1] == '\0');
	assert(strlen(small_buf) <= sizeof(small_buf) - 1);
	printf("  [PASS] small buffer truncation: '%.*s'\n",
	       (int)sizeof(small_buf), small_buf);

	/* Test 5: AD machine name formatting simulation
	 * (NI_MAXHOST + 1 with sizeof - 1)
	 */
	memset(full_host, 0, sizeof(full_host));
	ret = gsh_gethostname(full_host, sizeof(full_host) - 1);
	assert(ret == 0);
	assert(strlen(full_host) <= sizeof(full_host) - 2);

	strlcpy(ad_host, full_host, sizeof(ad_host));
	for (i = 0; ad_host[i] != '\0'; ++i)
		ad_host[i] = toupper((unsigned char)ad_host[i]);
	assert(i + 1 < sizeof(ad_host));
	ad_host[i] = '$';
	ad_host[i + 1] = '\0';
	printf("  [PASS] Active Directory name formatting: '%s'\n", ad_host);

	printf("All gsh_gethostname tests passed successfully!\n");
	return 0;
}
