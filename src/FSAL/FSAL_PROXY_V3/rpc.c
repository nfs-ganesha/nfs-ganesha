/*
 * Copyright 2020-2021 Google LLC
 * Author: Solomon Boulos <boulos@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include <inttypes.h>
#include <rpc/pmap_prot.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nfs23.h"

#include "proxyv3_fsal_methods.h"

static unsigned int rand_seed = 123451;
static char rpcMachineName[MAXHOSTNAMELEN + 1] = { 0 };
static pthread_mutex_t rpcLock;
static unsigned int rpcNumSockets;

/* Resizable buffer (capacity is allocated, len is used). */
struct rpc_buf {
	char *buf;
	size_t capacity;
	size_t len;
};

/* An entry in our pool of sockets/buffers */
struct fd_entry {
	bool in_use;
	bool is_open;

	/* Reuse needs to match the socket/socklen/port. */
	sockaddr_t socket;
	socklen_t socklen;
	uint16_t port;

	int fd;

	struct rpc_buf rpc_buf;
};

/* @todo Replace with free list / hash table / whatever. */
struct fd_entry *fd_entries;

/**
 * @brief Setup our RPC "stack" for PROXY_V3.
 *
 * @return - True, if no error.
 *         - False, with emitted warnings, otherwise.
 */

bool proxyv3_rpc_init(const uint num_sockets)
{
	LogDebug(COMPONENT_FSAL,
		 "Setting up connection pool with %u sockets", num_sockets);
	/* Cache our hostname for client auth later. */
	if (gethostname(rpcMachineName, sizeof(rpcMachineName)) != 0) {
		const char *kClientName = "127.0.0.1";

		LogCrit(COMPONENT_FSAL,
			"gethostname() failed. Errno %d (%s). Hardcoding a client IP instead.",
			errno, strerror(errno));
		memcpy(rpcMachineName, kClientName,
		       strlen(kClientName) + 1 /* For NUL */);
	}

	if (pthread_mutex_init(&rpcLock, NULL) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Failed to initialize a mutex... Errno %d (%s).",
			errno, strerror(errno));
		return false;
	}

	/* Initialize the fd_entries with not in_use sockets. */
	rpcNumSockets = num_sockets;
	fd_entries = gsh_calloc(rpcNumSockets, sizeof(struct fd_entry));
	/* Just in case the alloc failed, bail. */
	return fd_entries != NULL;
}


/**
 * @brief Given a host:port pair, try to open a socket.
 *
 * @param host Backend NFS host (as a sockaddr)
 * @param socklen Length of the host sockaddr (for IPv6 vs IPv4).
 * @param port The Port to connect to.
 *
 * @return - A valid fd on success, -1 otherwise.
 */

static int
proxyv3_openfd(const struct sockaddr *host,
	       const socklen_t socklen,
	       uint16_t port)
{
	int rc;

	LogDebug(COMPONENT_FSAL,
		 "Opening a new socket");

	if (host->sa_family != AF_INET &&
	    host->sa_family != AF_INET6) {
		LogCrit(COMPONENT_FSAL,
			"passed a host with sa_family %u",
			host->sa_family);
		return -1;
	}

	char addrForErrors[INET6_ADDRSTRLEN] = { 0 };

	/*
	 * Strangely, inet_ntop takes the length of the *buffer* not the length
	 * of the socket (perhaps it just uses the sa_family for socklen)
	 */

	if (!inet_ntop(host->sa_family, host,
		       addrForErrors, INET6_ADDRSTRLEN)) {
		LogCrit(COMPONENT_FSAL,
			"Couldn't decode host socket for debugging");
		return -1;
	}

	bool ipv6 = host->sa_family == AF_INET6;
	size_t expected_len = (ipv6) ?
		sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	if (socklen != expected_len) {
		LogCrit(COMPONENT_FSAL,
			"Given an ipv%s sockaddr (%s) with len %u != %zu",
			(ipv6) ? "6" : "4",
			addrForErrors,
			socklen,
			expected_len);
		return -1;
	}

	sockaddr_t hostAndPort;

	memset(&hostAndPort, 0, sizeof(hostAndPort));
	/* Copy the input, and then override the port. */
	memcpy(&hostAndPort, host, socklen);

	struct sockaddr_in  *hostv4 = (struct sockaddr_in *) &hostAndPort;
	struct sockaddr_in6 *hostv6 = (struct sockaddr_in6 *) &hostAndPort;

	/* Check that the caller is letting us slip the port in. */
	if ((ipv6 && hostv6->sin6_port != 0) ||
	    (!ipv6 && hostv4->sin_port != 0)) {
		unsigned int port = (ipv6) ?
			hostv6->sin6_port : hostv4->sin_port;
		LogCrit(COMPONENT_FSAL,
			"passed an address (%s) with non-zero port %u",
			addrForErrors, port);
		return -1;
	}

	int fd = socket((ipv6) ? PF_INET6 /* IPv6 */ : PF_INET /* IPv4 */,
			SOCK_STREAM /* TCP */,
			0 /* Pick it up from TCP */);

	if (fd < 0) {
		LogCrit(COMPONENT_FSAL,
			"Failed to create a socket. %d %s",
			errno, strerror(errno));
		return -1;
	}

	/*
	 * NOTE(boulos): NFS daemons like nfsd in Linux require that the
	 * clients come from a privileged port, so that they "must" be run
	 * as root on the client.
	 *
	 * NOTE(boulos): Some bindresvport_sa implementations are *also* not
	 * thread-safe (including libntirpc). So we need to hold a lock around
	 * calling it. Our only caller (proxyv3_getfdentry) no longer holds the
	 * rpcLock, so we can use that one.
	 */

	if (pthread_mutex_lock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_lock falied %d %s",
			errno, strerror(errno));
		close(fd);
		return -1;
	}

	rc = bindresvport_sa(fd, NULL);

	/* Unlock the rpclock before we exit, even if bindresvport_sa failed */
	if (pthread_mutex_unlock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_unlock falied %d %s",
			errno, strerror(errno));
		close(fd);
		return -1;
	}

	if (rc < 0) {
		LogCrit(COMPONENT_FSAL,
			"Failed to reserve a privileged port. %d %s",
			errno, strerror(errno));
		close(fd);
		return -1;
	}

	if (ipv6) {
		hostv6->sin6_port = htons(port);
	} else {
		hostv4->sin_port = htons(port);
	}

	if (connect(fd, (struct sockaddr *)&hostAndPort, socklen) < 0) {
		LogCrit(COMPONENT_FSAL,
			"Failed to connect to host '%s'. errno %d (%s)",
			addrForErrors, errno, strerror(errno));
		close(fd);
		return -1;
	}

	LogDebug(COMPONENT_FSAL,
		 "Got a new socket (%d) open to host %s",
		 fd, addrForErrors);

	return fd;
}


/**
 * @brief Check that an fd (from a socket) is open and ready.
 *
 * @param fd The file descriptor for our socket.
 *
 * @return - True if the socket is open, false otherwise.
 */

static bool
proxyv3_fd_is_open(int fd)
{
	/*
	 * If it's been a long time since we opened the socket, the
	 * other end probably hung up. We peek at the recv buffer here,
	 * to ensure that the socket is still open. If we happen to find
	 * bytes, something horrible must have happened.
	 */

	char buf[1];
	ssize_t bytes_read;

	/*
	 * We need both DONTWAIT for non-blocking and PEEK, so we don't
	 * actually pull any data off.
	 */

	bytes_read = recv(fd, buf, sizeof(buf),
			  MSG_DONTWAIT | MSG_PEEK);

	if (bytes_read == -1 &&
	    ((errno == EAGAIN || errno == EWOULDBLOCK))) {
		/* We would block => the socket is open! */
		LogFullDebug(COMPONENT_FSAL,
			     "Socket %d was still open. Reusing.", fd);
		return true;
	}

	/*
	 * Okay, we can't just re-use the existing socket. So we'll need
	 * a new one, but first report why we did.
	 */

	if (bytes_read == 0) {
		/* The other end closed at some point. */
		LogDebug(COMPONENT_FSAL,
			 "Socket %d was closed by the backend.", fd);
	} else if (bytes_read > 0) {
		LogCrit(COMPONENT_FSAL,
			"Unexpected data left in socket %d.", fd);
	} else {
		/* Some other error. Log and exit. */
		LogCrit(COMPONENT_FSAL,
			"Checking that socket %d was open had an error: %d '%s'.",
			fd, errno, strerror(errno));
	}

	return false;
}

/**
 * @brief Create an rpc_buf with a given capacity.
 *
 * @param rpc_buf Input rpc_buf pointer.
 * @param capacity The desired capacity in bytes.
 */

static void
proxyv3_rpcBuf_create(struct rpc_buf *rpc_buf, size_t capacity)
{
	rpc_buf->buf = gsh_calloc(1, capacity);
	rpc_buf->capacity = capacity;
	rpc_buf->len = 0;
}

/**
 * @brief Resize an rpc_buf to be len bytes long (realloc'ing if needed).
 *
 * @param rpc_buf Input rpc_buf pointer.
 * @param len The desired length in bytes.
 *
 * @return - The underlying buffer pointer (potentially realloc'ed).
 */
static char*
proxyv3_rpcBuf_resize(struct rpc_buf *rpc_buf, size_t len)
{
	if (rpc_buf->capacity < len) {
		/*
		 * Need to grow the buffer. NOTE(boulos): Unlike std::vector
		 * this isn't going to be used in a loop growing byte by byte or
		 * something, so while we could round up the requested length,
		 * we're unlikely to get N^2 style re-allocs.
		 */

		rpc_buf->buf = gsh_realloc(rpc_buf->buf, len);
		rpc_buf->capacity = len;
	}

	rpc_buf->len = len;
	return rpc_buf->buf;
}

/**
 * @brief Given a host:port pair, try to get/open an fd_entry from our pool.
 *
 * @param host Backend NFS host (as a sockaddr)
 * @param socklen Length of the host sockaddr (for IPv6 vs IPv4).
 * @param port The port to connect to.
 * @param retry Whether or not the caller should retry later.
 *
 * @return - A valid fd_entry on success, NULL otherwise.
 */

static struct fd_entry*
proxyv3_getfdentry(const struct sockaddr *host,
		   const socklen_t socklen,
		   uint16_t port,
		   bool *retry)
{
	/* In case we fail catastrophically, don't suggest a retry. */
	*retry = false;

	if (pthread_mutex_lock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_lock failed %d %s",
			errno, strerror(errno));
		return NULL;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Looking for an open socket for port %" PRIu16,
		     port);

	/* Find the first free, preferably open socket */
	struct fd_entry *first_free = NULL;
	struct fd_entry *first_open = NULL;
	struct fd_entry *result = NULL;
	size_t i;

	for (i = 0; i < rpcNumSockets; i++) {
		struct fd_entry *entry = &fd_entries[i];

		if (entry->in_use) {
			continue;
		}

		/* Remember that we saw a free slot. */
		if (first_free == NULL) {
			first_free = entry;
		}

		/* NOTE(boulos): first_free is now definitely not NULL. */
		if (!entry->is_open) {
			/*
			 * This entry is a free and not even opened slot, prefer
			 * that over our existing first_free if that one was
			 * open, so we allow socket reuse by others.
			 */
			if (first_free->is_open == true) {
				first_free = entry;
			}
		} else {
			/* See if this open socket matches what we need. */
			if (entry->socklen == socklen &&
			    entry->port == port &&
			    memcmp(&entry->socket, host, socklen) == 0) {
				LogFullDebug(COMPONENT_FSAL,
					     "Found an already open socket, will reuse that");
				first_open = entry;
				break;
			}
		}
	}

	/* The list is full! The caller needs to block. */
	if (first_free == NULL) {
		LogFullDebug(COMPONENT_FSAL,
			     "No available sockets. Tell the caller to wait");

		if (pthread_mutex_unlock(&rpcLock) != 0) {
			LogCrit(COMPONENT_FSAL,
				"pthread_mutex_unlock failed %d %s",
				errno, strerror(errno));
			return NULL;
		}

		*retry = true;
		return NULL;
	}

	/* Grab our result entry, and mark it as in use. */
	result = (first_open != NULL) ? first_open : first_free;
	result->in_use = true;

	/* Release the lock now that we got our entry. */
	if (pthread_mutex_unlock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_unlock failed %d %s",
			errno, strerror(errno));
		/*
		 * Return the entry to the list, since we aren't going to end up
		 * using it.
		 */
		result->in_use = false;
		return NULL;
	}

	/* If we already got one, return it, if it's still open. */
	if (first_open != NULL && proxyv3_fd_is_open(result->fd)) {
		return result;
	}

	if (result->is_open) {
		/* We should first close the existing socket. */
		LogFullDebug(COMPONENT_FSAL,
			     "Closing fd %d before we re-use the slot",
			     result->fd);
		if (close(result->fd) != 0) {
			LogCrit(COMPONENT_FSAL,
				"close(%d) of re-used fd failed. Continuing. Errno %d (%s)",
				result->fd, errno, strerror(errno));
		}

		/* Mark the entry as no longer open. */
		result->is_open = false;
	}

	/* Allocate a buffer, if we've never done so. */
	if (result->rpc_buf.buf == NULL) {
		/*
		 * First time create. NOTE(boulos): We wait to allocate this
		 * until its needed, because we want maxwrite to be filled in to
		 * match the NFS FSINFO result (which it's not during rpc init).
		 */

		const uint kHeaderPadding = 512;
		const uint kBufSize =
			PROXY_V3.module.fs_info.maxwrite + kHeaderPadding;

		proxyv3_rpcBuf_create(&result->rpc_buf, kBufSize);
	}

	/*
	 * No matter what, mark the buffer as having 0 bytes in use so far
	 * (capacity will remain unchanged).
	 */
	proxyv3_rpcBuf_resize(&result->rpc_buf, 0);

	int fd = proxyv3_openfd(host, socklen, port);

	if (fd < 0) {
		/*
		 * Failed for some reason. Mark this slot as empty, but leave
		 * the memory buffer alone. NOTE(boulos): retry is still false.
		 */
		result->in_use = false;
		return NULL;
	}

	/* Fill in the socket info. */
	result->fd = fd;
	result->is_open = true;
	memcpy(&result->socket, host, socklen);
	result->socklen = socklen;
	result->port = port;

	return result;
}

/**
 * @brief Given a host:port pair, try to open a socket (w/ exponential backoff).
 *
 * @param host Backend NFS host (as a sockaddr)
 * @param socklen Length of the host sockaddr (for IPv6 vs IPv4).
 * @param port The port to connect to.
 *
 * @return - A valid fd_entry pointer on success
 *         - NULL, otherwise.
 */

static struct fd_entry*
proxyv3_getfd_blocking(const struct sockaddr *host,
		       const socklen_t socklen,
		       uint16_t port)
{
	const size_t kMaxIterations = 100;
	/*
	 * So, within a datacenter, it's likely that we'll need to wait about 1
	 * millisecond for someone to finish. Let's start the backoff sooner
	 * though at 256 microseconds, because while an end-to-end op is 1ms,
	 * people should be finishing all the time. For folks across a WAN,
	 *  we'll back off quickly enough anyway.
	 */
	size_t numMicros = 256;
	/* Don't back off to more than 10 ms aka 10000 microsecond sleeps. */
	const size_t maxMicros = 10000;
	size_t i;

	for (i = 0; i < kMaxIterations; i++) {
		bool retry = false;
		struct fd_entry *entry =
			proxyv3_getfdentry(host, socklen, port, &retry);

		/* If we got back a valid entry, return it. */
		if (entry != NULL) {
			return entry;
		}

		/* If we not told to retry, exit. */
		if (!retry) {
			return NULL;
		}

		/* We were told to retry, let's wait. */
		struct timespec how_long = {
			/* 1M micros per second */
			.tv_sec  = numMicros / 1000000,
			/* Remainder => nanoseconds */
			.tv_nsec = (numMicros % 1000000) * 1000
		};

		LogFullDebug(COMPONENT_FSAL,
			     "Going to sleep for %zu microseconds",
			     numMicros);

		if (nanosleep(&how_long, NULL) != 0) {
			/*
			 * Let interrupts wake us up and not care. Anything else
			 * should be fatal.
			 */

			if (errno != EINTR) {
				LogCrit(COMPONENT_FSAL,
					"nanosleep failed. Asked for %zu micros. Errno %d (%s)",
					numMicros, errno, strerror(errno));
				return NULL;
			}
		}

		/* Next time around, double it. */
		numMicros *= 2;
		if (numMicros > maxMicros) {
			numMicros = maxMicros;
		}
	}

	LogCrit(COMPONENT_FSAL,
		"Failed to ever acquire a new fd, dying");
	return NULL;
}

/**
 * @brief Release an fd_entry to our pool (optionally closing the socket).
 *
 * @param entry An fd_entry pointer for an entry in our pool.
 * @param force_close Whether to always close the socket.
 *
 * @return - True, if we successfully cleaned up the entry.
 *         - False, otherwise.
 */

static bool proxyv3_release_fdentry(struct fd_entry *entry, bool force_close)
{
	LogFullDebug(COMPONENT_FSAL,
		     "Releasing fd %d back into the pool (close = %s)",
		     entry->fd, (force_close) ? "T" : "F");

	if (pthread_mutex_lock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_lock failed %d %s",
			errno, strerror(errno));
		return false;
	}

	if (entry->in_use != true) {
		LogCrit(COMPONENT_FSAL,
			"Tried to release entry (fd %d) that wasn't in_use!",
			entry->fd);
	} else {
		/*
		 * Mark the entry as no longer in use. (But leave it open,
		 * unless asked not to).
		 */
		entry->in_use = false;

		if (force_close) {
			/* Close the socket first. */
			if (close(entry->fd) < 0) {
				LogCrit(COMPONENT_FSAL,
					"close(%d) failed. Errno %d (%s)",
					entry->fd, errno, strerror(errno));
			}
			/* Clear the bytes that were *touched* not allocated. */
			memset(entry->rpc_buf.buf, 0, entry->rpc_buf.len);
			entry->is_open = false;
		}
	}

	if (pthread_mutex_unlock(&rpcLock) != 0) {
		LogCrit(COMPONENT_FSAL,
			"pthread_mutex_unlock failed %d %s",
			errno, strerror(errno));
		return false;
	}

	return true;
}

/*
 * NOTE(boulos): proxyv3_call is basically rpc_call redone by hand, because
 * ganesha's NFSD hijacks the RPC setup to the point where we can't issue our
 * own NFS-related rpcs as a simple client via clnt_ncreate (internally,
 * svc_exprt_lookup explodes saying "fd %d max_connections 0 exceeded").
 */

/**
 * @brief Send an RPC to host and get a reply, handling XDR encode/decode.
 *
 * @param host Backend NFS host (as a sockaddr)
 * @param socklen Length of the host sockaddr (for IPv6 vs IPv4).
 * @param port The port to connect to.
 * @param creds Optional credentials for auth.
 * @param rpcProgram The RPC Program (e.g., MOUNTPROG).
 * @param rpcVersion The RPC Version (e.g., NFS_V3).
 * @param rpcProc The RPC Procedure (e.g., NFSPROC3_LOOKUP).
 * @param encodeFunc The XDR encoding function (e.g., xdr_LOOKUP3args).
 * @param args The arg data (passed to encodeFunc).
 * @param decodeFunc The XDR decoding function (e.g., xdr_LOOKUP3res)
 * @param output The output buffer (passed to decodeFunc).
 *
 * @return - True, if no error.
 *         - False, with emitted warnings, otherwise.
 */

bool proxyv3_call(const struct sockaddr *host,
		  const socklen_t socklen,
		  uint16_t port,
		  const struct user_cred *creds,
		  const rpcprog_t rpcProgram,
		  const rpcvers_t rpcVersion,
		  const rpcproc_t rpcProc,
		  const xdrproc_t encodeFunc, void *args,
		  const xdrproc_t decodeFunc, void *output)
{
	XDR x;
	struct rpc_msg rmsg;
	struct rpc_msg reply;
	struct fd_entry *fd_entry;

	/* Log on entry, so we know what we were doing before we open the fd. */
	LogFullDebug(COMPONENT_FSAL,
		     "Sending an RPC: Program = %" PRIu32
		     ", Version = %" PRIu32
		     ", Procedure = %" PRIu32,
		     rpcProgram, rpcVersion, rpcProc);

	fd_entry = proxyv3_getfd_blocking(host, socklen, port);

	if (fd_entry == NULL) {
		/*
		 * Failed to get an fd even after blocking. Something probably
		 * went wrong.
		 */
		return false;
	}

	int fd = fd_entry->fd;
	char *msgBuf = fd_entry->rpc_buf.buf;
	size_t bufSize = fd_entry->rpc_buf.capacity;

	AUTH *au;

	if (creds != NULL) {
		au = authunix_ncreate(rpcMachineName,
				      creds->caller_uid, creds->caller_gid,
				      creds->caller_glen, creds->caller_garray);
	} else {
		/*
		 * Let ganesha do lots of syscalls to figure out our machiine
		 * name, uid, gid and so on.
		 */
		LogFullDebug(COMPONENT_FSAL,
			     "rpc, no creds => authunix_ncreate_default()");
		au = authunix_ncreate_default();
	}

	/*
	 * We need some transaction ID, so how about a random one. Note that
	 * while this isn't threadsafe, we're not particularly concerned about
	 * it since we just want random bytes.
	 */
	u_int xid = rand_r(&rand_seed);

	rmsg.rm_xid = xid;
	rmsg.rm_direction = CALL;
	rmsg.rm_call.cb_rpcvers = RPC_MSG_VERSION; /* *RPC* version not NFS */
	rmsg.cb_prog = rpcProgram;
	rmsg.cb_vers = rpcVersion;
	rmsg.cb_proc = rpcProc;
	rmsg.cb_cred = au->ah_cred;
	rmsg.cb_verf = au->ah_verf;

	memset(&x, 0, sizeof(x));

	/*
	 * Setup x with our buffer for encoding. Keep space at the front for the
	 * u_int recmark.
	 */

	xdrmem_create(&x,
		      msgBuf + sizeof(u_int),
		      bufSize - sizeof(u_int),
		      XDR_ENCODE);

	if (!xdr_callmsg(&x, &rmsg)) {
		LogCrit(COMPONENT_FSAL,
			"Failed to Setup xdr_callmsg");
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		AUTH_DESTROY(au);
		return false;
	}

	if (!encodeFunc(&x, args)) {
		LogCrit(COMPONENT_FSAL,
			"Failed to xdr-encode the args");
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		AUTH_DESTROY(au);
		return false;
	}

	/* Extract out the position to encode the record marker. */
	u_int pos = xdr_getpos(&x);
	u_int recmark = ntohl(pos | (1U << 31));
	/* Write the recmark at the start of the buffer */
	memcpy(msgBuf, &recmark, sizeof(recmark));
	/* Send the message plus the recmark. */
	size_t bytes_to_send = pos + sizeof(recmark);

	/*
	 * xdrmem_create should have respected our length parameter. Make sure,
	 * before we note via resize how many bytes we filled in.
	 */
	if (fd_entry->rpc_buf.capacity < bytes_to_send) {
		LogCrit(COMPONENT_FSAL,
			"xdrmem_create produced %zu bytes to send for our %zu-byte buffer",
			bytes_to_send, fd_entry->rpc_buf.capacity);
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		AUTH_DESTROY(au);
		return false;
	}

	/* Do the actual "resize". */
	(void) proxyv3_rpcBuf_resize(&fd_entry->rpc_buf, bytes_to_send);

	LogFullDebug(COMPONENT_FSAL,
		     "Sending XID %" PRIu32 " with %zu bytes",
		     rmsg.rm_xid, bytes_to_send);

	size_t total_bytes_written = 0;

	while (total_bytes_written < bytes_to_send) {
		size_t remaining = bytes_to_send - total_bytes_written;
		ssize_t bytes_written =
			write(fd,
			      msgBuf + total_bytes_written,
			      remaining);
		if (bytes_written < 0) {
			LogCrit(COMPONENT_FSAL,
				"Write at %zu failed (remaining was %zu). Errno %d (%s)",
				total_bytes_written, remaining,
				errno, strerror(errno));
			proxyv3_release_fdentry(fd_entry,
						true /* force close */);
			AUTH_DESTROY(au);
			return false;
		}

		total_bytes_written += bytes_written;
	}

	/* Cleanup the auth struct. We're just reading from here on out. */
	AUTH_DESTROY(au);

	/* Aww, short write. Exit. */
	if (total_bytes_written != bytes_to_send) {
		LogCrit(COMPONENT_FSAL,
			"Only wrote %zu bytes out of %zu",
			total_bytes_written, bytes_to_send);
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		return false;
	}

	/* Now flip it around and get the reply. */
	struct {
		uint recmark;
		uint xid;
	} response_header;

	LogFullDebug(COMPONENT_FSAL,
		     "Let's go ask for a response.");

	/* First try to read just the response "header". */
	if (read(fd, &response_header, 8) != 8) {
		LogCrit(COMPONENT_FSAL,
			"Didn't get a response header. errno %d, errstring %s",
			errno, strerror(errno));
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		return false;
	}

	/* Flip endian-ness if required */
	response_header.recmark = ntohl(response_header.recmark);
	response_header.xid = ntohl(response_header.xid);

	LogFullDebug(COMPONENT_FSAL,
		     "Got recmark %" PRIx32 " (%" PRIu32 " bytes) xid %" PRIu32,
		     response_header.recmark,
		     response_header.recmark & ~(1U << 31),
		     response_header.xid);

	if (response_header.xid != xid) {
		LogCrit(COMPONENT_FSAL,
			"Response xid %" PRIu32 " != request %" PRIu32,
			response_header.xid, xid);
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		return false;
	}

	/* Clear the top bit of the recmark */
	response_header.recmark &= ~(1U << 31);
	if (response_header.recmark < 8) {
		LogCrit(COMPONENT_FSAL,
			"Response claims to only have %" PRIu32 " bytes",
			response_header.recmark);
		proxyv3_release_fdentry(fd_entry, true /* force close */);
		return false;
	}

	/* We've already read the header (record mark) and xid. */
	size_t bytes_to_read = response_header.recmark;
	size_t total_bytes_read = 4;
	size_t read_buffer_size = bytes_to_read + sizeof(xid);

	/*
	 * We're going to need to read `read_buffer_size` bytes. Resize the
	 * buffer if needed to let us slurp the whole response back.
	 */
	msgBuf = proxyv3_rpcBuf_resize(&fd_entry->rpc_buf, read_buffer_size);

	/* Write the xid into the buffer. */
	memcpy(msgBuf, &xid, sizeof(xid));

	LogFullDebug(COMPONENT_FSAL,
		     "Going to read the remaining %zu bytes",
		     bytes_to_read - total_bytes_read);

	while (total_bytes_read < bytes_to_read) {
		ssize_t bytes_read =
			read(fd,
			     msgBuf + total_bytes_read,
			     bytes_to_read - total_bytes_read);

		if (bytes_read < 0) {
			LogCrit(COMPONENT_FSAL,
				"Read at %zu failed. Errno %d (%s)",
				total_bytes_read, errno, strerror(errno));
			proxyv3_release_fdentry(fd_entry,
						true /* force close */);
			return false;
		}

		total_bytes_read += bytes_read;
	}

	/* Aww, short read. Exit. */
	if (total_bytes_read != bytes_to_read) {
		LogCrit(COMPONENT_FSAL,
			"Only read %zu bytes out of %zu",
			total_bytes_read, bytes_to_read);
		proxyv3_release_fdentry(fd_entry,
					true /* force close */);
		return false;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Got all the bytes, time to decode");

	/* Lets decode the reply. */
	memset(&x, 0, sizeof(x));
	xdrmem_create(&x, msgBuf, total_bytes_read, XDR_DECODE);

	memset(&reply, 0, sizeof(reply));
	reply.RPCM_ack.ar_results.proc = decodeFunc;
	reply.RPCM_ack.ar_results.where = output;

	bool decoded = xdr_replymsg(&x, &reply);

	bool success =
		decoded &&
		reply.rm_reply.rp_stat == MSG_ACCEPTED &&
		reply.rm_reply.rp_acpt.ar_stat == SUCCESS;

	/* If we failed to decode, say so. */
	if (!decoded) {
		LogCrit(COMPONENT_FSAL,
			"Failed to do xdr_replymsg");
	}

	/* Check that it was accepted, if not, say why not. */
	if (reply.rm_reply.rp_stat != MSG_ACCEPTED) {
		LogCrit(COMPONENT_FSAL,
			"Reply received but not accepted. REJ %d",
			reply.rm_reply.rp_rjct.rj_stat);
	}

	/* Check that it was accepted with success. */
	if (reply.rm_reply.rp_acpt.ar_stat != SUCCESS) {
		LogCrit(COMPONENT_FSAL,
			"Reply accepted but unsuccesful. Reason %d",
			reply.rm_reply.rp_acpt.ar_stat);
	}

	/*
	 * Clean up whatever xdr_replymsg may have allocated, but don't smash
	 * the data in the output buffer.
	 */

	reply.RPCM_ack.ar_results.proc = (xdrproc_t) xdr_void;
	reply.RPCM_ack.ar_results.where = NULL;
	xdr_free((xdrproc_t) xdr_replymsg, &reply);

	/* Return our socket and buffer to the pool. */
	proxyv3_release_fdentry(fd_entry,
				false /* let's reuse the socket */);

	LogFullDebug(COMPONENT_FSAL,
		     "RPC Completed %s: Program = %" PRIu32
		     ", Version = %" PRIu32
		     ", Procedure = %" PRIu32,
		     (success) ? "SUCCESSFULLY" : " but FAILED",
		     rpcProgram, rpcVersion, rpcProc);

	return success;
}

/**
 * @brief Wrapper around proxyv3_call for NFS v3.
 */
bool proxyv3_nfs_call(const struct sockaddr *host,
		      const socklen_t socklen,
		      const uint nfsdPort,
		      const struct user_cred *creds,
		      const rpcproc_t nfsProc,
		      const xdrproc_t encodeFunc, void *args,
		      const xdrproc_t decodeFunc, void *output)
{
	const int kProgramNFS = NFS_PROGRAM;
	const int kVersionNFSv3 = NFS_V3;

	return proxyv3_call(host, socklen, nfsdPort, creds,
			    kProgramNFS, kVersionNFSv3,
			    nfsProc, encodeFunc, args, decodeFunc, output);
}

/**
 * @brief Wrapper around proxyv3_call for MOUNT v3.
 */
bool proxyv3_mount_call(const struct sockaddr *host,
			const socklen_t socklen,
			const uint mountdPort,
			const struct user_cred *creds,
			const rpcproc_t mountProc,
			const xdrproc_t encodeFunc, void *args,
			const xdrproc_t decodeFunc, void *output)
{
	const int kProgramMount = MOUNTPROG;
	const int kVersionMountv3 = MOUNT_V3;

	return proxyv3_call(host, socklen, mountdPort, creds,
			    kProgramMount, kVersionMountv3,
			    mountProc, encodeFunc, args, decodeFunc, output);
}

/**
 * @brief Wrapper around proxyv3_call for NLM v4.
 */
bool proxyv3_nlm_call(const struct sockaddr *host,
		      const socklen_t socklen,
		      const uint nlmPort,
		      const struct user_cred *creds,
		      const rpcproc_t nlmProc,
		      const xdrproc_t encodeFunc, void *args,
		      const xdrproc_t decodeFunc, void *output)
{
	const int kProgramNLM = NLMPROG;
	const int kVersionNLMv4 = NLM4_VERS;

	return proxyv3_call(host, socklen, nlmPort, creds,
			    kProgramNLM, kVersionNLMv4,
			    nlmProc, encodeFunc, args, decodeFunc, output);
}

/**
 * @brief Ask portmapd for where MOUNTD and NFSD are running.
 *
 * @param host Backend NFS host (as a sockaddr)
 * @param socklen Length of the host sockaddr (for IPv6 vs IPv4).
 * @param mountd_port Port for MOUNTD.
 * @param nfsd_port Port for NFS (v3).
 * @param nlm_port Port for NLM.
 *
 * @return - True, if no error.
 *         - False, with emitted warnings, otherwise.
 */
bool proxyv3_find_ports(const struct sockaddr *host,
			const socklen_t socklen,
			u_int *mountd_port,
			u_int *nfsd_port,
			u_int *nlm_port)
{
	struct pmap mountd_query = {
		.pm_prog = MOUNTPROG,
		.pm_vers = MOUNT_V3,
		.pm_prot = IPPROTO_TCP,
		.pm_port = 0 /* ignored for getport */
	};

	struct pmap nfsd_query = {
		.pm_prog = NFS_PROGRAM,
		.pm_vers = NFS_V3,
		.pm_prot = IPPROTO_TCP,
		.pm_port = 0 /* ignored */
	};

	struct pmap nlm_query = {
		.pm_prog = NLMPROG,
		.pm_vers = NLM4_VERS,
		.pm_prot = IPPROTO_TCP,
		.pm_port = 0 /* ignored */
	};

	struct {
		struct pmap *input;
		u_int *port;
		const char *name;
	} queries[] = {
		{ &mountd_query, mountd_port, "mountd" },
		{ &nfsd_query, nfsd_port, "nfsd" },
		/* If we put NLM last, we can let it just warn in debug mode. */
		{ &nlm_query, nlm_port, "nlm" }
	};

	size_t i;

	for (i = 0; i < sizeof(queries)/sizeof(queries[0]); i++) {
		LogDebug(COMPONENT_FSAL,
			 "Asking portmap to tell us what the %s/tcp port is",
			 queries[i].name);

		if (!proxyv3_call(host, socklen, PMAPPORT,
				  NULL /* no auth for portmapd */,
				  PMAPPROG, PMAPVERS,
				  PMAPPROC_GETPORT,
				  (xdrproc_t) xdr_pmap, queries[i].input,
				  (xdrproc_t) xdr_u_int, queries[i].port)) {
			LogDebug(COMPONENT_FSAL,
				 "Failed to find %s", queries[i].name);
			return false;
		}

		LogDebug(COMPONENT_FSAL,
			 "Got back %s port %" PRIu32,
			 queries[i].name, *queries[i].port);
	}

	return true;
}
