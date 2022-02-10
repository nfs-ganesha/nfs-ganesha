// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *             : M. Mohan Kumar <mohan@in.ibm.com>
 *
 * --------------------------
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
 *
 */

#include <memory.h> /* for memset */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <rpc/types.h>
#include <rpc/nettype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "nsm.h"

#define STR_SIZE 100

#define USAGE "usage: %s [-p <port>] -l <local address> " \
	"-m <monitor host> -r <remote address> -s <state>\n"

#define ERR_MSG1 "%s address too long\n"

/* attempt to match (irrational) behaviour of previous versions */
static const struct timespec tout = { 15, 0 };

/* This function is dragged in by the use of abstract_mem.h, so
 * we define a simple version that does a printf rather than
 * pull in the entirety of log_functions.c into this standalone
 * program.
 */
void LogMallocFailure(const char *file, int line, const char *function,
		      const char *allocator)
{
	printf("Aborting %s due to out of memory", allocator);
}

void *
nsm_notify_1(notify *argp, CLIENT *clnt)
{
	static char clnt_res;
	struct clnt_req *cc;
	enum clnt_stat ret;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));

	cc = gsh_malloc(sizeof(*cc));
	clnt_req_fill(cc, clnt, authnone_ncreate(), SM_NOTIFY,
		      (xdrproc_t) xdr_notify, argp,
		      (xdrproc_t) xdr_void, &clnt_res);
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		cc->cc_refreshes = 1;
		ret = CLNT_CALL_WAIT(cc);
	}
	clnt_req_release(cc);

	if (ret != RPC_SUCCESS)
		return NULL;
	return (void *)&clnt_res;
}

int main(int argc, char **argv)
{
	int c;
	int port = 0;
	int state = 0, sflag = 0;
	char mon_client[STR_SIZE], mflag = 0;
	char remote_addr_s[STR_SIZE], rflag = 0;
	char local_addr_s[STR_SIZE], lflag = 0;
	notify arg;
	CLIENT *clnt;
	struct netbuf *buf;
	char port_str[20];

	struct sockaddr_in local_addr;

	int fd;

	while ((c = getopt(argc, argv, "p:r:m:l:s:")) != EOF)
		switch (c) {
		case 'p':
			port = atoi(optarg);
			break;
		case 's':
			state = atoi(optarg);
			sflag = 1;
			break;
		case 'm':
			if (strlcpy(mon_client, optarg, sizeof(mon_client))
			    >= sizeof(mon_client)) {
				fprintf(stderr, ERR_MSG1, "monitor host");
				exit(1);
			}
			mflag = 1;
			break;
		case 'r':
			if (strlcpy(remote_addr_s, optarg,
				    sizeof(remote_addr_s))
			    >= sizeof(remote_addr_s)) {
				fprintf(stderr, ERR_MSG1, "remote address");
				exit(1);
			}
			rflag = 1;
			break;
		case 'l':
			if (strlcpy(local_addr_s, optarg, sizeof(local_addr_s))
			    >= sizeof(local_addr_s)) {
				fprintf(stderr, ERR_MSG1, "local address");
				exit(1);
			}
			lflag = 1;
			break;
		case '?':
		default:
			fprintf(stderr, USAGE, argv[0]);
			exit(1);
			break;
	}

	if ((sflag + lflag + mflag + rflag) != 4) {
		fprintf(stderr, USAGE, argv[0]);
		exit(1);
	}


	/* create a udp socket */
	fd = socket(PF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_UDP);
	if (fd < 0) {
		fprintf(stderr, "socket call failed. errno=%d\n", errno);
		exit(1);
	}

	/* set up the sockaddr for local endpoint */
	memset(&local_addr, 0, sizeof(struct sockaddr_in));
	local_addr.sin_family = PF_INET;
	local_addr.sin_port = htons(port);
	local_addr.sin_addr.s_addr = inet_addr(local_addr_s);

	if (bind(fd, (struct sockaddr *)&local_addr,
			sizeof(struct sockaddr)) < 0) {
		fprintf(stderr, "bind call failed. errno=%d\n", errno);
		exit(1);
	}

	/* find the port for SM service of the remote server */
	buf = rpcb_find_mapped_addr(
				"udp",
				SM_PROG, SM_VERS,
				remote_addr_s);

	/* handle error here, for example,
	 * client side blocking rpc call
	 */
	if (buf == NULL) {
		close(fd);
		exit(1);
	}

	/* convert port to string format */
	(void) sprintf(port_str, "%d",
		       htons(((struct sockaddr_in *) buf->buf)->sin_port));

	clnt = clnt_dg_ncreate(fd, buf, SM_PROG,
			SM_VERS, 0, 0);

	arg.my_name = mon_client;
	arg.state = state;
	nsm_notify_1(&arg, clnt);

	/* free resources */
	gsh_free(buf->buf);
	gsh_free(buf);
	CLNT_DESTROY(clnt);

	close(fd);

	return 0;
}
