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

#include   <rpc/rpc.h>
#include   <rpc/auth.h>
#include   <rpc/svc.h>
#include   <rpc/pmap_clnt.h>

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

SVCXPRT *Xports[FD_SETSIZE];

#define NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	400     /* this size is excessive */

#define max(a, b) (a > b ? a : b)

#ifdef _SOLARIS
#define _authenticate __authenticate
#endif

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

fd_set Svc_fdset;

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
/*  */
void Xprt_register(SVCXPRT * xprt)
{
#ifdef _FREEBSD
  register int sock = xprt->xp_fd;
#else
  register int sock = xprt->xp_sock;
#endif

  if(sock < FD_SETSIZE)
    {
      Xports[sock] = xprt;
      FD_SET(sock, &Svc_fdset);
      mysvc_maxfd = max(mysvc_maxfd, sock);

    }
}

/*
 * De-activate a transport handle. 
 */
/*  */
void Xprt_unregister(SVCXPRT * xprt)
{
#ifdef _FREEBSD
  register int sock = xprt->xp_fd;
#else
  register int sock = xprt->xp_sock;
#endif

  if((sock < FD_SETSIZE) && (Xports[sock] == xprt))
    {
      Xports[sock] = (SVCXPRT *) 0;

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

void Svc_getreqset(fd_set * readfds)
{
  enum xprt_stat stat;
  struct rpc_msg msg;
  int prog_found;
  u_long low_vers;
  u_long high_vers;
  struct svc_req r;
  register SVCXPRT *xprt;
  register int bit;
  /*    register u_int32_t mask, *maskp; */
  register long mask, *maskp;
  register int sock;
  char cred_area[2 * MAX_AUTH_BYTES + RQCRED_SIZE];
  msg.rm_call.cb_cred.oa_base = cred_area;

  /* Set the struct Svc_req r to zero */
  memset(&r, 0, sizeof(struct svc_req));

  msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
  r.rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

/* portable access to fds_bits */
  maskp = __FDS_BITS(readfds);

  for(sock = 0; sock < FD_SETSIZE; sock += NFDBITS)
    {
      for(mask = *maskp++; bit = ffs(mask); mask ^= (1 << (bit - 1)))
        {
          /* sock has input waiting */
          xprt = Xports[sock + bit - 1];
          if(xprt == NULL)
            /* But do we control sock? */
            continue;
          /* now receive msgs from xprtprt (support batch calls) */
          do
            {
              if(SVC_RECV(xprt, &msg))
                {
                  /* now find the exported program and call it */
                  register struct svc_callout *s;
                  enum auth_stat why;

                  r.rq_xprt = xprt;
                  r.rq_prog = msg.rm_call.cb_prog;
                  r.rq_vers = msg.rm_call.cb_vers;
                  r.rq_proc = msg.rm_call.cb_proc;
                  r.rq_cred = msg.rm_call.cb_cred;

                  /* first authenticate the message */
                  if((why = _authenticate(&r, &msg)) != AUTH_OK)
                    {
                      svcerr_auth(xprt, why);
                      goto call_done;
                    }
                  /* now match message with a registered service */
                  prog_found = FALSE;
                  low_vers = 0 - 1;
                  high_vers = 0;
                  for(s = svc_head; s != NULL_SVC; s = s->sc_next)
                    {
                      if(s->sc_prog == r.rq_prog)
                        {
                          if(s->sc_vers == r.rq_vers)
                            {
                              (*s->sc_dispatch) (&r, xprt);
                              goto call_done;
                            }   /* found correct version */
                          prog_found = TRUE;
                          if(s->sc_vers < low_vers)
                            low_vers = s->sc_vers;
                          if(s->sc_vers > high_vers)
                            high_vers = s->sc_vers;
                        }       /* found correct program */
                    }
                  /*
                   * if we got here, the program or version
                   * is not served ...
                   */
                  if(prog_found)
                    svcerr_progvers(xprt, low_vers, high_vers);
                  else
                    svcerr_noprog(xprt);
                  /* Fall through to ... */
                }
 call_done:
              if((stat = SVC_STAT(xprt)) == XPRT_DIED)
                {
                  SVC_DESTROY(xprt);
                  break;
                }
            }
          while(stat == XPRT_MOREREQS);
        }
    }
}

bool_t svcauth_wrap_dummy(XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
  return ((*xdr_func) (xdrs, xdr_ptr));
}
