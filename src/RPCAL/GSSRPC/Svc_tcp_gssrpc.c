/* @(#)svc_tcp.c	2.2 88/08/01 4.0 RPCSRC */
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
#include   "stuff_alloc.h"

int fridgethr_get( pthread_t * pthrid, void *(*thrfunc)(void*), void * thrarg ) ;

/*
 * svc_tcp.c, Server side for TCP/IP based RPC. 
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * Actually implements two flavors of transporter -
 * a tcp rendezvouser (a listner and connection establisher)
 * and a record/tcp stream.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../rpcal.h"
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
/*extern bool_t abort();
extern errno;
*/

#define SVCAUTH_WRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_wrap))(auth, xdrs, xfunc, xwhere))

#define SVCAUTH_UNWRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_unwrap))(auth, xdrs, xfunc, xwhere))

#define SVCAUTH_DESTROY(auth) \
     ((*((auth)->svc_ah_ops->svc_ah_destroy))(auth))

/*
 * Ops vector for TCP/IP based rpc service handle
 */
static bool_t Svctcp_recv(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat Svctcp_stat(SVCXPRT *);
static bool_t Svctcp_getargs(SVCXPRT *, xdrproc_t, void *);
static bool_t Svctcp_reply(SVCXPRT *, struct rpc_msg *);
static bool_t Svctcp_freeargs(SVCXPRT *, xdrproc_t, void *);
static void Svctcp_destroy(SVCXPRT *);

static struct xp_ops Svctcp_op = {
  Svctcp_recv,
  Svctcp_stat,
  Svctcp_getargs,
  Svctcp_reply,
  Svctcp_freeargs,
  Svctcp_destroy
};

/*
 * Ops vector for TCP/IP rendezvous handler
 */
static bool_t Rendezvous_request(SVCXPRT *, struct rpc_msg *);
static bool_t Abortx(void);
static bool_t Abortx_getargs(SVCXPRT *, xdrproc_t, void *);
static bool_t Abortx_reply(SVCXPRT *, struct rpc_msg *);
static bool_t Abortx_freeargs(SVCXPRT *, xdrproc_t, void *);
static enum xprt_stat Rendezvous_stat(SVCXPRT *);

static struct xp_ops Svctcp_rendezvous_op = {
  Rendezvous_request,
  Rendezvous_stat,
  Abortx_getargs,
  Abortx_reply,
  Abortx_freeargs,
  Svctcp_destroy
};

static int Readtcp(char *, caddr_t, int), Writetcp(char *, caddr_t, int);

struct tcp_rendezvous
{                               /* kept in xprt->xp_p1 */
  u_int sendsize;
  u_int recvsize;
};

struct tcp_conn
{                               /* kept in xprt->xp_p1 */
  enum xprt_stat strm_stat;
  uint32_t x_id;
  XDR xdrs;
  char verf_body[MAX_AUTH_BYTES];
};

/*
 * Usage:
 *	xprt = svctcp_create(sock, send_buf_size, recv_buf_size);
 *
 * Creates, registers, and returns a (rpc) tcp based transporter.
 * Once *xprt is initialized, it is registered as a transporter
 * see (svc.h, xprt_register).  This routine returns
 * a NULL if a problem occurred.
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svctcp_create
 * binds it to an arbitrary port.  The routine then starts a tcp
 * listener on the socket's associated port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 *
 * Since tcp streams do buffered io similar to stdio, the caller can specify
 * how big the send and receive buffers are via the second and third parms;
 * 0 => use the system default.
 */
SVCXPRT *Svctcp_create(register int sock, u_int sendsize, u_int recvsize)
{
  bool_t madesock = FALSE;
  register SVCXPRT *xprt;
  register struct tcp_rendezvous *r;
  struct sockaddr_in addr;
  int len = sizeof(struct sockaddr_in);

  if(sock == RPC_ANYSOCK)
    {
      if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        {
          perror("svctcp_.c - udp socket creation problem");
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
      perror("svc_tcp.c - cannot getsockname");
      if(madesock)
        (void)close(sock);
      return ((SVCXPRT *) NULL);
    }
  if(listen(sock, 2) != 0)
    {
      perror("svctcp_.c - cannot listen");
      if(madesock)
        (void)close(sock);
      return ((SVCXPRT *) NULL);
    }
  r = (struct tcp_rendezvous *)mem_alloc(sizeof(*r));
  if(r == NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svctcp_create: out of memory");
      return (NULL);
    }
  r->sendsize = sendsize;
  r->recvsize = recvsize;
  xprt = (SVCXPRT *) mem_alloc(sizeof(SVCXPRT));
  if(xprt == NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svctcp_create: out of memory");
      return (NULL);
    }
  xprt->xp_p2 = NULL;
  xprt->xp_p1 = (caddr_t) r;
  xprt->xp_auth = NULL;
  /*xprt->xp_verf = null_auth ; */
  xprt->xp_ops = &Svctcp_rendezvous_op;
  xprt->xp_port = ntohs(addr.sin_port);
  xprt->XP_SOCK = sock;
  xprt->xp_laddrlen = 0;
  Xprt_register(xprt);
  return (xprt);
}

/*
 * Like svtcp_create(), except the routine takes any *open* UNIX file
 * descriptor as its first input. It is only called by Rendezvous_request
 * which will use poll() not select() so it doesn't need to call Xprt_register.
 */
static SVCXPRT *Makefd_xprt(int fd, u_int sendsize, u_int recvsize)
{
  register SVCXPRT *xprt;
  register struct tcp_conn *cd;

  xprt = (SVCXPRT *) mem_alloc(sizeof(SVCXPRT));
  if(xprt == (SVCXPRT *) NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svc_tcp: makefd_xprt: out of memory");
      goto done;
    }
  cd = (struct tcp_conn *)mem_alloc(sizeof(struct tcp_conn));
  if(cd == (struct tcp_conn *)NULL)
    {
      LogCrit(COMPONENT_DISPATCH, "svc_tcp: makefd_xprt: out of memory");
      mem_free((char *)xprt, sizeof(SVCXPRT));
      xprt = (SVCXPRT *) NULL;
      goto done;
    }
  cd->strm_stat = XPRT_IDLE;
  xdrrec_create(&(cd->xdrs), sendsize, recvsize, (caddr_t) xprt, Readtcp, Writetcp);
  xprt->xp_p2 = NULL;
  xprt->xp_p1 = (caddr_t) cd;
  xprt->xp_auth = NULL;
  xprt->xp_verf.oa_base = cd->verf_body;
  xprt->xp_addrlen = 0;
  xprt->xp_laddrlen = 0;
  xprt->xp_ops = &Svctcp_op;    /* truely deals with calls */
  xprt->xp_port = 0;            /* this is a connection, not a rendezvouser */
  xprt->XP_SOCK = fd;
  Xports[fd] = xprt;
 done:
  return (xprt);
}

void *rpc_tcp_socket_manager_thread(void *Arg);

static bool_t Rendezvous_request(register SVCXPRT * xprt, struct rpc_msg *msg)
{
  int sock;
  struct tcp_rendezvous *r;
  struct sockaddr_in addr, laddr;
  int len, llen;

  pthread_t sockmgr_thrid;
  int rc = 0;

  r = (struct tcp_rendezvous *)xprt->xp_p1;
 again:
  len = llen = sizeof(struct sockaddr_in);
  if((sock = accept(xprt->XP_SOCK, (struct sockaddr *)&addr, &len)) < 0)
    {
      if(errno == EINTR)
        goto again;
      return (FALSE);
    }
  if(getsockname(sock, &laddr, &llen) < 0)
    return (FALSE);

  /*
   * make a new transporter (re-uses xprt)
   */
  xprt = Makefd_xprt(sock, r->sendsize, r->recvsize);
  xprt->xp_raddr = addr;
  xprt->xp_addrlen = len;
  xprt->xp_laddr = laddr;
  xprt->xp_laddrlen = llen;

  FD_CLR(xprt->XP_SOCK, &Svc_fdset);

  if(pthread_cond_init(&condvar_xprt[xprt->XP_SOCK], NULL) != 0)
    return FALSE;

  if(pthread_mutex_init(&mutex_cond_xprt[xprt->XP_SOCK], NULL) != 0)
    return FALSE;

  if((rc =
      fridgethr_get(&sockmgr_thrid, rpc_tcp_socket_manager_thread,
                     (void *)((unsigned long)xprt->XP_SOCK))) != 0)
    {
      return FALSE;
    }

  return (FALSE);               /* there is never an rpc msg to be processed */
}

static enum xprt_stat Rendezvous_stat(register SVCXPRT * xprt)
{

  return (XPRT_IDLE);
}

static void Svctcp_destroy(register SVCXPRT * xprt)
{
  register struct tcp_conn *cd = (struct tcp_conn *)xprt->xp_p1;

  Xprt_unregister(xprt);
  (void)close(xprt->XP_SOCK);
  if(xprt->xp_port != 0)
    {
      /* a rendezvouser socket */
      xprt->xp_port = 0;
    }
  else
    {
      /* an actual connection socket */
      XDR_DESTROY(&(cd->xdrs));
    }
  if(xprt->xp_auth != NULL)
    {
      SVCAUTH_DESTROY(xprt->xp_auth);
      xprt->xp_auth = NULL;
    }
  mem_free((caddr_t) cd, sizeof(struct tcp_conn));
  mem_free((caddr_t) xprt, sizeof(SVCXPRT));
}

/*
 * All read operations timeout after 35 seconds.
 * A timeout is fatal for the connection.
 */
static struct timeval wait_per_try = { 35, 0 };

/*
 * reads data from the tcp conection.
 * any error is fatal and the connection is closed.
 * (And a read of zero bytes is a half closed stream => error.)
 */
static int Readtcp(char *xprtptr, caddr_t buf, register int len)
{
  register SVCXPRT *xprt = (SVCXPRT *) (void *)xprtptr;
  register int sock = xprt->XP_SOCK;
  struct timeval tout;
#ifdef FD_SETSIZE
  fd_set mask;
  fd_set readfds;

  FD_ZERO(&mask);
  FD_SET(sock, &mask);
#else
  register int mask = 1 << sock;
  int readfds;
#endif                          /* def FD_SETSIZE */
#ifdef FD_SETSIZE
#define loopcond (!FD_ISSET(sock, &readfds))
#else
#define loopcond (readfds != mask)
#endif
  do
    {
      readfds = mask;
      tout = wait_per_try;
      if(select(sock + 1, &readfds, (fd_set *) NULL, (fd_set *) NULL, &tout) <= 0)
        {
          if(errno == EINTR)
            {
              continue;
            }
          goto fatal_err;
        }
    }
  while(loopcond);
  if((len = read(sock, buf, (size_t) len)) > 0)
    {
      return (len);
    }
 fatal_err:
  ((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
  return (-1);
}

/*
 * writes data to the tcp connection.
 * Any error is fatal and the connection is closed.
 */
static int Writetcp(char *xprtptr, caddr_t buf, int len)
{
  register SVCXPRT *xprt = (SVCXPRT *) (void *)xprtptr;
  register int i, cnt;

  for(cnt = len; cnt > 0; cnt -= i, buf += i)
    {
      if((i = write(xprt->XP_SOCK, buf, (size_t) cnt)) < 0)
        {
          ((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
          return (-1);
        }
    }
  return (len);
}

static enum xprt_stat Svctcp_stat(SVCXPRT * xprt)
{
  register struct tcp_conn *cd = (struct tcp_conn *)(xprt->xp_p1);

  if(cd->strm_stat == XPRT_DIED)
    return (XPRT_DIED);
  if(!xdrrec_eof(&(cd->xdrs)))
    return (XPRT_MOREREQS);
  return (XPRT_IDLE);
}

static bool_t Svctcp_recv(SVCXPRT * xprt, register struct rpc_msg *msg)
{
  register struct tcp_conn *cd = (struct tcp_conn *)(xprt->xp_p1);
  register XDR *xdrs = &(cd->xdrs);

  xdrs->x_op = XDR_DECODE;
  (void)xdrrec_skiprecord(xdrs);
  if(xdr_callmsg(xdrs, msg))
    {
      cd->x_id = msg->rm_xid;
      return (TRUE);
    }
  return (FALSE);
}

static bool_t Svctcp_getargs(SVCXPRT * xprt, xdrproc_t xdr_args, void *args_ptr)
{
  if(!SVCAUTH_UNWRAP(xprt->xp_auth,
                     &(((struct tcp_conn *)(xprt->xp_p1))->xdrs), xdr_args, args_ptr))
    {
      (void)Svctcp_freeargs(xprt, xdr_args, args_ptr);
      return FALSE;
    }
  return TRUE;
}

static bool_t Svctcp_freeargs(SVCXPRT * xprt, xdrproc_t xdr_args, void *args_ptr)
{
  register XDR *xdrs = &(((struct tcp_conn *)(xprt->xp_p1))->xdrs);

  xdrs->x_op = XDR_FREE;
  return ((*xdr_args) (xdrs, args_ptr));
}

static bool_t Svctcp_reply(SVCXPRT * xprt, register struct rpc_msg *msg)
{
  register struct tcp_conn *cd = (struct tcp_conn *)(xprt->xp_p1);
  register XDR *xdrs = &(cd->xdrs);
  register bool_t stat;

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
  msg->rm_xid = cd->x_id;
  stat = FALSE;
  if(xdr_replymsg(xdrs, msg) &&
     (!has_args || (SVCAUTH_WRAP(xprt->xp_auth, xdrs, xdr_results, xdr_location))))
    {
      stat = TRUE;
    }
  (void)xdrrec_endofrecord(xdrs, TRUE);
  return (stat);
}

static bool_t Abortx(void)
{
  abort();
  return 1;
}

static bool_t Abortx_getargs(SVCXPRT * xprt, xdrproc_t proc, void *info)
{
  return Abortx();
}

static bool_t Abortx_reply(SVCXPRT * xprt, struct rpc_msg *msg)
{
  return Abortx();
}

static bool_t Abortx_freeargs(SVCXPRT * xprt, xdrproc_t proc, void *info)
{
  return Abortx();
}

/*
 * Create a copy of xprt. Currently, sendsize and recvsize of XDR is
 * hard-coded. This should be fixed.
 */
SVCXPRT *Svcxprt_copycreate()
{
  register SVCXPRT *xprt;
  register struct tcp_conn *cd;

  xprt = (SVCXPRT *) Mem_Alloc(sizeof(SVCXPRT));
  if(xprt == (SVCXPRT *) NULL)
    {
      goto done;
    }

  cd = (struct tcp_conn *) Mem_Alloc(sizeof(struct tcp_conn));
  if(cd == (struct tcp_conn *) NULL)
    {
      Mem_Free((char *)xprt);
      xprt = (SVCXPRT *) NULL;
      goto done;
    }

  cd->strm_stat = XPRT_IDLE;
  xdrrec_create(&(cd->xdrs), 32768, 32768, (caddr_t) xprt, Readtcp, Writetcp);

  xprt->xp_p1 = (caddr_t) cd;
  xprt->xp_verf.oa_base = cd->verf_body;

  done:
    return (xprt);
 }

/*
 * Duplicate xprt from original to copy.
 */
SVCXPRT *Svcxprt_copy(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig)
{
  register struct tcp_conn *cd_copy = (struct tcp_conn *)(xprt_copy->xp_p1);
  register struct tcp_conn *cd_orig = (struct tcp_conn *)(xprt_orig->xp_p1);

  memcpy(xprt_copy, xprt_orig, sizeof(SVCXPRT));
  xprt_copy->xp_p1 = (caddr_t) cd_copy;
  xprt_copy->xp_verf.oa_base = cd_copy->verf_body;

  cd_copy->strm_stat = cd_orig->strm_stat;
  cd_copy->x_id = cd_orig->x_id;
  memcpy(cd_copy->verf_body, cd_orig->verf_body, MAX_AUTH_BYTES);

  return xprt_copy;
}


