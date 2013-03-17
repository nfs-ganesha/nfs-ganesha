/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Proxy handle methods */

#include "config.h"

#include "fsal.h"
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include "nlm_list.h"
#include "fsal_types.h"
#include "FSAL/fsal_commonlib.h"
#include "pxy_fsal_methods.h"
#include "fsal_nfsv4_macros.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

#define FSAL_PROXY_NFS_V4 4

static clientid4 pxy_clientid;
static pthread_mutex_t pxy_clientid_mutex = PTHREAD_MUTEX_INITIALIZER;
static char pxy_hostname[MAXNAMLEN];
static pthread_t pxy_recv_thread;
static pthread_t pxy_renewer_thread;
static struct glist_head rpc_calls;
static struct glist_head free_contexts;
static int rpc_sock = -1;
static unsigned int rpc_xid;
static pthread_mutex_t listlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sockless = PTHREAD_COND_INITIALIZER;
static pthread_cond_t need_context = PTHREAD_COND_INITIALIZER;

/* NB! nfs_prog is just an easy way to get this info into the call
 *     It should really be fetched via export pointer */
struct pxy_rpc_io_context {
        pthread_mutex_t iolock;
        pthread_cond_t iowait;
        struct glist_head calls;
        uint32_t rpc_xid;
        int iodone;
        int ioresult;
        unsigned int nfs_prog;
        unsigned int sendbuf_sz;
        unsigned int recvbuf_sz;
        char *sendbuf;
        char *recvbuf;
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
        char padowner[MAXNAMLEN];
        char padgroup[MAXNAMLEN];
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
#ifdef _HANDLE_MAPPING
        nfs23_map_handle_t h23;
#endif
        fsal_openflags_t openflags;
        struct pxy_handle_blob blob;
};

static struct pxy_obj_handle *
pxy_alloc_handle(struct fsal_export *exp, const nfs_fh4 *fh,
                 const struct attrlist *attr);

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
        case NFS4ERR_BADNAME:
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
	.map[0] = (PXY_ATTR_BIT(FATTR4_TYPE) |
		   PXY_ATTR_BIT(FATTR4_CHANGE) |
		   PXY_ATTR_BIT(FATTR4_SIZE) |
		   PXY_ATTR_BIT(FATTR4_FSID) |
		   PXY_ATTR_BIT(FATTR4_FILEID)),
	.map[1] = (PXY_ATTR_BIT2(FATTR4_MODE) |
		   PXY_ATTR_BIT2(FATTR4_NUMLINKS) |
		   PXY_ATTR_BIT2(FATTR4_OWNER) |
		   PXY_ATTR_BIT2(FATTR4_OWNER_GROUP) | 
		   PXY_ATTR_BIT2(FATTR4_SPACE_USED) | 
		   PXY_ATTR_BIT2(FATTR4_TIME_ACCESS) |
		   PXY_ATTR_BIT2(FATTR4_TIME_METADATA) |
		   PXY_ATTR_BIT2(FATTR4_TIME_MODIFY) |
		   PXY_ATTR_BIT2(FATTR4_RAWDEV)),
	.bitmap4_len = 2
};

/* Until readdir callback can take more information do not ask for more then
 * just type */
static struct bitmap4 pxy_bitmap_readdir = {
	.map[0] = PXY_ATTR_BIT(FATTR4_TYPE),
	.bitmap4_len = 1
};

static struct bitmap4 pxy_bitmap_fsinfo = {
        .map[0] = (PXY_ATTR_BIT(FATTR4_FILES_AVAIL) |
		   PXY_ATTR_BIT(FATTR4_FILES_FREE) |
		   PXY_ATTR_BIT(FATTR4_FILES_TOTAL)),
	.map[1] = (PXY_ATTR_BIT2(FATTR4_SPACE_AVAIL) |
		   PXY_ATTR_BIT2(FATTR4_SPACE_FREE) |
		   PXY_ATTR_BIT2(FATTR4_SPACE_TOTAL)),
	.bitmap4_len = 2
};

static struct bitmap4 lease_bits = {
	.map[0]= PXY_ATTR_BIT(FATTR4_LEASE_TIME),
	.bitmap4_len = 1
};

#undef PXY_ATTR_BIT
#undef PXY_ATTR_BIT2

static struct
{
        attrmask_t mask;
        int fattr_bit;
} fsal_mask2bit[] = {
        {ATTR_SIZE, FATTR4_SIZE},
        {ATTR_MODE, FATTR4_MODE},
        {ATTR_OWNER, FATTR4_OWNER},
        {ATTR_GROUP, FATTR4_OWNER_GROUP},
        {ATTR_ATIME, FATTR4_TIME_ACCESS_SET},
        {ATTR_ATIME_SERVER, FATTR4_TIME_ACCESS_SET},
        {ATTR_MTIME, FATTR4_TIME_MODIFY_SET},
        {ATTR_MTIME_SERVER, FATTR4_TIME_MODIFY_SET},
        {ATTR_CTIME, FATTR4_TIME_METADATA}
};

static struct bitmap4 empty_bitmap = {
	.map[0] = 0,
	.map[1] = 0,
	.map[2] = 0,
	.bitmap4_len = 2
};

static int
pxy_fsalattr_to_fattr4(const struct attrlist *attrs, fattr4 *data)
{
        int i;
        struct bitmap4 bmap = empty_bitmap;

        for(i=0; i < ARRAY_SIZE(fsal_mask2bit); i++) {
                if(FSAL_TEST_MASK(attrs->mask, fsal_mask2bit[i].mask)) {
                        if (fsal_mask2bit[i].mask > 31) {
                                bmap.map[1] = 1U << (fsal_mask2bit[i].mask - 32);
                                bmap.bitmap4_len = 2;
                        } else {
                                bmap.map[0] = 1U << fsal_mask2bit[i].mask;
                        }
                }
        }

        return nfs4_FSALattr_To_Fattr(attrs, data, NULL, NULL, &bmap);
}

static GETATTR4resok *
pxy_fill_getattr_reply(nfs_resop4 *resop,
                       char *blob, size_t blob_sz)
{
        GETATTR4resok *a = &resop->nfs_resop4_u.opgetattr.GETATTR4res_u.resok4;

        a->obj_attributes.attrmask = empty_bitmap;
        a->obj_attributes.attr_vals.attrlist4_val = blob;
        a->obj_attributes.attr_vals.attrlist4_len = blob_sz;

        return a;
}

static int 
pxy_got_rpc_reply(struct pxy_rpc_io_context *ctx, int sock, int sz, u_int xid)
{
        char *repbuf = ctx->recvbuf;
        int size;

        if(sz > ctx->recvbuf_sz)
                return -E2BIG;

        pthread_mutex_lock(&ctx->iolock);
        memcpy(repbuf, &xid, sizeof(xid));
        /*
         * sz includes 4 bytes of xid which have been processed
         * together with record mark - reduce the read to avoid
         * gobbing up next record mark.
         */
        repbuf += 4;
        ctx->ioresult = 4;
        sz -= 4;

        while(sz > 0) {
                /* TODO: handle timeouts - use poll(2) */
                int bc = read(sock, repbuf, sz);

                if(bc <= 0) {
                        ctx->ioresult = -((bc < 0) ? errno : ETIMEDOUT);
                        break;
                }
                repbuf += bc;
                ctx->ioresult += bc;
                sz -= bc;
        }
        ctx->iodone = 1;
        size = ctx->ioresult;
        pthread_cond_signal(&ctx->iowait);
        pthread_mutex_unlock(&ctx->iolock);
        return size;
}

static int
pxy_rpc_read_reply(int sock)
{
        struct {
                uint recmark;
                uint xid;
        } h;
        char *buf = (char *)&h;
        struct glist_head *c;
        char sink[256];
        int cnt = 0;

        while(cnt < 8) {
                int bc = read(sock, buf + cnt, 8 - cnt);
                if(bc < 0)
                        return -errno;
                cnt += bc;
        }

        h.recmark = ntohl(h.recmark);
        /* TODO: check for final fragment */
        h.xid = ntohl(h.xid);

        LogDebug(COMPONENT_FSAL, "Recmark %x, xid %u\n", h.recmark, h.xid);
        h.recmark &= ~(1U<<31);

        pthread_mutex_lock(&listlock);
        glist_for_each(c, &rpc_calls) {
                struct pxy_rpc_io_context *ctx =
                        container_of(c, struct pxy_rpc_io_context, calls);

                if(ctx->rpc_xid == h.xid) {
                        glist_del(c);
                        pthread_mutex_unlock(&listlock);
                        return pxy_got_rpc_reply(ctx, sock, h.recmark, h.xid);
                }
        }
        pthread_mutex_unlock(&listlock);

        cnt = h.recmark - 4;
        LogDebug(COMPONENT_FSAL,
                 "xid %u is not on the list, skip %d bytes\n", h.xid, cnt);
        while(cnt > 0) {
                int rb = (cnt > sizeof(sink)) ? sizeof(sink) : cnt;

                rb = read(sock, sink, rb);
                if(rb <= 0)
                        return -errno;
                cnt -= rb;
        }

        return 0;
}

static void pxy_new_socket_ready(void)
{
        struct glist_head *nxt;
        struct glist_head *c;


        /* If there is anyone waiting for the socket then tell them
         * it's ready */
        pthread_cond_broadcast(&sockless);

        /* If there are any outstanding calls then tell them to resend */
        glist_for_each_safe(c, nxt, &rpc_calls) {
                struct pxy_rpc_io_context *ctx =
                        container_of(c, struct pxy_rpc_io_context, calls);

                glist_del(c);

                pthread_mutex_lock(&ctx->iolock);
                ctx->iodone = 1;
                ctx->ioresult = -EAGAIN;
                pthread_cond_signal(&ctx->iowait);
                pthread_mutex_unlock(&ctx->iolock);
        }
}

static int
pxy_connect(const proxyfs_specific_initinfo_t *info,
            struct sockaddr_in *dest)
{
        int sock;
        if(info->use_privileged_client_port) {
                int priv_port = 0;
                if((sock = rresvport(&priv_port)) < 0) 
                        LogCrit(COMPONENT_FSAL,
                                "Cannot create TCP socket on privileged port");
        } else {
                if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                        LogCrit(COMPONENT_FSAL,
                                "Cannot create TCP socket - %d", errno);
        }

        if(sock >= 0) {
                if(connect(sock, (struct sockaddr *)dest, sizeof(*dest)) < 0) {
                        close(sock);
                        sock = -1;
                } else {
                        pxy_new_socket_ready();
                } 
        }
        return sock;
}

/*
 * NB! rpc_sock can be closed by the sending thread but it will not be
 *     changing its value. Only this function will change rpc_sock which 
 *     means that it can look at the value without holding the lock.
 */
static void *
pxy_rpc_recv(void *arg)
{
        const proxyfs_specific_initinfo_t *info = arg;
        struct sockaddr_in addr_rpc;
        char addr[INET_ADDRSTRLEN];
        struct pollfd pfd;
        int millisec = info->srv_timeout * 1000;

        memset(&addr_rpc, 0, sizeof(addr_rpc));
        addr_rpc.sin_family = AF_INET;
        addr_rpc.sin_port = info->srv_port;
        addr_rpc.sin_addr.s_addr = info->srv_addr;

        for(;;) {
                int nsleeps = 0;
                pthread_mutex_lock(&listlock);
                do {
                        rpc_sock = pxy_connect(info, &addr_rpc);
                        if(rpc_sock < 0) {
                                if (nsleeps == 0)
                                        LogCrit(COMPONENT_FSAL,
                                                "Cannot connect to server %s:%u",
                                                inet_ntop(AF_INET,
                                                          &info->srv_addr, addr,
                                                          sizeof(addr)),
                                                ntohs(info->srv_port));
                                pthread_mutex_unlock(&listlock);
                                sleep(info->retry_sleeptime);
                                nsleeps++;
                                pthread_mutex_lock(&listlock);
                        } else {
                                LogDebug(COMPONENT_FSAL,
                                         "Connected after %d sleeps, "
                                         "resending outstanding calls",
                                         nsleeps);
                        }
                } while(rpc_sock < 0);
                pthread_mutex_unlock(&listlock);

                pfd.fd = rpc_sock;
                pfd.events = POLLIN | POLLRDHUP;

                while(rpc_sock >= 0) {
                        switch (poll(&pfd, 1, millisec)) {
                        case 0:
                                LogDebug(COMPONENT_FSAL,
                                         "Timeout, wait again...");
                                continue;

                        case -1:
                                break;

                        default:
                                if(pfd.revents & POLLRDHUP) {
                                        LogEvent(COMPONENT_FSAL,
                                                 "Other end has closed "
                                                 "connection, reconnecting...");
                                } else if(pfd.revents & POLLNVAL) {
                                        LogEvent(COMPONENT_FSAL,
                                                 "Socket is closed");
                                } else {
                                        if(pxy_rpc_read_reply(rpc_sock) >= 0)
                                                continue;
                                }
                                break;
                        }

                        pthread_mutex_lock(&listlock);
                        close(rpc_sock);
                        rpc_sock = -1;
                        pthread_mutex_unlock(&listlock);
                }
        }

        return NULL;
}

static enum clnt_stat
pxy_process_reply(struct pxy_rpc_io_context * ctx, COMPOUND4res *res)
{
        enum clnt_stat rc = RPC_CANTRECV;
        struct timespec ts;

        pthread_mutex_lock(&ctx->iolock);
        ts.tv_sec = time(NULL) + 60;
        ts.tv_nsec = 0;

        while(!ctx->iodone) {
                int w = pthread_cond_timedwait(&ctx->iowait, &ctx->iolock, &ts);
                if(w == ETIMEDOUT) {
                        pthread_mutex_unlock(&ctx->iolock);
                        return RPC_TIMEDOUT;
                }
        }

        ctx->iodone = 0;
        pthread_mutex_unlock(&ctx->iolock);

        if(ctx->ioresult > 0) {
                struct rpc_msg reply;
                XDR x;

                memset(&reply, 0, sizeof(reply));
                reply.acpted_rply.ar_results.proc = (xdrproc_t)xdr_COMPOUND4res;
                reply.acpted_rply.ar_results.where = (caddr_t)res;

		memset(&x, 0, sizeof(x));
                xdrmem_create(&x, ctx->recvbuf, ctx->ioresult, XDR_DECODE);

                if(xdr_replymsg(&x, &reply)) {
                        if(reply.rm_reply.rp_stat == MSG_ACCEPTED) {
                                switch(reply.rm_reply.rp_acpt.ar_stat) {
                                case SUCCESS: rc = RPC_SUCCESS; break;
                                case PROG_UNAVAIL: rc = RPC_PROGUNAVAIL; break;
                                case PROG_MISMATCH: rc = RPC_PROGVERSMISMATCH; break;
                                case PROC_UNAVAIL: rc = RPC_PROCUNAVAIL; break;
                                case GARBAGE_ARGS: rc = RPC_CANTDECODEARGS; break;
                                case SYSTEM_ERR: rc = RPC_SYSTEMERROR; break;
                                default: rc = RPC_FAILED; break;
                                }
                        } else {
                                switch(reply.rm_reply.rp_rjct.rj_stat) {
                                case RPC_MISMATCH: rc = RPC_VERSMISMATCH; break;
                                case AUTH_ERROR: rc = RPC_AUTHERROR; break;
                                default: rc = RPC_FAILED; break;
                                }
                        }
                } else {
                        rc = RPC_CANTDECODERES;
                }

                reply.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
                reply.acpted_rply.ar_results.where = NULL;

                xdr_free((xdrproc_t)xdr_replymsg,&reply);
        }
        return rc;
}

static void
pxy_rpc_need_sock(void)
{
        pthread_mutex_lock(&listlock);
        while(rpc_sock < 0)
                pthread_cond_wait(&sockless, &listlock);
        pthread_mutex_unlock(&listlock);
}

static int 
pxy_rpc_renewer_wait(int timeout)
{
        struct timespec ts;
        int rc;

        pthread_mutex_lock(&listlock);
        ts.tv_sec = time(NULL) + timeout;
        ts.tv_nsec = 0;

        rc = pthread_cond_timedwait(&sockless, &listlock, &ts);
        pthread_mutex_unlock(&listlock);
        return (rc == ETIMEDOUT);
}

static int 
pxy_compoundv4_call(struct pxy_rpc_io_context * pcontext, 
                    const struct user_cred *cred,
                    COMPOUND4args *args,
                    COMPOUND4res *res)
{
        XDR x;
        struct rpc_msg rmsg;
        AUTH *au;
        enum clnt_stat rc;

        pthread_mutex_lock(&listlock);
        rmsg.rm_xid = rpc_xid++;
        pthread_mutex_unlock(&listlock);
        rmsg.rm_direction = CALL;

        rmsg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
        rmsg.rm_call.cb_prog = pcontext->nfs_prog;
        rmsg.rm_call.cb_vers = FSAL_PROXY_NFS_V4;
        rmsg.rm_call.cb_proc = NFSPROC4_COMPOUND;

        if(cred) {
                au = authunix_create(pxy_hostname,
                                     cred->caller_uid,
                                     cred->caller_gid,
                                     cred->caller_glen,
                                     cred->caller_garray);
        } else {
                au = authunix_create_default();
        }
        if(au == NULL)
                return RPC_AUTHERROR;

        rmsg.rm_call.cb_cred = au->ah_cred;
        rmsg.rm_call.cb_verf = au->ah_verf;

	memset(&x, 0, sizeof(x));
        xdrmem_create(&x, pcontext->sendbuf + 4, pcontext->sendbuf_sz,
                      XDR_ENCODE);
        if(xdr_callmsg(&x, &rmsg) && xdr_COMPOUND4args(&x, args)) {
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
                        pthread_mutex_lock(&listlock);
                        while(bc < pos) {
                                int wc = write(rpc_sock, buf, pos - bc);
                                if(wc <= 0) {
                                        close(rpc_sock);
                                        break;
                                }
                                bc += wc;
                                buf += wc;
                        }

                        if(bc == pos) {
                                if(first_try) {
                                        glist_add_tail(&rpc_calls,
                                                       &pcontext->calls);
                                        first_try = 0;
                                }
                        } else {
                                if(!first_try)
                                        glist_del(&pcontext->calls);
                        }
                        pthread_mutex_unlock(&listlock);

                        if(bc == pos)
                                rc = pxy_process_reply(pcontext, res);
                        else
                                rc = RPC_CANTSEND;
                } while(rc == RPC_TIMEDOUT);
        } else {
                rc = RPC_CANTENCODEARGS;
        }
        if(au)
                auth_destroy(au);
        return rc;
}

int 
pxy_compoundv4_execute(const char *caller,
                       const struct user_cred *creds,
                       uint32_t cnt,
                       nfs_argop4 *argoparray,
                       nfs_resop4 *resoparray)
{
        enum clnt_stat rc;
        struct pxy_rpc_io_context *ctx;
        COMPOUND4args arg = {
                .argarray.argarray_val = argoparray,
                .argarray.argarray_len = cnt
        };
        COMPOUND4res res = {
                .resarray.resarray_val = resoparray,
                .resarray.resarray_len = cnt
        };

        pthread_mutex_lock(&listlock);
        while(glist_empty(&free_contexts))
                pthread_cond_wait(&need_context, &listlock);
        ctx = glist_first_entry(&free_contexts, struct pxy_rpc_io_context,
                                calls);
        glist_del(&ctx->calls);
        pthread_mutex_unlock(&listlock);

        do {
                rc = pxy_compoundv4_call(ctx, creds, &arg, &res);
                if(rc != RPC_SUCCESS)
                        LogDebug(COMPONENT_FSAL,
                                 "%s failed with %d", caller, rc);
                if(rc == RPC_CANTSEND)
                        pxy_rpc_need_sock();
        } while ((rc == RPC_CANTRECV && (ctx->ioresult == -EAGAIN)) ||
                 (rc == RPC_CANTSEND));


        pthread_mutex_lock(&listlock);
        if(glist_empty(&free_contexts))
                pthread_cond_signal(&need_context);
        glist_add(&free_contexts, &ctx->calls);
        pthread_mutex_unlock(&listlock);

        if(rc == RPC_SUCCESS)
                return res.status;
        return rc;
}

#define pxy_nfsv4_call(exp, creds, cnt, args, resp) \
        pxy_compoundv4_execute(__func__, creds, cnt, args, resp)

void pxy_get_clientid(clientid4 *ret)
{
  pthread_mutex_lock(&pxy_clientid_mutex);
  *ret = pxy_clientid;
  pthread_mutex_unlock(&pxy_clientid_mutex);
}

static int 
pxy_setclientid(clientid4 *resultclientid, uint32_t *lease_time)
{
        int rc;
        int opcnt = 0;
#define FSAL_CLIENTID_NB_OP_ALLOC 2
        nfs_argop4 arg[FSAL_CLIENTID_NB_OP_ALLOC];
        nfs_resop4 res[FSAL_CLIENTID_NB_OP_ALLOC]; 
        nfs_client_id4 nfsclientid;
        cb_client4 cbproxy;
        char clientid_name[MAXNAMLEN];
        SETCLIENTID4resok *sok;
        extern struct timespec ServerBootTime;
        struct sockaddr_in sin;
        socklen_t slen = sizeof(sin);
        char addrbuf[sizeof("255.255.255.255")];

        LogEvent(COMPONENT_FSAL,
                 "Negotiating a new ClientId with the remote server") ;

        if(getsockname(rpc_sock, &sin, &slen))
                return -errno;

        snprintf(clientid_name, MAXNAMLEN, "%s(%d) - GANESHA NFSv4 Proxy",
                 inet_ntop(AF_INET, &sin.sin_addr, addrbuf, sizeof(addrbuf)),
                 getpid());
        nfsclientid.id.id_len = strlen(clientid_name);
        nfsclientid.id.id_val = clientid_name;
        if(sizeof(ServerBootTime.tv_sec) == NFS4_VERIFIER_SIZE)
                memcpy(&nfsclientid.verifier, &ServerBootTime.tv_sec,
                       sizeof(nfsclientid.verifier));
        else
                snprintf(nfsclientid.verifier, NFS4_VERIFIER_SIZE, "%08x",
                         (int)ServerBootTime.tv_sec);

        cbproxy.cb_program = 0;
        cbproxy.cb_location.r_netid = "tcp";
        cbproxy.cb_location.r_addr = "127.0.0.1";

        sok = &res[0].nfs_resop4_u.opsetclientid.SETCLIENTID4res_u.resok4;
        arg[0].argop = NFS4_OP_SETCLIENTID;
        arg[0].nfs_argop4_u.opsetclientid.client = nfsclientid;
        arg[0].nfs_argop4_u.opsetclientid.callback = cbproxy;
        arg[0].nfs_argop4_u.opsetclientid.callback_ident = 0;

        rc = pxy_compoundv4_execute(__func__, NULL, 1, arg, res);
        if(rc != NFS4_OK)
                return -1;

        arg[0].argop = NFS4_OP_SETCLIENTID_CONFIRM;
        arg[0].nfs_argop4_u.opsetclientid_confirm.clientid = sok->clientid;
        memcpy(arg[0].nfs_argop4_u.opsetclientid_confirm.setclientid_confirm,
               sok->setclientid_confirm, NFS4_VERIFIER_SIZE);

        rc = pxy_compoundv4_execute(__func__, NULL, 1, arg, res);
        if(rc != NFS4_OK)
                return -1;

        /* Keep the confirmed client id */
        *resultclientid = arg[0].nfs_argop4_u.opsetclientid_confirm.clientid;

        /* Get the lease time */
        opcnt = 0;
        COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, arg);
        pxy_fill_getattr_reply(res + opcnt,
                               (char *)lease_time, sizeof(*lease_time));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, arg, lease_bits);

        rc = pxy_compoundv4_execute(__func__, NULL, opcnt, arg, res);
        if(rc != NFS4_OK)
                *lease_time = 60;
        else
                *lease_time = ntohl(*lease_time);

        return 0;
}

static void *
pxy_clientid_renewer(void *Arg)
{
        int rc;
        int needed = 1;
        nfs_argop4 arg;
        nfs_resop4 res;
        uint32_t lease_time = 60;
 
        while(1) {
                clientid4 newcid = 0;

                if(!needed && pxy_rpc_renewer_wait(lease_time - 5)) {
                        /* Simply renew the client id you've got */
                        LogDebug(COMPONENT_FSAL, "Renewing client id %lx",
                                 pxy_clientid);
                        arg.argop = NFS4_OP_RENEW;
                        arg.nfs_argop4_u.oprenew.clientid = pxy_clientid;
                        rc = pxy_compoundv4_execute(__func__, NULL, 1, &arg, &res);
                        if(rc == NFS4_OK) {
                                LogDebug(COMPONENT_FSAL,
                                         "Renewed client id %lx", pxy_clientid);
                                continue;
                        }
                }

                /* We've either failed to renew or rpc socket has been 
                 * reconnected and we need new client id */
                LogDebug(COMPONENT_FSAL, "Need %d new client id", needed);
                pxy_rpc_need_sock();
                needed = pxy_setclientid(&newcid, &lease_time);
                if(!needed) {
                        pthread_mutex_lock(&pxy_clientid_mutex);
                        pxy_clientid = newcid;
                        pthread_mutex_unlock(&pxy_clientid_mutex);
                }
        }
        return NULL;
}

static void
free_io_contexts(void)
{
        struct glist_head *cur, *n;
        glist_for_each_safe(cur, n, &free_contexts) {
                struct pxy_rpc_io_context *c =
                        container_of(cur, struct pxy_rpc_io_context, calls);
                glist_del(cur);
                free(c);
        }
}

int
pxy_init_rpc(const struct pxy_fsal_module *pm)
{
        int rc;
        int i = 16;

        /* TCP is the only protocol supported at the moment */
        if(strcmp(pm->special.srv_proto, "tcp")) {
                LogCrit(COMPONENT_FSAL,
                        "Protocol '%s' is not supported, consider using TCP",
                        pm->special.srv_proto);
                return ENOTSUP;
        }

        init_glist(&rpc_calls);
        init_glist(&free_contexts);

        rpc_xid = getpid() ^ time(NULL);

        if(gethostname(pxy_hostname, sizeof(pxy_hostname)))
                strncpy(pxy_hostname, "NFS-GANESHA/Proxy",
                        sizeof(pxy_hostname));

        for(i = i?:16; i > 0; i--) {
                struct pxy_rpc_io_context *c = malloc(sizeof(*c) +
                                                      pm->special.srv_sendsize +
                                                      pm->special.srv_recvsize);
                if(!c) {
                        free_io_contexts();
                        return ENOMEM;
                }
                pthread_mutex_init(&c->iolock, NULL);
                pthread_cond_init(&c->iowait, NULL);
                c->nfs_prog = pm->special.srv_prognum;
                c->sendbuf_sz = pm->special.srv_sendsize;
                c->recvbuf_sz = pm->special.srv_recvsize;
                c->sendbuf = (char *)(c + 1);
                c->recvbuf = c->sendbuf + c->sendbuf_sz;

                glist_add(&free_contexts, &c->calls);
        }

        rc = pthread_create(&pxy_recv_thread, NULL, pxy_rpc_recv,
                            (void *)&pm->special);
        if(rc) {
                LogCrit(COMPONENT_FSAL,
                        "Cannot create proxy rpc receiver thread - %s",
                        strerror(rc));
                free_io_contexts();
                return rc;
        }

        rc = pthread_create(&pxy_renewer_thread, NULL, pxy_clientid_renewer,
                            NULL);
        if(rc) {
                LogCrit(COMPONENT_FSAL,
                        "Cannot create proxy clientid renewer thread - %s",
                        strerror(rc));
                free_io_contexts();
        }
        return rc;
}

static fsal_status_t
pxy_make_object(struct fsal_export *export, fattr4 *obj_attributes,
                const nfs_fh4 *fh, struct fsal_obj_handle **handle)
{
        struct attrlist attributes;
        struct pxy_obj_handle *pxy_hdl;

	if(nfs4_Fattr_To_FSAL_attr(&attributes, obj_attributes,
				   NULL) != NFS4_OK)
                return fsalstat(ERR_FSAL_INVAL, 0);

        pxy_hdl = pxy_alloc_handle(export, fh, &attributes);
        if (pxy_hdl == NULL)
                return fsalstat(ERR_FSAL_FAULT, 0);
        *handle = &pxy_hdl->obj;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * NULL parent pointer is only used by lookup_path when it starts 
 * from the root handle and has its own export pointer, everybody
 * else is supposed to provide a real parent pointer and matching
 * export
 */
static fsal_status_t
pxy_lookup_impl(struct fsal_obj_handle *parent,
                struct fsal_export *export,
                const struct user_cred *cred,
	        const char *path,
	        struct fsal_obj_handle **handle)
{
        int rc;
        uint32_t opcnt = 0;
        GETATTR4resok *atok;
        GETFH4resok *fhok;
#define FSAL_LOOKUP_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_LOOKUP_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_LOOKUP_NB_OP_ALLOC];
        char fattr_blob[FATTR_BLOB_SZ];
        char padfilehandle[NFS4_FHSIZE];

        if(!handle)
                return fsalstat(ERR_FSAL_INVAL, 0);

        if(!parent) {
                COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, argoparray);
        } else {
                struct pxy_obj_handle *pxy_obj = 
                        container_of(parent, struct pxy_obj_handle, obj);
                switch (parent->type) {
                case DIRECTORY:
                        break;

                case FS_JUNCTION:
                        return fsalstat(ERR_FSAL_XDEV, 0);

                default:
                        return fsalstat(ERR_FSAL_NOTDIR, 0);
                }
                
                COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, pxy_obj->fh4);
        }

        if(path) {
                if(!strcmp(path, ".")) {
                        if(!parent)
                                return fsalstat(ERR_FSAL_FAULT, 0);
                } else if(!strcmp(path, "..")) {
                        if(!parent)
                                return fsalstat(ERR_FSAL_FAULT, 0);
                        COMPOUNDV4_ARG_ADD_OP_LOOKUPP(opcnt, argoparray);
                } else {
                        COMPOUNDV4_ARG_ADD_OP_LOOKUP(opcnt, argoparray, path);
                }
        }

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = pxy_fill_getattr_reply(resoparray+opcnt,
                                      fattr_blob, sizeof(fattr_blob));

        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        fhok->object.nfs_fh4_val = (char *)padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);

        rc = pxy_nfsv4_call(export, cred, opcnt, argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        return pxy_make_object(export, &atok->obj_attributes, 
                               &fhok->object, handle);
}

static fsal_status_t
pxy_lookup(struct fsal_obj_handle *parent,
           const struct req_op_context *opctx,
	   const char *path,
	   struct fsal_obj_handle **handle)
{
        if(!parent || !opctx)
                return fsalstat(ERR_FSAL_INVAL, 0);
        return pxy_lookup_impl(parent, parent->export, opctx->creds, path, handle);
}

static fsal_status_t
pxy_do_close(const struct user_cred *creds,
             const nfs_fh4 *fh4,
             stateid4 *sid,
             struct fsal_export *exp)
{
        int rc;
        int opcnt = 0;
#define FSAL_CLOSE_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_CLOSE_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_CLOSE_NB_OP_ALLOC];
        char All_Zero[] = "\0\0\0\0\0\0\0\0\0\0\0\0"; /* 12 times \0 */

        /* Check if this was a "stateless" open, then nothing is to be done at close */
        if(!memcmp(sid->other, All_Zero, 12))
                return fsalstat(ERR_FSAL_NO_ERROR, 0);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, *fh4);
        COMPOUNDV4_ARG_ADD_OP_CLOSE(opcnt, argoparray, sid);

        rc = pxy_nfsv4_call(exp, creds, opcnt, argoparray, resoparray);
        if (rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);
        sid->seqid++;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_open_confirm(const struct user_cred *cred,
                 const nfs_fh4 *fh4,
                 stateid4 *stateid,
                 struct fsal_export *export)
{
        int rc;
        int opcnt = 0;
#define FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC];
        nfs_argop4 *op;
        OPEN_CONFIRM4resok *conok;

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, *fh4);

        conok = &resoparray[opcnt].nfs_resop4_u.opopen_confirm.OPEN_CONFIRM4res_u.resok4;

        op = argoparray + opcnt++;
        op->argop = NFS4_OP_OPEN_CONFIRM;
        op->nfs_argop4_u.opopen_confirm.open_stateid.seqid = stateid->seqid;
        memcpy(op->nfs_argop4_u.opopen_confirm.open_stateid.other,
               stateid->other, 12);
        op->nfs_argop4_u.opopen_confirm.seqid = stateid->seqid + 1;

        rc = pxy_nfsv4_call(export, cred, opcnt, argoparray, resoparray);
        if (rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        stateid->seqid = conok->open_stateid.seqid;
        memcpy(stateid->other, conok->open_stateid.other, 12);
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* TODO: make this per-export */
static uint64_t fcnt;

static fsal_status_t
pxy_create(struct fsal_obj_handle *dir_hdl,
           const struct req_op_context *opctx,
           const char *name,
           struct attrlist *attrib,
           struct fsal_obj_handle **handle)
{
        int rc;
        int opcnt = 0;
        fattr4 input_attr;
        char padfilehandle[NFS4_FHSIZE];
        char fattr_blob[FATTR_BLOB_SZ];
#define FSAL_CREATE_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_CREATE_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_CREATE_NB_OP_ALLOC];
        char owner_val[128];
        unsigned int owner_len = 0;
        GETFH4resok *fhok;
        GETATTR4resok *atok;
        OPEN4resok *opok;
        struct pxy_obj_handle *ph;
        fsal_status_t st;
        clientid4 cid;

        if(!dir_hdl || !name || !attrib || !handle || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        /* Create the owner */
        snprintf(owner_val, sizeof(owner_val), "GANESHA/PROXY: pid=%u %ld",
                 getpid(), __sync_add_and_fetch(&fcnt, 1));
        owner_len = strnlen(owner_val, sizeof(owner_val));

        attrib->mask &= ATTR_MODE|ATTR_OWNER|ATTR_GROUP;
        if(pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
                return fsalstat(ERR_FSAL_INVAL, -1);

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        opok = &resoparray[opcnt].nfs_resop4_u.opopen.OPEN4res_u.resok4;
        opok->attrset = empty_bitmap;
        pxy_get_clientid(&cid);
        COMPOUNDV4_ARG_ADD_OP_OPEN_CREATE(opcnt, argoparray, (char*) name, input_attr,
                                          cid, owner_val, owner_len);

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        fhok->object.nfs_fh4_val = padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = pxy_fill_getattr_reply(resoparray + opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(dir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        /* See if a OPEN_CONFIRM is required */
        if(opok->rflags & OPEN4_RESULT_CONFIRM) {
                st = pxy_open_confirm(opctx->creds, &fhok->object,
                                      &opok->stateid, dir_hdl->export);
                if(FSAL_IS_ERROR(st))
                        return st;
        }

        /* The created file is still opened, to preserve the correct 
         * seqid for later use, we close it */
        st = pxy_do_close(opctx->creds, &fhok->object, &opok->stateid,
                          dir_hdl->export);
        if(FSAL_IS_ERROR(st))
                return st;
        st = pxy_make_object(dir_hdl->export, &atok->obj_attributes, 
                             &fhok->object, handle);
        if(FSAL_IS_ERROR(st))
                return st;
        *attrib = (*handle)->attributes;
        return st;
}

static fsal_status_t 
pxy_mkdir(struct fsal_obj_handle *dir_hdl,
          const struct req_op_context *opctx,
          const char *name,
          struct attrlist *attrib,
          struct fsal_obj_handle **handle)
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

#define FSAL_MKDIR_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_MKDIR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_MKDIR_NB_OP_ALLOC];

        if(!dir_hdl || !name || !handle || !attrib || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        /*
         * The caller gives us partial attributes which include mode and owner
         * and expects the full attributes back at the end of the call.
         */
        attrib->mask &= ATTR_MODE|ATTR_OWNER|ATTR_GROUP;
        if(pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
                return fsalstat(ERR_FSAL_INVAL, -1);

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset = empty_bitmap;
        COMPOUNDV4_ARG_ADD_OP_MKDIR(opcnt, argoparray, (char*) name, input_attr);

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        fhok->object.nfs_fh4_val = padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = pxy_fill_getattr_reply(resoparray + opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(dir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        st = pxy_make_object(dir_hdl->export, &atok->obj_attributes,
                             &fhok->object, handle);
        if(!FSAL_IS_ERROR(st))
                *attrib = (*handle)->attributes;
        return st;
}

static fsal_status_t
pxy_mknod(struct fsal_obj_handle *dir_hdl,
          const struct req_op_context *opctx,
          const char *name,
          object_file_type_t nodetype,
          fsal_dev_t *dev,
          struct attrlist *attrib,
          struct fsal_obj_handle **handle)
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
        specdata4 specdata = {0, 0};

        nfs_argop4 argoparray[4];
        nfs_resop4 resoparray[4];

        if(!dir_hdl || !name || !name || !handle || !attrib || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        switch(nodetype) {
        case CHARACTER_FILE:
                if(!dev)
                        return fsalstat(ERR_FSAL_FAULT, EINVAL);
                specdata.specdata1 = dev->major;
                specdata.specdata2 = dev->minor;
                nf4type = NF4CHR;
                break;
        case BLOCK_FILE:
                if(!dev)
                        return fsalstat(ERR_FSAL_FAULT, EINVAL);
                specdata.specdata1 = dev->major;
                specdata.specdata2 = dev->minor;
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
        attrib->mask &= ATTR_MODE|ATTR_OWNER|ATTR_GROUP;
        if(pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
                return fsalstat(ERR_FSAL_INVAL, -1);

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset = empty_bitmap;
        COMPOUNDV4_ARG_ADD_OP_CREATE(opcnt, argoparray, (char*)name, 
                                     nf4type, input_attr, specdata);

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        fhok->object.nfs_fh4_val = padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = pxy_fill_getattr_reply(resoparray + opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(dir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        st = pxy_make_object(dir_hdl->export, &atok->obj_attributes,
                             &fhok->object, handle);
        if(!FSAL_IS_ERROR(st))
                *attrib = (*handle)->attributes;
        return st;
}

static fsal_status_t
pxy_symlink(struct fsal_obj_handle *dir_hdl,
            const struct req_op_context *opctx,
            const char *name,
            const char *link_path,
            struct attrlist *attrib,
            struct fsal_obj_handle **handle)
{
        int rc;
        int opcnt = 0;
        fattr4 input_attr;
        char padfilehandle[NFS4_FHSIZE];
        char fattr_blob[FATTR_BLOB_SZ];
#define FSAL_SYMLINK_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_SYMLINK_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_SYMLINK_NB_OP_ALLOC];
        GETATTR4resok *atok;
        GETFH4resok *fhok;
        fsal_status_t st;
        struct pxy_obj_handle *ph;

        if(!dir_hdl || !name || !link_path || !opctx ||
           !attrib || !handle || !(attrib->mask & ATTR_MODE))
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        /* Tests if symlinking is allowed by configuration. */
        if( !dir_hdl->export->ops->fs_supports(dir_hdl->export,
                                               fso_symlink_support))
                return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);

        attrib->mask = ATTR_MODE;
        if(pxy_fsalattr_to_fattr4(attrib, &input_attr) == -1)
                return fsalstat(ERR_FSAL_INVAL, -1);

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        resoparray[opcnt].nfs_resop4_u.opcreate.CREATE4res_u.resok4.attrset = empty_bitmap;
        COMPOUNDV4_ARG_ADD_OP_SYMLINK(opcnt, argoparray, (char*) name,
                                      (char*) link_path, input_attr);

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        fhok->object.nfs_fh4_val = padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = pxy_fill_getattr_reply(resoparray + opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(dir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        st = pxy_make_object(dir_hdl->export, &atok->obj_attributes,
                             &fhok->object, handle);
        if(!FSAL_IS_ERROR(st))
                *attrib = (*handle)->attributes;
        return st;
}

static fsal_status_t
pxy_readlink(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx,
             struct gsh_buffdesc *link_content,
             bool refresh)
{
        int rc;
        int opcnt = 0;
        struct pxy_obj_handle *ph;
#define FSAL_READLINK_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_READLINK_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_READLINK_NB_OP_ALLOC];
        READLINK4resok *rlok;

        if(!obj_hdl || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

	/* This saves us from having to do one allocation for the XDR,
	   another allocation for the return, and a copy just to get
	   the \NUL terminator. The link length should be cached in
	   the file handle. */

	link_content->len = obj_hdl->attributes.filesize ?
		(obj_hdl->attributes.filesize + 1) :
		fsal_default_linksize;
	link_content->addr = gsh_malloc(link_content->len);

	if (link_content->addr == NULL) {
		return fsalstat(ERR_FSAL_NOMEM, 0);
	}

        rlok = &resoparray[opcnt].nfs_resop4_u.opreadlink.READLINK4res_u.resok4;
        rlok->link.utf8string_val = link_content->addr;
        rlok->link.utf8string_len = link_content->len;
        COMPOUNDV4_ARG_ADD_OP_READLINK(opcnt, argoparray);

        rc = pxy_nfsv4_call(obj_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        if(rc != NFS4_OK) {
		gsh_free(link_content->addr);
		link_content->addr = NULL;
		link_content->len = 0;
                return nfsstat4_to_fsal(rc);
	}
        
        rlok->link.utf8string_val[rlok->link.utf8string_len] = '\0';
        link_content->len = rlok->link.utf8string_len + 1;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_link(struct fsal_obj_handle *obj_hdl,
         const struct req_op_context *opctx,
         struct fsal_obj_handle *destdir_hdl,
         const char *name)
{
        int rc;
        struct pxy_obj_handle *tgt;
        struct pxy_obj_handle *dst;
#define FSAL_LINK_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_LINK_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_LINK_NB_OP_ALLOC];
        int opcnt = 0;

        if(!obj_hdl || !destdir_hdl || !name || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        /* Tests if hardlinking is allowed by configuration. */
        if( !destdir_hdl->export->ops->fs_supports(destdir_hdl->export,
                                                   fso_link_support))
                return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);

        tgt = container_of(obj_hdl, struct pxy_obj_handle, obj);
        dst = container_of(destdir_hdl, struct pxy_obj_handle, obj);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, tgt->fh4);
        COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argoparray);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt,argoparray, dst->fh4);
        COMPOUNDV4_ARG_ADD_OP_LINK(opcnt, argoparray, (char*) name);

        rc = pxy_nfsv4_call(obj_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        return nfsstat4_to_fsal(rc);
}


static bool
xdr_readdirres(XDR *x, nfs_resop4 *rdres)
{
        return xdr_nfs_resop4(x, rdres) && xdr_nfs_resop4(x, rdres + 1);
}

/*
 * Trying to guess how many entries can fit into a readdir buffer
 * is complicated and usually results in either gross over-allocation
 * of the memory for results or under-allocation (on large directories)
 * and buffer overruns - just pay the price of allocating the memory
 * inside XDR decoding and free it when done
 */
static fsal_status_t
pxy_do_readdir(const struct req_op_context *opctx,
               struct pxy_obj_handle *ph, nfs_cookie4 *cookie,
               fsal_readdir_cb cb, void *cbarg, bool *eof)
{
        uint32_t opcnt = 0;
        int rc;
        entry4 *e4;
#define FSAL_READDIR_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_READDIR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_READDIR_NB_OP_ALLOC];
        READDIR4resok *rdok;
        fsal_status_t st = {ERR_FSAL_NO_ERROR, 0};

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        rdok = &resoparray[opcnt].nfs_resop4_u.opreaddir.READDIR4res_u.resok4;
        rdok->reply.entries = NULL;
        COMPOUNDV4_ARG_ADD_OP_READDIR(opcnt, argoparray, *cookie,
                                      pxy_bitmap_readdir);

        rc = pxy_nfsv4_call(ph->obj.export, opctx->creds, opcnt,
                            argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        *eof = rdok->reply.eof;

        for(e4 = rdok->reply.entries; e4; e4 = e4->nextentry) {
                struct attrlist attr;
                char name[MAXNAMLEN+1];

                /* UTF8 name does not include trailing 0 */
                if(e4->name.utf8string_len > sizeof(name) - 1)
                        return fsalstat(ERR_FSAL_SERVERFAULT, E2BIG);
                memcpy(name, e4->name.utf8string_val, e4->name.utf8string_len);
                name[e4->name.utf8string_len] = '\0';

		if(nfs4_Fattr_To_FSAL_attr(&attr, &e4->attrs, NULL))
                        return fsalstat(ERR_FSAL_FAULT, 0);

                *cookie = e4->cookie;

                if(!cb(opctx, name, cbarg, e4->cookie)) {
                        break;
                }
        }
        xdr_free((xdrproc_t)xdr_readdirres, resoparray);
        return st;
}

/* What to do about verifier if server needs one? */
static fsal_status_t
pxy_readdir(struct fsal_obj_handle *dir_hdl,
            const struct req_op_context *opctx,
            fsal_cookie_t *whence,
            void *cbarg,
            fsal_readdir_cb cb,
            bool *eof)
{
        nfs_cookie4 cookie = 0;
        struct pxy_obj_handle *ph;

        if(!dir_hdl || !cb || !eof || !opctx)
                return fsalstat(ERR_FSAL_INVAL, 0);
        if(whence) {
               cookie = (nfs_cookie4)*whence;
        }

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);

        do {
                fsal_status_t st;

                st = pxy_do_readdir(opctx, ph,  &cookie, cb, cbarg, eof);
                if(FSAL_IS_ERROR(st)) {
                        return st;
                }
        } while (*eof == false);

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_rename(struct fsal_obj_handle *olddir_hdl,
           const struct req_op_context *opctx,
           const char *old_name,
           struct fsal_obj_handle *newdir_hdl,
           const char *new_name)
{
        int rc;
        int opcnt = 0;
#define FSAL_RENAME_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_RENAME_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_RENAME_NB_OP_ALLOC];
        struct pxy_obj_handle *src;
        struct pxy_obj_handle *tgt;

        if(!olddir_hdl || !newdir_hdl || !old_name || !new_name || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        src = container_of(olddir_hdl, struct pxy_obj_handle, obj);
        tgt = container_of(newdir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, src->fh4);
        COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argoparray);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, tgt->fh4);
        COMPOUNDV4_ARG_ADD_OP_RENAME(opcnt, argoparray,
                                     (char*) old_name,
                                     (char*) new_name);

        rc = pxy_nfsv4_call(olddir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        return nfsstat4_to_fsal(rc);
}

static fsal_status_t
pxy_getattrs_impl(const struct user_cred *creds,
                  struct fsal_export *exp,
                  nfs_fh4 *filehandle,
                  struct attrlist *obj_attr)
{
        int rc;
        uint32_t opcnt = 0;
#define FSAL_GETATTR_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_GETATTR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_GETATTR_NB_OP_ALLOC];
        GETATTR4resok *atok;
        char fattr_blob[FATTR_BLOB_SZ]; 

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, *filehandle);

        atok = pxy_fill_getattr_reply(resoparray+opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(exp, creds, opcnt, argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        if(nfs4_Fattr_To_FSAL_attr(obj_attr,
				   &atok->obj_attributes, NULL) != NFS4_OK)
                return fsalstat(ERR_FSAL_INVAL, 0);

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_getattrs(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx) 
{
        struct pxy_obj_handle *ph;
        fsal_status_t st;
        struct attrlist obj_attr;

        if(!obj_hdl || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
        st = pxy_getattrs_impl(opctx->creds, obj_hdl->export,
                               &ph->fh4, &obj_attr);
        if(!FSAL_IS_ERROR(st)) {
                obj_hdl->attributes = obj_attr;
        }
        return st;
}

/*
 * Couple of things to note:
 * 1. We assume that checks for things like cansettime are done
 *    by the caller.
 * 2. attrs can be modified in this function but caller cannot
 *    assume that the attributes are up-to-date
 */
static fsal_status_t
pxy_setattrs(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx,
	     struct attrlist *attrs)
{
        int rc;
        fattr4 input_attr;
        uint32_t opcnt = 0;
        struct pxy_obj_handle *ph;
        char fattr_blob[FATTR_BLOB_SZ];
        GETATTR4resok *atok;
        struct attrlist attrs_after;

#define FSAL_SETATTR_NB_OP_ALLOC 3
        nfs_argop4 argoparray[FSAL_SETATTR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_SETATTR_NB_OP_ALLOC];

        if(!obj_hdl || !attrs || ! opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
                attrs->mode &= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

        if(pxy_fsalattr_to_fattr4(attrs, &input_attr) == -1)
                return fsalstat(ERR_FSAL_INVAL, EINVAL);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        resoparray[opcnt].nfs_resop4_u.opsetattr.attrsset = empty_bitmap;
        COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, argoparray, input_attr);

        atok = pxy_fill_getattr_reply(resoparray+opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(obj_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

	rc = nfs4_Fattr_To_FSAL_attr(&attrs_after,
				     &atok->obj_attributes, NULL);
        if(rc != NFS4_OK) {
                LogWarn(COMPONENT_FSAL,
                        "Attribute conversion fails with %d, "
                        "ignoring attibutes after making changes", rc);
        } else {
                obj_hdl->attributes = attrs_after;
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool
pxy_handle_is(struct fsal_obj_handle *obj_hdl,
              object_file_type_t type)
{
        return obj_hdl->type == type;
}

static fsal_status_t
pxy_unlink(struct fsal_obj_handle *dir_hdl,
           const struct req_op_context *opctx,
           const char *name)
{
        int opcnt = 0;
        int rc;
        struct pxy_obj_handle *ph;
#define FSAL_UNLINK_NB_OP_ALLOC 3
        nfs_argop4 argoparray[FSAL_UNLINK_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_UNLINK_NB_OP_ALLOC];
        GETATTR4resok *atok;
        char fattr_blob[FATTR_BLOB_SZ];
        struct attrlist dirattr;

        if(!dir_hdl|| !name || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        COMPOUNDV4_ARG_ADD_OP_REMOVE(opcnt, argoparray, (char*) name);

        atok = pxy_fill_getattr_reply(resoparray+opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_getattr);

        rc = pxy_nfsv4_call(dir_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        if(rc != NFS4_OK)
          return nfsstat4_to_fsal(rc);

	if(nfs4_Fattr_To_FSAL_attr(&dirattr, &atok->obj_attributes,
				   NULL) == NFS4_OK)
                dir_hdl->attributes = dirattr;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t 
pxy_handle_digest(struct fsal_obj_handle *obj_hdl,
                  fsal_digesttype_t output_type,
                  struct gsh_buffdesc *fh_desc)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
        size_t fhs;
        void *data;
        uint32_t u32;

	/* sanity checks */
	if( !fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
#ifdef _HANDLE_MAPPING
                fhs = sizeof(ph->h23);
                data = &ph->h23;
                break;
#endif
	case FSAL_DIGEST_NFSV4:
                fhs = ph->blob.len;
                data = &ph->blob;
		break;
	case FSAL_DIGEST_FILEID2:
                fhs = FSAL_DIGEST_SIZE_FILEID2;
                u32 = ph->obj.attributes.fileid;
                data = &u32;
		break;
	case FSAL_DIGEST_FILEID3:
                fhs = FSAL_DIGEST_SIZE_FILEID3;
                data = &ph->obj.attributes.fileid;
		break;
	case FSAL_DIGEST_FILEID4:
                fhs = FSAL_DIGEST_SIZE_FILEID4;
                data = &ph->obj.attributes.fileid;
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

        if(fh_desc->len < fhs)
                return fsalstat(ERR_FSAL_TOOSMALL, 0);
        memcpy(fh_desc->addr, data, fhs);
        fh_desc->len = fhs;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static void
pxy_handle_to_key(struct fsal_obj_handle *obj_hdl,
		  struct gsh_buffdesc *fh_desc)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
	fh_desc->addr = &ph->blob;
	fh_desc->len =  ph->blob.len;
}

static fsal_status_t
pxy_hdl_release(struct fsal_obj_handle *obj_hdl)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
        pthread_mutex_lock(&obj_hdl->lock);
        if(obj_hdl->refs != 0) { 
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle @ %p with %d refs",
                        obj_hdl, obj_hdl->refs);
                pthread_mutex_unlock(&obj_hdl->lock);
                return fsalstat(ERR_FSAL_DELAY, EBUSY);
        }
        fsal_detach_handle(obj_hdl->export, &obj_hdl->handles);
        pthread_mutex_unlock(&obj_hdl->lock);
        pthread_mutex_destroy(&obj_hdl->lock);
        free(ph);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/*
 * Without name the 'open' for NFSv4 makes no sense - we could
 * send a getattr to the backend server but it's not going to
 * do anything useful anyway, so just save the openflags to record
 * the fact that file has been 'opened' and be done.
 */
static fsal_status_t
pxy_open(struct fsal_obj_handle *obj_hdl,
	 const struct req_op_context *opctx,
	 fsal_openflags_t openflags)
{
        struct pxy_obj_handle *ph; 

        if (!obj_hdl)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
        if((ph->openflags != FSAL_O_CLOSED) && (ph->openflags != openflags))
                return fsalstat(ERR_FSAL_FILE_OPEN, EBADF);
        ph->openflags = openflags;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_read(struct fsal_obj_handle *obj_hdl,
         const struct req_op_context *opctx,
	 uint64_t offset,
	 size_t buffer_size,
	 void *buffer,
	 size_t *read_amount,
	 bool * end_of_file)
{
        int rc;
        int opcnt = 0;
        struct pxy_obj_handle *ph;
#define FSAL_READ_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_READ_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_READ_NB_OP_ALLOC];
        READ4resok *rok;

        if (!obj_hdl || !read_amount || !end_of_file || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        if(!buffer_size) {
                *read_amount = 0;
                *end_of_file = false;
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
#if 0
        if((ph->openflags & (FSAL_O_RDONLY|FSAL_O_RDWR)) == 0)
                return fsalstat(ERR_FSAL_FILE_OPEN, EBADF);
#endif

        if(buffer_size > obj_hdl->export->ops->fs_maxread(obj_hdl->export))
                buffer_size = obj_hdl->export->ops->fs_maxread(obj_hdl->export);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        rok = &resoparray[opcnt].nfs_resop4_u.opread.READ4res_u.resok4;
        rok->data.data_val = buffer;
        rok->data.data_len = buffer_size;
        COMPOUNDV4_ARG_ADD_OP_READ(opcnt, argoparray, offset,
                                   buffer_size);

        rc = pxy_nfsv4_call(obj_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        *end_of_file = rok->eof;
        *read_amount = rok->data.data_len;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_write(struct fsal_obj_handle *obj_hdl,
          const struct req_op_context *opctx,
	  uint64_t offset,
	  size_t size,
	  void *buffer,
	  size_t *write_amount,
	  bool *fsal_stable)
{
        int rc;
        int opcnt = 0;
#define FSAL_WRITE_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_WRITE_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_WRITE_NB_OP_ALLOC];
        WRITE4resok *wok;
        struct pxy_obj_handle *ph;

        if (!obj_hdl || !write_amount || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        if(!size) {
                *write_amount = 0;
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
#if 0
        if((ph->openflags & (FSAL_O_WRONLY|FSAL_O_RDWR|FSAL_O_APPEND)) == 0) {
                return fsalstat(ERR_FSAL_FILE_OPEN, EBADF);
        }
#endif

        if(size > obj_hdl->export->ops->fs_maxwrite(obj_hdl->export))
                size = obj_hdl->export->ops->fs_maxwrite(obj_hdl->export);
        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        wok = &resoparray[opcnt].nfs_resop4_u.opwrite.WRITE4res_u.resok4;
        COMPOUNDV4_ARG_ADD_OP_WRITE(opcnt, argoparray, offset,
                                    buffer, size);

        rc = pxy_nfsv4_call(obj_hdl->export, opctx->creds, opcnt,
                            argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        *write_amount = wok->count;
        *fsal_stable = false;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* We send all out writes as DATA_SYNC, commit becomes a NO-OP */
static fsal_status_t
pxy_commit(struct fsal_obj_handle *obj_hdl,
	   off_t offset,
	   size_t len)
{
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_close(struct fsal_obj_handle *obj_hdl)
{
        struct pxy_obj_handle *ph; 

        if (!obj_hdl)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
        if(ph->openflags == FSAL_O_CLOSED)
                return fsalstat(ERR_FSAL_NOT_OPENED, EBADF);
        ph->openflags = FSAL_O_CLOSED;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void pxy_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = pxy_hdl_release;
	ops->lookup = pxy_lookup;
	ops->readdir = pxy_readdir;
	ops->create = pxy_create;
	ops->mkdir = pxy_mkdir;
	ops->mknode = pxy_mknod;
	ops->symlink = pxy_symlink;
	ops->readlink = pxy_readlink;
	ops->getattrs = pxy_getattrs;
	ops->setattrs = pxy_setattrs;
	ops->link = pxy_link;
	ops->rename = pxy_rename;
	ops->unlink = pxy_unlink;
	ops->open = pxy_open;
	ops->read = pxy_read;
	ops->write = pxy_write;
	ops->commit = pxy_commit;
	ops->close = pxy_close;
	ops->handle_is = pxy_handle_is;
	ops->handle_digest = pxy_handle_digest;
	ops->handle_to_key = pxy_handle_to_key;
}

#ifdef _HANDLE_MAPPING
static unsigned int
hash_nfs_fh4(const nfs_fh4 *fh, unsigned int cookie)
{
        const char *cpt;
        unsigned int sum = cookie;
        unsigned int extract;
        unsigned int mod = fh->nfs_fh4_len % sizeof(unsigned int);

        for(cpt = fh->nfs_fh4_val;
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
        if(mod) {
                extract = 0;
                while(cpt - fh->nfs_fh4_val < fh->nfs_fh4_len) {
                        extract <<= 8;
                        extract |= (uint8_t)(*cpt++);
                }
                sum = (3 * sum + 5 * extract + 1999);
        }

        return sum;
}
#endif

static struct pxy_obj_handle *
pxy_alloc_handle(struct fsal_export *exp, const nfs_fh4 *fh,
                 const struct attrlist *attr)
{
        struct pxy_obj_handle *n = malloc(sizeof(*n) + fh->nfs_fh4_len);

        if (n) {
                n->fh4 = *fh;
                n->fh4.nfs_fh4_val = n->blob.bytes;
                memcpy(n->blob.bytes, fh->nfs_fh4_val, fh->nfs_fh4_len);
                n->obj.attributes = *attr;
                n->blob.len = fh->nfs_fh4_len + sizeof(n->blob);
                n->blob.type = attr->type;
#ifdef _HANDLE_MAPPING
                int rc;
                memset(&n->h23, 0, sizeof(n->h23));
                n->h23.len = sizeof(n->h23);
                n->h23.type = PXY_HANDLE_MAPPED;
                n->h23.object_id = attr->fileid;
                n->h23.handle_hash = hash_nfs_fh4(fh, attr->fileid);

                rc = HandleMap_SetFH(&n->h23, &n->blob, n->blob.len);
                if((rc != HANDLEMAP_SUCCESS) && (rc != HANDLEMAP_EXISTS)) {
                        free(n);
                        return NULL;
                }
#endif
                if(fsal_obj_handle_init(&n->obj, exp,
                                        attr->type)) {
                        free(n);
                        n = NULL;
                }
        }
        return n;
}

/* export methods that create object handles
 */

fsal_status_t
pxy_lookup_path(struct fsal_export *exp_hdl,
                const struct req_op_context *opctx,
		const char *path,
		struct fsal_obj_handle **handle)
{
        struct fsal_obj_handle *next;
        struct fsal_obj_handle *parent = NULL;
        char *saved;
        char *pcopy;
        char *p;
        struct user_cred *creds = opctx ? opctx->creds : NULL;

        if(!path || path[0] != '/')
                return fsalstat(ERR_FSAL_INVAL, EINVAL);

        pcopy = strdup(path);
        if(!pcopy)
                return fsalstat(ERR_FSAL_NOMEM, ENOMEM);

        p = strtok_r(pcopy, "/", &saved);
        do {
                fsal_status_t st = pxy_lookup_impl(parent, exp_hdl,
                                                   creds, p, &next);
                if(FSAL_IS_ERROR(st)) {
                        free(pcopy);
                        return st;
                }

                if(p) {
                        p = strtok_r(NULL, "/", &saved);
                        parent = next;
                }
        } while(p);

        free(pcopy);
        *handle = next;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * Create an FSAL 'object' from the handle - used
 * to construct objects from a handle which has been
 * 'extracted' by .extract_handle.
 */
fsal_status_t
pxy_create_handle(struct fsal_export *exp_hdl,
                  const struct req_op_context *opctx,
		  struct gsh_buffdesc *hdl_desc,
		  struct fsal_obj_handle **handle)
{
        fsal_status_t st;
        nfs_fh4 fh4;
        struct attrlist attr;
        struct pxy_obj_handle *ph;
        struct pxy_handle_blob *blob;

        if(!exp_hdl || !hdl_desc || !handle || !opctx ||
           (hdl_desc->len > NFS4_FHSIZE) || (hdl_desc->len < sizeof(*blob)))
                return fsalstat(ERR_FSAL_INVAL, 0);

        blob = (struct pxy_handle_blob *)hdl_desc->addr;
        if (blob->len != hdl_desc->len)
                return fsalstat(ERR_FSAL_INVAL, 0);

        fh4.nfs_fh4_val = blob->bytes;
        fh4.nfs_fh4_len = blob->len - sizeof(*blob);

        st = pxy_getattrs_impl(opctx->creds, exp_hdl, &fh4, &attr);
        if (FSAL_IS_ERROR(st))
                return st;
        
        ph = pxy_alloc_handle(exp_hdl, &fh4, &attr);
        if(!ph)
                return fsalstat(ERR_FSAL_FAULT, 0);

        *handle = &ph->obj;
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
pxy_get_dynamic_info(struct fsal_export *exp_hdl,
                     const struct req_op_context *opctx,
                     fsal_dynamicfsinfo_t *infop)
{
        int rc;
        int opcnt = 0;

#define FSAL_FSINFO_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_FSINFO_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_FSINFO_NB_OP_ALLOC];
        GETATTR4resok *atok;
        char fattr_blob[48]; /* 6 values, 8 bytes each */
        struct fsal_obj_handle *obj;
        struct pxy_obj_handle *ph;

        if(!exp_hdl || !infop || !opctx)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        obj = exp_hdl->exp_entry->proot_handle;
        ph = container_of(obj, struct pxy_obj_handle, obj);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        atok = pxy_fill_getattr_reply(resoparray+opcnt,
                                      fattr_blob, sizeof(fattr_blob));
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, pxy_bitmap_fsinfo);

        rc = pxy_nfsv4_call(exp_hdl, opctx->creds, opcnt, 
                            argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        if(nfs4_Fattr_To_fsinfo(infop, &atok->obj_attributes) != NFS4_OK)
                return fsalstat(ERR_FSAL_INVAL, 0);

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Convert of-the-wire digest into unique 'handle' which
 * can be used to identify the object */
fsal_status_t
pxy_extract_handle(struct fsal_export *exp_hdl,
		   fsal_digesttype_t in_type,
		   struct gsh_buffdesc *fh_desc)
{
        struct pxy_handle_blob *pxyblob;
        size_t fh_size;

        if( !fh_desc || !fh_desc->addr)
                return fsalstat(ERR_FSAL_FAULT, EINVAL);

        pxyblob = (struct pxy_handle_blob *)fh_desc->addr;
        fh_size = pxyblob->len;
#ifdef _HANDLE_MAPPING
        if((in_type == FSAL_DIGEST_NFSV2) || (in_type == FSAL_DIGEST_NFSV3))
                fh_size = sizeof(nfs23_map_handle_t);
#endif
        if(in_type == FSAL_DIGEST_NFSV2) {
                if(fh_desc->len < fh_size) {
                        LogMajor(COMPONENT_FSAL,
                                 "V2 size too small for handle.  should be %lu, got %lu",
                                 fh_size, fh_desc->len);
                        return fsalstat(ERR_FSAL_SERVERFAULT, 0);
                }
        } else if(fh_desc->len != fh_size) {
                LogMajor(COMPONENT_FSAL,
                         "Size mismatch for handle.  should be %lu, got %lu",
                         fh_size, fh_desc->len);
                return fsalstat(ERR_FSAL_SERVERFAULT, 0);
        }
#ifdef _HANDLE_MAPPING
        if((in_type == FSAL_DIGEST_NFSV2) || (in_type == FSAL_DIGEST_NFSV3)) {
                nfs23_map_handle_t *h23 = (nfs23_map_handle_t*)fh_desc->addr;

                if(h23->type != PXY_HANDLE_MAPPED) 
                        return fsalstat(ERR_FSAL_STALE, ESTALE);

                /* As long as HandleMap_GetFH copies nfs23 handle into
                 * the key before lookup I can get away with using
                 * the same buffer for input and output */
                if(HandleMap_GetFH(h23, fh_desc) != HANDLEMAP_SUCCESS)
                        return fsalstat(ERR_FSAL_STALE, 0);
                fh_size = fh_desc->len;
        }
#endif

        fh_desc->len = fh_size;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
