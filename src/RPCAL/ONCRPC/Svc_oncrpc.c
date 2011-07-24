#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
#endif

#include   <stdlib.h>
#include   <string.h>

#include   <sys/errno.h>
#include   "stuff_alloc.h"
#include   "nfs_core.h"
#include   "../rpcal.h"

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

#define NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	400     /* this size is excessive */

#define max(a, b) (a > b ? a : b)

#ifdef xp_stat
#undef xp_statv
#endif
/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * apropriate procedure.
 */
static struct svc_callout
{
  struct svc_callout *sc_next;
  u_long sc_prog;
  u_long sc_vers;
  void (*sc_dispatch) ();
} *svc_head;

static struct svc_callout *Svc_find();

/* Un rajout pour voir */
unsigned int mysvc_maxfd;

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
/*  */
int Xprt_register(SVCXPRT * xprt)
{
  register int sock = xprt->XP_SOCK;

  Xports[sock] = xprt;

  if(sock < FD_SETSIZE)
    {
      FD_SET(sock, &Svc_fdset);
      mysvc_maxfd = max(mysvc_maxfd, sock);
    }

  return TRUE;
}

/*
 * De-activate a transport handle. 
 */
/*  */
void Xprt_unregister(SVCXPRT * xprt)
{
  register int sock = xprt->XP_SOCK;

  if(Xports[sock] == xprt)
    {
      Xports[sock] = (SVCXPRT *) 0;
    }

  if(sock < FD_SETSIZE)
    {
      FD_CLR(sock, &Svc_fdset);
      if(sock == mysvc_maxfd)
        {
          for(mysvc_maxfd--; mysvc_maxfd >= 0; mysvc_maxfd--)
            if(Xports[mysvc_maxfd])
              break;
        }
    }
}

/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
/*  */

bool_t Svc_register(SVCXPRT * xprt, u_long prog, u_long vers, void (*dispatch) (),
                    int protocol)
{
  struct svc_callout *prev;
  register struct svc_callout *s;

  if((s = Svc_find(prog, vers, &prev)) != NULL_SVC)
    {
      if(s->sc_dispatch == dispatch)
        goto pmap_it;           /* he is registering another xptr */
      return (FALSE);
    }
  s = (struct svc_callout *)Mem_Alloc(sizeof(struct svc_callout));
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
/*  */
void Svc_unregister(u_long prog, u_long vers)
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
  Mem_Free((char *)s);
  /* now unregister the information with the local binder service */
  (void)pmap_unset(prog, vers);
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *Svc_find(u_long prog, u_long vers, struct svc_callout **prev)
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

bool_t svcauth_wrap_dummy(XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
  return ((*xdr_func) (xdrs, xdr_ptr));
}
