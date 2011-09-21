/* These are definitions of RPC functions that are private within the RPCAL */

#ifndef GANESHA_RPCAL_H
#define GANESHA_RPCAL_H

#include "rpc.h"

#ifdef _USE_TIRPC
#include "RW_Lock.h"
#endif

#define xp_free(x) if(x) Mem_Free(x)

extern int Xprt_register(SVCXPRT * xprt);
extern void Xprt_unregister(SVCXPRT * xprt);

extern void FreeXprt(SVCXPRT *xprt);

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)
extern int CheckAuth(SVCAUTH *auth);
#else
#define CheckAuth(ptr)
#endif

#ifdef _USE_TIRPC
/* public data : */
extern rw_lock_t Svc_fd_lock;
extern unsigned int get_tirpc_xid(SVCXPRT *xprt);
#endif

#ifdef _HAVE_GSSAPI
/*
 * from mit-krb5-1.2.1 mechglue/mglueP.h:
 * Array of context IDs typed by mechanism OID
 */
typedef struct gss_union_ctx_id_t
{
  gss_OID mech_type;
  gss_ctx_id_t internal_ctx_id;
} gss_union_ctx_id_desc, *gss_union_ctx_id_t;

extern int copy_svc_authgss(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
extern void free_svc_authgss(SVCXPRT *xprt);
extern int sprint_ctx(char *buff, unsigned char *ctx, int len);
extern int Gss_ctx_Hash_Set(gss_union_ctx_id_desc *pgss_ctx,
                            struct svc_rpc_gss_data *gd);
extern int Gss_ctx_Hash_Del(gss_union_ctx_id_desc *pgss_ctx);
extern void Gss_ctx_Hash_Print(void);
extern int Gss_ctx_Hash_Get(gss_union_ctx_id_desc *pgss_ctx,
                            struct svc_rpc_gss_data *gd,
			    bool_t **established,
			    u_int **seqlast,
			    uint32_t **seqmask);
#endif                          /* _HAVE_GSSAPI */

#endif /* GANESHA_RPCAL_H */
