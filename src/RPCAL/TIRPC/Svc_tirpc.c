
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
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */
#include <pthread.h>
/* #include <reentrant.h> */

#include <sys/select.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tirpc.h"
#include "stuff_alloc.h"

#define	RQCRED_SIZE	400     /* this size is excessive */

#define max(a, b) (a > b ? a : b)

/* public data : */
rw_lock_t Svc_fd_lock;
int Svc_maxfd;

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
int Xprt_register(xprt)
SVCXPRT *xprt;
{
  int sock;

  assert(xprt != NULL);

  sock = xprt->xp_fd;

  if(pthread_cond_init(&condvar_xprt[xprt->xp_fd], NULL) != 0)
    return FALSE;

  /* Init the mutex */
  if(pthread_mutex_init(&mutex_cond_xprt[xprt->xp_fd], NULL) != 0)
    {
      pthread_cond_destroy(&condvar_xprt[xprt->xp_fd]);
      return FALSE;
    }

  P_w(&Svc_fd_lock);
  if(sock < FD_SETSIZE)
    {
      FD_SET(sock, &Svc_fdset);
      svc_maxfd = max(svc_maxfd, sock);
    }
  Svc_maxfd = max(svc_maxfd, sock);

  V_w(&Svc_fd_lock);

  Xports[sock] = xprt;

  return TRUE;
}

/*
 * De-activate a transport handle.
 */
static void __Xprt_do_unregister(xprt, dolock)
SVCXPRT *xprt;
bool_t dolock;
{
  int sock;

  assert(xprt != NULL);

  sock = xprt->xp_fd;

  if(dolock)
    P_w(&Svc_fd_lock);

  if(Xports[sock] == xprt)
    {
      Xports[sock] = NULL;

      if(sock < FD_SETSIZE)
        {
          FD_CLR(sock, &Svc_fdset);
          if(sock >= svc_maxfd)
            {
              for(svc_maxfd--; svc_maxfd >= 0; svc_maxfd--)
                if(Xports[svc_maxfd])
                  break;
            }
        }

      if(sock >= Svc_maxfd)
        {
          for(Svc_maxfd--; Svc_maxfd >= 0; Svc_maxfd--)
            if(Xports[Svc_maxfd])
              break;
        }

      pthread_cond_destroy(&condvar_xprt[xprt->xp_fd]);
      pthread_mutex_destroy(&mutex_cond_xprt[xprt->xp_fd]);
    }

  if(dolock)
    V_w(&Svc_fd_lock);
}

void Xprt_unregister(SVCXPRT * xprt)
{
  __Xprt_do_unregister(xprt, TRUE);
}

void __Xprt_unregister_unlocked(SVCXPRT * xprt)
{
  __Xprt_do_unregister(xprt, FALSE);
}

#define xp_free(x) if(x) Mem_Free(x)

void FreeXprt(SVCXPRT *xprt)
{
  if(!xprt)
    {
      LogFullDebug(COMPONENT_RPC,
                   "Attempt to free NULL xprt");
      return;
      
    }

  if(xprt->xp_ops == &dg_ops)
    {
      if(su_data(xprt))
        {
          struct svc_dg_data *su = su_data(xprt);
          if(su->su_cache)
            {
              struct cl_cache *uc = su->su_cache;
              xp_free(uc->uc_entries);
              xp_free(uc->uc_fifo);
              xp_free(su_data(xprt)->su_cache);
            }
        }
      xp_free(su_data(xprt));
      xp_free(rpc_buffer(xprt));
    }
  else if (xprt->xp_ops == &vc_ops)
    {
      struct cf_conn *cd = (struct cf_conn *)xprt->xp_p1;
      XDR_DESTROY(&(cd->xdrs));
      xp_free(xprt->xp_p1); /* cd */
    }
  else if (xprt->xp_ops == &rendezvous_ops)
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
  xp_free(xprt->xp_tp);
  xp_free(xprt->xp_netid);
  xp_free(xprt->xp_rtaddr.buf);
  xp_free(xprt->xp_ltaddr.buf);
  SVCAUTH_DESTROY(xprt->xp_auth);
  Mem_Free(xprt);
}

/*
 * Create a copy of xprt. No-op for TIRPC.
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
    return NULL;
  memset(xprt_copy, 0, sizeof(SVCXPRT));
  xprt_copy->xp_ops  = xprt_orig->xp_ops;
  xprt_copy->xp_ops2 = xprt_orig->xp_ops2;
  xprt_copy->xp_fd   = xprt_orig->xp_fd;

  if(xprt_orig->xp_ops == &dg_ops)
    {
      if(su_data(xprt_orig))
        {
          struct svc_dg_data *su_o = su_data(xprt_orig), *su_c;
          su_c = (struct svc_dg_data *) Mem_Alloc(sizeof(struct svc_dg_data));
          if(!su_c)
            goto fail;
          su_data_set(xprt_copy) = su_c;
          memset(su_c, 0, sizeof(struct svc_dg_data));
          su_c->su_iosz = su_o->su_iosz;
          su_c->su_cache = NULL;

          if(su_o->su_cache)
            {
              struct cl_cache *uc = su_o->su_cache;
              if(!Svc_dg_enablecache(xprt_copy, uc->uc_size))
                goto fail;
            }
      
          rpc_buffer(xprt_copy) = Mem_Alloc(su_c->su_iosz);
          if(!rpc_buffer(xprt_copy))
            goto fail;
          xdrmem_create(&(su_c->su_xdrs), rpc_buffer(xprt_copy), su_c->su_iosz, XDR_DECODE);
        }
      else
        goto fail;
    }
  else if (xprt_orig->xp_ops == &vc_ops)
    {
      struct cf_conn *cd_o = (struct cf_conn *)xprt_orig->xp_p1, *cd_c;
      cd_c = (struct cf_conn *) Mem_Alloc(sizeof(*cd_c));
      if(!cd_c)
        goto fail;
      memcpy(cd_c, cd_o, sizeof(*cd_c));
      xprt_copy->xp_p1 = cd_c;
      xdrrec_create(&(cd_c->xdrs), cd_c->sendsize, cd_c->recvsize, xprt_copy, Read_vc, Write_vc);
      xprt_copy->xp_verf.oa_base = cd_c->verf_body;
    }
  else if (xprt_orig->xp_ops == &rendezvous_ops)
    {
      goto fail;
    }
  else
    {
      LogCrit(COMPONENT_RPC,
              "Attempt to copy unknown xprt %p",
              xprt_orig);
      Mem_Free(xprt_copy);
      return NULL;
    }

  if(xprt_orig->xp_tp)
    {
      xprt_copy->xp_tp = Str_Dup(xprt_orig->xp_tp);
      if(!xprt_copy->xp_tp)
        goto fail;
    }

  if(xprt_orig->xp_netid)
    {
      xprt_copy->xp_netid = Str_Dup(xprt_orig->xp_netid);
      if(!xprt_copy->xp_netid)
        goto fail;
    }

  if(xprt_orig->xp_rtaddr.buf)
    {
      xprt_copy->xp_rtaddr.buf = Mem_Alloc(sizeof(struct sockaddr_storage));
      if(!xprt_copy->xp_rtaddr.buf)
        goto fail;
      xprt_copy->xp_rtaddr.maxlen = xprt_orig->xp_rtaddr.maxlen;
      xprt_copy->xp_rtaddr.len    = xprt_orig->xp_rtaddr.len;
      memcpy(xprt_copy->xp_rtaddr.buf, xprt_orig->xp_rtaddr.buf, sizeof(struct sockaddr_storage));
    }

  if(xprt_orig->xp_ltaddr.buf)
    {
      xprt_copy->xp_ltaddr.buf = Mem_Alloc(sizeof(struct sockaddr_storage));
      if(!xprt_copy->xp_ltaddr.buf)
        goto fail;
      xprt_copy->xp_ltaddr.maxlen = xprt_orig->xp_ltaddr.maxlen;
      xprt_copy->xp_ltaddr.len    = xprt_orig->xp_ltaddr.len;
      memcpy(xprt_copy->xp_ltaddr.buf, xprt_orig->xp_ltaddr.buf, sizeof(struct sockaddr_storage));
    }

  if(!copy_svc_authgss(xprt_copy, xprt_orig))
    goto fail;

  return xprt_copy;

 fail:
  FreeXprt(xprt_copy);
  return NULL;
}

#ifdef _DEBUG_MEMLEAKS

#define checkif(x, s)                \
  if(x)                              \
    {                                \
      rc = BuddyCheckLabel(x, 1, s); \
      if(!rc)                        \
        return 0;                    \
    }

#define check(x, s)                  \
  if(x)                              \
    {                                \
      rc = BuddyCheckLabel(x, 1, s); \
      if(!rc)                        \
        return 0;                    \
    }

int CheckXprt(SVCXPRT *xprt)
{
  int rc;
  if(!xprt)
    {
      LogWarn(COMPONENT_MEMALLOC,
              "CheckXprt xprt=NULL");
      return 0;
    }

  LogFullDebug(COMPONENT_MEMALLOC,
               "Checking Xprt %p",
               xprt);

  rc = BuddyCheckLabel(xprt, 1, "xprt");
  if(!rc)
    return 0;

  if(xprt->xp_ops == &dg_ops)
    {
      if(su_data(xprt))
        {
          struct svc_dg_data *su = su_data(xprt);
          if(su->su_cache)
            {
              struct cl_cache *uc = su->su_cache;
              check(uc->uc_entries, "uc_entries");
              check(uc->uc_fifo, "uc_fifo");
              check(su_data(xprt)->su_cache, "su_cache");
            }
        }
      check(su_data(xprt), "su_data");
      check(rpc_buffer(xprt), "rpc_buffer");
    }
  else if (xprt->xp_ops == &vc_ops)
    {
      check(xprt->xp_p1, "cd"); /* cd */
    }
  else if (xprt->xp_ops == &rendezvous_ops)
    {
      check(xprt->xp_p1, "r"); /* r */
    }
  else
    {
      LogCrit(COMPONENT_MEMALLOC,
              "Attempt to check unknown xprt %p",
              xprt);
      return 0;
    }
  check(xprt->xp_tp, "xp_tp");
  check(xprt->xp_netid, "xp_netid");
  check(xprt->xp_rtaddr.buf, "xp_rtaddr.buf");
  check(xprt->xp_ltaddr.buf, "xp_ltaddr.buf");
  check(xprt->xp_auth, "xp_auth");
  return 1;
}
#endif

/* ********************** CALLOUT list related stuff ************* */

#ifdef PORTMAP
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
  rpcvers_t sc_vers;
  char *sc_netid;
  void (*sc_dispatch) (struct svc_req *, SVCXPRT *);
} *svc_head;


/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *Svc_find(prog, vers, prev, netid)
rpcprog_t prog;
rpcvers_t vers;
struct svc_callout **prev;
char *netid;
{
  struct svc_callout *s, *p;

  assert(prev != NULL);

  p = NULL;
  for(s = svc_head; s != NULL; s = s->sc_next)
    {
      if(((s->sc_prog == prog) && (s->sc_vers == vers)) &&
         ((netid == NULL) || (s->sc_netid == NULL) || (strcmp(netid, s->sc_netid) == 0)))
        break;
      p = s;
    }
  *prev = p;
  return (s);
}

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
bool_t Svc_register(xprt, prog, vers, dispatch, protocol)
SVCXPRT *xprt;
u_long prog;
u_long vers;
void (*dispatch) (struct svc_req *, SVCXPRT *);
int protocol;
{
  struct svc_callout *prev;
  struct svc_callout *s;

  assert(xprt != NULL);
  assert(dispatch != NULL);

  if((s = Svc_find((rpcprog_t) prog, (rpcvers_t) vers, &prev, NULL)) != NULL)
    {
      if(s->sc_dispatch == dispatch)
        goto pmap_it;           /* he is registering another xprt */
      return (FALSE);
    }
  s = Mem_Alloc(sizeof(struct svc_callout));
  if(s == NULL)
    {
      return (FALSE);
    }
  s->sc_prog = (rpcprog_t) prog;
  s->sc_vers = (rpcvers_t) vers;
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
void Svc_unregister(prog, vers)
u_long prog;
u_long vers;
{
  struct svc_callout *prev;
  struct svc_callout *s;

  if((s = Svc_find((rpcprog_t) prog, (rpcvers_t) vers, &prev, NULL)) == NULL)
    return;
  if(prev == NULL)
    {
      svc_head = s->sc_next;
    }
  else
    {
      prev->sc_next = s->sc_next;
    }
  s->sc_next = NULL;
  Mem_Free(s);
  /* now unregister the information with the local binder service */
  (void)pmap_unset(prog, vers);
}
#endif                          /* PORTMAP */
