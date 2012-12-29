/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_RPC_H
#define GANESHA_RPC_H

#include <stdbool.h>
#include <rpc/xdr_inline.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/svc_dg.h>
#include <rpc/clnt.h>

#ifdef _HAVE_GSSAPI
#include <rpc/auth_gss.h>
#endif
#include <rpc/svc_auth.h>
#include <rpc/svc_rqst.h>
#include <rpc/rpc_dplx.h>
#include <rpc/gss_internal.h> /* XXX */

#include "nfs_req_queue.h"
#include "HashTable.h"
#include "log.h"

#define NFS_LOOKAHEAD_NONE      0x0000
#define NFS_LOOKAHEAD_MOUNT     0x0001
#define NFS_LOOKAHEAD_OPEN      0x0002
#define NFS_LOOKAHEAD_CLOSE     0x0004
#define NFS_LOOKAHEAD_READ      0x0008
#define NFS_LOOKAHEAD_WRITE     0x0010
#define NFS_LOOKAHEAD_COMMIT    0x0020
#define NFS_LOOKAHEAD_CREATE    0x0040
#define NFS_LOOKAHEAD_REMOVE    0x0080
#define NFS_LOOKAHEAD_RENAME    0x0100
#define NFS_LOOKAHEAD_LOCK      0x0200 /* !_U types */
#define NFS_LOOKAHEAD_READDIR       0x0400
#define NFS_LOOKAHEAD_LAYOUTCOMMIT  0x0040
#define NFS_LOOKAHEAD_SETATTR  0x0080
#define NFS_LOOKAHEAD_SETCLIENTID  0x0100
#define NFS_LOOKAHEAD_SETCLIENTID_CONFIRM  0x0200
#define NFS_LOOKAHEAD_LOOKUP  0x0400
/* ... */

struct nfs_request_lookahead {
    uint32_t flags;
    uint16_t read;
    uint16_t write;
};

#define NFS_LOOKAHEAD_HIGH_LATENCY(lkhd) \
    (((lkhd).flags & (NFS_LOOKAHEAD_READ | \
                      NFS_LOOKAHEAD_WRITE | \
                      NFS_LOOKAHEAD_COMMIT | \
                      NFS_LOOKAHEAD_LAYOUTCOMMIT | \
                      NFS_LOOKAHEAD_READDIR)))

void socket_setoptions(int socketFd);

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

typedef struct sockaddr_storage sockaddr_t;

#define SOCK_NAME_MAX 128

extern void Svc_dg_soft_destroy(SVCXPRT * xprt);
extern struct netconfig *getnetconfigent(const char *netid);
extern void freenetconfigent(struct netconfig *);
extern SVCXPRT *Svc_vc_create(int, u_int, u_int);
extern SVCXPRT *Svc_dg_create(int, u_int, u_int);

#ifdef _SOLARIS
#define _authenticate __authenticate
#endif

#ifdef _HAVE_GSSAPI

typedef struct nfs_krb5_param__
{
  char keytab[MAXPATHLEN];
  char ccache_dir[MAXPATHLEN];
    /* XXX representation of GSSAPI service, independent of
     * GSSRPC or TI-RPC global variables.  Initially, use it just
     * for callbacks. */
  struct {
      char principal[MAXPATHLEN];
      gss_name_t gss_name;
  } svc;
  bool_t active_krb5;
} nfs_krb5_parameter_t;

void log_sperror_gss(char *outmsg, OM_uint32 maj_stat, OM_uint32 min_stat);
const char *str_gc_proc(rpc_gss_proc_t gc_proc);

#endif                          /* _HAVE_GSSAPI */

/* Private data associated with a new TI-RPC (TCP) SVCXPRT (transport
 * connection), ie, xprt->xp_u1.
 */
#define XPRT_PRIVATE_FLAG_NONE       0x0000
#define XPRT_PRIVATE_FLAG_LOCKED     0x0001
#define XPRT_PRIVATE_FLAG_INCREQ     0x0002
#define XPRT_PRIVATE_FLAG_DECREQ     0x0004
#define XPRT_PRIVATE_FLAG_DECODING   0x0008
#define XPRT_PRIVATE_FLAG_STALLED    0x0010 /* ie, -on stallq- */

struct drc;
typedef struct gsh_xprt_private
{
    SVCXPRT *xprt;
    uint32_t flags;
    uint32_t req_cnt; /* outstanding requests counter */
    struct drc *drc; /* TCP DRC */
    struct glist_head stallq;
} gsh_xprt_private_t;

static inline gsh_xprt_private_t *
alloc_gsh_xprt_private(SVCXPRT *xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = gsh_malloc(sizeof(gsh_xprt_private_t));

    xu->xprt = xprt;
    xu->flags = XPRT_PRIVATE_FLAG_NONE;
    xu->req_cnt = 0;
    xu->drc = NULL;

    return (xu);
}

void nfs_dupreq_put_drc(SVCXPRT *xprt, struct drc *drc, uint32_t flags);

#ifndef DRC_FLAG_RELEASE
#define DRC_FLAG_RELEASE       0x0030
#endif

static inline void
free_gsh_xprt_private(SVCXPRT *xprt)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    if (xu->drc)
        nfs_dupreq_put_drc(xprt, xu->drc, DRC_FLAG_RELEASE);
    gsh_free(xu);
}

static inline bool
gsh_xprt_ref(SVCXPRT *xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    uint32_t req_cnt;
    bool refd;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    if (flags & XPRT_PRIVATE_FLAG_INCREQ)
        req_cnt = ++(xu->req_cnt);

    refd = SVC_REF(xprt,  SVC_REF_FLAG_LOCKED);
    /* !LOCKED */

    LogFullDebug(COMPONENT_DISPATCH,
                 "xprt %p req_cnt=%u",
                 xprt, req_cnt);

    return (refd);
}

static inline void
gsh_xprt_unref(SVCXPRT *xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    uint32_t refcnt, req_cnt;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    if (flags & XPRT_PRIVATE_FLAG_DECREQ)
        req_cnt = --(xu->req_cnt);
    else
        req_cnt = xu->req_cnt;

    if (flags & XPRT_PRIVATE_FLAG_DECODING)
        if (xu->flags & XPRT_PRIVATE_FLAG_DECODING)
            xu->flags &= ~XPRT_PRIVATE_FLAG_DECODING;

    /* idempotent:  note expected value after release */
    refcnt = xprt->xp_refcnt - 1;

    /* release xprt refcnt */
    SVC_RELEASE(xprt, SVC_RELEASE_FLAG_LOCKED);
    /* !LOCKED */

    LogFullDebug(COMPONENT_RPC,
                 "xprt %p req_cnt=%u refcnt=%u",
                 xprt, req_cnt, refcnt);
    return;
}

static inline bool
gsh_xprt_decoder_guard(SVCXPRT *xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    bool rslt = FALSE;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    if (xu->flags & XPRT_PRIVATE_FLAG_DECODING) {
        LogDebug(COMPONENT_DISPATCH,
                 "guard failed: flag %s", "XPRT_PRIVATE_FLAG_DECODING");
        goto unlock;
    }

    if (xu->flags & XPRT_PRIVATE_FLAG_STALLED) {
        LogDebug(COMPONENT_DISPATCH,
                 "guard failed: flag %s", "XPRT_PRIVATE_FLAG_STALLED");
        goto unlock;
    }
    
    xu->flags |= XPRT_PRIVATE_FLAG_DECODING;
    rslt = TRUE;

unlock:
    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_unlock(&xprt->xp_lock);

    return (rslt);
}

static inline void
gsh_xprt_clear_flag(SVCXPRT * xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    if (flags & XPRT_PRIVATE_FLAG_DECODING)
        if (xu->flags & XPRT_PRIVATE_FLAG_DECODING)
            xu->flags &= ~XPRT_PRIVATE_FLAG_DECODING;

    /* unconditional */
    pthread_mutex_unlock(&xprt->xp_lock);

    return;
}

#define DISP_SLOCK(x) do { \
    if (! slocked) { \
        rpc_dplx_slx((x)); \
        slocked = TRUE; \
      }\
    } while (0);

#define DISP_SUNLOCK(x) do { \
    if (slocked) { \
        rpc_dplx_sux((x)); \
        slocked = FALSE; \
      }\
    } while (0);

#define DISP_RLOCK(x) do { \
    if (! rlocked) { \
        rpc_dplx_rlx((x)); \
        rlocked = TRUE; \
      }\
    } while (0);

#define DISP_RUNLOCK(x) do { \
    if (rlocked) { \
        rpc_dplx_rux((x)); \
        rlocked = FALSE; \
      }\
    } while (0);

extern int copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt);
extern int sprint_sockaddr(sockaddr_t *addr, char *buf, int len);
extern int sprint_sockip(sockaddr_t *addr, char *buf, int len);
extern SVCXPRT *Svcxprt_copy(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
extern SVCXPRT *Svcxprt_copycreate();

extern const char *xprt_type_to_str(xprt_type_t type);

typedef enum _ignore_port
{
	IGNORE_PORT,
	CHECK_PORT
} ignore_port_t;

extern int cmp_sockaddr(sockaddr_t *addr_1,
                        sockaddr_t *addr_2,
                        ignore_port_t ignore_port);
extern int sockaddr_cmpf(sockaddr_t *addr1,
			 sockaddr_t *addr2,
			 ignore_port_t ignore_port);
extern unsigned long hash_sockaddr(sockaddr_t *addr,
                                   ignore_port_t ignore_port);

extern in_addr_t get_in_addr(sockaddr_t *addr);
extern int get_port(sockaddr_t *addr);

/* Returns an EAI value, accepts only numeric strings */
extern int ipstring_to_sockaddr(const char *str, sockaddr_t *addr);

extern CLIENT *Clnt_create(char *host,
                           unsigned long prog,
                           unsigned long vers,
                           char *proto);

void Clnt_destroy(CLIENT *clnt);

#endif /* GANESHA_RPC_H */
