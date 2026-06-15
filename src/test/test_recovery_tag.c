// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Tests for nfs4_create_clid_name(), which builds the recovery tag stored in
 * the grace-period client list.  The tag format is "<addr>-(len:opaque)", or
 * "(len:opaque)" when RecoverySkipIp omits the source address.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sal_functions.h"
#include "client_mgr.h"
#include "common_utils.h"
#include "gsh_config.h"
#include "abstract_mem.h"

static int failures;

#define CHECK(cond, fmt, ...)                                               \
	do {                                                                \
		if (!(cond)) {                                              \
			printf("FAIL %s:%d: " fmt "\n", __func__, __LINE__, \
			       ##__VA_ARGS__);                              \
			failures++;                                         \
		}                                                           \
	} while (0)

/* Build a recovery tag for the given opaque owner and source address.
 * A NULL addr leaves gsh_client unset (the "(unknown)" case).
 */
static char *build_tag(const char *opaque, int opaque_len, const char *addr,
		       bool skip_ip)
{
	nfs_client_record_t *cl_rec = gsh_malloc(sizeof(*cl_rec) + opaque_len);
	struct gsh_client client;
	nfs_client_id_t clientid;
	char *tag;

	memset(cl_rec, 0, sizeof(*cl_rec));
	cl_rec->cr_client_val_len = opaque_len;
	memcpy(cl_rec->cr_client_val, opaque, opaque_len);

	memset(&clientid, 0, sizeof(clientid));
	clientid.cid_client_record = cl_rec;

	if (addr != NULL) {
		memset(&client, 0, sizeof(client));
		(void)strlcpy(client.hostaddr_str, addr,
			      sizeof(client.hostaddr_str));
		clientid.gsh_client = &client;
	}

	nfs_param.nfsv4_param.recovery_skip_ip = skip_ip;
	tag = nfs4_create_clid_name(&clientid, NULL);
	gsh_free(cl_rec);

	return tag;
}

/* Build a tag and assert it matches the exact expected string. */
static void expect(const char *opaque, int opaque_len, const char *addr,
		   bool skip_ip, const char *want)
{
	char *tag = build_tag(opaque, opaque_len, addr, skip_ip);

	CHECK(tag != NULL && strcmp(tag, want) == 0, "want [%s] got [%s]", want,
	      tag ? tag : "(null)");
	gsh_free(tag);
}

int main(void)
{
	/* Default: tag carries the "<addr>-" prefix. */
	expect("Linux NFSv4.1 client", 20, "192.168.1.10", false,
	       "192.168.1.10-(20:Linux NFSv4.1 client)");
	expect("A", 1, "10.0.0.1", false, "10.0.0.1-(1:A)");
	expect("client", 6, "fe80::1", false, "fe80::1-(6:client)");

	/* RecoverySkipIp: address omitted, just "(len:opaque)". */
	expect("Linux NFSv4.1 client", 20, "192.168.1.10", true,
	       "(20:Linux NFSv4.1 client)");
	expect("A", 1, "10.0.0.1", true, "(1:A)");
	expect("client", 6, "fe80::1", true, "(6:client)");

	/* No gsh_client: the address renders as "(unknown)". */
	expect("A", 1, NULL, false, "(unknown)-(1:A)");
	expect("A", 1, NULL, true, "(1:A)");

	if (failures)
		printf("%d test(s) FAILED\n", failures);
	else
		printf("All tests passed\n");

	return failures ? 1 : 0;
}
