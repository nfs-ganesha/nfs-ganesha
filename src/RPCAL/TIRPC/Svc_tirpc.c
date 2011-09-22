
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

xprt_type_t get_xprt_type(SVCXPRT *xprt)
{
  if(xprt->xp_ops == &dg_ops)
    return XPRT_UDP;
  else if (xprt->xp_ops == &vc_ops)
    return XPRT_TCP;
  else if (xprt->xp_ops == &rendezvous_ops)
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

  if (xprt->xp_auth)
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

unsigned int get_tirpc_xid(SVCXPRT *xprt)
{
  if(xprt->xp_ops == &dg_ops)
    {
      struct svc_dg_data *su = su_data(xprt);
      if(su != NULL)
        return su->su_xid;
    }
  else if (xprt->xp_ops == &vc_ops)
    {
      struct cf_conn *cd = (struct cf_conn *)xprt->xp_p1;
      if(cd != NULL)
        return cd->x_id;
    }
  return 0;
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
  memset(xprt_copy, 0, sizeof(SVCXPRT));
  xprt_copy->xp_ops  = xprt_orig->xp_ops;
  xprt_copy->xp_ops2 = xprt_orig->xp_ops2;
  xprt_copy->xp_fd   = xprt_orig->xp_fd;

  if(xprt_orig->xp_ops == &dg_ops)
    {
      if(su_data(xprt_orig))
        {
          struct svc_dg_data *su_o = su_data(xprt_orig), *su_c;
          su_c = (struct svc_dg_data *) Mem_Alloc(sizeof(*su_c));
          if(!su_c)
            goto fail;
          su_data_set(xprt_copy) = su_c;
          memcpy(su_c, su_o, sizeof(*su_c));

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
  else if (xprt_orig->xp_ops == &vc_ops)
    {
      struct cf_conn *cd_o = (struct cf_conn *)xprt_orig->xp_p1, *cd_c;
      cd_c = (struct cf_conn *) Mem_Alloc(sizeof(*cd_c));
      if(!cd_c)
        goto fail;
      memcpy(cd_c, cd_o, sizeof(*cd_c));
      xprt_copy->xp_p1 = cd_c;
#ifndef NO_XDRREC_PATCH
      Xdrrec_create(&(cd_c->xdrs), cd_c->sendsize, cd_c->recvsize, xprt_copy, Read_vc, Write_vc);
#else
      xdrrec_create(&(cd_c->xdrs), cd_c->sendsize, cd_c->recvsize, xprt_copy, Read_vc, Write_vc);
#endif
      if(xprt_orig->xp_verf.oa_base == cd_o->verf_body)
        xprt_copy->xp_verf.oa_base = cd_c->verf_body;
      else
        xprt_copy->xp_verf.oa_base = xprt_orig->xp_verf.oa_base;
      xprt_copy->xp_verf.oa_flavor = xprt_orig->xp_verf.oa_flavor;
      xprt_copy->xp_verf.oa_length = xprt_orig->xp_verf.oa_length;
    }
  else if (xprt_orig->xp_ops == &rendezvous_ops)
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
      memset(xprt_copy->xp_rtaddr.buf, 0, sizeof(struct sockaddr_storage));
      xprt_copy->xp_rtaddr.maxlen = sizeof(struct sockaddr_storage);
      xprt_copy->xp_rtaddr.len    = xprt_orig->xp_rtaddr.len;
      memcpy(xprt_copy->xp_rtaddr.buf, xprt_orig->xp_rtaddr.buf, xprt_orig->xp_rtaddr.len);
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
 fail_no_xprt:
  /* Let caller know about failure */
  LogCrit(COMPONENT_RPC,
          "Failed to copy xprt");
  svcerr_systemerr(xprt_orig);
  return NULL;
}

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)

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
  return CheckAuth(xprt->xp_auth);
}
#endif
