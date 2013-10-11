/* These are definitions of RPC functions that are private within the RPCAL */

#ifndef GANESHA_RPCAL_H
#define GANESHA_RPCAL_H

#include "ganesha_rpc.h"

#define xp_free(x) if(x) gsh_free(x)

#ifdef _HAVE_GSSAPI

extern int copy_svc_authgss(SVCXPRT * xprt_copy, SVCXPRT * xprt_orig);
extern void free_svc_authgss(SVCXPRT * xprt);
extern int sprint_ctx(char *buff, unsigned char *ctx, int len);

#endif				/* _HAVE_GSSAPI */

#endif				/* GANESHA_RPCAL_H */
