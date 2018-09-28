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
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_req_queue.h"
#include "client_mgr.h"
#include "server_stats.h"
#include "9p.h"
#include <stdbool.h>

#define P_FAMILY AF_INET6

static struct fridgethr *worker_fridge;

static struct nfs_req_st nfs_req_st;	/*< Shared request queues */

static const char *req_q_s[N_REQ_QUEUES] = {
	"REQ_Q_LOW_LATENCY",
};

/* static */
uint32_t _9p_outstanding_reqs_est(void)
{
	static uint32_t ctr;
	static uint32_t nreqs;
	struct req_q_pair *qpair;
	uint32_t treqs;
	int ix;

	if ((atomic_inc_uint32_t(&ctr) % 10) != 0)
		return atomic_fetch_uint32_t(&nreqs);

	treqs = 0;
	for (ix = 0; ix < N_REQ_QUEUES; ++ix) {
		qpair = &(nfs_req_st.reqs.nfs_request_q.qset[ix]);
		treqs += atomic_fetch_uint32_t(&qpair->producer.size);
		treqs += atomic_fetch_uint32_t(&qpair->consumer.size);
	}

	atomic_store_uint32_t(&nreqs, treqs);
	return treqs;
}

static inline request_data_t *_9p_consume_req(struct req_q_pair *qpair)
{
	request_data_t *reqdata = NULL;

	pthread_spin_lock(&qpair->consumer.sp);
	if (qpair->consumer.size > 0) {
		reqdata =
		    glist_first_entry(&qpair->consumer.q, request_data_t,
				      req_q);
		glist_del(&reqdata->req_q);
		--(qpair->consumer.size);
		pthread_spin_unlock(&qpair->consumer.sp);
		goto out;
	} else {
		char *s = NULL;
		uint32_t csize = ~0U;
		uint32_t psize = ~0U;

		pthread_spin_lock(&qpair->producer.sp);
		if (isFullDebug(COMPONENT_DISPATCH)) {
			s = (char *)qpair->s;
			csize = qpair->consumer.size;
			psize = qpair->producer.size;
		}
		if (qpair->producer.size > 0) {
			/* splice */
			glist_splice_tail(&qpair->consumer.q,
					  &qpair->producer.q);
			qpair->consumer.size = qpair->producer.size;
			qpair->producer.size = 0;
			/* consumer.size > 0 */
			pthread_spin_unlock(&qpair->producer.sp);
			reqdata =
			    glist_first_entry(&qpair->consumer.q,
					      request_data_t, req_q);
			glist_del(&reqdata->req_q);
			--(qpair->consumer.size);
			pthread_spin_unlock(&qpair->consumer.sp);
			if (s)
				LogFullDebug(COMPONENT_DISPATCH,
					     "try splice, qpair %s consumer qsize=%u producer qsize=%u",
					     s, csize, psize);
			goto out;
		}

		pthread_spin_unlock(&qpair->producer.sp);
		pthread_spin_unlock(&qpair->consumer.sp);

		if (s)
			LogFullDebug(COMPONENT_DISPATCH,
				     "try splice, qpair %s consumer qsize=%u producer qsize=%u",
				     s, csize, psize);
	}
 out:
	return reqdata;
}

static request_data_t *nfs_rpc_dequeue_req(nfs_worker_data_t *worker)
{
	request_data_t *reqdata = NULL;
	struct req_q_set *nfs_request_q = &nfs_req_st.reqs.nfs_request_q;
	struct req_q_pair *qpair;
	uint32_t ix, slot;
	struct timespec timeout;

	/* XXX: the following stands in for a more robust/flexible
	 * weighting function */

 retry_deq:
	slot = atomic_inc_uint32_t(&nfs_req_st.reqs.ctr) % N_REQ_QUEUES;
	for (ix = 0; ix < N_REQ_QUEUES; ++ix) {
		qpair = &(nfs_request_q->qset[slot]);

		LogFullDebug(COMPONENT_DISPATCH,
			     "dequeue_req try qpair %s %p:%p", qpair->s,
			     &qpair->producer, &qpair->consumer);

		/* anything? */
		reqdata = _9p_consume_req(qpair);
		if (reqdata) {
			break;
		}

		++slot;
		slot = slot % N_REQ_QUEUES;

	}			/* for */

	/* wait */
	if (!reqdata) {
		struct fridgethr_context *ctx =
			container_of(worker, struct fridgethr_context, wd);
		wait_q_entry_t *wqe = &worker->wqe;

		assert(wqe->waiters == 0); /* wqe is not on any wait queue */
		PTHREAD_MUTEX_lock(&wqe->lwe.mtx);
		wqe->flags = Wqe_LFlag_WaitSync;
		wqe->waiters = 1;
		/* XXX functionalize */
		pthread_spin_lock(&nfs_req_st.reqs.sp);
		glist_add_tail(&nfs_req_st.reqs.wait_list, &wqe->waitq);
		++(nfs_req_st.reqs.waiters);
		pthread_spin_unlock(&nfs_req_st.reqs.sp);
		while (!(wqe->flags & Wqe_LFlag_SyncDone)) {
			timeout.tv_sec = time(NULL) + 5;
			timeout.tv_nsec = 0;
			pthread_cond_timedwait(&wqe->lwe.cv, &wqe->lwe.mtx,
					       &timeout);
			if (fridgethr_you_should_break(ctx)) {
				/* We are returning;
				 * so take us out of the waitq */
				pthread_spin_lock(&nfs_req_st.reqs.sp);
				if (wqe->waitq.next != NULL
				    || wqe->waitq.prev != NULL) {
					/* Element is still in wqitq,
					 * remove it */
					glist_del(&wqe->waitq);
					--(nfs_req_st.reqs.waiters);
					--(wqe->waiters);
					wqe->flags &=
					    ~(Wqe_LFlag_WaitSync |
					      Wqe_LFlag_SyncDone);
				}
				pthread_spin_unlock(&nfs_req_st.reqs.sp);
				PTHREAD_MUTEX_unlock(&wqe->lwe.mtx);
				return NULL;
			}
		}

		/* XXX wqe was removed from nfs_req_st.waitq
		 * (by signalling thread) */
		wqe->flags &= ~(Wqe_LFlag_WaitSync | Wqe_LFlag_SyncDone);
		PTHREAD_MUTEX_unlock(&wqe->lwe.mtx);
		LogFullDebug(COMPONENT_DISPATCH, "wqe wakeup %p", wqe);
		goto retry_deq;
	} /* !reqdata */

#if defined(HAVE_BLKIN)
	/* thread id */
	BLKIN_KEYVAL_INTEGER(
		&reqdata->r_u.req.svc.bl_trace,
		&reqdata->r_u.req.xprt->blkin.endp,
		"worker-id",
		worker->worker_index
		);

	BLKIN_TIMESTAMP(
		&reqdata->r_u.req.svc.bl_trace,
		&reqdata->r_u.req.xprt->blkin.endp,
		"dequeue-req");
#endif
	return reqdata;
}

static void nfs_rpc_enqueue_req(request_data_t *reqdata)
{
	struct req_q_set *nfs_request_q;
	struct req_q_pair *qpair;
	struct req_q *q;

#if defined(HAVE_BLKIN)
	BLKIN_TIMESTAMP(
		&reqdata->r_u.req.svc.bl_trace,
		&reqdata->r_u.req.xprt->blkin.endp,
		"enqueue-enter");
#endif

	nfs_request_q = &nfs_req_st.reqs.nfs_request_q;

	switch (reqdata->rtype) {
	case _9P_REQUEST:
		/* XXX identify high-latency requests and allocate
		 * to the high-latency queue, as above */
		qpair = &(nfs_request_q->qset[REQ_Q_LOW_LATENCY]);
		break;
	case NFS_REQUEST:
	case NFS_CALL:
	default:
		goto out;
	}

	/* this one is real, timestamp it
	 */
	now(&reqdata->time_queued);
	/* always append to producer queue */
	q = &qpair->producer;
	pthread_spin_lock(&q->sp);
	glist_add_tail(&q->q, &reqdata->req_q);
	++(q->size);
	pthread_spin_unlock(&q->sp);

#if defined(HAVE_BLKIN)
	/* log the queue depth */
	BLKIN_KEYVAL_INTEGER(
		&reqdata->r_u.req.svc.bl_trace,
		&reqdata->r_u.req.xprt->blkin.endp,
		"reqs-est",
		_9p_outstanding_reqs_est()
		);

	BLKIN_TIMESTAMP(
		&reqdata->r_u.req.svc.bl_trace,
		&reqdata->r_u.req.xprt->blkin.endp,
		"enqueue-exit");
#endif
	LogDebug(COMPONENT_DISPATCH,
		 "enqueued req, q %p (%s %p:%p) size is %d (enq %"
		 PRIu64 " deq %" PRIu64 ")",
		 q, qpair->s, &qpair->producer, &qpair->consumer, q->size,
		 nfs_health_.enqueued_reqs, nfs_health_.dequeued_reqs);

	/* potentially wakeup some thread */

	/* global waitq */
	{
		wait_q_entry_t *wqe;

		/* SPIN LOCKED */
		pthread_spin_lock(&nfs_req_st.reqs.sp);
		if (nfs_req_st.reqs.waiters) {
			wqe = glist_first_entry(&nfs_req_st.reqs.wait_list,
						wait_q_entry_t, waitq);

			LogFullDebug(COMPONENT_DISPATCH,
				     "nfs_req_st.reqs.waiters %u signal wqe %p (for q %p)",
				     nfs_req_st.reqs.waiters, wqe, q);

			/* release 1 waiter */
			glist_del(&wqe->waitq);
			--(nfs_req_st.reqs.waiters);
			--(wqe->waiters);
			/* ! SPIN LOCKED */
			pthread_spin_unlock(&nfs_req_st.reqs.sp);
			PTHREAD_MUTEX_lock(&wqe->lwe.mtx);
			/* XXX reliable handoff */
			wqe->flags |= Wqe_LFlag_SyncDone;
			if (wqe->flags & Wqe_LFlag_WaitSync)
				pthread_cond_signal(&wqe->lwe.cv);
			PTHREAD_MUTEX_unlock(&wqe->lwe.mtx);
		} else
			/* ! SPIN LOCKED */
			pthread_spin_unlock(&nfs_req_st.reqs.sp);
	}

 out:
	return;
}

/**
 * @brief Execute a 9p request
 *
 * @param[in,out] req9p       9p request
 */
static void _9p_execute(request_data_t *reqdata)
{
	struct _9p_request_data *req9p = &reqdata->r_u._9p;
	struct req_op_context req_ctx;
	struct export_perms export_perms;

	memset(&req_ctx, 0, sizeof(struct req_op_context));
	memset(&export_perms, 0, sizeof(struct export_perms));
	op_ctx = &req_ctx;
	op_ctx->caller_addr = (sockaddr_t *)&reqdata->r_u._9p.pconn->addrpeer;
	op_ctx->req_type = reqdata->rtype;
	op_ctx->export_perms = &export_perms;

	if (req9p->pconn->trans_type == _9P_TCP)
		_9p_tcp_process_request(req9p);
#ifdef _USE_9P_RDMA
	else if (req9p->pconn->trans_type == _9P_RDMA)
		_9p_rdma_process_request(req9p);
#endif
	op_ctx = NULL;
}				/* _9p_execute */

/**
 * @brief Free resources allocated for a 9p request
 *
 * This does not free the request itself.
 *
 * @param[in] nfsreq 9p request
 */
static void _9p_free_reqdata(struct _9p_request_data *req9p)
{
	if (req9p->pconn->trans_type == _9P_TCP)
		gsh_free(req9p->_9pmsg);

	/* decrease connection refcount */
	(void) atomic_dec_uint32_t(&req9p->pconn->refcount);
}

static uint32_t worker_indexer;

/**
 * @brief Initialize a worker thread
 *
 * @param[in] ctx Thread fridge context
 */

static void worker_thread_initializer(struct fridgethr_context *ctx)
{
	struct nfs_worker_data *wd = &ctx->wd;
	char thr_name[32];

	wd->worker_index = atomic_inc_uint32_t(&worker_indexer);
	snprintf(thr_name, sizeof(thr_name), "work-%u", wd->worker_index);
	SetNameFunction(thr_name);

	/* Initialize thr waitq */
	init_wait_q_entry(&wd->wqe);
}

/**
 * @brief Finalize a worker thread
 *
 * @param[in] ctx Thread fridge context
 */

static void worker_thread_finalizer(struct fridgethr_context *ctx)
{
	ctx->thread_info = NULL;
}

/**
 * @brief The main function for a worker thread
 *
 * This is the body of the worker thread. Its starting arguments are
 * located in global array worker_data. The argument is no pointer but
 * the worker's index.  It then uses this index to address its own
 * worker data in the array.
 *
 * @param[in] ctx Fridge thread context
 */

static void worker_run(struct fridgethr_context *ctx)
{
	struct nfs_worker_data *worker_data = &ctx->wd;
	request_data_t *reqdata;

	/* Worker's loop */
	while (!fridgethr_you_should_break(ctx)) {
		reqdata = nfs_rpc_dequeue_req(worker_data);

		if (!reqdata)
			continue;

		switch (reqdata->rtype) {
		case _9P_REQUEST:
			_9p_execute(reqdata);
			_9p_free_reqdata(&reqdata->r_u._9p);
			break;

		case UNKNOWN_REQUEST:
		case NFS_REQUEST:
		case NFS_CALL:
		default:
			LogCrit(COMPONENT_DISPATCH,
				"Unexpected unknown request");
			break;
		}

		/* Free the req by releasing the entry */
		LogFullDebug(COMPONENT_DISPATCH,
			     "Invalidating processed entry");

		pool_free(nfs_request_pool, reqdata);
		(void) atomic_inc_uint64_t(&nfs_health_.dequeued_reqs);
	}
}

int _9p_worker_init(void)
{
	struct fridgethr_params frp;
	struct req_q_pair *qpair;
	int ix;
	int rc = 0;

	/* Init request queue before workers */
	pthread_spin_init(&nfs_req_st.reqs.sp, PTHREAD_PROCESS_PRIVATE);
	nfs_req_st.reqs.size = 0;
	for (ix = 0; ix < N_REQ_QUEUES; ++ix) {
		qpair = &(nfs_req_st.reqs.nfs_request_q.qset[ix]);
		qpair->s = req_q_s[ix];
		nfs_rpc_q_init(&qpair->producer);
		nfs_rpc_q_init(&qpair->consumer);
	}

	/* waitq */
	glist_init(&nfs_req_st.reqs.wait_list);
	nfs_req_st.reqs.waiters = 0;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = nfs_param.core_param.nb_worker;
	frp.thr_min = nfs_param.core_param.nb_worker;
	frp.flavor = fridgethr_flavor_looper;
	frp.thread_initialize = worker_thread_initializer;
	frp.thread_finalize = worker_thread_finalizer;
	frp.wake_threads = nfs_rpc_queue_awaken;
	frp.wake_threads_arg = &nfs_req_st;

	rc = fridgethr_init(&worker_fridge, "9P", &frp);
	if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to initialize worker fridge: %d", rc);
		return rc;
	}

	rc = fridgethr_populate(worker_fridge, worker_run, NULL);

	if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to populate worker fridge: %d", rc);
	}

	return rc;
}

int _9p_worker_shutdown(void)
{
	int rc = fridgethr_sync_command(worker_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_DISPATCH,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(worker_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Failed shutting down worker threads: %d", rc);
	}
	return rc;
}

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
		}
		break;

	default:
		LogCrit(COMPONENT_DISPATCH,
			"/!\\ Implementation error, 9P Dispatch function is called for non-9P request !!!!");
		return;
	}

	/* increase connection refcount */
	(void) atomic_inc_uint32_t(&req->r_u._9p.pconn->refcount);

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
	char strcaller[INET6_ADDRSTRLEN];
	request_data_t *req = NULL;
	int tag;
	unsigned long sequence = 0;
	unsigned int i = 0;
	char *_9pmsg = NULL;
	uint32_t msglen;

	struct _9p_conn _9p_conn;
	socklen_t addrpeerlen;

	int readlen = 0;
	int total_readlen = 0;

	snprintf(my_name, MAXNAMLEN, "9p_sock_mgr#fd=%ld", tcp_sock);
	SetNameFunction(my_name);

	/* Init the struct _9p_conn structure */
	memset(&_9p_conn, 0, sizeof(_9p_conn));
	PTHREAD_MUTEX_init(&_9p_conn.sock_lock, NULL);
	_9p_conn.trans_type = _9P_TCP;
	_9p_conn.trans_data.sockfd = tcp_sock;
	for (i = 0; i < FLUSH_BUCKETS; i++) {
		PTHREAD_MUTEX_init(&_9p_conn.flush_buckets[i].lock, NULL);
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

	addrpeerlen = sizeof(_9p_conn.addrpeer);
	rc = getpeername(tcp_sock, (struct sockaddr *)&_9p_conn.addrpeer,
			 &addrpeerlen);
	if (rc == -1) {
		LogMajor(COMPONENT_9P,
			 "Cannot get peername to tcp socket for 9p, error %d (%s)",
			 errno, strerror(errno));
		/* XXX */
		strncpy(strcaller, "(unresolved)", INET6_ADDRSTRLEN);
		strcaller[12] = '\0';
		goto end;
	} else {
		switch (_9p_conn.addrpeer.ss_family) {
		case AF_INET:
			inet_ntop(_9p_conn.addrpeer.ss_family,
				  &((struct sockaddr_in *)&_9p_conn.addrpeer)->
				  sin_addr, strcaller, INET6_ADDRSTRLEN);
			break;
		case AF_INET6:
			inet_ntop(_9p_conn.addrpeer.ss_family,
				  &((struct sockaddr_in6 *)&_9p_conn.addrpeer)->
				  sin6_addr, strcaller, INET6_ADDRSTRLEN);
			break;
		default:
			snprintf(strcaller, INET6_ADDRSTRLEN, "BAD ADDRESS");
			break;
		}

		LogEvent(COMPONENT_9P, "9p socket #%ld is connected to %s",
			 tcp_sock, strcaller);
	}
	_9p_conn.client = get_gsh_client(&_9p_conn.addrpeer, false);

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
		(void) atomic_inc_uint64_t(&nfs_health_.enqueued_reqs);
		req = pool_alloc(nfs_request_pool);

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
 * _9p_create_socket_V4 : create the socket and bind for 9P using
 * the available V4 interfaces on the host. This is not the default
 * for ganesha. We expect _9p_create_socket_V6 to be called first
 * and succeed. Only when the V6 function returns failure is this
 * function expected to be called. See _9p_create_socket().
 *
 * @return socket fd or -1 in case of failure
 *
 */
static int _9p_create_socket_V4(void)
{
	int			sock = -1;
	int			one = 1;
	int			centvingt = 120;
	int			neuf = 9;
	struct	netbuf		netbuf_tcp;
	struct	t_bind		bindaddr_tcp;
	struct	__rpc_sockinfo	si_tcp;
	struct	sockaddr_in	sinaddr_tcp;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Error creating 9p V4 socket, error %d(%s)",
			errno, strerror(errno));
		return -1;
	}

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
			&neuf, sizeof(neuf)) == -1)) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Error setting 9p V4 socket option, error %d(%s)",
			errno, strerror(errno));
		goto err;
	}

	memset(&sinaddr_tcp, 0, sizeof(sinaddr_tcp));
	sinaddr_tcp.sin_family = AF_INET;

	/* All the interfaces on the machine are used */
	sinaddr_tcp.sin_addr.s_addr = htonl(INADDR_ANY);
	sinaddr_tcp.sin_port = htons(_9p_param._9p_tcp_port);

	netbuf_tcp.maxlen = sizeof(sinaddr_tcp);
	netbuf_tcp.len = sizeof(sinaddr_tcp);
	netbuf_tcp.buf = &sinaddr_tcp;

	bindaddr_tcp.qlen = SOMAXCONN;
	bindaddr_tcp.addr = netbuf_tcp;

	if (!__rpc_fd2sockinfo(sock, &si_tcp)) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot get 9p socket info for tcp V4 socket error %d(%s)",
			errno, strerror(errno));
		goto err;
	}

	if (bind(sock,
		 (struct sockaddr *)bindaddr_tcp.addr.buf,
		 (socklen_t) si_tcp.si_alen) == -1) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot bind 9p tcp V4 socket, error %d(%s)", errno,
			strerror(errno));
		goto err;
	}

	if (listen(sock, 20) == -1) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot bind 9p tcp V4 socket, error %d(%s)", errno,
			strerror(errno));
		goto err;
	}

	return sock;

err:

	close(sock);
	return -1;
}

/**
 * _9p_create_socket_V6 : create the socket and bind for 9P using
 * the available V6 interfaces on the host
 *
 * @return socket fd or -1 in case of failure
 *
 */
static int _9p_create_socket_V6(void)
{
	int sock = -1;
	int one	= 1;
	int centvingt = 120;
	int neuf = 9;
	struct sockaddr_in6 sinaddr_tcp6;
	struct netbuf netbuf_tcp6;
	struct t_bind bindaddr_tcp6;
	struct __rpc_sockinfo si_tcp6;

	sock = socket(P_FAMILY, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1) {
		if (errno == EAFNOSUPPORT) {
			LogWarn(COMPONENT_9P_DISPATCH,
				"Error creating socket, V6 intfs disabled? error %d(%s)",
				errno, strerror(errno));
			return _9p_create_socket_V4();
		}

		return -1;
	}

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
			&neuf, sizeof(neuf)) == -1)) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Error setting V6 socket option, error %d(%s)",
			errno, strerror(errno));
		goto err;
	}

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
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot get 9p socket info for tcp6 socket error %d(%s)",
			errno, strerror(errno));
		goto err;
	}

	if (bind(sock,
		 (struct sockaddr *)bindaddr_tcp6.addr.buf,
		 (socklen_t) si_tcp6.si_alen) == -1) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot bind 9p tcp6 socket, error %d (%s)", errno,
			strerror(errno));
		goto err;
	}

	if (listen(sock, 20) == -1) {
		LogWarn(COMPONENT_9P_DISPATCH,
			"Cannot bind 9p tcp6 socket, error %d (%s)", errno,
			strerror(errno));
		goto err;
	}

	return sock;

err:

	close(sock);
	return -1;
}

/**
 * _9p_dispatcher_thread: thread used for RPC dispatching.
 *
 * This function is the main loop for the 9p dispatcher.
 * It never returns because it is an infinite loop.
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *_9p_dispatcher_thread(void *Arg)
{
	int _9p_socket;
	int rc = 0;
	long int newsock = -1;
	pthread_attr_t attr_thr;
	pthread_t tcp_thrid;

	SetNameFunction("_9p_disp");

	/* Calling dispatcher main loop */
	LogInfo(COMPONENT_9P_DISPATCH, "Entering nfs/rpc dispatcher");

	LogDebug(COMPONENT_9P_DISPATCH, "My pthread id is %p",
		 (void *) pthread_self());

	/* Set up the _9p_socket (trying V6 first, will fall back to V4
	 * if V6 fails).
	 */
	_9p_socket = _9p_create_socket_V6();

	if (_9p_socket == -1) {
		LogFatal(COMPONENT_9P_DISPATCH,
			 "Can't get socket for 9p dispatcher");
	}

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
		newsock = accept(_9p_socket, NULL, NULL);

		if (newsock < 0) {
			LogCrit(COMPONENT_9P_DISPATCH, "accept failed: %d",
				errno);
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

	close(_9p_socket);
	pthread_attr_destroy(&attr_thr);

	return NULL;
}				/* _9p_dispatcher_thread */
