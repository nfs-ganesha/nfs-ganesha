/* These are definitions of RPC functions that are private within the RPCAL */

#ifndef GANESHA_RPCAL_H
#define GANESHA_RPCAL_H

#include "rpc.h"

#ifdef _USE_TIRPC
#include "RW_Lock.h"
#endif

#ifdef _USE_TIRPC
extern int Xprt_register(SVCXPRT * xprt);
extern void Xprt_unregister(SVCXPRT * xprt);
extern void FreeXprt(SVCXPRT *xprt);
#endif                          /* _USE_TIRPC */

#ifdef _USE_TIRPC
/* public data : */
extern rw_lock_t Svc_fd_lock;
#endif

#ifdef _HAVE_GSSAPI
int copy_svc_authgss(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
#endif                          /* _HAVE_GSSAPI */

#endif /* GANESHA_RPCAL_H */
