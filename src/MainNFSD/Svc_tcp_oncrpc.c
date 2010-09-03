#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
typedef unsigned int u_int32_t;
#define _RPC_SVC_MT_H
#endif

#include   <sys/types.h>
#include   <sys/poll.h>

#include   <stdio.h>
#include   <stdlib.h>
#include   <string.h>
#include   <unistd.h>
#include   "stuff_alloc.h"

#ifdef _USE_GSSRPC
#include   <gssrpc/rpc.h>
#include   <gssrpc/auth.h>
#include   <gssrpc/svc.h>
#else
#include   <rpc/rpc.h>
#include   <rpc/auth.h>
#include   <rpc/svc.h>
#endif

#include   <sys/socket.h>
#include   <sys/poll.h>
#include   <errno.h>
#include   <pthread.h>

#include   "log_macros.h"

#ifndef MAX
#define MAX(a, b)     ((a > b) ? a : b)
#endif

void Xprt_register(SVCXPRT * xprt);
void Xprt_unregister(SVCXPRT * xprt);

void socket_setoptions(int socketFd);

bool_t svcauth_wrap_dummy(XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr);

pthread_mutex_t mutex_cond_xprt[FD_SETSIZE];
pthread_cond_t condvar_xprt[FD_SETSIZE];
int etat_xprt[FD_SETSIZE];

#define SVCAUTH_WRAP(auth, xdrs, xfunc, xwhere) svcauth_wrap_dummy( xdrs, xfunc, xwhere)
#define SVCAUTH_UNWRAP(auth, xdrs, xfunc, xwhere) svcauth_wrap_dummy( xdrs, xfunc, xwhere)

/*
 * Ops vector for TCP/IP based rpc service handle
 */
static bool_t Svctcp_recv();
static enum xprt_stat Svctcp_stat();
static bool_t Svctcp_getargs();
static bool_t Svctcp_reply();
static bool_t Svctcp_freeargs();
static void Svctcp_destroy();

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
static bool_t Rendezvous_request();
static enum xprt_stat Rendezvous_stat();

static struct xp_ops Svctcp_rendezvous_op = {
  Rendezvous_request,
  Rendezvous_stat,
  (bool_t(*)())abort,
  (bool_t(*)())abort,
  (bool_t(*)())abort,
  Svctcp_destroy
};

int Readtcp(), Writetcp();
static SVCXPRT *Makefd_xprt();

struct tcp_rendezvous
{                               /* kept in xprt->xp_p1 */
  u_int sendsize;
  u_int recvsize;
};

struct tcp_conn
{                               /* kept in xprt->xp_p1 */
  enum xprt_stat strm_stat;
  u_long x_id;
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
/*  */
SVCXPRT *Svctcp_create(register int sock, u_int sendsize, u_int recvsize)
{
  bool_t madesock = FALSE;
  register SVCXPRT *xprt;
  register struct tcp_rendezvous *r;
  struct sockaddr_in addr;
  unsigned long len = sizeof(struct sockaddr_in);

  if(sock == RPC_ANYSOCK)
    {
      if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        {
          perror("svctcp_.c - udp socket creation problem");
          return ((SVCXPRT *) NULL);
        }
      madesock = TRUE;
    }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  if(bindresvport(sock, &addr))
    {
      addr.sin_port = 0;
      (void)bind(sock, (struct sockaddr *)&addr, len);
    }
  if((getsockname(sock, (struct sockaddr *)&addr, (socklen_t *) & len) != 0) ||
     (listen(sock, SOMAXCONN) != 0))
    {
      perror("svctcp_.c - cannot getsockname or listen");
      if(madesock)
        (void)close(sock);
      return ((SVCXPRT *) NULL);
    }
  r = (struct tcp_rendezvous *)Mem_Alloc(sizeof(*r));
  if(r == NULL)
    {
      return (NULL);
    }
  r->sendsize = sendsize;
  r->recvsize = recvsize;
  xprt = (SVCXPRT *) Mem_Alloc(sizeof(SVCXPRT));
  if(xprt == NULL)
    {
      return (NULL);
    }
  xprt->xp_p2 = NULL;
  xprt->xp_p1 = (caddr_t) r;
  xprt->xp_verf = _null_auth;
  xprt->xp_ops = &Svctcp_rendezvous_op;
  xprt->xp_port = ntohs(addr.sin_port);
#ifdef _FREEBSD
  xprt->xp_fd = sock;
#else
  xprt->xp_sock = sock;
#endif
  Xprt_register(xprt);
  return (xprt);
}

/*
 * Like svtcp_create(), except the routine takes any *open* UNIX file
 * descriptor as its first input.
 */
/*  */
SVCXPRT *Svcfd_create(int fd, u_int sendsize, u_int recvsize)
{

  return (Makefd_xprt(fd, sendsize, recvsize));
}

static SVCXPRT *Makefd_xprt(int fd, u_int sendsize, u_int recvsize)
{
  register SVCXPRT *xprt;
  register struct tcp_conn *cd;

  xprt = (SVCXPRT *) Mem_Alloc(sizeof(SVCXPRT));
  if(xprt == (SVCXPRT *) NULL)
    {
      goto done;
    }
  cd = (struct tcp_conn *)Mem_Alloc(sizeof(struct tcp_conn));
  if(cd == (struct tcp_conn *)NULL)
    {
      Mem_Free((char *)xprt);
      xprt = (SVCXPRT *) NULL;
      goto done;
    }
  cd->strm_stat = XPRT_IDLE;
  xdrrec_create(&(cd->xdrs), sendsize, recvsize, (caddr_t) xprt, Readtcp, Writetcp);
  xprt->xp_p2 = NULL;
  xprt->xp_p1 = (caddr_t) cd;
  xprt->xp_verf.oa_base = cd->verf_body;
  xprt->xp_addrlen = 0;
  xprt->xp_ops = &Svctcp_op;    /* truely deals with calls */
  xprt->xp_port = 0;            /* this is a connection, not a rendezvouser */
#ifdef _FREEBSD
  xprt->xp_fd = fd;
#else
  xprt->xp_sock = fd;
#endif
  Xprt_register(xprt);
 done:
  return (xprt);
}

void print_xdrrec_fbtbc(char *tag, SVCXPRT * xprt)
{
  register struct tcp_conn *cd;

  typedef struct rec_strm_local
  {
    caddr_t tcp_handle;
    caddr_t the_buffer;
    /*
     * out-goung bits
     */
    int (*writeit) (caddr_t, caddr_t, int);
    caddr_t out_base;           /* output buffer (points to frag header) */
    caddr_t out_finger;         /* next output position */
    caddr_t out_boundry;        /* data cannot up to this address */
    u_int32_t *frag_header;     /* beginning of current fragment */
    bool_t frag_sent;           /* true if buffer sent in middle of record */
    /*
     * in-coming bits
     */
    int (*readit) (caddr_t, caddr_t, int);
    u_long in_size;             /* fixed size of the input buffer */
    caddr_t in_base;
    caddr_t in_finger;          /* location of next byte to be had */
    caddr_t in_boundry;         /* can read up to this location */
    long fbtbc;                 /* fragment bytes to be consumed */
    bool_t last_frag;
    u_int sendsize;
    u_int recvsize;
  } RECSTREAM_local;

  RECSTREAM_local *rstrm;

  cd = (struct tcp_conn *)xprt->xp_p1;
  rstrm = (RECSTREAM_local *) & (cd->xdrs.x_private);
  LogFullDebug(COMPONENT_DISPATCH, "=====> tag=%s xprt=%p  fbtbc=%ld", tag, xprt, rstrm->fbtbc);
}

void *rpc_tcp_socket_manager_thread(void *Arg);
extern fd_set Svc_fdset;

static bool_t Rendezvous_request(register SVCXPRT * xprt)
{
  int sock;
  struct tcp_rendezvous *r;
  struct sockaddr_in addr;
  unsigned long len;

  pthread_attr_t attr_thr;
  pthread_t sockmgr_thrid;
  int rc = 0;

  r = (struct tcp_rendezvous *)xprt->xp_p1;
 again:
  len = sizeof(struct sockaddr_in);
#ifdef _FREEBSD
  if((sock = accept(xprt->xp_fd, (struct sockaddr *)&addr,
#else
  if((sock = accept(xprt->xp_sock, (struct sockaddr *)&addr,
#endif
                    (socklen_t *) & len)) < 0)
    {
      if(errno == EINTR)
        goto again;
      return (FALSE);
    }

  /* socket_setoptions( sock ) ; */

  /*
   * make a new transporter (re-uses xprt)
   */
  xprt = Makefd_xprt(sock, r->sendsize, r->recvsize);
  memcpy(&(xprt->xp_raddr), &addr, sizeof(addr));
  xprt->xp_addrlen = len;

  /* Spawns a new thread to handle the connection */
  pthread_attr_init(&attr_thr);
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_DETACHED);      /* If not, the conn mgr will be "defunct" threads */

#ifdef _FREEBSD
  FD_CLR(xprt->xp_fd, &Svc_fdset);
  if(pthread_cond_init(&condvar_xprt[xprt->xp_fd], NULL) != 0)
    return FALSE;

  if(pthread_mutex_init(&mutex_cond_xprt[xprt->xp_fd], NULL) != 0)
    return FALSE;
  etat_xprt[xprt->xp_fd] = 0;

  if((rc =
      pthread_create(&sockmgr_thrid, &attr_thr, rpc_tcp_socket_manager_thread,
                     (void *)((unsigned long)xprt->xp_fd))) != 0)
    return FALSE;
#else
  FD_CLR(xprt->xp_sock, &Svc_fdset);
  if(pthread_cond_init(&condvar_xprt[xprt->xp_sock], NULL) != 0)
    return FALSE;

  if(pthread_mutex_init(&mutex_cond_xprt[xprt->xp_sock], NULL) != 0)
    return FALSE;
  etat_xprt[xprt->xp_sock] = 0;

  if((rc =
      pthread_create(&sockmgr_thrid, &attr_thr, rpc_tcp_socket_manager_thread,
                     (void *)((unsigned long)xprt->xp_sock))) != 0)
    return FALSE;

#endif

  return (FALSE);               /* there is never an rpc msg to be processed */
}

static enum xprt_stat Rendezvous_stat()
{
  return (XPRT_IDLE);
}

static void Svctcp_destroy(register SVCXPRT * xprt)
{
  register struct tcp_conn *cd = (struct tcp_conn *)xprt->xp_p1;

  Xprt_unregister(xprt);
#ifdef _FREEBSD
  (void)close(xprt->xp_fd);
#else
  (void)close(xprt->xp_sock);
#endif

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

  Mem_Free((caddr_t) cd);
  Mem_Free((caddr_t) xprt);
}

/*
 * reads data from the tcp conection.
 * any error is fatal and the connection is closed.
 * (And a read of zero bytes is a half closed stream => error.)
 * All read operations timeout after 35 seconds.  A timeout is
 * fatal for the connection.
 */
int Readtcp(register SVCXPRT * xprt, caddr_t buf, register int len)
{
  /* LogFullDebug(COMPONENT_DISPATCH, "Readtcp: xprt=%p len=%d", xprt, len ) ; */
  /* print_xdrrec_fbtbc( "Readtcp",  xprt ) ;           */

#ifdef _FREEBSD
  register int sock = xprt->xp_fd;
#else
  register int sock = xprt->xp_sock;
#endif
  int milliseconds = 35 * 1000;
  struct pollfd pollfd;

  do
    {
      pollfd.fd = sock;
      pollfd.events = POLLIN;
      switch (poll(&pollfd, 1, milliseconds))
        {
        case -1:
          if(errno == EINTR)
            {
              continue;
            }
          goto fatal_err;

        case 0:
          continue;

        default:
          break;
        }
    }
  while((pollfd.revents & POLLIN) == 0);

  len = read(sock, buf, len);

  if(len > 0)
    {
      /* LogFullDebug(COMPONENT_DISPATCH, "Readtcp (end): xprt=%p len=%d", xprt, len ) ; */
      /* print_xdrrec_fbtbc( "Readtcp (end)",  xprt ) ;           */

      return (len);
    }
  else
    {
      if(len < 0 && errno == EAGAIN)
        {
          len = 0;
          return len;
        }
      else
        goto fatal_err;
    }
 fatal_err:
  ((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
  return (-1);
}

/*
 * writes data to the tcp connection.
 * Any error is fatal and the connection is closed.
 */
int Writetcp(register SVCXPRT * xprt, caddr_t buf, int len)
{
  register int i, cnt;

  /* LogFullDebug(COMPONENT_DISPATCH, "Writetcp: xprt=%p len=%d", xprt, len ) ; */
  /* print_xdrrec_fbtbc( "WriteTcp", xprt ) ; */

  for(cnt = len; cnt > 0; cnt -= i, buf += i)
    {
#ifdef _FREEBSD
      i = write(xprt->xp_fd, buf, cnt);
#else
      i = write(xprt->xp_sock, buf, cnt);
#endif
      if(i < 0)
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

static bool_t Svctcp_getargs(SVCXPRT * xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{

  return (SVCAUTH_UNWRAP(NULL,
                         &(((struct tcp_conn *)(xprt->xp_p1))->xdrs),
                         xdr_args, args_ptr));
}

static bool_t Svctcp_freeargs(SVCXPRT * xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
  register XDR *xdrs = &(((struct tcp_conn *)(xprt->xp_p1))->xdrs);

  xdrs->x_op = XDR_FREE;
  return ((*xdr_args) (xdrs, args_ptr));
}

static bool_t Svctcp_reply(SVCXPRT * xprt, register struct rpc_msg *msg)
{
  register struct tcp_conn *cd = (struct tcp_conn *)(xprt->xp_p1);
  register XDR *xdrs = &(cd->xdrs);
  xdrproc_t xdr_proc;
  caddr_t xdr_where;

  xdrs->x_op = XDR_ENCODE;
  msg->rm_xid = cd->x_id;

  if(msg->rm_reply.rp_stat == MSG_ACCEPTED && msg->rm_reply.rp_acpt.ar_stat == SUCCESS)
    {
      xdr_proc = msg->acpted_rply.ar_results.proc;
      xdr_where = msg->acpted_rply.ar_results.where;
      msg->acpted_rply.ar_results.proc = (xdrproc_t) xdr_void;
      msg->acpted_rply.ar_results.where = NULL;

      if(!xdr_replymsg(xdrs, msg) || !SVCAUTH_WRAP(NULL, xdrs, xdr_proc, xdr_where))
        return (FALSE);
    }
  else if(!xdr_replymsg(xdrs, msg))
    {
      return (FALSE);
    }

  (void)xdrrec_endofrecord(xdrs, TRUE);

  return (TRUE);
}
