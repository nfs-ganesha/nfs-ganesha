/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
 * Copyright CEA/DAM/DIF  (2008)
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 */

/* Proxy handle methods */

#include "config.h"

#include "fsal.h"
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <netdb.h>
#include "gsh_list.h"
#include "abstract_atomic.h"
#include "fsal_types.h"
#include "FSAL/fsal_commonlib.h"
#include "pxy_fsal_methods.h"
#include "fsal_nfsv4_macros.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"
#include "common_utils.h"

#define FSAL_PROXY_NFS_V4 4
#define FSAL_PROXY_NFS_V4_MINOR 1
#define NB_RPC_SLOT 16
#define NB_MAX_OPERATIONS 10

/* NB! nfs_prog is just an easy way to get this info into the call
 *     It should really be fetched via export pointer */
/**
 * We mutualize rpc_context and slot NFSv4.1.
 */
struct pxy_rpc_io_context {
	pthread_mutex_t iolock;
	pthread_cond_t iowait;
	struct glist_head calls;
	uint32_t rpc_xid;
	bool iodone;
	int ioresult;
	unsigned int nfs_prog;
	unsigned int sendbuf_sz;
	unsigned int recvbuf_sz;
	char *sendbuf;
	char *recvbuf;
	slotid4 slotid;
	sequenceid4 seqid;
};

/* Use this to estimate storage requirements for fattr4 blob */
struct pxy_fattr_storage {
	fattr4_type type;
	fattr4_change change_time;
	fattr4_size size;
	fattr4_fsid fsid;
	fattr4_filehandle filehandle;
	fattr4_fileid fileid;
	fattr4_mode mode;
	fattr4_numlinks numlinks;
	fattr4_owner owner;
	fattr4_owner_group owner_group;
	fattr4_space_used space_used;
	fattr4_time_access time_access;
	fattr4_time_metadata time_metadata;
	fattr4_time_modify time_modify;
	fattr4_rawdev rawdev;
	char padowner[MAXNAMLEN + 1];
	char padgroup[MAXNAMLEN + 1];
	char padfh[NFS4_FHSIZE];
};

#define FATTR_BLOB_SZ sizeof(struct pxy_fattr_storage)

/*
 * This is what becomes an opaque FSAL handle for the upper layers.
 *
 * The type is a placeholder for future expansion.
 */
struct pxy_handle_blob {
	uint8_t len;
	uint8_t type;
	uint8_t bytes[0];
};

struct pxy_obj_handle {
	struct fsal_obj_handle obj;
	nfs_fh4 fh4;
#ifdef PROXY_HANDLE_MAPPING
	nfs23_map_handle_t h23;
#endif
	fsal_openflags_t openflags;
	struct pxy_handle_blob blob;
};

static struct pxy_obj_handle *pxy_alloc_handle(struct fsal_export *exp,
					       const nfs_fh4 *fh,
					       fattr4 *obj_attributes,
					       struct attrlist *attrs_out);

struct pxy_state {
	struct state_t state;
	stateid4 stateid;
};

struct state_t *pxy_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state)
{
	return init_state(gsh_calloc(1, sizeof(struct pxy_state)), exp_hdl,
			   state_type, related_state);
}

void pxy_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct pxy_state *pxy_state_id = container_of(state, struct pxy_state,
						      state);

	gsh_free(pxy_state_id);
}

#define FSAL_VERIFIER_T_TO_VERIFIER4(verif4, fsal_verif)		\
do { \
	BUILD_BUG_ON(sizeof(fsal_verifier_t) != sizeof(verifier4));	\
	memcpy(verif4, fsal_verif, sizeof(fsal_verifier_t));		\
} while (0)

static fsal_status_t nfsstat4_to_fsal(nfsstat4 nfsstatus)
{
	switch (nfsstatus) {
	case NFS4ERR_SAME:
	case NFS4ERR_NOT_SAME:
	case NFS4_OK:
		return fsalstat(ERR_FSAL_NO_ERROR, (int)nfsstatus);
	case NFS4ERR_PERM:
		return fsalstat(ERR_FSAL_PERM, (int)nfsstatus);
	case NFS4ERR_NOENT:
		return fsalstat(ERR_FSAL_NOENT, (int)nfsstatus);
	case NFS4ERR_IO:
		return fsalstat(ERR_FSAL_IO, (int)nfsstatus);
	case NFS4ERR_NXIO:
		return fsalstat(ERR_FSAL_NXIO, (int)nfsstatus);
	case NFS4ERR_EXPIRED:
	case NFS4ERR_LOCKED:
	case NFS4ERR_SHARE_DENIED:
	case NFS4ERR_LOCK_RANGE:
	case NFS4ERR_OPENMODE:
	case NFS4ERR_FILE_OPEN:
	case NFS4ERR_ACCESS:
	case NFS4ERR_DENIED:
		return fsalstat(ERR_FSAL_ACCESS, (int)nfsstatus);
	case NFS4ERR_EXIST:
		return fsalstat(ERR_FSAL_EXIST, (int)nfsstatus);
	case NFS4ERR_XDEV:
		return fsalstat(ERR_FSAL_XDEV, (int)nfsstatus);
	case NFS4ERR_NOTDIR:
		return fsalstat(ERR_FSAL_NOTDIR, (int)nfsstatus);
	case NFS4ERR_ISDIR:
		return fsalstat(ERR_FSAL_ISDIR, (int)nfsstatus);
	case NFS4ERR_FBIG:
		return fsalstat(ERR_FSAL_FBIG, 0);
	case NFS4ERR_NOSPC:
		return fsalstat(ERR_FSAL_NOSPC, (int)nfsstatus);
	case NFS4ERR_ROFS:
		return fsalstat(ERR_FSAL_ROFS, (int)nfsstatus);
	case NFS4ERR_MLINK:
		return fsalstat(ERR_FSAL_MLINK, (int)nfsstatus);
	case NFS4ERR_NAMETOOLONG:
		return fsalstat(ERR_FSAL_NAMETOOLONG, (int)nfsstatus);
	case NFS4ERR_NOTEMPTY:
		return fsalstat(ERR_FSAL_NOTEMPTY, (int)nfsstatus);
	case NFS4ERR_DQUOT:
		return fsalstat(ERR_FSAL_DQUOT, (int)nfsstatus);
	case NFS4ERR_STALE:
		return fsalstat(ERR_FSAL_STALE, (int)nfsstatus);
	case NFS4ERR_NOFILEHANDLE:
	case NFS4ERR_BADHANDLE:
		return fsalstat(ERR_FSAL_BADHANDLE, (int)nfsstatus);
	case NFS4ERR_BAD_COOKIE:
		return fsalstat(ERR_FSAL_BADCOOKIE, (int)nfsstatus);
	case NFS4ERR_NOTSUPP:
		return fsalstat(ERR_FSAL_NOTSUPP, (int)nfsstatus);
	case NFS4ERR_TOOSMALL:
		return fsalstat(ERR_FSAL_TOOSMALL, (int)nfsstatus);
	case NFS4ERR_SERVERFAULT:
		return fsalstat(ERR_FSAL_SERVERFAULT, (int)nfsstatus);
	case NFS4ERR_BADTYPE:
		return fsalstat(ERR_FSAL_BADTYPE, (int)nfsstatus);
	case NFS4ERR_GRACE:
	case NFS4ERR_DELAY:
		return fsalstat(ERR_FSAL_DELAY, (int)nfsstatus);
	case NFS4ERR_FHEXPIRED:
		return fsalstat(ERR_FSAL_FHEXPIRED, (int)nfsstatus);
	case NFS4ERR_WRONGSEC:
		return fsalstat(ERR_FSAL_SEC, (int)nfsstatus);
	case NFS4ERR_SYMLINK:
		return fsalstat(ERR_FSAL_SYMLINK, (int)nfsstatus);
	case NFS4ERR_ATTRNOTSUPP:
		return fsalstat(ERR_FSAL_ATTRNOTSUPP, (int)nfsstatus);
	case NFS4ERR_BADNAME:
		return fsalstat(ERR_FSAL_BADNAME, (int)nfsstatus);
	case NFS4ERR_INVAL:
	case NFS4ERR_CLID_INUSE:
	case NFS4ERR_MOVED:
	case NFS4ERR_RESOURCE:
	case NFS4ERR_MINOR_VERS_MISMATCH:
	case NFS4ERR_STALE_CLIENTID:
	case NFS4ERR_STALE_STATEID:
	case NFS4ERR_OLD_STATEID:
	case NFS4ERR_BAD_STATEID:
	case NFS4ERR_BAD_SEQID:
	case NFS4ERR_RESTOREFH:
	case NFS4ERR_LEASE_MOVED:
	case NFS4ERR_NO_GRACE:
	case NFS4ERR_RECLAIM_BAD:
	case NFS4ERR_RECLAIM_CONFLICT:
	case NFS4ERR_BADXDR:
	case NFS4ERR_BADCHAR:
	case NFS4ERR_BAD_RANGE:
	case NFS4ERR_BADOWNER:
	case NFS4ERR_OP_ILLEGAL:
	case NFS4ERR_LOCKS_HELD:
	case NFS4ERR_LOCK_NOTSUPP:
	case NFS4ERR_DEADLOCK:
	case NFS4ERR_ADMIN_REVOKED:
	case NFS4ERR_CB_PATH_DOWN:
	default:
		return fsalstat(ERR_FSAL_INVAL, (int)nfsstatus);
	}
}

#define PXY_ATTR_BIT(b) (1U << b)
#define PXY_ATTR_BIT2(b) (1U << (b - 32))

static struct bitmap4 pxy_bitmap_getattr = {
	.map[0] =
	    (PXY_ATTR_BIT(FATTR4_SUPPORTED_ATTRS) |
	     PXY_ATTR_BIT(FATTR4_TYPE) | PXY_ATTR_BIT(FATTR4_CHANGE) |
	     PXY_ATTR_BIT(FATTR4_SIZE) | PXY_ATTR_BIT(FATTR4_FSID) |
	     PXY_ATTR_BIT(FATTR4_FILEID)),
	.map[1] =
	    (PXY_ATTR_BIT2(FATTR4_MODE) | PXY_ATTR_BIT2(FATTR4_NUMLINKS) |
	     PXY_ATTR_BIT2(FATTR4_OWNER) | PXY_ATTR_BIT2(FATTR4_OWNER_GROUP) |
	     PXY_ATTR_BIT2(FATTR4_SPACE_USED) |
	     PXY_ATTR_BIT2(FATTR4_TIME_ACCESS) |
	     PXY_ATTR_BIT2(FATTR4_TIME_METADATA) |
	     PXY_ATTR_BIT2(FATTR4_TIME_MODIFY) | PXY_ATTR_BIT2(FATTR4_RAWDEV)),
	.bitmap4_len = 2
};

static struct bitmap4 pxy_bitmap_fsinfo = {
	.map[0] =
	    (PXY_ATTR_BIT(FATTR4_FILES_AVAIL) | PXY_ATTR_BIT(FATTR4_FILES_FREE)
	     | PXY_ATTR_BIT(FATTR4_FILES_TOTAL)),
	.map[1] =
	    (PXY_ATTR_BIT2(FATTR4_SPACE_AVAIL) |
	     PXY_ATTR_BIT2(FATTR4_SPACE_FREE) |
	     PXY_ATTR_BIT2(FATTR4_SPACE_TOTAL)),
	.bitmap4_len = 2
};

static struct bitmap4 lease_bits = {
	.map[0] = PXY_ATTR_BIT(FATTR4_LEASE_TIME),
	.bitmap4_len = 1
};

static struct bitmap4 pxy_bitmap_per_file_system_attr = {
	.map[0] = PXY_ATTR_BIT(FATTR4_MAXREAD) | PXY_ATTR_BIT(FATTR4_MAXWRITE),
	.bitmap4_len = 1
};

#undef PXY_ATTR_BIT
#undef PXY_ATTR_BIT2

static struct {
	attrmask_t mask;
	int fattr_bit;
} fsal_mask2bit[] = {
	{
	ATTR_SIZE, FATTR4_SIZE}, {
	ATTR_MODE, FATTR4_MODE}, {
	ATTR_OWNER, FATTR4_OWNER}, {
	ATTR_GROUP, FATTR4_OWNER_GROUP}, {
	ATTR_ATIME, FATTR4_TIME_ACCESS_SET}, {
	ATTR_ATIME_SERVER, FATTR4_TIME_ACCESS_SET}, {
	ATTR_MTIME, FATTR4_TIME_MODIFY_SET}, {
	ATTR_MTIME_SERVER, FATTR4_TIME_MODIFY_SET}, {
	ATTR_CTIME, FATTR4_TIME_METADATA}
};

static struct bitmap4 empty_bitmap = {
	.map[0] = 0,
	.map[1] = 0,
	.map[2] = 0,
	.bitmap4_len = 2
};

static int pxy_fsalattr_to_fattr4(const struct attrlist *attrs, fattr4 *data)
{
	int i;
	struct bitmap4 bmap = empty_bitmap;
	struct xdr_attrs_args args;

	for (i = 0; i < ARRAY_SIZE(fsal_mask2bit); i++) {
		if (FSAL_TEST_MASK(attrs->valid_mask, fsal_mask2bit[i].mask)) {
			if (fsal_mask2bit[i].fattr_bit > 31) {
				bmap.map[1] |=
				    1U << (fsal_mask2bit[i].fattr_bit - 32);
				bmap.bitmap4_len = 2;
			} else {
				bmap.map[0] |=
					1U << fsal_mask2bit[i].fattr_bit;
			}
		}
	}

	memset(&args, 0, sizeof(args));
	args.attrs = (struct attrlist *)attrs;
	args.data = NULL;

	return nfs4_FSALattr_To_Fattr(&args, &bmap, data);
}

static GETATTR4resok *pxy_fill_getattr_reply(nfs_resop4 *resop, char *blob,
					     size_t blob_sz)
{
	GETATTR4resok *a = &resop->nfs_resop4_u.opgetattr.GETATTR4res_u.resok4;

	a->obj_attributes.attrmask = empty_bitmap;
	a->obj_attributes.attr_vals.attrlist4_val = blob;
	a->obj_attributes.attr_vals.attrlist4_len = blob_sz;

	return a;
}

static int pxy_got_rpc_reply(struct pxy_rpc_io_context *ctx, int sock, int sz,
			     u_int xid)
{
	char *repbuf = ctx->recvbuf;
	int size;

	if (sz > ctx->recvbuf_sz)
		return -E2BIG;

	PTHREAD_MUTEX_lock(&ctx->iolock);
	memcpy(repbuf, &xid, sizeof(xid));
	/*
	 * sz includes 4 bytes of xid which have been processed
	 * together with record mark - reduce the read to avoid
	 * gobbing up next record mark.
	 */
	repbuf += 4;
	ctx->ioresult = 4;
	sz -= 4;

	while (sz > 0) {
		/* TODO: handle timeouts - use poll(2) */
		int bc = read(sock, repbuf, sz);

		if (bc <= 0) {
			ctx->ioresult = -((bc < 0) ? errno : ETIMEDOUT);
			break;
		}
		repbuf += bc;
		ctx->ioresult += bc;
		sz -= bc;
	}
	ctx->iodone = true;
	size = ctx->ioresult;
	pthread_cond_signal(&ctx->iowait);
	PTHREAD_MUTEX_unlock(&ctx->iolock);
	return size;
}

static int pxy_rpc_read_reply(struct pxy_export *pxy_exp)
{
	struct {
		uint recmark;
		uint xid;
	} h;
	char *buf = (char *)&h;
	struct glist_head *c;
	char sink[256];
	int cnt = 0;

	while (cnt < 8) {
		int bc = read(pxy_exp->rpc.rpc_sock, buf + cnt, 8 - cnt);

		if (bc < 0)
			return -errno;
		cnt += bc;
	}

	h.recmark = ntohl(h.recmark);
	/* TODO: check for final fragment */
	h.xid = ntohl(h.xid);

	LogDebug(COMPONENT_FSAL, "Recmark %x, xid %u\n", h.recmark, h.xid);
	h.recmark &= ~(1U << 31);

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	glist_for_each(c, &pxy_exp->rpc.rpc_calls) {
		struct pxy_rpc_io_context *ctx =
		    container_of(c, struct pxy_rpc_io_context, calls);

		if (ctx->rpc_xid == h.xid) {
			glist_del(c);
			PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
			return pxy_got_rpc_reply(ctx, pxy_exp->rpc.rpc_sock,
						 h.recmark, h.xid);
		}
	}
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);

	cnt = h.recmark - 4;
	LogDebug(COMPONENT_FSAL, "xid %u is not on the list, skip %d bytes\n",
		 h.xid, cnt);
	while (cnt > 0) {
		int rb = (cnt > sizeof(sink)) ? sizeof(sink) : cnt;

		rb = read(pxy_exp->rpc.rpc_sock, sink, rb);
		if (rb <= 0)
			return -errno;
		cnt -= rb;
	}

	return 0;
}

/* called with listlock */
static void pxy_new_socket_ready(struct pxy_export *pxy_exp)
{
	struct glist_head *nxt;
	struct glist_head *c;

	/* If there are any outstanding calls then tell them to resend */
	glist_for_each_safe(c, nxt, &pxy_exp->rpc.rpc_calls) {
		struct pxy_rpc_io_context *ctx =
		    container_of(c, struct pxy_rpc_io_context, calls);

		glist_del(c);

		PTHREAD_MUTEX_lock(&ctx->iolock);
		ctx->iodone = true;
		ctx->ioresult = -EAGAIN;
		pthread_cond_signal(&ctx->iowait);
		PTHREAD_MUTEX_unlock(&ctx->iolock);
	}

	/* If there is anyone waiting for the socket then tell them
	 * it's ready */
	pthread_cond_broadcast(&pxy_exp->rpc.sockless);
}

/* called with listlock */
static int pxy_connect(struct pxy_export *pxy_exp,
		       sockaddr_t *dest, uint16_t port)
{
	int sock;
	int socklen;

	if (pxy_exp->info.use_privileged_client_port) {
		int priv_port = 0;

		sock = rresvport_af(&priv_port, dest->ss_family);
		if (sock < 0)
			LogCrit(COMPONENT_FSAL,
				"Cannot create TCP socket on privileged port");
	} else {
		sock = socket(dest->ss_family, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0)
			LogCrit(COMPONENT_FSAL, "Cannot create TCP socket - %d",
				errno);
	}

	switch (dest->ss_family) {
	case AF_INET:
		((struct sockaddr_in *)dest)->sin_port = htons(port);
		socklen = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)dest)->sin6_port = htons(port);
		socklen = sizeof(struct sockaddr_in6);
		break;
	default:
		LogCrit(COMPONENT_FSAL, "Unknown address family %d",
			dest->ss_family);
		close(sock);
		return -1;
	}

	if (sock >= 0) {
		if (connect(sock, (struct sockaddr *)dest, socklen) < 0) {
			close(sock);
			sock = -1;
		} else {
			pxy_new_socket_ready(pxy_exp);
		}
	}
	return sock;
}

/*
 * NB! rpc_sock can be closed by the sending thread but it will not be
 *     changing its value. Only this function will change rpc_sock which
 *     means that it can look at the value without holding the lock.
 */
static void *pxy_rpc_recv(void *arg)
{
	struct pxy_export *pxy_exp = arg;
	char addr[INET6_ADDRSTRLEN];
	struct pollfd pfd;
	int millisec = pxy_exp->info.srv_timeout * 1000;

	SetNameFunction("pxy_rcv_thread");

	while (!pxy_exp->rpc.close_thread) {
		int nsleeps = 0;

		PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
		do {
			pxy_exp->rpc.rpc_sock = pxy_connect(pxy_exp,
						&pxy_exp->info.srv_addr,
						pxy_exp->info.srv_port);
			/* early stop test */
			if (pxy_exp->rpc.close_thread) {
				PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
				return NULL;
			}
			if (pxy_exp->rpc.rpc_sock < 0) {
				if (nsleeps == 0)
					sprint_sockaddr(&pxy_exp->info.srv_addr,
							addr, sizeof(addr));
					LogCrit(COMPONENT_FSAL,
						"Cannot connect to server %s:%u",
						addr, pxy_exp->info.srv_port);
				PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
				sleep(pxy_exp->info.retry_sleeptime);
				nsleeps++;
				PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
			} else {
				LogDebug(COMPONENT_FSAL,
					 "Connected after %d sleeps, resending outstanding calls",
					 nsleeps);
			}
		} while (pxy_exp->rpc.rpc_sock < 0 &&
						!pxy_exp->rpc.close_thread);
		PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
		/* early stop test */
		if (pxy_exp->rpc.close_thread)
			return NULL;

		pfd.fd = pxy_exp->rpc.rpc_sock;
		pfd.events = POLLIN | POLLRDHUP;

		while (pxy_exp->rpc.rpc_sock >= 0) {
			switch (poll(&pfd, 1, millisec)) {
			case 0:
				LogDebug(COMPONENT_FSAL,
					 "Timeout, wait again...");
				continue;

			case -1:
				break;

			default:
				if (pfd.revents & POLLRDHUP) {
					LogEvent(COMPONENT_FSAL,
						 "Other end has closed connection, reconnecting...");
				} else if (pfd.revents & POLLNVAL) {
					LogEvent(COMPONENT_FSAL,
						 "Socket is closed");
				} else {
					if (pxy_rpc_read_reply(pxy_exp) >= 0)
						continue;
				}
				break;
			}

			PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
			close(pxy_exp->rpc.rpc_sock);
			pxy_exp->rpc.rpc_sock = -1;
			PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
		}
	}

	return NULL;
}

static enum clnt_stat pxy_process_reply(struct pxy_rpc_io_context *ctx,
					COMPOUND4res *res)
{
	enum clnt_stat rc = RPC_CANTRECV;
	struct timespec ts;

	PTHREAD_MUTEX_lock(&ctx->iolock);
	ts.tv_sec = time(NULL) + 60;
	ts.tv_nsec = 0;

	while (!ctx->iodone) {
		int w = pthread_cond_timedwait(&ctx->iowait, &ctx->iolock, &ts);

		if (w == ETIMEDOUT) {
			PTHREAD_MUTEX_unlock(&ctx->iolock);
			return RPC_TIMEDOUT;
		}
	}

	ctx->iodone = false;
	PTHREAD_MUTEX_unlock(&ctx->iolock);

	if (ctx->ioresult > 0) {
		struct rpc_msg reply;
		XDR x;

		memset(&reply, 0, sizeof(reply));
		reply.RPCM_ack.ar_results.proc =
		    (xdrproc_t) xdr_COMPOUND4res;
		reply.RPCM_ack.ar_results.where = res;

		memset(&x, 0, sizeof(x));
		xdrmem_create(&x, ctx->recvbuf, ctx->ioresult, XDR_DECODE);

		/* macro is defined, GCC 4.7.2 ignoring */
		if (xdr_replymsg(&x, &reply)) {
			if (reply.rm_reply.rp_stat == MSG_ACCEPTED) {
				switch (reply.rm_reply.rp_acpt.ar_stat) {
				case SUCCESS:
					rc = RPC_SUCCESS;
					break;
				case PROG_UNAVAIL:
					rc = RPC_PROGUNAVAIL;
					break;
				case PROG_MISMATCH:
					rc = RPC_PROGVERSMISMATCH;
					break;
				case PROC_UNAVAIL:
					rc = RPC_PROCUNAVAIL;
					break;
				case GARBAGE_ARGS:
					rc = RPC_CANTDECODEARGS;
					break;
				case SYSTEM_ERR:
					rc = RPC_SYSTEMERROR;
					break;
				default:
					rc = RPC_FAILED;
					break;
				}
			} else {
				switch (reply.rm_reply.rp_rjct.rj_stat) {
				case RPC_MISMATCH:
					rc = RPC_VERSMISMATCH;
					break;
				case AUTH_ERROR:
					rc = RPC_AUTHERROR;
					break;
				default:
					rc = RPC_FAILED;
					break;
				}
			}
		} else {
			rc = RPC_CANTDECODERES;
		}

		reply.RPCM_ack.ar_results.proc = (xdrproc_t) xdr_void;
		reply.RPCM_ack.ar_results.where = NULL;

		xdr_free((xdrproc_t) xdr_replymsg, &reply);
	}
	return rc;
}

static inline int pxy_rpc_need_sock(struct pxy_export *pxy_exp)
{
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	while (pxy_exp->rpc.rpc_sock < 0 && !pxy_exp->rpc.close_thread)
		pthread_cond_wait(&pxy_exp->rpc.sockless,
				  &pxy_exp->rpc.listlock);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
	return pxy_exp->rpc.close_thread;
}

static inline int pxy_rpc_renewer_wait(int timeout, struct pxy_export *pxy_exp)
{
	struct timespec ts;
	int rc;

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	ts.tv_sec = time(NULL) + timeout;
	ts.tv_nsec = 0;

	rc = pthread_cond_timedwait(&pxy_exp->rpc.sockless,
				    &pxy_exp->rpc.listlock, &ts);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
	return (rc == ETIMEDOUT);
}

static int pxy_compoundv4_call(struct pxy_rpc_io_context *pcontext,
			       const struct user_cred *cred,
			       COMPOUND4args *args, COMPOUND4res *res,
			       struct pxy_export *pxy_exp)
{
	XDR x;
	struct rpc_msg rmsg;
	AUTH *au;
	enum clnt_stat rc;

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	rmsg.rm_xid = pxy_exp->rpc.rpc_xid++;
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
	rmsg.rm_direction = CALL;

	rmsg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	rmsg.cb_prog = pcontext->nfs_prog;
	rmsg.cb_vers = FSAL_PROXY_NFS_V4;
	rmsg.cb_proc = NFSPROC4_COMPOUND;

	if (cred) {
		au = authunix_ncreate(pxy_exp->rpc.pxy_hostname,
				      cred->caller_uid, cred->caller_gid,
				      cred->caller_glen, cred->caller_garray);
	} else {
		au = authunix_ncreate_default();
	}
	if (AUTH_FAILURE(au)) {
		char *err = rpc_sperror(&au->ah_error, "failed");

		LogDebug(COMPONENT_FSAL, "%s", err);
		gsh_free(err);
		AUTH_DESTROY(au);
		return RPC_AUTHERROR;
	}

	rmsg.cb_cred = au->ah_cred;
	rmsg.cb_verf = au->ah_verf;

	memset(&x, 0, sizeof(x));
	xdrmem_create(&x, pcontext->sendbuf + 4, pcontext->sendbuf_sz,
		      XDR_ENCODE);
	if (xdr_callmsg(&x, &rmsg) && xdr_COMPOUND4args(&x, args)) {
		u_int pos = xdr_getpos(&x);
		u_int recmark = ntohl(pos | (1U << 31));
		int first_try = 1;

		pcontext->rpc_xid = rmsg.rm_xid;

		memcpy(pcontext->sendbuf, &recmark, sizeof(recmark));
		pos += 4;

		do {
			int bc = 0;
			char *buf = pcontext->sendbuf;

			LogDebug(COMPONENT_FSAL, "%ssend XID %u with %d bytes",
				 (first_try ? "First attempt to " : "Re"),
				 rmsg.rm_xid, pos);
			PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
			while (bc < pos) {
				int wc = write(pxy_exp->rpc.rpc_sock, buf,
					       pos - bc);

				if (wc <= 0) {
					close(pxy_exp->rpc.rpc_sock);
					break;
				}
				bc += wc;
				buf += wc;
			}

			if (bc == pos) {
				if (first_try) {
					glist_add_tail(&pxy_exp->rpc.rpc_calls,
						       &pcontext->calls);
					first_try = 0;
				}
			} else {
				if (!first_try)
					glist_del(&pcontext->calls);
			}
			PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);

			if (bc == pos)
				rc = pxy_process_reply(pcontext, res);
			else
				rc = RPC_CANTSEND;
		} while (rc == RPC_TIMEDOUT);
	} else {
		rc = RPC_CANTENCODEARGS;
	}

	auth_destroy(au);
	return rc;
}

int pxy_compoundv4_execute(const char *caller, const struct user_cred *creds,
			   uint32_t cnt, nfs_argop4 *argoparray,
			   nfs_resop4 *resoparray, struct pxy_export *pxy_exp)
{
	enum clnt_stat rc;
	struct pxy_rpc_io_context *ctx;
	COMPOUND4args arg = {
		.minorversion = FSAL_PROXY_NFS_V4_MINOR,
		.argarray.argarray_val = argoparray,
		.argarray.argarray_len = cnt
	};
	COMPOUND4res res = {
		.resarray.resarray_val = resoparray,
		.resarray.resarray_len = cnt
	};

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.context_lock);
	while (glist_empty(&pxy_exp->rpc.free_contexts))
		pthread_cond_wait(&pxy_exp->rpc.need_context,
				  &pxy_exp->rpc.context_lock);
	ctx =
	    glist_first_entry(&pxy_exp->rpc.free_contexts,
			      struct pxy_rpc_io_context, calls);
	glist_del(&ctx->calls);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.context_lock);

	/* fill slotid and sequenceid */
	if (argoparray->argop == NFS4_OP_SEQUENCE) {
		SEQUENCE4args *opsequence =
					&argoparray->nfs_argop4_u.opsequence;

		/* set slotid */
		opsequence->sa_slotid = ctx->slotid;
		/* increment and set sequence id */
		opsequence->sa_sequenceid = ++ctx->seqid;
	}

	do {
		rc = pxy_compoundv4_call(ctx, creds, &arg, &res, pxy_exp);
		if (rc != RPC_SUCCESS)
			LogDebug(COMPONENT_FSAL, "%s failed with %d", caller,
				 rc);
		if (rc == RPC_CANTSEND)
			if (pxy_rpc_need_sock(pxy_exp))
				return -1;
	} while ((rc == RPC_CANTRECV && (ctx->ioresult == -EAGAIN))
		 || (rc == RPC_CANTSEND));

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.context_lock);
	pthread_cond_signal(&pxy_exp->rpc.need_context);
	glist_add(&pxy_exp->rpc.free_contexts, &ctx->calls);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.context_lock);

	if (rc == RPC_SUCCESS)
		return res.status;
	return rc;
}

static inline int pxy_nfsv4_call(const struct user_cred *creds, uint32_t cnt,
				 nfs_argop4 *args, nfs_resop4 *resp)
{
	struct pxy_export *pxy_exp = container_of(op_ctx->fsal_export,
						  struct pxy_export, exp);

	return pxy_compoundv4_execute(__func__, creds, cnt, args, resp,
				      pxy_exp);
}

static inline void pxy_get_clientid(struct pxy_export *pxy_exp, clientid4 *ret)
{
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.pxy_clientid_mutex);
	*ret = pxy_exp->rpc.pxy_clientid;
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.pxy_clientid_mutex);
}

static inline void pxy_get_client_sessionid(sessionid4 ret)
{
	struct pxy_export *pxy_exp = container_of(op_ctx->fsal_export,
						  struct pxy_export, exp);

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.pxy_clientid_mutex);
	while (pxy_exp->rpc.no_sessionid)
		pthread_cond_wait(&pxy_exp->rpc.cond_sessionid,
				  &pxy_exp->rpc.pxy_clientid_mutex);
	memcpy(ret, pxy_exp->rpc.pxy_client_sessionid, sizeof(sessionid4));
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.pxy_clientid_mutex);
}

static inline void pxy_get_client_sessionid_export(sessionid4 ret,
						   struct pxy_export *pxy_exp)
{
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.pxy_clientid_mutex);
	while (pxy_exp->rpc.no_sessionid)
		pthread_cond_wait(&pxy_exp->rpc.cond_sessionid,
				  &pxy_exp->rpc.pxy_clientid_mutex);
	memcpy(ret, pxy_exp->rpc.pxy_client_sessionid, sizeof(sessionid4));
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.pxy_clientid_mutex);
}

static inline void pxy_get_client_seqid(struct pxy_export *pxy_exp,
					sequenceid4 *ret)
{
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.pxy_clientid_mutex);
	*ret = pxy_exp->rpc.pxy_client_seqid;
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.pxy_clientid_mutex);
}

/**
 * Confirm pxy_clientid to set a new session.
 *
 * @param[out] new_sessionid The new session id
 * @param[out] new_lease_time Lease time from the background NFSv4.1 server
 *
 * @return 0 on success or NFS error code
 */
static int pxy_setsessionid(sessionid4 new_sessionid, uint32_t *lease_time,
			    struct pxy_export *pxy_exp)
{
	int rc;
	int opcnt = 0;
	/* CREATE_SESSION to set session id */
	/* SEQUENCE RECLAIM_COMPLETE PUTROOTFH GETATTR to get lease time */
#define FSAL_SESSIONID_NB_OP_ALLOC 4
	nfs_argop4 arg[FSAL_SESSIONID_NB_OP_ALLOC];
	nfs_resop4 res[FSAL_SESSIONID_NB_OP_ALLOC];
	clientid4 cid;
	sequenceid4 seqid;
	CREATE_SESSION4res *s_res;
	CREATE_SESSION4resok *res_ok;
	callback_sec_parms4 sec_parms4;
	uint32_t fore_ca_rdma_ird_val_sink;
	uint32_t back_ca_rdma_ird_val_sink;

	pxy_get_clientid(pxy_exp, &cid);
	pxy_get_client_seqid(pxy_exp, &seqid);
	LogDebug(COMPONENT_FSAL, "Getting new session id for client id %"PRIx64
				" with sequence id %"PRIx32, cid, seqid);
	s_res = &res->nfs_resop4_u.opcreate_session;
	res_ok = &s_res->CREATE_SESSION4res_u.csr_resok4;
	res_ok->csr_fore_chan_attrs.ca_rdma_ird.ca_rdma_ird_len = 0;
	res_ok->csr_fore_chan_attrs.ca_rdma_ird.ca_rdma_ird_val =
						&fore_ca_rdma_ird_val_sink;
	res_ok->csr_back_chan_attrs.ca_rdma_ird.ca_rdma_ird_len = 0;
	res_ok->csr_back_chan_attrs.ca_rdma_ird.ca_rdma_ird_val =
						&back_ca_rdma_ird_val_sink;

	COMPOUNDV4_ARG_ADD_OP_CREATE_SESSION(opcnt, arg, cid, seqid,
					     (&(pxy_exp->info)), &sec_parms4);
	rc = pxy_compoundv4_execute(__func__, NULL, opcnt, arg, res, pxy_exp);
	if (rc != NFS4_OK)
		return -1;

	/*get session_id in res*/
	if (s_res->csr_status != NFS4_OK)
		return -1;

	memcpy(new_sessionid,
	       res_ok->csr_sessionid,
	       sizeof(sessionid4));

	/* Get the lease time */
	opcnt = 0;
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, arg, new_sessionid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_GLOBAL_RECLAIM_COMPLETE(opcnt, arg);
	COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, arg);
	pxy_fill_getattr_reply(res + opcnt, (char *)lease_time,
			       sizeof(*lease_time));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, arg, lease_bits);

	rc = pxy_compoundv4_execute(__func__, NULL, opcnt, arg, res, pxy_exp);
	if (rc != NFS4_OK) {
		*lease_time = 60;
		LogDebug(COMPONENT_FSAL,
			 "Setting new lease_time to default %d", *lease_time);
	} else {
		*lease_time = ntohl(*lease_time);
		LogDebug(COMPONENT_FSAL,
			 "Getting new lease %d", *lease_time);
	}

	return 0;
}

static int pxy_setclientid(clientid4 *new_clientid, sequenceid4 *new_seqid,
			   struct pxy_export *pxy_exp)
{
	int rc;
#define FSAL_EXCHANGE_ID_NB_OP_ALLOC 1
	nfs_argop4 arg[FSAL_EXCHANGE_ID_NB_OP_ALLOC];
	nfs_resop4 res[FSAL_EXCHANGE_ID_NB_OP_ALLOC];
	client_owner4 clientid;
	char clientid_name[MAXNAMLEN + 1];
	uint64_t temp_verifier;
	EXCHANGE_ID4args opexchange_id;
	EXCHANGE_ID4res *ei_res;
	EXCHANGE_ID4resok *ei_resok;
	char so_major_id_val[NFS4_OPAQUE_LIMIT];
	char eir_server_scope_val[NFS4_OPAQUE_LIMIT];
	nfs_impl_id4 eir_server_impl_id_val;
	struct sockaddr_in sin;
	socklen_t slen = sizeof(sin);
	char addrbuf[sizeof("255.255.255.255")];

	LogEvent(COMPONENT_FSAL,
		 "Negotiating a new ClientId with the remote server");

	/* prepare input */
	if (getsockname(pxy_exp->rpc.rpc_sock, &sin, &slen))
		return -errno;

	snprintf(clientid_name, MAXNAMLEN, "%s(%d) - GANESHA NFSv4 Proxy",
		 inet_ntop(AF_INET, &sin.sin_addr, addrbuf, sizeof(addrbuf)),
		 getpid());
	clientid.co_ownerid.co_ownerid_len = strlen(clientid_name);
	clientid.co_ownerid.co_ownerid_val = clientid_name;

	/* copy to intermediate uint64_t to 0-fill or truncate as needed */
	temp_verifier = (uint64_t)nfs_ServerBootTime.tv_sec;
	BUILD_BUG_ON(sizeof(clientid.co_verifier) != sizeof(uint64_t));
	memcpy(&clientid.co_verifier, &temp_verifier, sizeof(uint64_t));


	arg[0].argop = NFS4_OP_EXCHANGE_ID;
	opexchange_id.eia_clientowner = clientid;
	opexchange_id.eia_flags = 0;
	opexchange_id.eia_state_protect.spa_how = SP4_NONE;
	opexchange_id.eia_client_impl_id.eia_client_impl_id_len = 0;
	opexchange_id.eia_client_impl_id.eia_client_impl_id_val = NULL;
	arg[0].nfs_argop4_u.opexchange_id = opexchange_id;

	/* prepare reply */
	ei_res = &res->nfs_resop4_u.opexchange_id;
	ei_resok = &ei_res->EXCHANGE_ID4res_u.eir_resok4;
	ei_resok->eir_server_owner.so_major_id.so_major_id_val =
							so_major_id_val;
	ei_resok->eir_server_scope.eir_server_scope_val = eir_server_scope_val;
	ei_resok->eir_server_impl_id.eir_server_impl_id_val =
						&eir_server_impl_id_val;

	rc = pxy_compoundv4_execute(__func__, NULL, 1, arg, res, pxy_exp);
	if (rc != NFS4_OK) {
		LogDebug(COMPONENT_FSAL,
			 "Compound setclientid res request returned %d",
			 rc);
		return -1;
	}

	/* Keep the confirmed client id and sequence id*/
	if (ei_res->eir_status != NFS4_OK) {
		LogDebug(COMPONENT_FSAL, "EXCHANGE_ID res status is %d",
			 ei_res->eir_status);
		return -1;
	}
	*new_clientid = ei_resok->eir_clientid;
	*new_seqid = ei_resok->eir_sequenceid;

	return 0;
}

static void *pxy_clientid_renewer(void *arg)
{
	struct pxy_export *pxy_exp = arg;
	int clientid_needed = 1;
	int sessionid_needed = 1;
	uint32_t lease_time = 60;

	SetNameFunction("pxy_clientid_renewer");

	while (!pxy_exp->rpc.close_thread) {
		clientid4 newcid = 0;
		sequenceid4 newseqid = 0;

		if (!sessionid_needed &&
				pxy_rpc_renewer_wait(lease_time - 5, pxy_exp)) {
			/* Simply renew the session id you've got */
			nfs_argop4 seq_arg;
			nfs_resop4 res;
			int opcnt = 0;
			int rc;
			sessionid4 sid;
			clientid4 cid;
			SEQUENCE4res *s_res;
			SEQUENCE4resok *s_resok;

			pxy_get_clientid(pxy_exp, &cid);
			pxy_get_client_sessionid_export(sid, pxy_exp);
			LogDebug(COMPONENT_FSAL,
				 "Try renew session id for client id %"PRIx64,
				 cid);
			COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, &seq_arg, sid,
						       NB_RPC_SLOT);
			s_res = &res.nfs_resop4_u.opsequence;
			s_resok = &s_res->SEQUENCE4res_u.sr_resok4;
			s_resok->sr_status_flags = 0;
			rc = pxy_compoundv4_execute(__func__, NULL, 1, &seq_arg,
						    &res, pxy_exp);
			if (rc == NFS4_OK && !s_resok->sr_status_flags) {
				LogDebug(COMPONENT_FSAL,
					 "New session id for client id %"PRIu64,
					 cid);
				continue;
			} else if (rc == NFS4_OK &&
				   s_resok->sr_status_flags) {
				LogEvent(COMPONENT_FSAL,
	"sr_status_flags received on renewing session with seqop : %"PRIu32,
					 s_resok->sr_status_flags);
				continue;
			} else if (rc != NFS4_OK) {
				LogEvent(COMPONENT_FSAL,
					 "Failed to renew session");
			}
		}

		/* early stop test */
		if (pxy_exp->rpc.close_thread)
			return NULL;

		/* We've either failed to renew or rpc socket has been
		 * reconnected and we need new clientid or sessionid. */
		if (pxy_rpc_need_sock(pxy_exp))
			/* early stop test */
			return NULL;

		/* We need a new session_id */
		if (!clientid_needed) {
			sessionid4 new_sessionid;

			LogDebug(COMPONENT_FSAL, "Need %d new session id",
				 sessionid_needed);
			sessionid_needed = pxy_setsessionid(new_sessionid,
							&lease_time, pxy_exp);
			if (!sessionid_needed) {
				PTHREAD_MUTEX_lock(
					&pxy_exp->rpc.pxy_clientid_mutex);
				/* Set new session id */
				memcpy(pxy_exp->rpc.pxy_client_sessionid,
				       new_sessionid, sizeof(sessionid4));
				pxy_exp->rpc.no_sessionid = false;
				pthread_cond_broadcast(
						&pxy_exp->rpc.cond_sessionid);
				/*
				 * We finish our create session request next
				 * one will use the next client sequence id.
				 */
				pxy_exp->rpc.pxy_client_seqid++;
				PTHREAD_MUTEX_unlock(
					&pxy_exp->rpc.pxy_clientid_mutex);
				continue;
			}
		}

		LogDebug(COMPONENT_FSAL, "Need %d new client id",
			 clientid_needed);
		clientid_needed = pxy_setclientid(&newcid, &newseqid, pxy_exp);
		if (!clientid_needed) {
			PTHREAD_MUTEX_lock(&pxy_exp->rpc.pxy_clientid_mutex);
			pxy_exp->rpc.pxy_clientid = newcid;
			pxy_exp->rpc.pxy_client_seqid = newseqid;
			PTHREAD_MUTEX_unlock(&pxy_exp->rpc.pxy_clientid_mutex);
		}
	}

	return NULL;
}

static void free_io_contexts(struct pxy_export *pxy_exp)
{
	struct glist_head *cur, *n;

	glist_for_each_safe(cur, n, &pxy_exp->rpc.free_contexts) {
		struct pxy_rpc_io_context *c =
		    container_of(cur, struct pxy_rpc_io_context, calls);

		glist_del(cur);
		gsh_free(c);
	}
}

int pxy_close_thread(struct pxy_export *pxy_exp)
{
	int rc;

	/* setting boolean to stop thread */
	pxy_exp->rpc.close_thread = true;

	/* waiting threads ends */
	/* pxy_clientid_renewer is usually waiting on sockless cond : wake up */
	/* pxy_rpc_recv is usually polling rpc_sock : wake up by closing it */
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	pthread_cond_broadcast(&pxy_exp->rpc.sockless);
	close(pxy_exp->rpc.rpc_sock);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
	rc = pthread_join(pxy_exp->rpc.pxy_renewer_thread, NULL);
	if (rc) {
		LogWarn(COMPONENT_FSAL,
			"Error on waiting the pxy_renewer_thread end : %d", rc);
		return rc;
	}

	rc = pthread_join(pxy_exp->rpc.pxy_recv_thread, NULL);
	if (rc) {
		LogWarn(COMPONENT_FSAL,
			"Error on waiting the pxy_recv_thread end : %d", rc);
		return rc;
	}

	return 0;
}

int pxy_init_rpc(struct pxy_export *pxy_exp)
{
	int rc;
	int i = NB_RPC_SLOT-1;

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	glist_init(&pxy_exp->rpc.rpc_calls);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);

	PTHREAD_MUTEX_lock(&pxy_exp->rpc.context_lock);
	glist_init(&pxy_exp->rpc.free_contexts);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.context_lock);

/**
 * @todo this lock is not really necessary so long as we can
 *       only do one export at a time.  This is a reminder that
 *       there is work to do to get this fnctn to truely be
 *       per export.
 */
	PTHREAD_MUTEX_lock(&pxy_exp->rpc.listlock);
	if (pxy_exp->rpc.rpc_xid == 0)
		pxy_exp->rpc.rpc_xid = getpid() ^ time(NULL);
	PTHREAD_MUTEX_unlock(&pxy_exp->rpc.listlock);
	if (gethostname(pxy_exp->rpc.pxy_hostname,
			sizeof(pxy_exp->rpc.pxy_hostname)))
		strncpy(pxy_exp->rpc.pxy_hostname, "NFS-GANESHA/Proxy",
			sizeof(pxy_exp->rpc.pxy_hostname));

	for (i = NB_RPC_SLOT-1; i >= 0; i--) {
		struct pxy_rpc_io_context *c =
		    gsh_malloc(sizeof(*c) + pxy_exp->info.srv_sendsize +
			       pxy_exp->info.srv_recvsize);
		PTHREAD_MUTEX_init(&c->iolock, NULL);
		PTHREAD_COND_init(&c->iowait, NULL);
		c->nfs_prog = pxy_exp->info.srv_prognum;
		c->sendbuf_sz = pxy_exp->info.srv_sendsize;
		c->recvbuf_sz = pxy_exp->info.srv_recvsize;
		c->sendbuf = (char *)(c + 1);
		c->recvbuf = c->sendbuf + c->sendbuf_sz;
		c->slotid = i;
		c->seqid = 0;
		c->iodone = false;

		PTHREAD_MUTEX_lock(&pxy_exp->rpc.context_lock);
		glist_add(&pxy_exp->rpc.free_contexts, &c->calls);
		PTHREAD_MUTEX_unlock(&pxy_exp->rpc.context_lock);
	}

	rc = pthread_create(&pxy_exp->rpc.pxy_recv_thread, NULL, pxy_rpc_recv,
			    (void *)pxy_exp);
	if (rc) {
		LogCrit(COMPONENT_FSAL,
			"Cannot create proxy rpc receiver thread - %s",
			strerror(rc));
		free_io_contexts(pxy_exp);
		return rc;
	}

	rc = pthread_create(&pxy_exp->rpc.pxy_renewer_thread, NULL,
			    pxy_clientid_renewer, (void *)pxy_exp);
	if (rc) {
		LogCrit(COMPONENT_FSAL,
			"Cannot create proxy clientid renewer thread - %s",
			strerror(rc));
		free_io_contexts(pxy_exp);
	}
	return rc;
}

static fsal_status_t pxy_make_object(struct fsal_export *export,
				     fattr4 *obj_attributes,
				     const nfs_fh4 *fh,
				     struct fsal_obj_handle **handle,
				     struct attrlist *attrs_out)
{
	struct pxy_obj_handle *pxy_hdl;

	pxy_hdl = pxy_alloc_handle(export, fh, obj_attributes,
				   attrs_out);
	if (pxy_hdl == NULL)
		return fsalstat(ERR_FSAL_FAULT, 0);
	*handle = &pxy_hdl->obj;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * cap maxread and maxwrite config values to background server values
 */
static void pxy_check_maxread_maxwrite(struct fsal_export *export, fattr4 *f4)
{
	fsal_dynamicfsinfo_t info;
	int rc;

	rc = nfs4_Fattr_To_fsinfo(&info, f4);
	if (rc != NFS4_OK) {
		LogWarn(COMPONENT_FSAL,
			"Unable to get maxread and maxwrite from background NFS server : %d",
			rc);
	} else {
		struct pxy_fsal_module *pm =
		    container_of(export->fsal, struct pxy_fsal_module, module);

		if (info.maxread != 0 &&
			pm->module.fs_info.maxread > info.maxread) {
			LogWarn(COMPONENT_FSAL,
				"Reduced maxread from %"PRIu64
				" to align with remote server %"PRIu64,
				pm->module.fs_info.maxread, info.maxread);
			pm->module.fs_info.maxread = info.maxread;
		}

		if (info.maxwrite != 0 &&
		 pm->module.fs_info.maxwrite > info.maxwrite) {
			LogWarn(COMPONENT_FSAL,
				"Reduced maxwrite from %"PRIu64
				" to align with remote server %"PRIu64,
				pm->module.fs_info.maxwrite, info.maxwrite);
			pm->module.fs_info.maxwrite = info.maxwrite;
		}
	}
}

/*
 * NULL parent pointer is only used by lookup_path when it starts
 * from the root handle and has its own export pointer, everybody
 * else is supposed to provide a real parent pointer and matching
 * export
 */
static fsal_status_t pxy_lookup_impl(struct fsal_obj_handle *parent,
				     struct fsal_export *export,
				     const struct user_cred *cred,
				     const char *path,
				     struct fsal_obj_handle **handle,
				     struct attrlist *attrs_out)
{
	int rc;
	uint32_t opcnt = 0;
	GETATTR4resok *atok;
	GETATTR4resok *atok_per_file_system_attr = NULL;
	GETFH4resok *fhok;
	sessionid4 sid;
	/* SEQUENCE PUTROOTFH/PUTFH LOOKUP GETFH GETATTR (GETATTR) */
#define FSAL_LOOKUP_NB_OP_ALLOC 6
	nfs_argop4 argoparray[FSAL_LOOKUP_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_LOOKUP_NB_OP_ALLOC];
	char fattr_blob[FATTR_BLOB_SZ];
	char fattr_blob_per_file_system_attr[FATTR_BLOB_SZ];
	char padfilehandle[NFS4_FHSIZE];

	if (!handle)
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	if (!parent) {
		COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, argoparray);
	} else {
		struct pxy_obj_handle *pxy_obj =
		    container_of(parent, struct pxy_obj_handle, obj);
		switch (parent->type) {
		case DIRECTORY:
			break;

		default:
			return fsalstat(ERR_FSAL_NOTDIR, 0);
		}

		COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, pxy_obj->fh4);
	}

	if (path) {
		if (!strcmp(path, ".")) {
			if (!parent)
				return fsalstat(ERR_FSAL_FAULT, 0);
		} else if (!strcmp(path, "..")) {
			if (!parent)
				return fsalstat(ERR_FSAL_FAULT, 0);
			COMPOUNDV4_ARG_ADD_OP_LOOKUPP(opcnt, argoparray);
		} else {
			COMPOUNDV4_ARG_ADD_OP_LOOKUP(opcnt, argoparray, path);
		}
	}

	fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
	COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));

	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	/* Dynamic ask of server per file system attr */
	if (!parent) {
		atok_per_file_system_attr =
		    pxy_fill_getattr_reply(resoparray + opcnt,
				fattr_blob_per_file_system_attr,
				sizeof(fattr_blob_per_file_system_attr));
		COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray,
					      pxy_bitmap_per_file_system_attr);
	}
	fhok->object.nfs_fh4_val = (char *)padfilehandle;
	fhok->object.nfs_fh4_len = sizeof(padfilehandle);

	rc = pxy_nfsv4_call(cred, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	/* Dynamic check of server per file system attr */
	if (!parent) {
		/* maxread and maxwrite */
		pxy_check_maxread_maxwrite(export,
				&atok_per_file_system_attr->obj_attributes);
	}

	return pxy_make_object(export, &atok->obj_attributes, &fhok->object,
			       handle, attrs_out);
}

static fsal_status_t pxy_lookup(struct fsal_obj_handle *parent,
				const char *path,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out)
{
	return pxy_lookup_impl(parent, op_ctx->fsal_export,
			       op_ctx->creds, path, handle, attrs_out);
}

/* TODO: make this per-export */
static uint64_t fcnt;

static fsal_status_t pxy_mkdir(struct fsal_obj_handle *dir_hdl,
			       const char *name, struct attrlist *attrib,
			       struct fsal_obj_handle **handle,
			       struct attrlist *attrs_out)
{
	int rc;
	int opcnt = 0;
	fattr4 input_attr;
	char padfilehandle[NFS4_FHSIZE];
	struct pxy_obj_handle *ph;
	char fattr_blob[FATTR_BLOB_SZ];
	GETATTR4resok *atok;
	GETFH4resok *fhok;
	fsal_status_t st;
	sessionid4 sid;
#define FSAL_MKDIR_NB_OP_ALLOC 5 /* SEQUENCE PUTFH CREATE GETFH GETATTR */
	nfs_argop4 argoparray[FSAL_MKDIR_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_MKDIR_NB_OP_ALLOC];

	/*
	 * The caller gives us partial attributes which include mode and owner
	 * and expects the full attributes back at the end of the call.
	 */
	attrib->valid_mask &= ATTR_MODE | ATTR_OWNER | ATTR_GROUP;
	if (pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
		return fsalstat(ERR_FSAL_INVAL, -1);

	ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset =
	    empty_bitmap;
	COMPOUNDV4_ARG_ADD_OP_MKDIR(opcnt, argoparray, (char *)name,
				    input_attr);

	fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
	fhok->object.nfs_fh4_val = padfilehandle;
	fhok->object.nfs_fh4_len = sizeof(padfilehandle);
	COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	nfs4_Fattr_Free(&input_attr);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	st = pxy_make_object(op_ctx->fsal_export, &atok->obj_attributes,
			     &fhok->object, handle, attrs_out);
	if (FSAL_IS_ERROR(st))
		return st;

	return (*handle)->obj_ops->getattrs(*handle, attrib);
}

static fsal_status_t pxy_mknod(struct fsal_obj_handle *dir_hdl,
			       const char *name, object_file_type_t nodetype,
			       struct attrlist *attrib,
			       struct fsal_obj_handle **handle,
			       struct attrlist *attrs_out)
{
	int rc;
	int opcnt = 0;
	fattr4 input_attr;
	char padfilehandle[NFS4_FHSIZE];
	struct pxy_obj_handle *ph;
	char fattr_blob[FATTR_BLOB_SZ];
	GETATTR4resok *atok;
	GETFH4resok *fhok;
	fsal_status_t st;
	enum nfs_ftype4 nf4type;
	specdata4 specdata = { 0, 0 };
	sessionid4 sid;
#define FSAL_MKNOD_NB_OP_ALLOC 5 /* SEQUENCE PUTFH CREATE GETFH GETATTR */
	nfs_argop4 argoparray[FSAL_MKNOD_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_MKNOD_NB_OP_ALLOC];

	switch (nodetype) {
	case CHARACTER_FILE:
		specdata.specdata1 = attrib->rawdev.major;
		specdata.specdata2 = attrib->rawdev.minor;
		nf4type = NF4CHR;
		break;
	case BLOCK_FILE:
		specdata.specdata1 = attrib->rawdev.major;
		specdata.specdata2 = attrib->rawdev.minor;
		nf4type = NF4BLK;
		break;
	case SOCKET_FILE:
		nf4type = NF4SOCK;
		break;
	case FIFO_FILE:
		nf4type = NF4FIFO;
		break;
	default:
		return fsalstat(ERR_FSAL_FAULT, EINVAL);
	}

	/*
	 * The caller gives us partial attributes which include mode and owner
	 * and expects the full attributes back at the end of the call.
	 */
	attrib->valid_mask &= ATTR_MODE | ATTR_OWNER | ATTR_GROUP;
	if (pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
		return fsalstat(ERR_FSAL_INVAL, -1);

	ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset =
	    empty_bitmap;
	COMPOUNDV4_ARG_ADD_OP_CREATE(opcnt, argoparray, (char *)name, nf4type,
				     input_attr, specdata);

	fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
	fhok->object.nfs_fh4_val = padfilehandle;
	fhok->object.nfs_fh4_len = sizeof(padfilehandle);
	COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	nfs4_Fattr_Free(&input_attr);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	st = pxy_make_object(op_ctx->fsal_export, &atok->obj_attributes,
			     &fhok->object, handle, attrs_out);
	if (FSAL_IS_ERROR(st))
		return st;

	return (*handle)->obj_ops->getattrs(*handle, attrib);
}

static fsal_status_t pxy_symlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	int rc;
	int opcnt = 0;
	fattr4 input_attr;
	char padfilehandle[NFS4_FHSIZE];
	char fattr_blob[FATTR_BLOB_SZ];
	sessionid4 sid;
#define FSAL_SYMLINK_NB_OP_ALLOC 5 /* SEQUENCE PUTFH CREATE GETFH GETATTR */
	nfs_argop4 argoparray[FSAL_SYMLINK_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_SYMLINK_NB_OP_ALLOC];
	GETATTR4resok *atok;
	GETFH4resok *fhok;
	fsal_status_t st;
	struct pxy_obj_handle *ph;

	/* Tests if symlinking is allowed by configuration. */
	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						  fso_symlink_support))
		return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);

	attrib->valid_mask = ATTR_MODE;
	if (pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
		return fsalstat(ERR_FSAL_INVAL, -1);

	ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset =
	    empty_bitmap;
	COMPOUNDV4_ARG_ADD_OP_SYMLINK(opcnt, argoparray, (char *)name,
				      (char *)link_path, input_attr);

	fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
	fhok->object.nfs_fh4_val = padfilehandle;
	fhok->object.nfs_fh4_len = sizeof(padfilehandle);
	COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	nfs4_Fattr_Free(&input_attr);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	st = pxy_make_object(op_ctx->fsal_export, &atok->obj_attributes,
			     &fhok->object, handle, attrs_out);
	if (FSAL_IS_ERROR(st))
		return st;

	return (*handle)->obj_ops->getattrs(*handle, attrib);
}

static fsal_status_t pxy_readlink(struct fsal_obj_handle *obj_hdl,
				  struct gsh_buffdesc *link_content,
				  bool refresh)
{
	int rc;
	int opcnt = 0;
	struct pxy_obj_handle *ph;
	sessionid4 sid;
#define FSAL_READLINK_NB_OP_ALLOC 3 /* SEQUENCE PUTFH READLINK */
	nfs_argop4 argoparray[FSAL_READLINK_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_READLINK_NB_OP_ALLOC];
	READLINK4resok *rlok;

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	/* This saves us from having to do one allocation for the XDR,
	   another allocation for the return, and a copy just to get
	   the \NUL terminator. The link length should be cached in
	   the file handle. */

	link_content->len = fsal_default_linksize;
	link_content->addr = gsh_malloc(link_content->len);

	rlok = &resoparray[opcnt].nfs_resop4_u.opreadlink.READLINK4res_u.resok4;
	rlok->link.utf8string_val = link_content->addr;
	rlok->link.utf8string_len = link_content->len;
	COMPOUNDV4_ARG_ADD_OP_READLINK(opcnt, argoparray);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK) {
		gsh_free(link_content->addr);
		link_content->addr = NULL;
		link_content->len = 0;
		return nfsstat4_to_fsal(rc);
	}

	rlok->link.utf8string_val[rlok->link.utf8string_len] = '\0';
	link_content->len = rlok->link.utf8string_len + 1;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_link(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	int rc;
	struct pxy_obj_handle *tgt;
	struct pxy_obj_handle *dst;
	sessionid4 sid;
#define FSAL_LINK_NB_OP_ALLOC 5 /* SEQUENCE PUTFH SAVEFH PUTFH LINK */
	nfs_argop4 argoparray[FSAL_LINK_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_LINK_NB_OP_ALLOC];
	int opcnt = 0;

	/* Tests if hardlinking is allowed by configuration. */
	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						  fso_link_support))
		return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);

	tgt = container_of(obj_hdl, struct pxy_obj_handle, obj);
	dst = container_of(destdir_hdl, struct pxy_obj_handle, obj);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, tgt->fh4);
	COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argoparray);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, dst->fh4);
	COMPOUNDV4_ARG_ADD_OP_LINK(opcnt, argoparray, (char *)name);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	return nfsstat4_to_fsal(rc);
}

#define FSAL_READDIR_NB_OP_ALLOC 3 /* SEQUENCE PUTFH READDIR */
static bool xdr_readdirres(XDR *x, nfs_resop4 *rdres)
{
	int i;
	int res = true;

	for (i = 0; i < FSAL_READDIR_NB_OP_ALLOC; i++) {
		res  = xdr_nfs_resop4(x, rdres + i);
		if (res != true)
			return res;
	}

	return res;
}

/*
 * Trying to guess how many entries can fit into a readdir buffer
 * is complicated and usually results in either gross over-allocation
 * of the memory for results or under-allocation (on large directories)
 * and buffer overruns - just pay the price of allocating the memory
 * inside XDR decoding and free it when done
 */
static fsal_status_t pxy_do_readdir(struct pxy_obj_handle *ph,
				    nfs_cookie4 *cookie, fsal_readdir_cb cb,
				    void *cbarg, attrmask_t attrmask,
				    bool *eof, bool *again)
{
	uint32_t opcnt = 0;
	int rc;
	entry4 *e4;
	sessionid4 sid;
	nfs_argop4 argoparray[FSAL_READDIR_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_READDIR_NB_OP_ALLOC];
	READDIR4resok *rdok;
	fsal_status_t st = { ERR_FSAL_NO_ERROR, 0 };

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	/* PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	rdok = &resoparray[opcnt].nfs_resop4_u.opreaddir.READDIR4res_u.resok4;
	rdok->reply.entries = NULL;
	/* READDIR */
	COMPOUNDV4_ARG_ADD_OP_READDIR(opcnt, argoparray, *cookie,
				      pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	*eof = rdok->reply.eof;

	for (e4 = rdok->reply.entries; e4; e4 = e4->nextentry) {
		struct attrlist attrs;
		char name[MAXNAMLEN + 1];
		struct fsal_obj_handle *handle;
		enum fsal_dir_result cb_rc;

		/* UTF8 name does not include trailing 0 */
		if (e4->name.utf8string_len > sizeof(name) - 1)
			return fsalstat(ERR_FSAL_SERVERFAULT, E2BIG);
		memcpy(name, e4->name.utf8string_val, e4->name.utf8string_len);
		name[e4->name.utf8string_len] = '\0';

		if (nfs4_Fattr_To_FSAL_attr(&attrs, &e4->attrs, NULL))
			return fsalstat(ERR_FSAL_FAULT, 0);

		/*
		 *  If *again==false : we are in readahead,
		 *  we only call cb for next entries but don't update result
		 *  for calling readdir.
		 */
		if (*again) {
			*cookie = e4->cookie;
			*eof = rdok->reply.eof && !e4->nextentry;
		}

		/** @todo FSF: this could be handled by getting handle as part
		 *             of readdir attributes. However, if acl is
		 *             requested, we might get it separately to
		 *             avoid over large READDIR response.
		 */
		st = pxy_lookup_impl(&ph->obj, op_ctx->fsal_export,
				     op_ctx->creds, name, &handle, NULL);
		if (FSAL_IS_ERROR(st))
			break;

		cb_rc = cb(name, handle, &attrs, cbarg, e4->cookie);

		fsal_release_attrs(&attrs);

		if (cb_rc >= DIR_TERMINATE) {
			*again = false;
			break;
		}
		/*
		 * Read ahead is supported by this FSAL
		 * but limited to the current background readdir request.
		 */
		if (cb_rc >= DIR_READAHEAD && *again) {
			*again = false;
		}
	}
	xdr_free((xdrproc_t) xdr_readdirres, resoparray);
	return st;
}

/**
 *  @todo We might add a verifier to the cookie provided
 *        if server needs one ...
 */
static fsal_status_t pxy_readdir(struct fsal_obj_handle *dir_hdl,
				 fsal_cookie_t *whence, void *cbarg,
				 fsal_readdir_cb cb, attrmask_t attrmask,
				 bool *eof)
{
	nfs_cookie4 cookie = 0;
	struct pxy_obj_handle *ph;
	bool again = true;

	if (whence)
		cookie = (nfs_cookie4) *whence;

	ph = container_of(dir_hdl, struct pxy_obj_handle, obj);

	do {
		fsal_status_t st;

		st = pxy_do_readdir(ph, &cookie, cb, cbarg, attrmask, eof,
				    &again);
		if (FSAL_IS_ERROR(st))
			return st;
	} while (*eof == false && again);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_rename(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	int rc;
	int opcnt = 0;
	sessionid4 sid;
#define FSAL_RENAME_NB_OP_ALLOC 5 /* SEQUENCE PUTFH SAVEFH PUTFH RENAME */
	nfs_argop4 argoparray[FSAL_RENAME_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_RENAME_NB_OP_ALLOC];
	struct pxy_obj_handle *src;
	struct pxy_obj_handle *tgt;

	src = container_of(olddir_hdl, struct pxy_obj_handle, obj);
	tgt = container_of(newdir_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, src->fh4);
	COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argoparray);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, tgt->fh4);
	COMPOUNDV4_ARG_ADD_OP_RENAME(opcnt, argoparray, (char *)old_name,
				     (char *)new_name);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	return nfsstat4_to_fsal(rc);
}

static inline int nfs4_Fattr_To_FSAL_attr_savreqmask(struct attrlist *FSAL_attr,
						     fattr4 *Fattr,
						     compound_data_t *data)
{
	int rc = 0;
	attrmask_t saved_request_mask = FSAL_attr->request_mask;

	rc = nfs4_Fattr_To_FSAL_attr(FSAL_attr, Fattr, data);
	FSAL_attr->request_mask = saved_request_mask;
	return rc;
}

static fsal_status_t pxy_getattrs(struct fsal_obj_handle *obj_hdl,
				  struct attrlist *attrs)
{
	struct pxy_obj_handle *ph;
	int rc;
	uint32_t opcnt = 0;
	sessionid4 sid;
#define FSAL_GETATTR_NB_OP_ALLOC 3 /* SEQUENCE PUTFH GETATTR */
	nfs_argop4 argoparray[FSAL_GETATTR_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_GETATTR_NB_OP_ALLOC];
	GETATTR4resok *atok;
	char fattr_blob[FATTR_BLOB_SZ];

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	atok = pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				      sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);

	if (rc != NFS4_OK) {
		if (attrs->request_mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			attrs->valid_mask = ATTR_RDATTR_ERR;
		}
		return nfsstat4_to_fsal(rc);
	}

	if (nfs4_Fattr_To_FSAL_attr_savreqmask(attrs, &atok->obj_attributes,
					       NULL) != NFS4_OK)
		return fsalstat(ERR_FSAL_INVAL, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_unlink(struct fsal_obj_handle *dir_hdl,
				struct fsal_obj_handle *obj_hdl,
				const char *name)
{
	int opcnt = 0;
	int rc;
	struct pxy_obj_handle *ph;
	sessionid4 sid;
#define FSAL_UNLINK_NB_OP_ALLOC 4 /* SEQUENCE PUTFH REMOVE GETATTR */
	nfs_argop4 argoparray[FSAL_UNLINK_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_UNLINK_NB_OP_ALLOC];
#if GETATTR_AFTER
	GETATTR4resok *atok;
	char fattr_blob[FATTR_BLOB_SZ];
	struct attrlist dirattr;
#endif

	ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	COMPOUNDV4_ARG_ADD_OP_REMOVE(opcnt, argoparray, (char *)name);

#if GETATTR_AFTER
	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);
#endif

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

#if GETATTR_AFTER
	if (nfs4_Fattr_To_FSAL_attr(&dirattr, &atok->obj_attributes, NULL) ==
	    NFS4_OK)
		ph->attributes = dirattr;
#endif

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_handle_to_wire(const struct fsal_obj_handle *obj_hdl,
					fsal_digesttype_t output_type,
					struct gsh_buffdesc *fh_desc)
{
	struct pxy_obj_handle *ph =
	    container_of(obj_hdl, struct pxy_obj_handle, obj);
	size_t fhs;
	void *data;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
#ifdef PROXY_HANDLE_MAPPING
		fhs = sizeof(ph->h23);
		data = &ph->h23;
		break;
#endif
	case FSAL_DIGEST_NFSV4:
		fhs = ph->blob.len;
		data = &ph->blob;
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (fh_desc->len < fhs)
		return fsalstat(ERR_FSAL_TOOSMALL, 0);
	memcpy(fh_desc->addr, data, fhs);
	fh_desc->len = fhs;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static void pxy_handle_to_key(struct fsal_obj_handle *obj_hdl,
			      struct gsh_buffdesc *fh_desc)
{
	struct pxy_obj_handle *ph =
	    container_of(obj_hdl, struct pxy_obj_handle, obj);
	fh_desc->addr = &ph->blob;
	fh_desc->len = ph->blob.len;
}

static void pxy_hdl_release(struct fsal_obj_handle *obj_hdl)
{
	struct pxy_obj_handle *ph =
	    container_of(obj_hdl, struct pxy_obj_handle, obj);

	fsal_obj_handle_fini(obj_hdl);

	gsh_free(ph);
}

/*
 * In this first FSAL_PROXY support_ex version without state
 * nothing to do on close.
 */
static fsal_status_t pxy_close(struct fsal_obj_handle *obj_hdl)
{
	return fsalstat(ERR_FSAL_NOT_OPENED, 0);
}

/*
 * support_ex methods
 *
 * This first dirty version of support_ex in FSAL_PROXY doesn't take care of
 * any state.
 *
 * The goal achieves by this first dirty version is only to be compliant with
 * support_ex fsal api.
 */

/**
 * @brief Fill share_access and share_deny fields of an OPEN4args struct
 *
 * This function fills share_access and share_deny fields of an OPEN4args
 * struct to prepare an OPEN v4.1 call.
 *
 * @param[in]     openflags      fsal open flags
 * @param[in,out] share_access   share_access field to be filled
 * @param[in,out] share_deny     share_deny field to be filled
 *
 * @return FSAL status.
 */
static fsal_status_t  fill_share_OPEN4args(uint32_t *share_access,
					   uint32_t *share_deny,
					   fsal_openflags_t openflags)
{

	/* share access */
	*share_access = 0;
	if (openflags & FSAL_O_READ)
		*share_access |= OPEN4_SHARE_ACCESS_READ;
	if (openflags & FSAL_O_WRITE)
		*share_access |= OPEN4_SHARE_ACCESS_WRITE;
	/* share write */
	*share_deny = OPEN4_SHARE_DENY_NONE;
	if (openflags & FSAL_O_DENY_READ)
		*share_deny |= OPEN4_SHARE_DENY_READ;
	if (openflags & FSAL_O_DENY_WRITE ||
	    openflags & FSAL_O_DENY_WRITE_MAND)
		*share_deny |= OPEN4_SHARE_DENY_WRITE;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Fill openhow field of an OPEN4args struct
 *
 * This function fills an openflags4 openhow field of an OPEN4args struct
 * to prepare an OPEN v4.1 call.
 *
 * @param[in]     attrs_in       open atributes
 * @param[in]     create_mode    open create mode
 * @param[in]     verifier       open verifier
 * @param[in,out] openhow        openhow field to be filled
 * @param[in,out] inattrs        created inattrs (need to be freed by calling
 *                               nfs4_Fattr_Free)
 * @param[out]    setattr_needed an additional setattr op is needed
 *
 * @return FSAL status.
 */
static fsal_status_t fill_openhow_OPEN4args(openflag4 *openhow,
					    fattr4 inattrs,
					    enum fsal_create_mode createmode,
					    fsal_verifier_t verifier,
					    bool *setattr_needed,
					    const char *name,
					    fsal_openflags_t openflags)
{
	if (openhow == NULL)
		return fsalstat(ERR_FSAL_INVAL, -1);

	/* openhow */
	/* not an open by handle and flag create */
	if (name && createmode != FSAL_NO_CREATE) {
		createhow4 *how = &(openhow->openflag4_u.how);

		openhow->opentype = OPEN4_CREATE;
		switch (createmode) {
		case FSAL_UNCHECKED:
			how->mode = UNCHECKED4;
			how->createhow4_u.createattrs = inattrs;
			break;
		case FSAL_GUARDED:
		case FSAL_EXCLUSIVE_9P:
			how->mode = GUARDED4;
			how->createhow4_u.createattrs = inattrs;
			break;
		case FSAL_EXCLUSIVE:
			how->mode = EXCLUSIVE4;
			FSAL_VERIFIER_T_TO_VERIFIER4(
				how->createhow4_u.createverf, verifier);
			/* no way to set attr in same op than old EXCLUSIVE4 */
			if (inattrs.attrmask.bitmap4_len > 0) {
				int i = 0;

				for (i = 0; i < inattrs.attrmask.bitmap4_len;
				     i++) {
					if (inattrs.attrmask.map[i]) {
						*setattr_needed = true;
						break;
					}
				}
			}
			break;
		case FSAL_EXCLUSIVE_41:
			how->mode = EXCLUSIVE4_1;
			FSAL_VERIFIER_T_TO_VERIFIER4(
				how->createhow4_u.ch_createboth.cva_verf,
				verifier);
			how->createhow4_u.ch_createboth.cva_attrs = inattrs;
		/*
		 * We assume verifier is stored in time metadata.
		 *
		 * We had better check suppattr_exclcreat from background
		 * server.
		 */
			if (inattrs.attrmask.bitmap4_len >= 2 &&
	inattrs.attrmask.map[1] & (1U << (FATTR4_TIME_METADATA - 32))) {
				*setattr_needed = true;
		how->createhow4_u.ch_createboth.cva_attrs.attrmask.map[1] &=
					~(1U << (FATTR4_TIME_METADATA - 32));
			}
			break;
		default:
			return fsalstat(ERR_FSAL_FAULT,
					EINVAL);
		}
	} else
		openhow->opentype = OPEN4_NOCREATE;

	/* include open by handle and TRUNCATE case in setattr_needed */
	if (!name && openflags & FSAL_O_TRUNC)
		*setattr_needed = true;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_open2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state,
			       fsal_openflags_t openflags,
			       enum fsal_create_mode createmode,
			       const char *name,
			       struct attrlist *attrs_in,
			       fsal_verifier_t verifier,
			       struct fsal_obj_handle **new_obj,
			       struct attrlist *attrs_out,
			       bool *caller_perm_check)
{
	struct pxy_obj_handle *ph;
	int rc; /* return code of nfs call */
	int opcnt = 0; /* nfs arg counter */
	fsal_status_t st; /* return code of fsal call */
	char padfilehandle[NFS4_FHSIZE]; /* gotten FH */
	char owner_val[128];
	unsigned int owner_len = 0;
	uint32_t share_access = 0;
	uint32_t share_deny = 0;
	openflag4 openhow;
	fattr4 inattrs;
	open_claim4 claim;
	sessionid4 sid;
	/* SEQUENCE, PUTFH, OPEN, GETFH, GETATTR */
	#define FSAL_OPEN_NB_OP 5
	nfs_argop4 argoparray[FSAL_OPEN_NB_OP];
	nfs_resop4 resoparray[FSAL_OPEN_NB_OP];
	/* SEQUENCE, PUTFH, SETATTR, GETATTR */
	#define FSAL_OPEN_SETATTR_NB_OP 4
	nfs_argop4 setattr_argoparray[FSAL_OPEN_SETATTR_NB_OP];
	nfs_resop4 setattr_resoparray[FSAL_OPEN_SETATTR_NB_OP];
	OPEN4resok *opok;
	GETFH4resok *fhok;
	GETATTR4resok *atok;
	char fattr_blob[FATTR_BLOB_SZ];
	bool setattr_needed = false;

	/* we have not done yet any check */
	*caller_perm_check = true;

	/* get back proxy handle */
	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* include TRUNCATE case in attrs_in */
	if (openflags & FSAL_O_TRUNC) {
		attrs_in->valid_mask |= ATTR_SIZE;
		attrs_in->filesize = 0;
	}

	/* fill inattrs */
	if (pxy_fsalattr_to_fattr4(attrs_in, &inattrs) == -1)
		return fsalstat(ERR_FSAL_INVAL, -1);

	/* Three cases need to do an open :
	 * -open by name to get an handle
	 * -open by handle to get attrs_out
	 * -open by handle to truncate
	 */
	if (name || attrs_out || openflags & FSAL_O_TRUNC) {
		/*
		* We do the open to get handle, check perm, check share, trunc,
		* create if needed ...
		*/
		/* open call will do the perm check */
		*caller_perm_check = false;

		/* prepare open call */
		/* SEQUENCE */
		pxy_get_client_sessionid(sid);
		COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid,
					       NB_RPC_SLOT);

		/* PUTFH */
		COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

		/* OPEN */
		/* prepare answer */
		opok =
		    &resoparray[opcnt].nfs_resop4_u.opopen.OPEN4res_u.resok4;
		opok->rflags = 0; /* set to NULL for safety */
		opok->attrset = empty_bitmap; /* set to empty for safety */
		/* prepare open input args */
		/* share_access and share_deny */
		st = fill_share_OPEN4args(&share_access, &share_deny,
					  openflags);
		if (FSAL_IS_ERROR(st)) {
			nfs4_Fattr_Free(&inattrs);
			return st;
		}

		/* owner */
		snprintf(owner_val, sizeof(owner_val),
			 "GANESHA/PROXY: pid=%u %" PRIu64, getpid(),
			 atomic_inc_uint64_t(&fcnt));
		owner_len = strnlen(owner_val, sizeof(owner_val));
		/* inattrs and openhow */
		st = fill_openhow_OPEN4args(&openhow, inattrs, createmode,
					    verifier, &setattr_needed, name,
					    openflags);
		if (FSAL_IS_ERROR(st)) {
			nfs4_Fattr_Free(&inattrs);
			return st;
		}

		/* claim : first support_ex version, no state -> no claim */
		if (name) {
		/* open by name */
			claim.claim = CLAIM_NULL;
			claim.open_claim4_u.file.utf8string_val = (char *)name;
			claim.open_claim4_u.file.utf8string_len = strlen(name);
		} else {
		/* open by handle */
			claim.claim = CLAIM_FH;
		}
		/* add open */
		COMPOUNDV4_ARGS_ADD_OP_OPEN_4_1(opcnt, argoparray, share_access,
						share_deny, owner_val,
						owner_len, openhow, claim);

		/* GETFH */
		/* prepare answer */
		fhok =
		    &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
		fhok->object.nfs_fh4_val = padfilehandle;
		fhok->object.nfs_fh4_len = sizeof(padfilehandle);
		COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);
		if (!setattr_needed && (new_obj || attrs_out)) {
			/* GETATTR */
			atok = pxy_fill_getattr_reply(resoparray + opcnt,
						      fattr_blob,
						      sizeof(fattr_blob));
			COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray,
						      pxy_bitmap_getattr);
		}
		/* nfs call*/
		rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray,
				    resoparray);
		if (rc != NFS4_OK) {
			nfs4_Fattr_Free(&inattrs);
			return nfsstat4_to_fsal(rc);
		}


		/* update stateid in current state */
		if (state) {
			struct pxy_state *pxy_state_id = container_of(state,
						struct pxy_state, state);

			pxy_state_id->stateid = opok->stateid;
		}
	}

	if (setattr_needed) {
		opcnt = 0;
		/* SEQUENCE */
		pxy_get_client_sessionid(sid);
		COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, setattr_argoparray, sid,
					       NB_RPC_SLOT);
		/* PUTFH */
		COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, setattr_argoparray,
					    fhok->object);
		/* SETATTR for truncate */
		setattr_resoparray[opcnt].nfs_resop4_u.opsetattr.attrsset =
								empty_bitmap;
		/* We have a stateid */
		/* cause we did an open when we set setattr_needed. */
		COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, setattr_argoparray,
					      inattrs, opok->stateid.other);

		if (new_obj || attrs_out) {
			/* GETATTR */
			atok = pxy_fill_getattr_reply(
						setattr_resoparray + opcnt,
						fattr_blob,
						sizeof(fattr_blob));
			COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, setattr_argoparray,
						      pxy_bitmap_getattr);
		}

		/* nfs call*/
		rc = pxy_nfsv4_call(op_ctx->creds, opcnt, setattr_argoparray,
				    setattr_resoparray);
		if (rc != NFS4_OK) {
			nfs4_Fattr_Free(&inattrs);
			return nfsstat4_to_fsal(rc);
		}
	}

	/* clean inattrs */
	nfs4_Fattr_Free(&inattrs);

	/* create a new object if asked and attrs_out by the way */
	if (new_obj) {
		if (name) {
			/* create new_obj and set attrs_out*/
			st = pxy_make_object(op_ctx->fsal_export,
					     &atok->obj_attributes,
					     &fhok->object,
					     new_obj, attrs_out);
			if (FSAL_IS_ERROR(st))
				return st;
		} else {
			*new_obj = obj_hdl;
		}
	}

	/* set attrs_out if needed and not yet done by creating new object */
	if (attrs_out && (!new_obj || (new_obj && !name))) {
		rc = nfs4_Fattr_To_FSAL_attr(attrs_out, &atok->obj_attributes,
					     NULL);
		if (rc != NFS4_OK)
			return nfsstat4_to_fsal(rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* XXX Note that this only currently supports a vector size of 1 */
static void pxy_read2(struct fsal_obj_handle *obj_hdl,
		      bool bypass,
		      fsal_async_cb done_cb,
		      struct fsal_io_arg *read_arg,
		      void *caller_arg)
{
	int maxReadSize;
	int rc;
	int opcnt = 0;
	struct pxy_obj_handle *ph;
	sessionid4 sid;
#define FSAL_READ2_NB_OP_ALLOC 3 /* SEQUENCE + PUTFH + READ */
	nfs_argop4 argoparray[FSAL_READ2_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_READ2_NB_OP_ALLOC];
	READ4resok *rok;

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	maxReadSize = op_ctx->fsal_export->exp_ops.fs_maxread(
							op_ctx->fsal_export);
	if (read_arg->iov[0].iov_len > maxReadSize)
		read_arg->iov[0].iov_len = maxReadSize;

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	/* prepare PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	/* prepare READ */
	rok = &resoparray[opcnt].nfs_resop4_u.opread.READ4res_u.resok4;
	rok->data.data_val = read_arg->iov[0].iov_base;
	rok->data.data_len = read_arg->iov[0].iov_len;
	if (bypass)
		COMPOUNDV4_ARG_ADD_OP_READ_BYPASS(opcnt, argoparray,
						  read_arg->offset,
						  read_arg->iov[0].iov_len);
	else {
		if (read_arg->state) {
			struct pxy_state *pxy_state_id = container_of(
						read_arg->state,
						struct pxy_state, state);

			COMPOUNDV4_ARG_ADD_OP_READ(opcnt, argoparray,
						   read_arg->offset,
						   read_arg->iov[0].iov_len,
						   pxy_state_id->stateid.other);

		} else {
			COMPOUNDV4_ARG_ADD_OP_READ_STATELESS(opcnt, argoparray,
					     read_arg->offset,
					     read_arg->iov[0].iov_len);
		}
	}

	/* nfs call */
	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK) {
		done_cb(obj_hdl, nfsstat4_to_fsal(rc), read_arg, caller_arg);
		return;
	}

	read_arg->end_of_file = rok->eof;
	read_arg->io_amount = rok->data.data_len;
	if (read_arg->info) {
		read_arg->info->io_content.what = NFS4_CONTENT_DATA;
		read_arg->info->io_content.data.d_offset = read_arg->offset +
			read_arg->io_amount;
		read_arg->info->io_content.data.d_data.data_len =
			read_arg->io_amount;
		read_arg->info->io_content.data.d_data.data_val =
			read_arg->iov[0].iov_base;
	}
	done_cb(obj_hdl, fsalstat(0, 0), read_arg, caller_arg);
}

static void pxy_write2(struct fsal_obj_handle *obj_hdl,
		       bool bypass,
		       fsal_async_cb done_cb,
		       struct fsal_io_arg *write_arg,
		       void *caller_arg)
{
	int maxWriteSize;
	int rc;
	int opcnt = 0;
	sessionid4 sid;
#define FSAL_WRITE_NB_OP_ALLOC 3 /* SEQUENCE + PUTFH + WRITE */
	nfs_argop4 argoparray[FSAL_WRITE_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_WRITE_NB_OP_ALLOC];
	WRITE4resok *wok;
	struct pxy_obj_handle *ph;
	stable_how4 stable_how;
	size_t buffer_size = write_arg->iov[0].iov_len;

	if (write_arg->info != NULL) {
		/* Currently we don't support WRITE_PLUS */
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), write_arg,
			caller_arg);
		return;
	}

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* check max write size */
	maxWriteSize = op_ctx->fsal_export->exp_ops.fs_maxwrite(
							op_ctx->fsal_export);
	if (buffer_size > maxWriteSize)
		buffer_size = maxWriteSize;

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	/* prepare PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	/* prepare write */
	wok = &resoparray[opcnt].nfs_resop4_u.opwrite.WRITE4res_u.resok4;

	if (write_arg->fsal_stable)
		stable_how = DATA_SYNC4;
	else
		stable_how = UNSTABLE4;
	if (write_arg->state) {
		struct pxy_state *pxy_state_id = container_of(write_arg->state,
							      struct pxy_state,
							      state);

		COMPOUNDV4_ARG_ADD_OP_WRITE(opcnt, argoparray,
					    write_arg->offset,
					    write_arg->iov[0].iov_base,
					    buffer_size, stable_how,
					    pxy_state_id->stateid.other);
	} else {
		COMPOUNDV4_ARG_ADD_OP_WRITE_STATELESS(opcnt, argoparray,
						  write_arg->offset,
						  write_arg->iov[0].iov_base,
						  buffer_size, stable_how);
	}

	/* nfs call */
	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK) {
		done_cb(obj_hdl, nfsstat4_to_fsal(rc), write_arg, caller_arg);
		return;
	}

	/* get res */
	write_arg->io_amount = wok->count;
	if (wok->committed == UNSTABLE4)
		write_arg->fsal_stable = false;
	else
		write_arg->fsal_stable = true;

	done_cb(obj_hdl, fsalstat(ERR_FSAL_NO_ERROR, 0), write_arg, caller_arg);
}

static fsal_status_t pxy_close2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state)
{
	struct pxy_obj_handle *ph;
	int rc;
	int opcnt = 0;
	sessionid4 sessionid;
	/* SEQUENCE, PUTFH, CLOSE */
#define FSAL_CLOSE_NB_OP_ALLOC 3
	nfs_argop4 argoparray[FSAL_CLOSE_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_CLOSE_NB_OP_ALLOC];
	char All_Zero[] = "\0\0\0\0\0\0\0\0\0\0\0\0";	/* 12 times \0 */
	struct pxy_state *pxy_state_id = NULL;

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* Check if this was a "stateless" open,
	 * then nothing is to be done at close */
	if (!state) {
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else {
		pxy_state_id = container_of(state, struct pxy_state, state);
		if (!memcmp(pxy_state_id->stateid.other, All_Zero, 12))
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* SEQUENCE */
	pxy_get_client_sessionid(sessionid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sessionid,
				       NB_RPC_SLOT);
	/* PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	/* CLOSE */
	if (state)
		COMPOUNDV4_ARG_ADD_OP_CLOSE_4_1(opcnt, argoparray,
						pxy_state_id->stateid);
	else
		COMPOUNDV4_ARG_ADD_OP_CLOSE_4_1_STATELESS(opcnt, argoparray);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK) {
		return nfsstat4_to_fsal(rc);
	}

	/* We clean local saved stateid. */
	if (state)
		memset(&pxy_state_id->stateid, 0, sizeof(stateid4));

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_setattr2(struct fsal_obj_handle *obj_hdl,
				  bool bypass,
				  struct state_t *state,
				  struct attrlist *attrib_set)
{
	int rc;
	fattr4 input_attr;
	uint32_t opcnt = 0;
	struct pxy_obj_handle *ph;
	sessionid4 sid;
#define FSAL_SETATTR2_NB_OP_ALLOC 3 /* SEQUENCE PUTFH SETATTR */
	nfs_argop4 argoparray[FSAL_SETATTR2_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_SETATTR2_NB_OP_ALLOC];

	/*prepare attributes */
	/*
	* No way to update CTIME using a NFSv4 SETATTR.
	* Server will return NFS4ERR_INVAL (22).
	* time_metadata is a readonly attribute in NFSv4 and NFSv4.1.
	* (section 5.7 in RFC7530 or RFC5651)
	* Nevermind : this update is useless, we prevent it.
	*/
	FSAL_UNSET_MASK(attrib_set->valid_mask, ATTR_CTIME);

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
		attrib_set->mode &=
			~op_ctx->fsal_export->exp_ops.fs_umask(
							op_ctx->fsal_export);

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	if (pxy_fsalattr_to_fattr4(attrib_set, &input_attr) == -1)
		return fsalstat(ERR_FSAL_INVAL, EINVAL);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	/* prepare PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	/* prepare SETATTR */
	resoparray[opcnt].nfs_resop4_u.opsetattr.attrsset = empty_bitmap;

	/* We don't use the special "bypass" stateid. */
	/* Indeed, bypass state will be treated like anonymous value. */
	/* RFC 5661, section 8.2.3 */
	if (state) {
		struct pxy_state *pxy_state_id = container_of(state,
							      struct pxy_state,
							      state);
		COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, argoparray, input_attr,
					      pxy_state_id->stateid.other);
	} else {
		COMPOUNDV4_ARG_ADD_OP_SETATTR_STATELESS(opcnt, argoparray,
							input_attr);
	}

	/* nfs call */
	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	nfs4_Fattr_Free(&input_attr);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_openflags_t pxy_status2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state)
{
	/* first version of support_ex, no state, no saved openflags */
	fsal_openflags_t null_flags = 0; /* closed and deny_none*/

	return null_flags;
}

static fsal_status_t pxy_reopen2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags)
{
	/* no way to open by handle in v4 */
	/* waiting for v4.1 or solid state to really do the job */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t pxy_commit2(struct fsal_obj_handle *obj_hdl,
		      off_t offset,
		      size_t len)
{
	struct pxy_obj_handle *ph;
	int rc; /* return code of nfs call */
	int opcnt = 0; /* nfs arg counter */
	sessionid4 sid;
#define FSAL_COMMIT2_NB_OP 3 /* SEQUENCE, PUTFH, COMMIT */
	nfs_argop4 argoparray[FSAL_COMMIT2_NB_OP];
	nfs_resop4 resoparray[FSAL_COMMIT2_NB_OP];

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	/* prepare PUTFH */
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	/* prepare COMMIT */
	COMPOUNDV4_ARG_ADD_OP_COMMIT(opcnt, argoparray, offset, len);

	/* nfs call */
	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void pxy_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = pxy_hdl_release;
	ops->lookup = pxy_lookup;
	ops->readdir = pxy_readdir;
	ops->mkdir = pxy_mkdir;
	ops->mknode = pxy_mknod;
	ops->symlink = pxy_symlink;
	ops->readlink = pxy_readlink;
	ops->getattrs = pxy_getattrs;
	ops->link = pxy_link;
	ops->rename = pxy_rename;
	ops->unlink = pxy_unlink;
	ops->close = pxy_close;
	ops->handle_to_wire = pxy_handle_to_wire;
	ops->handle_to_key = pxy_handle_to_key;
	ops->open2 = pxy_open2;
	ops->read2 = pxy_read2;
	ops->write2 = pxy_write2;
	ops->close2 = pxy_close2;
	ops->setattr2 = pxy_setattr2;
	ops->status2 = pxy_status2;
	ops->reopen2 = pxy_reopen2;
	ops->commit2 = pxy_commit2;
}

#ifdef PROXY_HANDLE_MAPPING
static unsigned int hash_nfs_fh4(const nfs_fh4 *fh, unsigned int cookie)
{
	const char *cpt;
	unsigned int sum = cookie;
	unsigned int extract;
	unsigned int mod = fh->nfs_fh4_len % sizeof(unsigned int);

	for (cpt = fh->nfs_fh4_val;
	     cpt - fh->nfs_fh4_val < fh->nfs_fh4_len - mod;
	     cpt += sizeof(unsigned int)) {
		memcpy(&extract, cpt, sizeof(unsigned int));
		sum = (3 * sum + 5 * extract + 1999);
	}

	/*
	 * If the handle is not 32 bits-aligned, the last loop will
	 * get uninitialized chars after the end of the handle. We
	 * must avoid this by skipping the last loop and doing a
	 * special processing for the last bytes
	 */
	if (mod) {
		extract = 0;
		while (cpt - fh->nfs_fh4_val < fh->nfs_fh4_len) {
			extract <<= 8;
			extract |= (uint8_t) (*cpt++);
		}
		sum = (3 * sum + 5 * extract + 1999);
	}

	return sum;
}
#endif

static struct pxy_obj_handle *pxy_alloc_handle(struct fsal_export *exp,
					       const nfs_fh4 *fh,
					       fattr4 *obj_attributes,
					       struct attrlist *attrs_out)
{
	struct pxy_obj_handle *n = gsh_calloc(1, sizeof(*n) + fh->nfs_fh4_len);
	compound_data_t data;
	struct attrlist attributes;

	memset(&attributes, 0, sizeof(attributes));
	memset(&data, 0, sizeof(data));

	data.current_obj = &n->obj;

	if (nfs4_Fattr_To_FSAL_attr(&attributes,
				    obj_attributes,
				    &data) != NFS4_OK) {
		gsh_free(n);
		return NULL;
	}

	n->fh4 = *fh;
	n->fh4.nfs_fh4_val = n->blob.bytes;
	memcpy(n->blob.bytes, fh->nfs_fh4_val, fh->nfs_fh4_len);
	n->blob.len = fh->nfs_fh4_len + sizeof(n->blob);
	n->blob.type = attributes.type;
#ifdef PROXY_HANDLE_MAPPING
	int rc;

	memset(&n->h23, 0, sizeof(n->h23));
	n->h23.len = sizeof(n->h23);
	n->h23.type = PXY_HANDLE_MAPPED;
	n->h23.object_id = n->obj.fileid;
	n->h23.handle_hash = hash_nfs_fh4(fh, n->obj.fileid);

	rc = HandleMap_SetFH(&n->h23, &n->blob, n->blob.len);
	if ((rc != HANDLEMAP_SUCCESS) && (rc != HANDLEMAP_EXISTS)) {
		gsh_free(n);
		return NULL;
	}
#endif

	fsal_obj_handle_init(&n->obj, exp, attributes.type);
	n->obj.fs = NULL;
	n->obj.state_hdl = NULL;
	n->obj.fsid = attributes.fsid;
	n->obj.fileid = attributes.fileid;
	n->obj.obj_ops = &PROXY.handle_ops;
	if (attrs_out != NULL) {
		/* We aren't keeping ACL ref ourself, so pass it
		 * to the caller.
		 */
		fsal_copy_attrs(attrs_out, &attributes, true);
	} else {
		/* Make sure we release the attributes. */
		fsal_release_attrs(&attributes);
	}

	return n;
}

/* export methods that create object handles
 */

fsal_status_t pxy_lookup_path(struct fsal_export *exp_hdl,
			      const char *path,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
{
	struct fsal_obj_handle *next;
	struct fsal_obj_handle *parent = NULL;
	char *saved;
	char *pcopy;
	char *p, *pnext;
	struct user_cred *creds = op_ctx->creds;

	pcopy = gsh_strdup(path);

	p = strtok_r(pcopy, "/", &saved);
	if (!p) {
		fsal_status_t st = pxy_lookup_impl(parent, exp_hdl, creds,
						   NULL, &next, attrs_out);
		if (FSAL_IS_ERROR(st)) {
			gsh_free(pcopy);
			return st;
		}
	}
	while (p) {
		if (strcmp(p, "..") == 0) {
			/* Don't allow lookup of ".." */
			LogInfo(COMPONENT_FSAL,
				"Attempt to use \"..\" element in path %s",
				path);
			gsh_free(pcopy);
			return fsalstat(ERR_FSAL_ACCESS, EACCES);
		}
		/* Get the next token now, so we know if we are at the
		 * terminal token or not.
		 */
		pnext = strtok_r(NULL, "/", &saved);

		/* Note that if any element is a symlink, the following will
		 * fail, thus no security exposure. Only pass back the
		 * attributes of the terminal lookup.
		 */
		fsal_status_t st = pxy_lookup_impl(parent, exp_hdl, creds, p,
						   &next, pnext == NULL ?
						   attrs_out : NULL);
		if (FSAL_IS_ERROR(st)) {
			gsh_free(pcopy);
			return st;
		}

		p = pnext;
		parent = next;
	}
	/* The final element could be a symlink, but either way we are called
	 * will not work with a symlink, so no security exposure there.
	 */

	gsh_free(pcopy);
	*handle = next;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * Create an FSAL 'object' from the handle - used
 * to construct objects from a handle which has been
 * 'extracted' by .wire_to_host.
 */
fsal_status_t pxy_create_handle(struct fsal_export *exp_hdl,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out)
{
	nfs_fh4 fh4;
	struct pxy_obj_handle *ph;
	struct pxy_handle_blob *blob;
	int rc;
	uint32_t opcnt = 0;
	sessionid4 sid;
#define FSAL_CREATE_HANDLE_NB_OP_ALLOC 3 /* SEQUENCE PUTFH GETATTR */
	nfs_argop4 argoparray[FSAL_CREATE_HANDLE_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_CREATE_HANDLE_NB_OP_ALLOC];
	GETATTR4resok *atok;
	char fattr_blob[FATTR_BLOB_SZ];

	blob = (struct pxy_handle_blob *)hdl_desc->addr;
	if (blob->len != hdl_desc->len)
		return fsalstat(ERR_FSAL_INVAL, 0);

	fh4.nfs_fh4_val = blob->bytes;
	fh4.nfs_fh4_len = blob->len - sizeof(*blob);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, fh4);

	atok = pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				      sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);

	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	ph = pxy_alloc_handle(exp_hdl, &fh4, &atok->obj_attributes,
			      attrs_out);
	if (!ph)
		return fsalstat(ERR_FSAL_FAULT, 0);

	*handle = &ph->obj;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t pxy_get_dynamic_info(struct fsal_export *exp_hdl,
				   struct fsal_obj_handle *obj_hdl,
				   fsal_dynamicfsinfo_t *infop)
{
	int rc;
	int opcnt = 0;
	sessionid4 sid;
#define FSAL_FSINFO_NB_OP_ALLOC 3 /* SEQUENCE PUTFH GETATTR */
	nfs_argop4 argoparray[FSAL_FSINFO_NB_OP_ALLOC];
	nfs_resop4 resoparray[FSAL_FSINFO_NB_OP_ALLOC];
	GETATTR4resok *atok;
	char fattr_blob[48];	/* 6 values, 8 bytes each */
	struct pxy_obj_handle *ph;

	ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

	/* SEQUENCE */
	pxy_get_client_sessionid(sid);
	COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argoparray, sid, NB_RPC_SLOT);
	COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
	atok =
	    pxy_fill_getattr_reply(resoparray + opcnt, fattr_blob,
				   sizeof(fattr_blob));
	COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_fsinfo);

	rc = pxy_nfsv4_call(op_ctx->creds, opcnt, argoparray, resoparray);
	if (rc != NFS4_OK)
		return nfsstat4_to_fsal(rc);

	if (nfs4_Fattr_To_fsinfo(infop, &atok->obj_attributes) != NFS4_OK)
		return fsalstat(ERR_FSAL_INVAL, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Convert of-the-wire digest into unique 'handle' which
 * can be used to identify the object */
fsal_status_t pxy_wire_to_host(struct fsal_export *exp_hdl,
			       fsal_digesttype_t in_type,
			       struct gsh_buffdesc *fh_desc,
			       int flags)
{
	struct pxy_handle_blob *pxyblob;
	size_t fh_size;

	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, EINVAL);

	pxyblob = (struct pxy_handle_blob *)fh_desc->addr;
	fh_size = pxyblob->len;
#ifdef PROXY_HANDLE_MAPPING
	if (in_type == FSAL_DIGEST_NFSV3)
		fh_size = sizeof(nfs23_map_handle_t);
#endif
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %zu, got %zu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
#ifdef PROXY_HANDLE_MAPPING
	if (in_type == FSAL_DIGEST_NFSV3) {
		nfs23_map_handle_t *h23 = (nfs23_map_handle_t *) fh_desc->addr;

		if (h23->type != PXY_HANDLE_MAPPED)
			return fsalstat(ERR_FSAL_STALE, ESTALE);

		/* As long as HandleMap_GetFH copies nfs23 handle into
		 * the key before lookup I can get away with using
		 * the same buffer for input and output */
		if (HandleMap_GetFH(h23, fh_desc) != HANDLEMAP_SUCCESS)
			return fsalstat(ERR_FSAL_STALE, 0);
		fh_size = fh_desc->len;
	}
#endif

	fh_desc->len = fh_size;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
