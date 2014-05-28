/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_dispatcher.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   9P protocol dispatch thread.
 *
 * The file that contains the '_9p_dispatcher_thread' routine for ganesha
 * (and all the related stuff).
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>		/* For inet_ntop() */
#include "hashtable.h"
#include "log.h"
#include "abstract_mem.h"
#include "abstract_atomic.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"
#include "server_stats.h"
#include "9p.h"
#include <stdbool.h>

#define P_FAMILY AF_INET6

void DispatchWork9P(request_data_t *req)
{
	switch (req->rtype) {
	case _9P_REQUEST:
		switch (req->r_u._9p.pconn->trans_type) {
		case _9P_TCP:
			LogDebug(COMPONENT_DISPATCH,
				 "Dispatching 9P/TCP request %p, tcpsock=%lu",
				 req, req->r_u._9p.pconn->trans_data.sockfd);
			break;

		case _9P_RDMA:
			LogDebug(COMPONENT_DISPATCH,
				 "Dispatching 9P/RDMA request %p", req);
			break;

		default:
			LogCrit(COMPONENT_DISPATCH,
				"/!\\ Implementation error, bad 9P transport type");
			return;
			break;
		}
		break;

	default:
		LogCrit(COMPONENT_DISPATCH,
			"/!\\ Implementation error, 9P Dispatch function is called for non-9P request !!!!");
		return;
		break;
	}

	/* increase connection refcount */
	atomic_inc_uint32_t(&req->r_u._9p.pconn->refcount);

	/* new-style dispatch */
	nfs_rpc_enqueue_req(req);
}

/**
 * _9p_socket_thread: 9p socket manager.
 *
 * This function is the main loop for the 9p socket manager.
 * One such thread exists per connection.
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL
 *
 */

void *_9p_socket_thread(void *Arg)
{
	long int tcp_sock = (long int)Arg;
	int rc = -1;
	struct pollfd fds[1];
	int fdcount = 1;
	static char my_name[MAXNAMLEN + 1];
	socklen_t addrpeerlen = 0;
	struct sockaddr_storage addrpeer;
	char strcaller[INET6_ADDRSTRLEN];
	request_data_t *req = NULL;
	int tag;
	unsigned long sequence = 0;
	unsigned int i = 0;
	char *_9pmsg = NULL;
	uint32_t msglen;

	struct _9p_conn _9p_conn;

	int readlen = 0;
	int total_readlen = 0;

	snprintf(my_name, MAXNAMLEN, "9p_sock_mgr#fd=%ld", tcp_sock);
	SetNameFunction(my_name);

	/* Init the struct _9p_conn structure */
	memset(&_9p_conn, 0, sizeof(_9p_conn));
	pthread_mutex_init(&_9p_conn.sock_lock, NULL);
	_9p_conn.trans_type = _9P_TCP;
	_9p_conn.trans_data.sockfd = tcp_sock;
	for (i = 0; i < FLUSH_BUCKETS; i++) {
		pthread_mutex_init(&_9p_conn.flush_buckets[i].lock, NULL);
		glist_init(&_9p_conn.flush_buckets[i].list);
	}
	atomic_store_uint32_t(&_9p_conn.refcount, 0);

	/* Init the fids pointers array */
	memset(&_9p_conn.fids, 0, _9P_FID_PER_CONN * sizeof(struct _9p_fid *));

	/* Set initial msize.
	 * Client may request a lower value during TVERSION */
	_9p_conn.msize = _9p_param._9p_tcp_msize;

	if (gettimeofday(&_9p_conn.birth, NULL) == -1)
		LogFatal(COMPONENT_9P, "Cannot get connection's time of birth");

	addrpeerlen = sizeof(addrpeer);
	rc = getpeername(tcp_sock, (struct sockaddr *)&addrpeer,
			 &addrpeerlen);
	if (rc == -1) {
		LogMajor(COMPONENT_9P,
			 "Cannot get peername to tcp socket for 9p, error %d (%s)",
			 errno, strerror(errno));
		/* XXX */
		strncpy(strcaller, "(unresolved)", INET6_ADDRSTRLEN);
		strcaller[12] = '\0';
	} else {
		switch (addrpeer.ss_family) {
		case AF_INET:
			inet_ntop(addrpeer.ss_family,
				  &((struct sockaddr_in *)&addrpeer)->
				  sin_addr, strcaller, INET6_ADDRSTRLEN);
			break;
		case AF_INET6:
			inet_ntop(addrpeer.ss_family,
				  &((struct sockaddr_in6 *)&addrpeer)->
				  sin6_addr, strcaller, INET6_ADDRSTRLEN);
			break;
		default:
			snprintf(strcaller, INET6_ADDRSTRLEN, "BAD ADDRESS");
			break;
		}

		LogEvent(COMPONENT_9P, "9p socket #%ld is connected to %s",
			 tcp_sock, strcaller);
		printf("9p socket #%ld is connected to %s\n", tcp_sock,
		       strcaller);
	}
	_9p_conn.client = get_gsh_client(&addrpeer, false);

	/* Set up the structure used by poll */
	memset((char *)fds, 0, sizeof(struct pollfd));
	fds[0].fd = tcp_sock;
	fds[0].events =
	    POLLIN | POLLPRI | POLLRDBAND | POLLRDNORM | POLLRDHUP | POLLHUP |
	    POLLERR | POLLNVAL;

	for (;;) {
		total_readlen = 0;  /* new message */
		rc = poll(fds, fdcount, -1);
		if (rc == -1) {
			/* timeout = -1 => Wait indefinitely for events */
			/* Interruption if not an issue */
			if (errno == EINTR)
				continue;

			LogCrit(COMPONENT_9P,
				"Got error %u (%s) on fd %ld connect to %s while polling on socket",
				errno, strerror(errno), tcp_sock, strcaller);
		}

		if (fds[0].revents & POLLNVAL) {
			LogEvent(COMPONENT_9P,
				 "Client %s on socket %lu produced POLLNVAL",
				 strcaller, tcp_sock);
			goto end;
		}

		if (fds[0].revents & (POLLERR | POLLHUP | POLLRDHUP)) {
			LogEvent(COMPONENT_9P,
				 "Client %s on socket %lu has shut down and closed",
				 strcaller, tcp_sock);
			goto end;
		}

		if (!(fds[0].revents & (POLLIN | POLLRDNORM)))
			continue;

		/* Prepare to read the message */
		_9pmsg = gsh_malloc(_9p_conn.msize);
		if (_9pmsg == NULL) {
			LogCrit(COMPONENT_9P,
				"Could not allocate 9pmsg buffer for client %s on socket %lu",
				strcaller, tcp_sock);
			goto end;
		}

		/* An incoming 9P request: the msg has a 4 bytes header
		   showing the size of the msg including the header */
		readlen = recv(fds[0].fd, _9pmsg,
			       _9P_HDR_SIZE, MSG_WAITALL);
		if (readlen != _9P_HDR_SIZE)
			goto badmsg;

		msglen = *(uint32_t *) _9pmsg;
		if (msglen > _9p_conn.msize) {
			LogCrit(COMPONENT_9P,
				"Message size too big! got %u, max = %u",
				msglen, _9p_conn.msize);
			goto end;
		}

		LogFullDebug(COMPONENT_9P,
			     "Received 9P/TCP message of size %u from client %s on socket %lu",
			     msglen, strcaller, tcp_sock);

		total_readlen += readlen;
		while (total_readlen < msglen) {
			readlen = recv(fds[0].fd,
				       _9pmsg + total_readlen,
				       msglen - total_readlen,
				       0);

			if (readlen > 0) {
				total_readlen += readlen;
				continue;
			}
			if (readlen == 0 ||
			    (readlen < 0 && errno != EINTR))
				goto badmsg;
		}	/* while */

		server_stats_transport_done(_9p_conn.client,
					    total_readlen, 1, 0,
					    0, 0, 0);

		/* Message is good. */
		req = pool_alloc(request_pool, NULL);

		req->rtype = _9P_REQUEST;
		req->r_u._9p._9pmsg = _9pmsg;
		req->r_u._9p.pconn = &_9p_conn;

		/* Add this request to the request list,
		 * should it be flushed later. */
		tag = *(u16 *) (_9pmsg + _9P_HDR_SIZE +
				_9P_TYPE_SIZE);
		_9p_AddFlushHook(&req->r_u._9p, tag,
				 sequence++);
		LogFullDebug(COMPONENT_9P,
			     "Request tag is %d\n", tag);

		/* Message was OK push it */
		DispatchWork9P(req);

		/* Not our buffer anymore */
		_9pmsg = NULL;
		continue;

badmsg:
		if (readlen == 0)
			LogEvent(COMPONENT_9P,
				 "Premature end for Client %s on socket %lu, total read = %u",
				 strcaller, tcp_sock, total_readlen);
		else if (readlen < 0) {
			LogEvent(COMPONENT_9P,
				 "Read error client %s on socket %lu errno=%d, total read = %u",
				 strcaller, tcp_sock,
				 errno, total_readlen);
		} else
			LogEvent(COMPONENT_9P,
				 "Header too small! for client %s on socket %lu: readlen=%u expected=%u",
				 strcaller, tcp_sock, readlen,
				 _9P_HDR_SIZE);

		/* Either way, we close the connection.
		 * It is not possible to survive
		 * once we get out of sync in the TCP stream
		 * with the client
		 */
		break; /* bail out */
	}			/* for( ;; ) */

end:
	LogEvent(COMPONENT_9P, "Closing connection on socket %lu", tcp_sock);
	close(tcp_sock);

	/* Free buffer if we encountered an error
	 * before we could give it to a worker */
	if (_9pmsg)
		gsh_free(_9pmsg);

	while (atomic_fetch_uint32_t(&_9p_conn.refcount)) {
		LogEvent(COMPONENT_9P, "Waiting for workers to release pconn");
		sleep(1);
	}

	_9p_cleanup_fids(&_9p_conn);

	if (_9p_conn.client != NULL)
		put_gsh_client(_9p_conn.client);

	pthread_exit(NULL);
}				/* _9p_socket_thread */

/**
 * _9p_create_socket: create the accept socket for 9P
 *
 * This function create the accept socket for the 9p dispatcher thread.
 *
 * @return socket fd or -1 if failed.
 *
 */
int _9p_create_socket(void)
{
	int sock = -1;
	int one = 1;
	int centvingt = 120;
	int neuf = 9;
	struct sockaddr_in6 sinaddr_tcp6;
	struct netbuf netbuf_tcp6;
	struct t_bind bindaddr_tcp6;
	struct __rpc_sockinfo si_tcp6;

	sock = socket(P_FAMILY, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1)
		goto bad_socket;
	if ((setsockopt(sock,
			SOL_SOCKET, SO_REUSEADDR,
			&one, sizeof(one)) == -1) ||
	    (setsockopt(sock,
			IPPROTO_TCP, TCP_NODELAY,
			&one, sizeof(one)) == -1) ||
	    (setsockopt(sock,
			IPPROTO_TCP, TCP_KEEPIDLE,
			&centvingt, sizeof(centvingt)) == -1) ||
	    (setsockopt(sock,
			IPPROTO_TCP, TCP_KEEPINTVL,
			&centvingt, sizeof(centvingt)) == -1) ||
	    (setsockopt(sock,
			IPPROTO_TCP, TCP_KEEPCNT,
			&neuf, sizeof(neuf)) == -1))
		goto bad_socket;

	socket_setoptions(sock);
	memset(&sinaddr_tcp6, 0, sizeof(sinaddr_tcp6));
	sinaddr_tcp6.sin6_family = AF_INET6;
	/* All the interfaces on the machine are used */
	sinaddr_tcp6.sin6_addr = in6addr_any;
	sinaddr_tcp6.sin6_port = htons(_9p_param._9p_tcp_port);

	netbuf_tcp6.maxlen = sizeof(sinaddr_tcp6);
	netbuf_tcp6.len = sizeof(sinaddr_tcp6);
	netbuf_tcp6.buf = &sinaddr_tcp6;

	bindaddr_tcp6.qlen = SOMAXCONN;
	bindaddr_tcp6.addr = netbuf_tcp6;

	if (!__rpc_fd2sockinfo(sock, &si_tcp6)) {
		LogFatal(COMPONENT_DISPATCH,
			 "Cannot get 9p socket info for tcp6 socket errno=%d (%s)",
			 errno, strerror(errno));
		return -1;
	}

	if (bind(sock,
		 (struct sockaddr *)bindaddr_tcp6.addr.buf,
		 (socklen_t) si_tcp6.si_alen) == -1) {
		LogFatal(COMPONENT_DISPATCH,
			 "Cannot bind 9p tcp6 socket, error %d (%s)", errno,
			 strerror(errno));
		return -1;
	}

	if (listen(sock, 20) == -1) {
		LogFatal(COMPONENT_DISPATCH,
			 "Cannot bind 9p tcp6 socket, error %d (%s)", errno,
			 strerror(errno));
		return -1;
	}

	return sock;

bad_socket:
	LogFatal(COMPONENT_9P_DISPATCH,
		 "Bad socket option 9p, error %d (%s)", errno,
		 strerror(errno));
	return -1;
}				/* _9p_create_socket */

/**
 * _9p_dispatcher_svc_run: main loop for 9p dispatcher
 *
 * This function is the main loop for the 9p dispatcher.
 * It never returns because it is an infinite loop.
 *
 * @param sock accept socket for 9p dispatch
 *
 * @return nothing (void function).
 *
 */
void _9p_dispatcher_svc_run(long int sock)
{
	int rc = 0;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	long int newsock = -1;
	pthread_attr_t attr_thr;
	pthread_t tcp_thrid;

	/* Init for thread parameter (mostly for scheduling) */
	if (pthread_attr_init(&attr_thr) != 0)
		LogDebug(COMPONENT_9P_DISPATCH,
			 "can't init pthread's attributes");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
		LogDebug(COMPONENT_9P_DISPATCH, "can't set pthread's scope");

	if (pthread_attr_setdetachstate(&attr_thr,
					PTHREAD_CREATE_DETACHED) != 0)
		LogDebug(COMPONENT_9P_DISPATCH,
			 "can't set pthread's join state");

	LogEvent(COMPONENT_9P_DISPATCH, "9P dispatcher started");
	while (true) {
		newsock = accept(sock, (struct sockaddr *)&addr, &addrlen);
		if (newsock < 0) {
			LogCrit(COMPONENT_9P_DISPATCH, "accept failed");
			continue;
		}

		/* Starting the thread dedicated to signal handling */
		rc = pthread_create(&tcp_thrid, &attr_thr,
				    _9p_socket_thread, (void *)newsock);
		if (rc != 0) {
			LogFatal(COMPONENT_THREAD,
				 "Could not create 9p socket manager thread, error = %d (%s)",
				 errno, strerror(errno));
		}
	}			/* while */
	return;
}				/* _9p_dispatcher_svc_run */

/**
 * _9p_dispatcher_thread: thread used for RPC dispatching.
 *
 * Thead used for RPC dispatching. It gets the requests and then spool it to
 * one of the worker's LRU.  The worker chosen is the one with the smaller load
 * (its LRU is the shorter one).
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *_9p_dispatcher_thread(void *Arg)
{
	int _9p_socket = -1;

	SetNameFunction("_9p_disp");

	/* Calling dispatcher main loop */
	LogInfo(COMPONENT_9P_DISPATCH, "Entering nfs/rpc dispatcher");

	LogDebug(COMPONENT_9P_DISPATCH, "My pthread id is %p",
		 (caddr_t) pthread_self());

	/* Set up the _9p_socket */
	_9p_socket = _9p_create_socket();
	if (_9p_socket == -1) {
		LogCrit(COMPONENT_9P_DISPATCH,
			"Can't get socket for 9p dispatcher");
		exit(1);
	}

	_9p_dispatcher_svc_run(_9p_socket);

	return NULL;
}				/* _9p_dispatcher_thread */
