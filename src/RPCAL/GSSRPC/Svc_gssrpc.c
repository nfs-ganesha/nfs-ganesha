/* @(#)svc.c	2.4 88/08/11 4.0 RPCSRC; from 1.44 88/02/08 SMI */
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include "../rpcal.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>


extern int gssrpc_svc_fdset_init;

#define NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	1024    /* this size is excessive */

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * apropriate procedure.
 */
static struct svc_callout
{
  struct svc_callout *sc_next;
  rpcprog_t sc_prog;
  rpcprog_t sc_vers;
  void (*sc_dispatch) ();
} *svc_head;

static struct svc_callout *Svc_find(rpcprog_t, rpcvers_t, struct svc_callout **);

static void Svc_do_xprt(SVCXPRT * xprt);

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
void Xprt_register(SVCXPRT * xprt)
{
  register int sock = xprt->XP_SOCK;

#ifdef FD_SETSIZE
  if(gssrpc_svc_fdset_init == 0)
    {
      FD_ZERO(&Svc_fdset);
      gssrpc_svc_fdset_init++;
    }
  if(sock < FD_SETSIZE)
    {
      Xports[sock] = xprt;
      FD_SET(sock, &Svc_fdset);
    }
#else
  if(sock < NOFILE)
    {
      Xports[sock] = xprt;
      svc_fds |= (1 << sock);
    }
#endif                          /* def FD_SETSIZE */
  if(sock > svc_maxfd)
    svc_maxfd = sock;
}

/*
 * De-activate a transport handle. 
 */
void Xprt_unregister(SVCXPRT * xprt)
{
  register int sock = xprt->XP_SOCK;

#ifdef FD_SETSIZE
  if((sock < FD_SETSIZE) && (Xports[sock] == xprt))
    {
      Xports[sock] = (SVCXPRT *) 0;
      FD_CLR(sock, &Svc_fdset);
    }
#else
  if((sock < NOFILE) && (Xports[sock] == xprt))
    {
      Xports[sock] = (SVCXPRT *) 0;
      svc_fds &= ~(1 << sock);
    }
#endif                          /* def FD_SETSIZE */
  if(svc_maxfd <= sock)
    {
      while((svc_maxfd > 0) && Xports[svc_maxfd] == 0)
        svc_maxfd--;
    }
}

/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
bool_t
Svc_register(SVCXPRT * xprt,
             rpcprog_t prog, rpcvers_t vers, void (*dispatch) (), int protocol)
{
  struct svc_callout *prev;
  register struct svc_callout *s;

  if((s = Svc_find(prog, vers, &prev)) != NULL_SVC)
    {
      if(s->sc_dispatch == dispatch)
        goto pmap_it;           /* he is registering another xptr */
      return (FALSE);
    }
  s = (struct svc_callout *)mem_alloc(sizeof(struct svc_callout));
  if(s == (struct svc_callout *)0)
    {
      return (FALSE);
    }
  s->sc_prog = prog;
  s->sc_vers = vers;
  s->sc_dispatch = dispatch;
  s->sc_next = svc_head;
  svc_head = s;
 pmap_it:
  /* now register the information with the local binder service */
  if(protocol)
    {
      return (pmap_set(prog, vers, protocol, xprt->xp_port));
    }
  return (TRUE);
}

/*
 * Remove a service program from the callout list.
 */
void Svc_unregister(rpcprog_t prog, rpcvers_t vers)
{
  struct svc_callout *prev;
  register struct svc_callout *s;

  if((s = Svc_find(prog, vers, &prev)) == NULL_SVC)
    return;
  if(prev == NULL_SVC)
    {
      svc_head = s->sc_next;
    }
  else
    {
      prev->sc_next = s->sc_next;
    }
  s->sc_next = NULL_SVC;
  mem_free((char *)s, (u_int) sizeof(struct svc_callout));
  /* now unregister the information with the local binder service */
  (void)pmap_unset(prog, vers);
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *Svc_find(rpcprog_t prog,
                                    rpcvers_t vers, struct svc_callout **prev)
{
  register struct svc_callout *s, *p;

  p = NULL_SVC;
  for(s = svc_head; s != NULL_SVC; s = s->sc_next)
    {
      if((s->sc_prog == prog) && (s->sc_vers == vers))
        goto done;
      p = s;
    }
 done:
  *prev = p;
  return (s);
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
bool_t Svc_sendreply(register SVCXPRT * xprt, xdrproc_t xdr_results, caddr_t xdr_location)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = SUCCESS;
  rply.acpted_rply.ar_results.where = xdr_location;
  rply.acpted_rply.ar_results.proc = xdr_results;
  return (SVC_REPLY(xprt, &rply));
}

/*
 * No procedure error reply
 */
void Svcerr_noproc(register SVCXPRT * xprt)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = PROC_UNAVAIL;
  SVC_REPLY(xprt, &rply);
}

/*
 * Can't decode args error reply
 */
void Svcerr_decode(register SVCXPRT * xprt)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = GARBAGE_ARGS;
  SVC_REPLY(xprt, &rply);
}

/*
 * Some system error
 */
void Svcerr_systemerr(register SVCXPRT * xprt)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = SYSTEM_ERR;
  SVC_REPLY(xprt, &rply);
}

/*
 * Authentication error reply
 */
void Svcerr_auth(SVCXPRT * xprt, enum auth_stat why)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_DENIED;
  rply.rjcted_rply.rj_stat = AUTH_ERROR;
  rply.rjcted_rply.rj_why = why;
  SVC_REPLY(xprt, &rply);
}

/*
 * Auth too weak error reply
 */
void Svcerr_weakauth(SVCXPRT * xprt)
{

  svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void Svcerr_noprog(register SVCXPRT * xprt)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = PROG_UNAVAIL;
  SVC_REPLY(xprt, &rply);
}

/*
 * Program version mismatch error reply
 */
void Svcerr_progvers(register SVCXPRT * xprt, rpcvers_t low_vers, rpcvers_t high_vers)
{
  struct rpc_msg rply;

  rply.rm_direction = REPLY;
  rply.rm_reply.rp_stat = MSG_ACCEPTED;
  rply.acpted_rply.ar_verf = xprt->xp_verf;
  rply.acpted_rply.ar_stat = PROG_MISMATCH;
  rply.acpted_rply.ar_vers.low = low_vers;
  rply.acpted_rply.ar_vers.high = high_vers;
  SVC_REPLY(xprt, &rply);
}
