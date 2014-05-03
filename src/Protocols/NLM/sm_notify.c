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

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

void *
nsm_notify_1(notify *argp, CLIENT *clnt)
{
	static char clnt_res;
	AUTH *nsm_auth;
	nsm_auth = authnone_create();

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	if (clnt_call(clnt, nsm_auth, SM_NOTIFY,
		(xdrproc_t) xdr_notify, (caddr_t) argp,
		(xdrproc_t) xdr_void, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return NULL;
	}
	return (void *)&clnt_res;
}

int main(int argc, char **argv)
{
	int c;
	int port = 0;
	int state, sflag = 0;
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
			if (strlen(optarg) >= STR_SIZE) {
				fprintf(stderr, ERR_MSG1, "monitor host");
				exit(1);
			}
			strcpy(mon_client, optarg);
			mflag = 1;
			break;
		case 'r':
			if (strlen(optarg) >= STR_SIZE) {
				fprintf(stderr, ERR_MSG1, "remote address");
				exit(1);
			}
			strcpy(remote_addr_s, optarg);
			rflag = 1;
			break;
		case 'l':
			if (strlen(optarg) >= STR_SIZE) {
				fprintf(stderr, ERR_MSG1, "local address");
				exit(1);
			}
			strcpy(local_addr_s, optarg);
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
	sprintf(port_str, "%d",
		htons(((struct sockaddr_in *)
		buf->buf)->sin_port));

	clnt = clnt_dg_ncreate(fd, buf, SM_PROG,
			SM_VERS, 0, 0);

	arg.my_name = mon_client;
	arg.state = state;
	nsm_notify_1(&arg, clnt);

	/* free resources */
	gsh_free(buf->buf);
	gsh_free(buf);
	clnt_destroy(clnt);

	close(fd);

	return 0;
}
