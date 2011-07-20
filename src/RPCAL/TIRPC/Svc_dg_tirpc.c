
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
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#include <sys/cdefs.h>

/*
 * svc_dg.c, Server side for connectionless RPC.
 *
 * Does some caching in the hopes of achieving execute-at-most-once semantics.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netconfig.h>
#include <err.h>

#include "tirpc.h"
#include "RW_Lock.h"
#include "stuff_alloc.h"

#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

static void Svc_dg_ops(SVCXPRT *);
static enum xprt_stat Svc_dg_stat(SVCXPRT *);
static bool_t Svc_dg_recv(SVCXPRT *, struct rpc_msg *);
static bool_t Svc_dg_reply(SVCXPRT *, struct rpc_msg *);
static bool_t Svc_dg_getargs(SVCXPRT *, xdrproc_t, void *);
static bool_t Svc_dg_freeargs(SVCXPRT *, xdrproc_t, void *);
static void Svc_dg_destroy(SVCXPRT *);
static bool_t Svc_dg_control(SVCXPRT *, const u_int, void *);
static int cache_get(SVCXPRT *, struct rpc_msg *, char **, size_t *);
static void cache_set(SVCXPRT *, size_t);

/*
 * Usage:
 *	xprt = svc_dg_create(sock, sendsize, recvsize);
 * Does other connectionless specific initializations.
 * Once *xprt is initialized, it is registered.
 * see (svc.h, xprt_register). If recvsize or sendsize are 0 suitable
 * system defaults are chosen.
 * The routines returns NULL if a problem occurred.
 */
static const char svc_dg_str[] = "svc_dg_create: %s";
static const char svc_dg_err1[] = "could not get transport information";
static const char svc_dg_err2[] = " transport does not support data transfer";
static const char __no_mem_str[] = "out of memory";

SVCXPRT *Svc_dg_create(fd, sendsize, recvsize)
int fd;
u_int sendsize;
u_int recvsize;
{
  SVCXPRT *xprt;
  struct svc_dg_data *su = NULL;
  struct __rpc_sockinfo si;
  struct sockaddr_storage ss;
  socklen_t slen;

  /* __rpc_fd2sockinfo is private in libtirpc, it may change */
  if(!__rpc_fd2sockinfo(fd, &si))
    {
      warnx(svc_dg_str, svc_dg_err1);
      return (NULL);
    }
  /*
   * Find the receive and the send size
   */
  sendsize = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsize);
  recvsize = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsize);
  if((sendsize == 0) || (recvsize == 0))
    {
      warnx(svc_dg_str, svc_dg_err2);
      return (NULL);
    }

  xprt = (SVCXPRT *) Mem_Alloc(sizeof(SVCXPRT));
  if(xprt == NULL)
    goto freedata;
  memset(xprt, 0, sizeof(SVCXPRT));
  Svc_dg_ops(xprt);

  su = (struct svc_dg_data *)Mem_Alloc(sizeof(struct svc_dg_data));
  if(su == NULL)
    goto freedata;
  su->su_iosz = ((MAX(sendsize, recvsize) + 3) / 4) * 4;
  su->su_cache = NULL;
  su_data_set(xprt) = su;

  rpc_buffer(xprt) = Mem_Alloc(su->su_iosz);
  if(rpc_buffer(xprt) == NULL)
    goto freedata;
  xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_DECODE);
  xprt->xp_fd = fd;
  xprt->xp_verf.oa_base = su->su_verfbody;
  xprt->xp_rtaddr.maxlen = sizeof(struct sockaddr_storage);

  slen = sizeof ss;
  if(getsockname(fd, (struct sockaddr *)(void *)&ss, &slen) < 0)
    goto freedata;

  xprt->xp_ltaddr.buf = Mem_Alloc(sizeof(struct sockaddr_storage));
  if(!xprt->xp_ltaddr.buf)
    goto freedata;
  xprt->xp_ltaddr.maxlen = sizeof(struct sockaddr_storage);
  xprt->xp_ltaddr.len = slen;
  memcpy(xprt->xp_ltaddr.buf, &ss, slen);

  if(Xprt_register(xprt) == FALSE)
    goto freedata;

  return (xprt);
 freedata:
  (void)warnx(svc_dg_str, __no_mem_str);
  FreeXprt(xprt);
  return (NULL);
}

 /*ARGSUSED*/ static enum xprt_stat Svc_dg_stat(xprt)
SVCXPRT *xprt;
{
  return (XPRT_IDLE);
}

static bool_t Svc_dg_recv(xprt, msg)
SVCXPRT *xprt;
struct rpc_msg *msg;
{
  struct svc_dg_data *su = su_data(xprt);
  XDR *xdrs = &(su->su_xdrs);
  char *reply;
  struct sockaddr_storage ss;
  socklen_t alen;
  size_t replylen;
  ssize_t rlen;

 again:
  alen = sizeof(struct sockaddr_storage);
  rlen = recvfrom(xprt->xp_fd, rpc_buffer(xprt), su->su_iosz, 0,
                  (struct sockaddr *)(void *)&ss, &alen);
  if(rlen == -1 && errno == EINTR)
    goto again;
  if(rlen == -1 || (rlen < (ssize_t) (4 * sizeof(u_int32_t))))
    return (FALSE);
  if(xprt->xp_rtaddr.len < alen)
    {
      if(xprt->xp_rtaddr.len != 0)
        Mem_Free(xprt->xp_rtaddr.buf);
      xprt->xp_rtaddr.buf = Mem_Alloc(alen);
      if(xprt->xp_rtaddr.buf == NULL)
        return (FALSE);
      xprt->xp_rtaddr.len = alen;
    }
  memcpy(xprt->xp_rtaddr.buf, &ss, alen);
#ifdef PORTMAP
  if(ss.ss_family == AF_INET6)
    {
      xprt->xp_raddr = *(struct sockaddr_in6 *)xprt->xp_rtaddr.buf;
      xprt->xp_addrlen = sizeof(struct sockaddr_in6);
    }
#endif                          /* PORTMAP */
  xdrs->x_op = XDR_DECODE;
  XDR_SETPOS(xdrs, 0);
  if(!xdr_callmsg(xdrs, msg))
    {
      return (FALSE);
    }
  su->su_xid = msg->rm_xid;
  if(su->su_cache != NULL)
    {
      if(cache_get(xprt, msg, &reply, &replylen))
        {
          (void)sendto(xprt->xp_fd, reply, replylen, 0,
                       (struct sockaddr *)(void *)&ss, alen);
          return (FALSE);
        }
    }
  return (TRUE);
}

static bool_t Svc_dg_reply(xprt, msg)
SVCXPRT *xprt;
struct rpc_msg *msg;
{
  struct svc_dg_data *su = su_data(xprt);
  XDR *xdrs = &(su->su_xdrs);
  bool_t stat = FALSE;
  size_t slen;
  xdrproc_t xdr_results;
  caddr_t xdr_location;
  bool_t has_args;

  if(msg->rm_reply.rp_stat == MSG_ACCEPTED && msg->rm_reply.rp_acpt.ar_stat == SUCCESS)
    {
      has_args = TRUE;
      xdr_results = msg->acpted_rply.ar_results.proc;
      xdr_location = msg->acpted_rply.ar_results.where;

      msg->acpted_rply.ar_results.proc = (xdrproc_t) xdr_void;
      msg->acpted_rply.ar_results.where = NULL;
    }
  else
    {
      has_args = FALSE;
      xdr_results = NULL;
      xdr_location = NULL;
    }

  xdrs->x_op = XDR_ENCODE;
  XDR_SETPOS(xdrs, 0);
  msg->rm_xid = su->su_xid;
  if(xdr_replymsg(xdrs, msg) &&
     (!has_args || (SVCAUTH_WRAP(xprt->xp_auth, xdrs, xdr_results, xdr_location))))
    {
      slen = XDR_GETPOS(xdrs);
      if(sendto(xprt->xp_fd, rpc_buffer(xprt), slen, 0,
                (struct sockaddr *)xprt->xp_rtaddr.buf,
                (socklen_t) xprt->xp_rtaddr.len) == (ssize_t) slen)
        {
          stat = TRUE;
          if(su->su_cache)
            cache_set(xprt, slen);
        }
    }
  return (stat);
}

static bool_t Svc_dg_getargs(xprt, xdr_args, args_ptr)
SVCXPRT *xprt;
xdrproc_t xdr_args;
void *args_ptr;
{
  if(!SVCAUTH_UNWRAP(xprt->xp_auth, &(su_data(xprt)->su_xdrs), xdr_args, args_ptr))
    {
      (void)Svc_dg_freeargs(xprt, xdr_args, args_ptr);
      return FALSE;
    }
  return TRUE;
}

static bool_t Svc_dg_freeargs(xprt, xdr_args, args_ptr)
SVCXPRT *xprt;
xdrproc_t xdr_args;
void *args_ptr;
{
  XDR *xdrs = &(su_data(xprt)->su_xdrs);

  xdrs->x_op = XDR_FREE;
  return (*xdr_args) (xdrs, args_ptr);
}

void Svc_dg_soft_destroy(xprt)
SVCXPRT *xprt;
{
  FreeXprt(xprt);
}

static void Svc_dg_destroy(xprt)
SVCXPRT *xprt;
{
  struct svc_dg_data *su = su_data(xprt);

  Xprt_unregister(xprt);
  if(xprt->xp_fd != -1)
    (void)close(xprt->xp_fd);
  XDR_DESTROY(&(su->su_xdrs));

  FreeXprt(xprt);
}

static bool_t /*ARGSUSED*/ Svc_dg_control(xprt, rq, in)
SVCXPRT *xprt;
const u_int rq;
void *in;
{
  return (FALSE);
}

struct xp_ops  dg_ops =
  {
    .xp_recv = Svc_dg_recv,
    .xp_stat = Svc_dg_stat,
    .xp_getargs = Svc_dg_getargs,
    .xp_reply = Svc_dg_reply,
    .xp_freeargs = Svc_dg_freeargs,
    .xp_destroy = Svc_dg_destroy,
  };


struct xp_ops2 dg_ops2 =
  {
    .xp_control = Svc_dg_control,
  };

static void Svc_dg_ops(xprt)
SVCXPRT *xprt;
{
  xprt->xp_ops = &dg_ops;
  xprt->xp_ops2 = &dg_ops2;
}

/*  The CACHING COMPONENT */

/*
 * Could have been a separate file, but some part of it depends upon the
 * private structure of the client handle.
 *
 * Fifo cache for cl server
 * Copies pointers to reply buffers into fifo cache
 * Buffers are sent again if retransmissions are detected.
 */


/*
 * Enable use of the cache. Returns 1 on success, 0 on failure.
 * Note: there is no disable.
 */
static const char cache_enable_str[] = "svc_enablecache: %s %s";
static const char alloc_err[] = "could not allocate cache ";
static const char enable_err[] = "cache already enabled";

int Svc_dg_enablecache(transp, size)
SVCXPRT *transp;
u_int size;
{
  struct svc_dg_data *su = su_data(transp);
  struct cl_cache *uc;

  P(dupreq_lock);
  if(su->su_cache != NULL)
    {
      (void)warnx(cache_enable_str, enable_err, " ");
      V(dupreq_lock);
      return (0);
    }
  uc = (struct cl_cache *) Mem_Alloc(sizeof(struct cl_cache));
  if(uc == NULL)
    {
      warnx(cache_enable_str, alloc_err, " ");
      V(dupreq_lock);
      return (0);
    }
  su->su_cache = (char *)(void *)uc;
  uc->uc_size = size;
  uc->uc_nextvictim = 0;
  uc->uc_entries = (cache_ptr *) Mem_Alloc(sizeof(cache_ptr) * size * SPARSENESS);
  if(uc->uc_entries == NULL)
    {
      warnx(cache_enable_str, alloc_err, "data");
      xp_free(su->su_cache);
      V(dupreq_lock);
      return (0);
    }
  memset(uc->uc_entries, 0, sizeof(cache_ptr) * size * SPARSENESS);
  uc->uc_fifo = (cache_ptr *) Mem_Alloc(sizeof(cache_ptr) * size);
  if(uc->uc_fifo == NULL)
    {
      warnx(cache_enable_str, alloc_err, "fifo");
      xp_free(uc->uc_entries);
      xp_free(su->su_cache);
      V(dupreq_lock);
      return (0);
    }
  memset(uc->uc_fifo, 0, sizeof(cache_ptr) * size);
  V(dupreq_lock);
  return (1);
}

/*
 * Set an entry in the cache.  It assumes that the uc entry is set from
 * the earlier call to cache_get() for the same procedure.  This will always
 * happen because cache_get() is calle by svc_dg_recv and cache_set() is called
 * by svc_dg_reply().  All this hoopla because the right RPC parameters are
 * not available at svc_dg_reply time.
 */

static const char cache_set_str[] = "cache_set: %s";
static const char cache_set_err1[] = "victim not found";
static const char cache_set_err2[] = "victim alloc failed";
static const char cache_set_err3[] = "could not allocate new rpc buffer";

static void cache_set(xprt, replylen)
SVCXPRT *xprt;
size_t replylen;
{
  cache_ptr victim;
  cache_ptr *vicp;
  struct svc_dg_data *su = su_data(xprt);
  struct cl_cache *uc = (struct cl_cache *)su->su_cache;
  u_int loc;
  char *newbuf;

  P(dupreq_lock);
  /*
   * Find space for the new entry, either by
   * reusing an old entry, or by mallocing a new one
   */
  victim = uc->uc_fifo[uc->uc_nextvictim];
  if(victim != NULL)
    {
      loc = CACHE_LOC(xprt, victim->cache_xid);
      for(vicp = &uc->uc_entries[loc];
          *vicp != NULL && *vicp != victim; vicp = &(*vicp)->cache_next)
        ;
      if(*vicp == NULL)
        {
          warnx(cache_set_str, cache_set_err1);
          V(dupreq_lock);
          return;
        }
      *vicp = victim->cache_next;       /* remove from cache */
      newbuf = victim->cache_reply;
    }
  else
    {
      victim = (struct cache_node *)Mem_Alloc(sizeof(struct cache_node));
      if(victim == NULL)
        {
          warnx(cache_set_str, cache_set_err2);
          V(dupreq_lock);
          return;
        }
      newbuf = Mem_Alloc(su->su_iosz);
      if(newbuf == NULL)
        {
          warnx(cache_set_str, cache_set_err3);
          Mem_Free(victim);
          V(dupreq_lock);
          return;
        }
    }

  /*
   * Store it away
   */
  if(isFullDebug(COMPONENT_RPC_CACHE))
    {
      struct netconfig *nconf;
      char *uaddr;

      nconf = getnetconfigent(xprt->xp_netid);
      if(nconf)
        {
          uaddr = taddr2uaddr(nconf, &xprt->xp_rtaddr);
          freenetconfigent(nconf);
          LogFullDebug(COMPONENT_RPC_CACHE,
                       "cache set for xid= %x prog=%d vers=%d proc=%d for rmtaddr=%s",
                       su->su_xid, uc->uc_prog,
                       uc->uc_vers, uc->uc_proc, uaddr);
          free(uaddr);
        }
    }

  victim->cache_replylen = replylen;
  victim->cache_reply = rpc_buffer(xprt);
  rpc_buffer(xprt) = newbuf;
  xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_ENCODE);
  victim->cache_xid = su->su_xid;
  victim->cache_proc = uc->uc_proc;
  victim->cache_vers = uc->uc_vers;
  victim->cache_prog = uc->uc_prog;
  victim->cache_addr = xprt->xp_rtaddr;
  victim->cache_addr.buf = (char *) Mem_Alloc(xprt->xp_rtaddr.len);
  (void)memcpy(victim->cache_addr.buf, xprt->xp_rtaddr.buf, (size_t) xprt->xp_rtaddr.len);
  loc = CACHE_LOC(xprt, victim->cache_xid);
  victim->cache_next = uc->uc_entries[loc];
  uc->uc_entries[loc] = victim;
  uc->uc_fifo[uc->uc_nextvictim++] = victim;
  uc->uc_nextvictim %= uc->uc_size;
  V(dupreq_lock);
}

/*
 * Try to get an entry from the cache
 * return 1 if found, 0 if not found and set the stage for cache_set()
 */
static int cache_get(xprt, msg, replyp, replylenp)
SVCXPRT *xprt;
struct rpc_msg *msg;
char **replyp;
size_t *replylenp;
{
  u_int loc;
  cache_ptr ent;
  struct svc_dg_data *su = su_data(xprt);
  struct cl_cache *uc = (struct cl_cache *)su->su_cache;

  P(dupreq_lock);
  loc = CACHE_LOC(xprt, su->su_xid);
  for(ent = uc->uc_entries[loc]; ent != NULL; ent = ent->cache_next)
    {
      if(ent->cache_xid == su->su_xid &&
         ent->cache_proc == msg->rm_call.cb_proc &&
         ent->cache_vers == msg->rm_call.cb_vers &&
         ent->cache_prog == msg->rm_call.cb_prog &&
         ent->cache_addr.len == xprt->xp_rtaddr.len &&
         (memcmp(ent->cache_addr.buf, xprt->xp_rtaddr.buf, xprt->xp_rtaddr.len) == 0))
        {
          if(isFullDebug(COMPONENT_RPC_CACHE))
            {
              struct netconfig *nconf;
              char *uaddr;

              nconf = getnetconfigent(xprt->xp_netid);
              if(nconf)
                {
                  uaddr = taddr2uaddr(nconf, &xprt->xp_rtaddr);
                  freenetconfigent(nconf);
                  LogFullDebug(COMPONENT_RPC_CACHE,
                               "cache entry found for xid=%x prog=%d vers=%d proc=%d for rmtaddr=%s",
                               su->su_xid, msg->rm_call.cb_prog,
                               msg->rm_call.cb_vers,
                               msg->rm_call.cb_proc, uaddr);
                  free(uaddr);
                }
            }

          *replyp = ent->cache_reply;
          *replylenp = ent->cache_replylen;
          V(dupreq_lock);
          return (1);
        }
    }
  /*
   * Failed to find entry
   * Remember a few things so we can do a set later
   */
  uc->uc_proc = msg->rm_call.cb_proc;
  uc->uc_vers = msg->rm_call.cb_vers;
  uc->uc_prog = msg->rm_call.cb_prog;
  V(dupreq_lock);
  return (0);
}
