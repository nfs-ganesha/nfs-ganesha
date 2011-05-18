/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_RPC_H
#define GANESHA_RPC_H

#include "config.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#ifdef _USE_TIRPC
#include <tirpc/rpc/rpc.h>
#include <tirpc/rpc/svc.h>
#include <tirpc/rpc/types.h>
#include <tirpc/rpc/pmap_clnt.h>
#include <tirpc/rpc/svc_dg.h>
#include "RW_Lock.h"
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif
#endif

void socket_setoptions(int socketFd);

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

#ifdef _USE_TIRPC
typedef struct sockaddr_storage sockaddr_t;
#else
typedef struct sockaddr_in sockaddr_t;
#endif

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
#define XP_SOCK xp_fd
#else
#define XP_SOCK xp_sock
#endif

#ifdef _USE_TIRPC
extern void Svc_dg_soft_destroy(SVCXPRT * xport);
extern struct netconfig *getnetconfigent(const char *netid);
extern void freenetconfigent(struct netconfig *);
extern SVCXPRT *Svc_vc_create(int, u_int, u_int);
extern SVCXPRT *Svc_dg_create(int, u_int, u_int);
extern int Xprt_register(SVCXPRT * xprt);
extern void Xprt_unregister(SVCXPRT * xprt);

extern void FreeXprt(SVCXPRT *xprt);

#ifdef _DEBUG_MEMLEAKS
extern int CheckXprt(SVCXPRT *xprt);
#else
#define CheckXprt(ptr)
#endif

#else                       /* not _USE_TIRPC */

extern void Svcudp_soft_destroy(SVCXPRT * xprt);
extern SVCXPRT *Svctcp_create(register int sock, u_int sendsize, u_int recvsize);
extern SVCXPRT *Svcudp_bufcreate(register int sock, u_int sendsz, u_int recvsz);
extern bool_t Svc_register(SVCXPRT * xprt, u_long prog, u_long vers, void (*dispatch) (),
                    int protocol);
#define CheckXprt(ptr)

#endif                          /* _USE_TIRPC */


#ifdef _USE_GSSRPC
bool_t Svcauth_gss_import_name(char *service);
bool_t Svcauth_gss_acquire_cred(void);
#endif

#ifdef _USE_TIRPC
/* public data : */
extern rw_lock_t Svc_lock;
extern rw_lock_t Svc_fd_lock;
#endif

extern fd_set Svc_fdset;

/* Declare the various RPC transport dynamic arrays */
extern SVCXPRT         **Xports;
extern pthread_mutex_t  *mutex_cond_xprt;
extern pthread_cond_t   *condvar_xprt;

#ifdef _USE_TIRPC
#define SOCK_NAME_MAX 128
#else
#define SOCK_NAME_MAX 32
#endif
extern int copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt);
extern int sprint_sockaddr(sockaddr_t *addr, char *buf, int len);
extern int sprint_sockip(sockaddr_t *addr, char *buf, int len);
extern SVCXPRT *Svcxprt_copy(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
extern SVCXPRT *Svcxprt_copycreate();

typedef enum _ignore_port {
	IGNORE_PORT,
	CHECK_PORT
} ignore_port_t;

extern int cmp_sockaddr(sockaddr_t *addr_1,
                        sockaddr_t *addr_2,
                        ignore_port_t ignore_port);
extern unsigned long hash_sockaddr(sockaddr_t *addr,
                                   ignore_port_t ignore_port);

extern in_addr_t get_in_addr(sockaddr_t *addr);
extern int get_port(sockaddr_t *addr);

/* Returns an EAI value, accepts only numeric strings */
extern int ipstring_to_sockaddr(const char *str, sockaddr_t *addr);

CLIENT *Clnt_create(char *host,
                    unsigned long prog,
                    unsigned long vers,
                    char *proto);

Clnt_destroy(CLIENT *clnt);

#endif
