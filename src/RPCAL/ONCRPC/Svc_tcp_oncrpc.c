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
#include   "oncrpc.h"
#include   <sys/socket.h>
#include   <sys/poll.h>
#include   <errno.h>
#include   <pthread.h>

#include   "log_macros.h"

#include   "nfs_core.h"

#ifndef MAX
#define MAX(a, b)     ((a > b) ? a : b)
#endif

int fridgethr_get( pthread_t * pthrid, void *(*thrfunc)(void*), void * thrarg ) ;
bool_t svcauth_wrap_dummy(XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr);

#ifdef SVCAUTH_WRAP
#undef SVCAUTH_WRAP
#endif
#ifdef SVCAUTH_UNWRAP
#undef SVCAUTH_UNWRAP
#endif
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

struct xp_ops Svctcp_op = {
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

struct xp_ops Svctcp_rendezvous_op = {
  Rendezvous_request,
  Rendezvous_stat,
  (bool_t(*)())abort,
  (bool_t(*)())abort,
  (bool_t(*)())abort,
  Svctcp_destroy
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
  xprt->XP_SOCK = sock;
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
  xprt->XP_SOCK = fd;
  Xports[fd] = xprt;
 done:
  return (xprt);
}

xprt_type_t get_xprt_type(SVCXPRT *xprt)
{
  if(xprt->xp_ops == &Svcudp_op)
    return XPRT_UDP;
  else if (xprt->xp_ops == &Svctcp_op)
    return XPRT_TCP;
  else if (xprt->xp_ops == &Svctcp_rendezvous_op)
    return XPRT_RENDEZVOUS;
  return XPRT_UNKNOWN;
}

void FreeXprt(SVCXPRT *xprt)
{
  if(!xprt)
    {
      LogFullDebug(COMPONENT_RPC,
                   "Attempt to free NULL xprt");
      return;
      
    }

  LogFullDebug(COMPONENT_RPC,
               "FreeXprt xprt=%p", xprt);
  if(xprt->xp_ops == &Svcudp_op)
    {
      xp_free(Su_data(xprt));
      xp_free(rpc_buffer(xprt));
    }
  else if (xprt->xp_ops == &Svctcp_op)
    {
      struct tcp_conn *cd = (struct tcp_conn *)xprt->xp_p1;
      XDR_DESTROY(&(cd->xdrs));
      xp_free(xprt->xp_p1); /* cd */
    }
  else if (xprt->xp_ops == &Svctcp_rendezvous_op)
    {
      xp_free(xprt->xp_p1); /* r */
    }
  else
    {
      LogCrit(COMPONENT_RPC,
              "Attempt to free unknown xprt %p",
              xprt);
      return;
    }

  Mem_Free(xprt);
}

/*
 * Create a copy of xprt. Currently, sendsize and recvsize of XDR is
 * hard-coded. This should be fixed.
 */
SVCXPRT *Svcxprt_copycreate()
{
  return NULL;
 }

/*
 * Duplicate xprt from original to copy.
 */
SVCXPRT *Svcxprt_copy(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig)
{
  if(xprt_copy)
    FreeXprt(xprt_copy);

  xprt_copy = (SVCXPRT *) Mem_Alloc(sizeof(SVCXPRT));
  if(xprt_copy == NULL)
    goto fail_no_xprt;

  LogFullDebug(COMPONENT_RPC,
               "Svcxprt_copy copying xprt_orig=%p to xprt_copy=%p",
               xprt_orig, xprt_copy);
  memcpy(xprt_copy, xprt_orig, sizeof(SVCXPRT));
  xprt_copy->xp_p1 = NULL;
  xprt_copy->xp_p2 = NULL;

  if(xprt_orig->xp_ops == &Svcudp_op)
    {
      if(Su_data(xprt_orig))
        {
          struct Svcudp_data *su_o = Su_data(xprt_orig), *su_c;
          su_c = (struct Svcudp_data *) Mem_Alloc(sizeof(*su_c));
          if(!su_c)
            goto fail;
          Su_data_set(xprt_copy) = (void *) su_c;
          memcpy(su_c, su_o, sizeof(*su_c));

          rpc_buffer(xprt_copy) = Mem_Alloc_Label(su_c->su_iosz, "UDP IO Buffer");
          if(!rpc_buffer(xprt_copy))
            goto fail;
          xdrmem_create(&(su_c->su_xdrs), rpc_buffer(xprt_copy), su_c->su_iosz, XDR_DECODE);
          if(xprt_orig->xp_verf.oa_base == su_o->su_verfbody)
            xprt_copy->xp_verf.oa_base = su_c->su_verfbody;
          else
            xprt_copy->xp_verf.oa_base = xprt_orig->xp_verf.oa_base;
          xprt_copy->xp_verf.oa_flavor = xprt_orig->xp_verf.oa_flavor;
          xprt_copy->xp_verf.oa_length = xprt_orig->xp_verf.oa_length;
        }
      else
        goto fail;
    }
  else if (xprt_orig->xp_ops == &Svctcp_op)
    {
      struct tcp_conn *cd_o = (struct tcp_conn *)xprt_orig->xp_p1, *cd_c;
      cd_c = (struct tcp_conn *) Mem_Alloc(sizeof(*cd_c));
      if(!cd_c)
        goto fail;
      memcpy(cd_c, cd_o, sizeof(*cd_c));
      xprt_copy->xp_p1 = (void *) cd_c;
      xdrrec_create(&(cd_c->xdrs), 32768, 32768, (caddr_t) xprt_copy, Readtcp, Writetcp);
      if(xprt_orig->xp_verf.oa_base == cd_o->verf_body)
        xprt_copy->xp_verf.oa_base = cd_c->verf_body;
      else
        xprt_copy->xp_verf.oa_base = xprt_orig->xp_verf.oa_base;
      xprt_copy->xp_verf.oa_flavor = xprt_orig->xp_verf.oa_flavor;
      xprt_copy->xp_verf.oa_length = xprt_orig->xp_verf.oa_length;
    }
  else if (xprt_orig->xp_ops == &Svctcp_rendezvous_op)
    {
      goto fail;
    }
  else
    {
      LogDebug(COMPONENT_RPC,
               "Attempt to copy unknown xprt %p",
               xprt_orig);
      Mem_Free(xprt_copy);
      goto fail_no_xprt;
    }

  return xprt_copy;

 fail:
  FreeXprt(xprt_copy);
 fail_no_xprt:
  /* Let caller know about failure */
  LogCrit(COMPONENT_RPC,
          "Failed to copy xprt");
  svcerr_systemerr(xprt_orig);
  return NULL;
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
  LogFullDebug(COMPONENT_DISPATCH,
               "=====> tag=%s xprt=%p  fbtbc=%ld", tag, xprt, rstrm->fbtbc);
}

void *rpc_tcp_socket_manager_thread(void *Arg);

static bool_t Rendezvous_request(register SVCXPRT * xprt)
{
  int sock;
  struct tcp_rendezvous *r;
  struct sockaddr_in addr;
  unsigned long len;

  pthread_t sockmgr_thrid;
  int rc = 0;

  r = (struct tcp_rendezvous *)xprt->xp_p1;
 again:
  len = sizeof(struct sockaddr_in);
  if((sock = accept(xprt->XP_SOCK, (struct sockaddr *)&addr,
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

  if(pthread_cond_init(&condvar_xprt[xprt->XP_SOCK], NULL) != 0)
    return FALSE;

  if(pthread_mutex_init(&mutex_cond_xprt[xprt->XP_SOCK], NULL) != 0)
    return FALSE;

  if((rc =
	fridgethr_get( &sockmgr_thrid, rpc_tcp_socket_manager_thread,
                     (void *)((unsigned long)xprt->XP_SOCK))) != 0 )
    return FALSE;

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
int Readtcp(char *xprtp, char *buf, int len)
{
  register SVCXPRT *xprt = (SVCXPRT *)xprtp;
  register int sock = xprt->XP_SOCK;
  int milliseconds = 35 * 1000;
  struct pollfd pollfd;

  LogFullDebug(COMPONENT_DISPATCH, "Readtcp socket %d", sock);
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
      /* LogFullDebug(COMPONENT_DISPATCH,
                      "Readtcp (end): xprt=%p len=%d", xprt, len ) ; */
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
int Writetcp(char *xprtp, char *buf, int len)
{
  register int i, cnt;
  register SVCXPRT *xprt = (SVCXPRT *)xprtp;

  /* LogFullDebug(COMPONENT_DISPATCH,
                  "Writetcp: xprt=%p len=%d", xprt, len ) ; */
  /* print_xdrrec_fbtbc( "WriteTcp", xprt ) ; */

  for(cnt = len; cnt > 0; cnt -= i, buf += i)
    {
      i = write(xprt->XP_SOCK, buf, cnt);
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
