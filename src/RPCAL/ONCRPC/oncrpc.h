/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_ONCRPC_H
#define GANESHA_ONCRPC_H

#include "../rpcal.h"

extern struct xp_ops  Svctcp_rendezvous_op;
extern struct xp_ops  Svctcp_op;
extern struct xp_ops  Svcudp_op;

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
 * kept in xprt->xp_p2
 */
struct Svcudp_data
{
  u_int su_iosz;                /* byte size of send.recv buffer */
  u_long su_xid;                /* transaction id */
  XDR su_xdrs;                  /* XDR handle */
  char su_verfbody[MAX_AUTH_BYTES];     /* verifier body */
};

#define	Su_data(xprt)	((struct Svcudp_data *)(xprt->xp_p2))
#define	Su_data_set(xprt)	(xprt->xp_p2)
#define	rpc_buffer(xprt) ((xprt)->xp_p1)

extern pthread_mutex_t dupreq_lock;

extern int Readtcp(char *xprtp, char *buf, int len);
extern int Writetcp(char *xprtp, char *buf, int len);

#endif
