/* @(#)svc_udp.c	2.2 88/07/29 4.0 RPCSRC */
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

#include "log_macros.h"

/*
 * svc_udp.c,
 * Server side for UDP/IP based RPC.  (Does some caching in the hopes of
 * achieving execute-at-most-once semantics.)
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../rpcal.h"
#include <errno.h>
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#define SVCAUTH_WRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_wrap))(auth, xdrs, xfunc, xwhere))

#define SVCAUTH_UNWRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_unwrap))(auth, xdrs, xfunc, xwhere))

#define SVCAUTH_DESTROY(auth) \
     ((*((auth)->svc_ah_ops->svc_ah_destroy))(auth))

#define rpc_buffer(xprt) ((xprt)->xp_p1)
#ifndef MAX
#define MAX(a, b)     ((a > b) ? a : b)
#endif

static bool_t Svcudp_recv(SVCXPRT *, struct rpc_msg *);
static bool_t Svcudp_reply(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat Svcudp_stat(SVCXPRT *);
static bool_t Svcudp_getargs(SVCXPRT *, xdrproc_t, void *);
static bool_t Svcudp_freeargs(SVCXPRT *, xdrproc_t, void *);
static void Svcudp_destroy(SVCXPRT *);

static struct xp_ops Svcudp_op = {
  Svcudp_recv,
  Svcudp_stat,
  Svcudp_getargs,
  Svcudp_reply,
  Svcudp_freeargs,
  Svcudp_destroy
};

/*
 * kept in xprt->xp_p2
 */
struct svcudp_data
{
  u_int su_iosz;                /* byte size of send.recv buffer */
  uint32_t su_xid;              /* transaction id */
  XDR su_xdrs;                  /* XDR handle */
  char su_verfbody[MAX_AUTH_BYTES];     /* verifier body */
  void *su_cache;               /* cached data, NULL if no cache */
};
#define	su_data(xprt)	((struct svcudp_data *)(xprt->xp_p2))

/*
 * Usage:
 *	xprt = svcudp_create(sock);
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svcudp_create
 * binds it to an arbitrary port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 * Once *xprt is initialized, it is registered as a transporter;
 * see (svc.h, xprt_register).
 * The routines returns NULL if a problem occurred.
 */
SVCXPRT *Svcudp_bufcreate(register int sock, u_int sendsz, u_int recvsz)
{
  bool_t madesock = FALSE;
  register SVCXPRT *xprt;
  register struct svcudp_data *su;
  struct sockaddr_in addr;
  int len = sizeof(struct sockaddr_in);

  if(sock == RPC_ANYSOCK)
    {
      if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        {
          perror("svcudp_create: socket creation problem");
          return ((SVCXPRT *) NULL);
        }
      madesock = TRUE;
    }
  memset((char *)&addr, 0, sizeof(addr));
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  addr.sin_len = sizeof(addr);
#endif
  addr.sin_family = AF_INET;
  if(bindresvport(sock, &addr))
    {
      addr.sin_port = 0;
      (void)bind(sock, (struct sockaddr *)&addr, len);
    }
  if(getsockname(sock, (struct sockaddr *)&addr, &len) != 0)
    {
      perror("svcudp_create - cannot getsockname");
      if(madesock)
        (void)close(sock);
      return ((SVCXPRT *) NULL);
    }
  xprt = (SVCXPRT *) mem_alloc(sizeof(SVCXPRT));
  if(xprt == NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svcudp_create: out of memory");
      return (NULL);
    }
  su = (struct svcudp_data *)mem_alloc(sizeof(*su));
  if(su == NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svcudp_create: out of memory");
      return (NULL);
    }
  su->su_iosz = ((MAX(sendsz, recvsz) + 3) / 4) * 4;
  if((rpc_buffer(xprt) = mem_alloc(su->su_iosz)) == NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svcudp_create: out of memory");
      return (NULL);
    }
  xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_DECODE);
  su->su_cache = NULL;
  xprt->xp_p2 = (caddr_t) su;
  xprt->xp_auth = NULL;
  xprt->xp_verf.oa_base = su->su_verfbody;
  xprt->xp_ops = &Svcudp_op;
  xprt->xp_port = ntohs(addr.sin_port);
  xprt->XP_SOCK = sock;
  Xprt_register(xprt);
  return (xprt);
}

SVCXPRT *Svcudp_create(int sock)
{

  return (Svcudp_bufcreate(sock, UDPMSGSIZE, UDPMSGSIZE));
}

static enum xprt_stat Svcudp_stat(SVCXPRT * xprt)
{

  return (XPRT_IDLE);
}

static bool_t Svcudp_recv(register SVCXPRT * xprt, struct rpc_msg *msg)
{
  struct msghdr dummy;
  struct iovec dummy_iov[1];
  register struct svcudp_data *su = su_data(xprt);
  register XDR *xdrs = &(su->su_xdrs);
  register int rlen;
  char *reply;
  uint32_t replylen;

 again:
  memset((char *)&dummy, 0, sizeof(dummy));
  dummy_iov[0].iov_base = rpc_buffer(xprt);
  dummy_iov[0].iov_len = (int)su->su_iosz;
  dummy.msg_iov = dummy_iov;
  dummy.msg_iovlen = 1;
  dummy.msg_namelen = xprt->xp_laddrlen = sizeof(struct sockaddr_in);
  dummy.msg_name = (char *)&xprt->xp_laddr;
  rlen = recvmsg(xprt->XP_SOCK, &dummy, MSG_PEEK);
  if(rlen == -1)
    {
      if(errno == EINTR)
        goto again;
      else
        return (FALSE);
    }

  xprt->xp_addrlen = sizeof(struct sockaddr_in);
  rlen = recvfrom(xprt->XP_SOCK, rpc_buffer(xprt), (int)su->su_iosz,
                  0, (struct sockaddr *)&(xprt->xp_raddr), &(xprt->xp_addrlen));
  if(rlen == -1 && errno == EINTR)
    goto again;
  if(rlen < (int)4 * sizeof(uint32_t))
    return (FALSE);
  xdrs->x_op = XDR_DECODE;
  XDR_SETPOS(xdrs, 0);
  if(!xdr_callmsg(xdrs, msg))
    return (FALSE);
  su->su_xid = msg->rm_xid;

  return (TRUE);
}

static bool_t Svcudp_reply(register SVCXPRT * xprt, struct rpc_msg *msg)
{
  register struct svcudp_data *su = su_data(xprt);
  register XDR *xdrs = &(su->su_xdrs);
  register int slen;
  register bool_t stat = FALSE;

  xdrproc_t xdr_results;
  caddr_t xdr_location;
  bool_t has_args;

  if(msg->rm_reply.rp_stat == MSG_ACCEPTED && msg->rm_reply.rp_acpt.ar_stat == SUCCESS)
    {
      has_args = TRUE;
      xdr_results = msg->acpted_rply.ar_results.proc;
      xdr_location = msg->acpted_rply.ar_results.where;

      msg->acpted_rply.ar_results.proc = xdr_void;
      msg->acpted_rply.ar_results.where = NULL;
    }
  else
    has_args = FALSE;

  xdrs->x_op = XDR_ENCODE;
  XDR_SETPOS(xdrs, 0);
  msg->rm_xid = su->su_xid;
  if(xdr_replymsg(xdrs, msg) &&
     (!has_args || (SVCAUTH_WRAP(xprt->xp_auth, xdrs, xdr_results, xdr_location))))
    {
      slen = (int)XDR_GETPOS(xdrs);
      if(sendto(xprt->XP_SOCK, rpc_buffer(xprt), slen, 0,
                (struct sockaddr *)&(xprt->xp_raddr), xprt->xp_addrlen) == slen)
        {
          stat = TRUE;
        }
    }
  return (stat);
}

static bool_t Svcudp_getargs(SVCXPRT * xprt, xdrproc_t xdr_args, void *args_ptr)
{
  if(!SVCAUTH_UNWRAP(xprt->xp_auth, &(su_data(xprt)->su_xdrs), xdr_args, args_ptr))
    {
      (void)Svcudp_freeargs(xprt, xdr_args, args_ptr);
      return FALSE;
    }
  return TRUE;
}

static bool_t Svcudp_freeargs(SVCXPRT * xprt, xdrproc_t xdr_args, void *args_ptr)
{
  register XDR *xdrs = &(su_data(xprt)->su_xdrs);

  xdrs->x_op = XDR_FREE;
  return ((*xdr_args) (xdrs, args_ptr));
}

void Svcudp_soft_destroy(register SVCXPRT * xprt)
{
  register struct svcudp_data * su = xprt->xp_p2 ;

  mem_free(rpc_buffer(xprt), su->su_iosz);
  mem_free((caddr_t) su, sizeof(struct svcudp_data));
  mem_free((caddr_t) xprt, sizeof(SVCXPRT));
}

static void Svcudp_destroy(register SVCXPRT * xprt)
{
  register struct svcudp_data *su = su_data(xprt);

  Xprt_unregister(xprt);
  if(xprt->XP_SOCK != -1)
    (void)close(xprt->XP_SOCK);
  xprt->XP_SOCK = -1;
  if(xprt->xp_auth != NULL)
    {
      SVCAUTH_DESTROY(xprt->xp_auth);
      xprt->xp_auth = NULL;
    }
  XDR_DESTROY(&(su->su_xdrs));
  mem_free(rpc_buffer(xprt), su->su_iosz);
  mem_free((caddr_t) su, sizeof(struct svcudp_data));
  mem_free((caddr_t) xprt, sizeof(SVCXPRT));
}
