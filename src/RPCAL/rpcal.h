/* These are definitions of RPC functions that are private within the RPCAL */

#ifndef GANESHA_RPCAL_H
#define GANESHA_RPCAL_H

#include "rpc.h"

#ifdef _USE_TIRPC
#include "RW_Lock.h"
#endif

extern int Xprt_register(SVCXPRT * xprt);
extern void Xprt_unregister(SVCXPRT * xprt);

extern void FreeXprt(SVCXPRT *xprt);

#ifdef _DEBUG_MEMLEAKS
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
extern int copy_svc_authgss(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
extern void free_svc_authgss(SVCXPRT *xprt);
#endif                          /* _HAVE_GSSAPI */

#endif /* GANESHA_RPCAL_H */
