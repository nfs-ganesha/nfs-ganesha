/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

/*
 * svc_auth_nodes.c, Server-side rpc authenticator interface,
 * *WITHOUT* DES authentication.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "rpcal.h"

/*
 * Server side authenticators are called from authenticate by
 * using the client auth struct flavor field to index into svcauthsw.
 * The server auth flavors must implement a routine that looks  
 * like: 
 * 
 *	enum auth_stat
 *	flavorx_auth(rqst, msg)
 *		register struct svc_req *rqst; 
 *		register struct rpc_msg *msg;
 *
 */

enum auth_stat Gssrpc__svcauth_none(register struct svc_req *rqst,
                                    register struct rpc_msg *msg, bool_t * no_dispatch);

enum auth_stat Gssrpc__svcauth_unix(register struct svc_req *rqst,
                                    register struct rpc_msg *msg, bool_t * no_dispatch);

enum auth_stat Gssrpc__svcauth_gss(register struct svc_req *rqst,
                                   register struct rpc_msg *msg, bool_t * no_dispatch);

#define Gssrpc__svcauth_short Gssrpc__svcauth_unix

static struct svcauthsw_type
{
  u_int flavor;
  enum auth_stat (*authenticator) (struct svc_req *, struct rpc_msg *, bool_t *);
} svcauthsw[] =
{
#ifdef AUTH_GSSAPI
  {
  AUTH_GSSAPI, Gssrpc__svcauth_gss},    /* AUTH_GSSAPI */
#endif
  {
  AUTH_NONE, Gssrpc__svcauth_none},     /* AUTH_NONE */
#if 0
  {
  AUTH_GSSAPI_COMPAT, gssrpc__svcauth_gssapi},  /* AUTH_GSSAPI_COMPAT */
#endif
  {
  AUTH_UNIX, Gssrpc__svcauth_unix},     /* AUTH_UNIX */
  {
  AUTH_SHORT, Gssrpc__svcauth_short},   /* AUTH_SHORT */
  {
  RPCSEC_GSS, Gssrpc__svcauth_gss}      /* RPCSEC_GSS */
};

static int svcauthnum = sizeof(svcauthsw) / sizeof(struct svcauthsw_type);

/*
 * The call rpc message, msg has been obtained from the wire.  The msg contains
 * the raw form of credentials and verifiers.  authenticate returns AUTH_OK
 * if the msg is successfully authenticated.  If AUTH_OK then the routine also
 * does the following things:
 * set rqst->rq_xprt->verf to the appropriate response verifier;
 * sets rqst->rq_client_cred to the "cooked" form of the credentials.
 *
 * NB: rqst->rq_cxprt->verf must be pre-alloctaed;
 * its length is set appropriately.
 *
 * The caller still owns and is responsible for msg->u.cmb.cred and
 * msg->u.cmb.verf.  The authentication system retains ownership of
 * rqst->rq_client_cred, the cooked credentials.
 */
enum auth_stat
Rpcsecgss__authenticate(register struct svc_req *rqst,
                        struct rpc_msg *msg, bool_t * no_dispatch)
{
  register int cred_flavor, i;

  rqst->rq_cred = msg->rm_call.cb_cred;
  rqst->rq_xprt->xp_verf.oa_flavor = 0;
  rqst->rq_xprt->xp_verf.oa_length = 0;
  cred_flavor = rqst->rq_cred.oa_flavor;
  *no_dispatch = FALSE;
  for(i = 0; i < svcauthnum; i++)
    {
      if(cred_flavor == svcauthsw[i].flavor && svcauthsw[i].authenticator != NULL)
        {
          return ((*(svcauthsw[i].authenticator)) (rqst, msg, no_dispatch));
        }
    }

  return (AUTH_REJECTEDCRED);
}
