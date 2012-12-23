/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_RPC_H
#define GANESHA_RPC_H

#include "config.h"

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
#include "abstract_mem.h"
#include "nlm_list.h"
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

typedef struct sockaddr_storage sockaddr_t;

#define SOCK_NAME_MAX 128

extern struct netconfig *getnetconfigent(const char *netid);
extern void freenetconfigent(struct netconfig *);

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
  bool active_krb5;
} nfs_krb5_parameter_t;

void log_sperror_gss(char *outmsg, OM_uint32 maj_stat, OM_uint32 min_stat);
const char *str_gc_proc(rpc_gss_proc_t gc_proc);

/* Private data associated with a new TI-RPC (TCP) SVCXPRT (transport
 * connection), ie, xprt->xp_u1.
 */
#define XPRT_PRIVATE_FLAG_NONE       0x0000
#define XPRT_PRIVATE_FLAG_DESTROYED  0x0001 /* forward destroy */
#define XPRT_PRIVATE_FLAG_LOCKED     0x0002
#define XPRT_PRIVATE_FLAG_REF        0x0004
#define XPRT_PRIVATE_FLAG_INCREQ     0x0008
#define XPRT_PRIVATE_FLAG_DECODING   0x0010
#define XPRT_PRIVATE_FLAG_STALLED    0x0020 /* ie, -on stallq- */

struct drc;
typedef struct gsh_xprt_private
{
    SVCXPRT *xprt;
    uint32_t flags;
    uint32_t refcnt;
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

    if (flags & XPRT_PRIVATE_FLAG_REF)
        xu->refcnt = 1;
    else
        xu->refcnt = 0;

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

static inline void
gsh_xprt_ref(SVCXPRT *xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    uint32_t refcnt, req_cnt;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    refcnt = ++(xu->refcnt);
    if (flags & XPRT_PRIVATE_FLAG_INCREQ)
        req_cnt = ++(xu->req_cnt);
    else
        req_cnt = xu->req_cnt;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_unlock(&xprt->xp_lock);

    LogFullDebug(COMPONENT_DISPATCH,
                 "xprt %p refcnt=%u req_cnt=%u",
                 xprt, refcnt, req_cnt);
}

static inline bool
gsh_xprt_decoder_guard_ref(SVCXPRT *xprt, uint32_t flags)
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
    ++(xu->refcnt);
    rslt = TRUE;

unlock:
    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_unlock(&xprt->xp_lock);

    return (rslt);
}

static inline void
gsh_xprt_unref(SVCXPRT * xprt, uint32_t flags)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    uint32_t refcnt;

    if (! (flags & XPRT_PRIVATE_FLAG_LOCKED))
        pthread_mutex_lock(&xprt->xp_lock);

    refcnt = --(xu->refcnt);

    if (flags & XPRT_PRIVATE_FLAG_DECODING)
        if (xu->flags & XPRT_PRIVATE_FLAG_DECODING)
            xu->flags &= ~XPRT_PRIVATE_FLAG_DECODING;

    /* finalize */
    if (refcnt == 0) {
        if (xu->flags & XPRT_PRIVATE_FLAG_DESTROYED) {
            pthread_mutex_unlock(&xprt->xp_lock);
            SVC_DESTROY(xprt);
            goto out;
        }
    }

    /* unconditional */
    pthread_mutex_unlock(&xprt->xp_lock);

out:
    return;
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

static inline void
gsh_xprt_destroy(SVCXPRT *xprt)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;

    pthread_mutex_lock(&xprt->xp_lock);
    xu->flags |= XPRT_PRIVATE_FLAG_DESTROYED;

    gsh_xprt_unref(xprt, XPRT_PRIVATE_FLAG_LOCKED);
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

/* Serialized clnt_create and clnt_destroy */
CLIENT *gsh_clnt_create(char *, unsigned long, unsigned long, char *);
void gsh_clnt_destroy(CLIENT *);

#endif /* GANESHA_RPC_H */
