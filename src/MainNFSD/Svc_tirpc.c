
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

#include <rpc/rpc.h>
#ifdef PORTMAP
#include <rpc/pmap_clnt.h>
#endif                          /* PORTMAP */

#include <Rpc_com_tirpc.h>
#include "stuff_alloc.h"
#include "RW_Lock.h"

#define	RQCRED_SIZE	400     /* this size is excessive */

#define SVC_VERSQUIET 0x0001    /* keep quiet about vers mismatch */
#define version_keepquiet(xp) ((u_long)(xp)->xp_p3 & SVC_VERSQUIET)

#define max(a, b) (a > b ? a : b)

/* public data : */
fd_set Svc_fdset;
SVCXPRT *Xports[FD_SETSIZE];

extern rw_lock_t Svc_lock;
extern rw_lock_t Svc_fd_lock;
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

static struct svc_callout *Svc_find(rpcprog_t, rpcvers_t, struct svc_callout **, char *);
static void __Xprt_do_unregister(SVCXPRT * xprt, bool_t dolock);

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
void Xprt_register(xprt)
SVCXPRT *xprt;
{
  int sock;

  assert(xprt != NULL);

  sock = xprt->xp_fd;

  P_w(&Svc_fd_lock);
  if(sock < FD_SETSIZE)
    {
      Xports[sock] = xprt;
      FD_SET(sock, &Svc_fdset);
      svc_maxfd = max(svc_maxfd, sock);
    }
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

  if((sock < FD_SETSIZE) && (Xports[sock] == xprt))
    {
      Xports[sock] = NULL;
      FD_CLR(sock, &Svc_fdset);
      if(sock >= svc_maxfd)
        {
          for(svc_maxfd--; svc_maxfd >= 0; svc_maxfd--)
            if(Xports[svc_maxfd])
              break;
        }
    }

  if(dolock)
    V_w(&Svc_fd_lock);
}

/* ********************** CALLOUT list related stuff ************* */

#ifdef PORTMAP
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
        goto pmap_it;           /* he is registering another xptr */
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
