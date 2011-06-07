/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_TIRPC_H
#define GANESHA_TIRPC_H

#include "../rpcal.h"
#include <Rpc_com_tirpc.h>
#ifdef PORTMAP
#include <rpc/pmap_clnt.h>
#endif                          /* PORTMAP */

extern struct xp_ops  rendezvous_ops;
extern struct xp_ops2 rendesvous_ops2;
extern struct xp_ops  vc_ops;
extern struct xp_ops2 vc_ops2;
extern struct xp_ops  dg_ops;
extern struct xp_ops2 dg_ops2;
extern int Svc_maxfd;

#define	su_data(xprt)	((struct svc_dg_data *)(xprt->xp_p2))
#define	su_data_set(xprt)	(xprt->xp_p2)
#define	rpc_buffer(xprt) ((xprt)->xp_p1)

struct cf_rendezvous
{                               /* kept in xprt->xp_p1 for rendezvouser */
  u_int sendsize;
  u_int recvsize;
  int maxrec;
};

struct cf_conn
{                               /* kept in xprt->xp_p1 for actual connection */
  enum xprt_stat strm_stat;
  u_int32_t x_id;
  XDR xdrs;
  char verf_body[MAX_AUTH_BYTES];
  u_int sendsize;
  u_int recvsize;
  int maxrec;
  bool_t nonblock;
  struct timeval last_recv_time;
};

#define	SPARSENESS 4            /* 75% sparse */

/*
 * An entry in the cache
 */
typedef struct cache_node *cache_ptr;
struct cache_node
{
  /*
   * Index into cache is xid, proc, vers, prog and address
   */
  u_int32_t cache_xid;
  rpcproc_t cache_proc;
  rpcvers_t cache_vers;
  rpcprog_t cache_prog;
  struct netbuf cache_addr;
  /*
   * The cached reply and length
   */
  char *cache_reply;
  size_t cache_replylen;
  /*
   * Next node on the list, if there is a collision
   */
  cache_ptr cache_next;
};

/*
 * The entire cache
 */
struct cl_cache
{
  u_int uc_size;                /* size of cache */
  cache_ptr *uc_entries;        /* hash table of entries in cache */
  cache_ptr *uc_fifo;           /* fifo list of entries in cache */
  u_int uc_nextvictim;          /* points to next victim in fifo list */
  rpcprog_t uc_prog;            /* saved program number */
  rpcvers_t uc_vers;            /* saved version number */
  rpcproc_t uc_proc;            /* saved procedure number */
};

/*
 * the hashing function
 */
#define	CACHE_LOC(transp, xid)	\
	(xid % (SPARSENESS * ((struct cl_cache *) \
		su_data(transp)->su_cache)->uc_size))

extern pthread_mutex_t dupreq_lock;

extern int Svc_dg_enablecache(SVCXPRT *, u_int);
extern int Read_vc(void *, void *, int);
extern int Write_vc(void *, void *, int);

#ifndef NO_XDRREC_PATCH
extern void Xdrrec_create(XDR *xdrs,
                          u_int sendsize,
                          u_int recvsize,
                          void *tcp_handle,
                          int (*readit)(void *, void *, int), /* like read, but pass it a tcp_handle, not sock */
                          int (*writeit)(void *, void *, int)); /* like write, but pass it a tcp_handle, not sock */
extern bool_t   Xdrrec_eof(XDR *);
extern bool_t   __Xdrrec_setnonblock(XDR *, int);
extern bool_t   Xdrrec_endofrecord(XDR *, bool_t);
extern bool_t   __Xdrrec_getrec(XDR *, enum xprt_stat *, bool_t);
extern bool_t   Xdrrec_skiprecord(XDR *);
#endif

#endif
