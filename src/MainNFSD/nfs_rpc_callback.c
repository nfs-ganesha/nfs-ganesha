/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "nfs4.h"
#include "gssd.h"
#include "gss_util.h"
#include "krb5_util.h"
#include "sal_data.h"

/**
 *
 * \file nfs_rpc_callback.c
 * \author Matt Benjamin and Lee Dobryden
 * \brief RPC callback dispatch package
 *
 * \section DESCRIPTION
 *
 * This module implements APIs for submission, and dispatch of NFSv4.0
 * and (soon) NFSv4.1 format callbacks.
 *
 */

static pool_t *rpc_call_pool;

/* tried to re-use host_name in nfs_main.c, but linker became confused.
 * this is a quick fix */
static char host_name[MAXHOSTNAMELEN];


static inline void
nfs_rpc_cb_init_ccache(const char *ccache)
{
    int code = 0;

    mkdir(ccache, 700); /* XXX */
    ccachesearch[0] = nfs_param.krb5_param.ccache_dir;

    code = gssd_refresh_krb5_machine_credential(
        host_name, NULL, nfs_param.krb5_param.svc.principal);
    if (code) {
        LogWarn(COMPONENT_INIT, "gssd_refresh_krb5_machine_credential "
                 "failed (%d:%d)", code, errno);
        goto out;
    }

out:
    return;
}

/*
 * Initialize subsystem
 */
void nfs_rpc_cb_pkginit(void)
{
    char localmachine[MAXHOSTNAMELEN];

    /* Create a pool of rpc_call_t */
    rpc_call_pool = pool_init("RPC Call Pool",
                              sizeof(rpc_call_t),
                              pool_basic_substrate,
                              NULL,
                              nfs_rpc_init_call,
                              NULL);
    if(!(rpc_call_pool)) {
        LogCrit(COMPONENT_INIT,
                "Error while allocating rpc call pool");
        LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
        Fatal();
    }

    /* get host name */
    if(gethostname(localmachine, sizeof(localmachine)) != 0) {
        LogCrit(COMPONENT_INIT, "Failed to get local host name");
    }
    else
        strlcpy(host_name, localmachine, MAXHOSTNAMELEN);

    /* ccache */
    nfs_rpc_cb_init_ccache(nfs_param.krb5_param.ccache_dir);

    /* sanity check GSSAPI */
    if (gssd_check_mechs() != 0)
        LogCrit(COMPONENT_INIT,  "sanity check: gssd_check_mechs() failed");

    return;
}

/*
 * Shutdown subsystem
 */
void nfs_rpc_cb_pkgshutdown(void)
{
    /* do nothing */
}

/* XXXX this is automatically redundant, but in fact upstream TI-RPC is
 * not up-to-date with RFC 5665, will fix (Matt)
 *
 * (c) 2012, Linux Box Corp
 */

nc_type nfs_netid_to_nc(const char *netid)
{
    if (! strncmp(netid, netid_nc_table[_NC_TCP].netid,
                  netid_nc_table[_NC_TCP].netid_len))
        return(_NC_TCP);

    if (! strncmp(netid, netid_nc_table[_NC_TCP6].netid,
                  netid_nc_table[_NC_TCP6].netid_len))
        return(_NC_TCP6);

    if (! strncmp(netid, netid_nc_table[_NC_UDP].netid,
                  netid_nc_table[_NC_UDP].netid_len))
        return (_NC_UDP);

    if (! strncmp(netid, netid_nc_table[_NC_UDP6].netid,
                  netid_nc_table[_NC_UDP6].netid_len))
        return (_NC_UDP6);

    if (! strncmp(netid, netid_nc_table[_NC_RDMA].netid,
                  netid_nc_table[_NC_RDMA].netid_len))
        return (_NC_RDMA);

    if (! strncmp(netid, netid_nc_table[_NC_RDMA6].netid,
                 netid_nc_table[_NC_RDMA6].netid_len))
        return (_NC_RDMA6);

    if (! strncmp(netid, netid_nc_table[_NC_SCTP].netid,
                  netid_nc_table[_NC_SCTP].netid_len))
        return (_NC_SCTP);

    if (! strncmp(netid, netid_nc_table[_NC_SCTP6].netid,
                  netid_nc_table[_NC_SCTP6].netid_len))
        return (_NC_SCTP6);

    return (_NC_ERR);
}

static inline void
setup_client_saddr(nfs_client_id_t *pclientid, const char *uaddr)
{
    char addr_buf[SOCK_NAME_MAX];
    uint32_t bytes[11];
    int code;

    memset(&pclientid->cid_cb.cid_addr.ss, 0, sizeof(struct sockaddr_storage));

    switch (pclientid->cid_cb.cid_addr.nc) {
    case _NC_TCP:
    case _NC_RDMA:
    case _NC_SCTP:
    case _NC_UDP:
        /* IPv4 (ws inspired) */
        if (sscanf(uaddr, "%u.%u.%u.%u.%u.%u",
                   &bytes[1], &bytes[2], &bytes[3], &bytes[4],
                   &bytes[5], &bytes[6]) == 6) {
            struct sockaddr_in *sin =
                (struct sockaddr_in *) &pclientid->cid_cb.cid_addr.ss;
            snprintf(addr_buf, SOCK_NAME_MAX, "%u.%u.%u.%u",
                     bytes[1], bytes[2],
                     bytes[3], bytes[4]);
            sin->sin_family = AF_INET;
            sin->sin_port = htons((bytes[5]<<8) | bytes[6]);
            code = inet_pton(AF_INET, addr_buf, &sin->sin_addr);
            if (code != 1)
                LogWarn(COMPONENT_NFS_CB, "inet_pton failed (%d %s)",
                         code, addr_buf);
            else
                LogDebug(COMPONENT_NFS_CB, "client callback addr:port %s:%d",
                         addr_buf, ntohs(sin->sin_port));
        }
        break;
    case _NC_TCP6:
    case _NC_RDMA6:
    case _NC_SCTP6:
    case _NC_UDP6:
        /* IPv6 (ws inspired) */
        if (sscanf(uaddr, "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x.%u.%u",
                   &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5],
                   &bytes[6], &bytes[7], &bytes[8],
                   &bytes[9], &bytes[10]) == 10) {
            struct sockaddr_in6 *sin6 =
                (struct sockaddr_in6 *) &pclientid->cid_cb.cid_addr.ss;
            snprintf(addr_buf, SOCK_NAME_MAX,
                     "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x",
                     bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                     bytes[6], bytes[7], bytes[8]);
            code = inet_pton(AF_INET6, addr_buf, &sin6->sin6_addr);
            sin6->sin6_port = htons((bytes[9]<<8) | bytes[10]);
            sin6->sin6_family = AF_INET6;
            if (code != 1)
                LogWarn(COMPONENT_NFS_CB, "inet_pton failed (%d %s)",
                         code, addr_buf);
            else
                LogDebug(COMPONENT_NFS_CB, "client callback addr:port %s:%d",
                         addr_buf, ntohs(sin6->sin6_port));
        }
        break;
    default:
        /* unknown netid */
        break;
    };
}

void nfs_set_client_location(nfs_client_id_t *pclientid, const clientaddr4 *addr4)
{
    pclientid->cid_cb.cid_addr.nc = nfs_netid_to_nc(addr4->r_netid);
    strlcpy(pclientid->cid_cb.cid_client_r_addr, addr4->r_addr,
            SOCK_NAME_MAX);
    setup_client_saddr(pclientid, pclientid->cid_cb.cid_client_r_addr);
}

static inline int32_t
nfs_clid_connected_socket(nfs_client_id_t *pclientid, int *fd, int *proto)
{
    struct sockaddr_in *sin;
    struct sockaddr_in6 *sin6;
    int nfd, code = 0;

    *fd = 0;
    *proto = -1;

    switch (pclientid->cid_cb.cid_addr.ss.ss_family) {
    case AF_INET:
        sin = (struct sockaddr_in *) &pclientid->cid_cb.cid_addr.ss;
        switch (pclientid->cid_cb.cid_addr.nc) {
        case _NC_TCP:
            nfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
            *proto = IPPROTO_TCP;
            break;
        case _NC_UDP:
            nfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
            *proto = IPPROTO_UDP;
            break;
        default:
            code = EINVAL;
            goto out;
            break;
        }

        code = connect(nfd, (struct sockaddr *) sin,
                       sizeof(struct sockaddr_in));
        if (code == -1) {
            LogWarn(COMPONENT_NFS_CB, "connect fail errno %d", errno);
            goto out;
        }
        *fd = nfd;
        break;
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) &pclientid->cid_cb.cid_addr.ss;
        switch (pclientid->cid_cb.cid_addr.nc) {
        case _NC_TCP6:
            nfd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
            *proto = IPPROTO_TCP;
            break;
        case _NC_UDP6:
            nfd = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            *proto = IPPROTO_UDP;
            break;
        default:
            code = EINVAL;
            goto out;
            break;
        }
        code = connect(nfd, (struct sockaddr *) sin6,
                       sizeof(struct sockaddr_in6));
        if (code == -1) {
            LogWarn(COMPONENT_NFS_CB, "connect fail errno %d", errno);
            goto out;
        }
        *fd = nfd;
        break;
    default:
        code = EINVAL;
        break;
    }

out:
    return (code);
}

/* end refactorable RPC code */

static inline bool_t
supported_auth_flavor(int flavor)
{
    bool_t code = FALSE;

    switch (flavor) {
    case RPCSEC_GSS:
    case AUTH_SYS:
    case AUTH_NONE:
        code = TRUE;
        break;
    default:        
        break;
    };

    return (code);
}

/* from kerberos source, gssapi_krb5.c (Umich) */
gss_OID_desc krb5oid =
   {9, "\052\206\110\206\367\022\001\002\002"};

static inline char *
format_host_principal(rpc_call_channel_t *chan, char *buf, size_t len)
{
    char addr_buf[SOCK_NAME_MAX];
    const char *host = NULL;
    char *princ = NULL;

    switch (chan->type) {
    case RPC_CHAN_V40:
    {
        nfs_client_id_t *pclientid = chan->nvu.v40.pclientid;
        switch (chan->nvu.v40.pclientid->cid_cb.cid_addr.ss.ss_family) {
        case AF_INET:
        {
            struct sockaddr_in *sin = (struct sockaddr_in *) &pclientid->cid_cb.cid_addr.ss;
            host = inet_ntop(AF_INET, &sin->sin_addr, addr_buf,
                             INET_ADDRSTRLEN);
            break;
        }
        case AF_INET6:
        {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &pclientid->cid_cb.cid_addr.ss;
            host = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf,
                             INET6_ADDRSTRLEN);
            break;
        }
        default:
            break;
        }
        break;
    }
    case RPC_CHAN_V41:
        /* XXX implement */
        goto out;
        break;
    }

    if (host) {
        snprintf(buf, len, "nfs@%s", host);
        princ = buf; 
    }

out:
    return (princ);
}

static inline void
nfs_rpc_callback_setup_gss(rpc_call_channel_t *chan,
                           nfs_client_cred_t *cred)
{
    AUTH *auth;
    char hprinc[MAXPATHLEN];
    int32_t code = 0;

    assert(cred->flavor == RPCSEC_GSS);

    /* MUST RFC 3530bis, section 3.3.3 */
    chan->gss_sec.svc = cred->auth_union.auth_gss.svc;
    chan->gss_sec.qop = cred->auth_union.auth_gss.qop;

    /* the GSSAPI k5 mech needs to find an unexpired credential
     * for nfs/hostname in an accessible k5ccache */
    code = gssd_refresh_krb5_machine_credential(
        host_name, NULL, nfs_param.krb5_param.svc.principal);
    if (code) {
        LogWarn(COMPONENT_NFS_CB, "gssd_refresh_krb5_machine_credential "
                 "failed (%d:%d)", code, errno);
        goto out;
    }

    if (! format_host_principal(chan, hprinc, MAXPATHLEN)) {
        LogCrit(COMPONENT_NFS_CB, "format_host_principal failed");
        goto out;
    }

    chan->gss_sec.cred = GSS_C_NO_CREDENTIAL;
    chan->gss_sec.req_flags = 0;

    if (chan->gss_sec.svc != RPCSEC_GSS_SVC_NONE) {
        /* no more lipkey, spkm3 */
        chan->gss_sec.mech = (gss_OID) &krb5oid;
        chan->gss_sec.req_flags = GSS_C_MUTUAL_FLAG; /* XXX */
        auth = 
            authgss_create_default(chan->clnt,
                                   hprinc,
                                   &chan->gss_sec);
        /* authgss_create and authgss_create_default return NULL on
         * failure, don't assign NULL to clnt->cl_auth */
        if (auth)
            chan->clnt->cl_auth = auth;
    }

out:
    return;
}

static inline bool_t
nfs_rpc_callback_seccreate(rpc_call_channel_t *chan)
{
    nfs_client_cred_t *credential = NULL;
    bool_t code = TRUE;
    AUTH *auth = NULL;

    switch (chan->type) {
    case RPC_CHAN_V40:
        assert(&chan->nvu.v40.pclientid);
        credential = &chan->nvu.v40.pclientid->cid_credential;
        break;
    case RPC_CHAN_V41:
        /* XXX implement */
        goto out;
        break;
    }
    
    switch (credential->flavor) {
    case RPCSEC_GSS:
        nfs_rpc_callback_setup_gss(chan, credential);
        break;
    case AUTH_SYS:
        auth = authunix_create_default();
        /* XXX see above */
        if (auth)
            chan->clnt->cl_auth = auth;
        break;
    case AUTH_NONE:
        break;
    default:
        /* XXX prevented by forward check */
        break;
    }

out:
    return (code);
}

/* Create a channel for a new clientid (v4) or session, optionally
 * connecting it */
int nfs_rpc_create_chan_v40(nfs_client_id_t *pclientid,
                            uint32_t flags)
{
    struct netbuf raddr;
    int fd, proto, code = 0;
    rpc_call_channel_t *chan = &pclientid->cid_cb.cb_u.v40.cb_chan;



    assert(! chan->clnt);

    /* XXX we MUST error RFC 3530bis, sec. 3.3.3 */
    if (! supported_auth_flavor(pclientid->cid_credential.flavor)) {
        code = EINVAL;
        goto out;
    }

    chan->type = RPC_CHAN_V40;
    chan->nvu.v40.pclientid = pclientid;

    code = nfs_clid_connected_socket(pclientid, &fd, &proto);
    if (code) {
        LogWarn(COMPONENT_NFS_CB,
                 "Failed creating socket");
        goto out;
    }

    raddr.buf = &pclientid->cid_cb.cid_addr.ss;

    switch (proto) {
    case IPPROTO_TCP:
        raddr.maxlen = raddr.len = sizeof(struct sockaddr_in);
        chan->clnt = clnt_vc_create(fd,
                                    &raddr,
                                    pclientid->cid_cb.cid_program,
                                    1 /* Errata ID: 2291 */,
                                    0, 0);
        break;
    case IPPROTO_UDP:
        raddr.maxlen = raddr.len = sizeof(struct sockaddr_in6);
        chan->clnt = clnt_dg_create(fd,
                                    &raddr,
                                    pclientid->cid_cb.cid_program,
                                    1 /* Errata ID: 2291 */,
                                    0, 0);
        break;
    default:
        break;
    }

    if (! chan->clnt) {
        code = EINVAL;
        goto out;
    }

    /* channel protection */
    if (! nfs_rpc_callback_seccreate(chan)) {
        /* XXX */
        code = EINVAL;
    }

out:
    return (code);
}

rpc_call_channel_t *
nfs_rpc_get_chan(nfs_client_id_t *pclientid, uint32_t flags)
{
    /* XXX v41 */
    rpc_call_channel_t *chan = &pclientid->cid_cb.cb_u.v40.cb_chan;

    if (! chan->clnt) {
        nfs_rpc_create_chan_v40(pclientid, flags);
    }

    return (chan);
}

/* Dispose a channel. */
void nfs_rpc_destroy_chan(rpc_call_channel_t *chan)
{
    assert(chan);

    /* XXX lock, wait for outstanding calls, etc */

    switch (chan->type) {
    case RPC_CHAN_V40:
        /* channel has a dedicated RPC client */
        if (chan->clnt) {
            /* clean up auth, if any */
            if (chan->clnt->cl_auth)
                AUTH_DESTROY(chan->clnt->cl_auth);
            /* destroy it */
            clnt_destroy(chan->clnt);
            chan->clnt = NULL;
        }
        break;
    case RPC_CHAN_V41:
        /* XXX channel is shared */
        break;
    }

    chan->clnt = NULL;
    chan->last_called = 0;
}

/*
 * Call the NFSv4 client's CB_NULL procedure.
 */
enum clnt_stat
rpc_cb_null(rpc_call_channel_t *chan, struct timeval timeout)
{
    enum clnt_stat stat = RPC_SUCCESS;

    /* XXX TI-RPC does the signal masking */
    pthread_mutex_lock(&chan->mtx);

    if (! chan->clnt) {
        stat = RPC_INTR;
        goto unlock;
    }

    stat = clnt_call(chan->clnt, CB_NULL,
                     (xdrproc_t) xdr_void, NULL,
                     (xdrproc_t) xdr_void, NULL, timeout);

    /* If a call fails, we have to assume path down, or equally fatal
     * error.  We may need back-off. */
    if (stat != RPC_SUCCESS) {
        if (chan->clnt) {
            clnt_destroy(chan->clnt);
            chan->clnt = NULL;
        }
    }

unlock:
    pthread_mutex_unlock(&chan->mtx);
    
    return (stat);
}

static inline void free_argop(nfs_cb_argop4 *op)
{
    gsh_free(op);
}

static inline void free_resop(nfs_cb_resop4 *op)
{
    gsh_free(op);
}

rpc_call_t *alloc_rpc_call()
{
    rpc_call_t *call;

    call = pool_alloc(rpc_call_pool, NULL);

    return (call);
}

void free_rpc_call(rpc_call_t *call)
{
    free_argop(call->cbt.v_u.v4.args.argarray.argarray_val);
    free_resop(call->cbt.v_u.v4.res.resarray.resarray_val);
    pool_free(rpc_call_pool, call);
}

static inline void RPC_CALL_HOOK(rpc_call_t *call, rpc_call_hook hook,
                                 void* arg, uint32_t flags)
{
    if (call)
        (void) call->call_hook(call, hook, arg, flags);
}

int32_t
nfs_rpc_submit_call(rpc_call_t *call, uint32_t flags)
{
    int32_t code = 0;
    request_data_t *pnfsreq = NULL;
    rpc_call_channel_t *chan = call->chan;

    assert(chan);

    if (flags & NFS_RPC_CALL_INLINE) {
        code = nfs_rpc_dispatch_call(call, NFS_RPC_CALL_NONE);
    }
    else {
        /* select a thread from the general thread pool */
        int32_t thrd_ix;
        nfs_worker_data_t *worker;

        thrd_ix = nfs_core_select_worker_queue( WORKER_INDEX_ANY );
        worker = &workers_data[thrd_ix];

        LogFullDebug(COMPONENT_NFS_CB,
                     "Use request from Worker Thread #%u's pool, thread has %d "
                     "pending requests",
                     thrd_ix,
                     worker->pending_request_len);

        pnfsreq = nfs_rpc_get_nfsreq(worker, 0 /* flags */);
        pthread_mutex_lock(&call->we.mtx);
        call->states = NFS_CB_CALL_QUEUED;
        pnfsreq->rtype = NFS_CALL;
        pnfsreq->r_u.call = call;
        DispatchWorkNFS(pnfsreq, thrd_ix);
        pthread_mutex_unlock(&call->we.mtx);
    }

    return (code);
}

int32_t
nfs_rpc_dispatch_call(rpc_call_t *call, uint32_t flags)
{
    int code = 0;
    struct timeval CB_TIMEOUT = {15, 0}; /* XXX */

    /* send the call, set states, wake waiters, etc */
    pthread_mutex_lock(&call->we.mtx);

    switch (call->states) {
    case NFS_CB_CALL_DISPATCH:
    case NFS_CB_CALL_FINISHED:
        /* XXX invalid entry states for nfs_rpc_dispatch_call */
        abort();
    }

    call->states = NFS_CB_CALL_DISPATCH;
    pthread_mutex_unlock(&call->we.mtx);

    /* XXX TI-RPC does the signal masking */
    pthread_mutex_lock(&call->chan->mtx);

    if (! call->chan->clnt) {
        call->stat = RPC_INTR;
        goto unlock;
    }

    call->stat = clnt_call(call->chan->clnt,
                           CB_COMPOUND,
                           (xdrproc_t) xdr_CB_COMPOUND4args,
                           &call->cbt.v_u.v4.args,
                           (xdrproc_t) xdr_CB_COMPOUND4res,
                           &call->cbt.v_u.v4.res,
                           CB_TIMEOUT);

    /* If a call fails, we have to assume path down, or equally fatal
     * error.  We may need back-off. */
    if (call->stat != RPC_SUCCESS) {
        if (call->chan->clnt) {
            clnt_destroy(call->chan->clnt);
            call->chan->clnt = NULL;
        }
    }

unlock:
    pthread_mutex_unlock(&call->chan->mtx);

    /* signal waiter(s) */
    pthread_mutex_lock(&call->we.mtx);
    call->states |= NFS_CB_CALL_FINISHED;

    /* broadcast will generally be inexpensive */
    if (call->flags & NFS_RPC_CALL_BROADCAST)
        pthread_cond_broadcast(&call->we.cv);
    pthread_mutex_unlock(&call->we.mtx);

    /* call completion hook */
    RPC_CALL_HOOK(call, RPC_CALL_COMPLETE, NULL, NFS_RPC_CALL_NONE);

    return (code);
}

int32_t
nfs_rpc_abort_call(rpc_call_t *call)
{
    return (0);
}

