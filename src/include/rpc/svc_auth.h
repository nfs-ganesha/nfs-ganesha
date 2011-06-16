/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef _RPC_SVC_AUTH_H
#define _RPC_SVC_AUTH_H

/* Here is what we should pick up from  /usr/include/tirpc/rpc/svc_auth.h */
typedef struct SVCAUTH {
	struct svc_auth_ops {
		int	(*svc_ah_wrap)(struct SVCAUTH *, XDR *, xdrproc_t,
				       caddr_t);
		int	(*svc_ah_unwrap)(struct SVCAUTH *, XDR *, xdrproc_t,
					 caddr_t);
		int	(*svc_ah_destroy)(struct SVCAUTH *);
	} *svc_ah_ops;
	void * svc_ah_private;
} SVCAUTH;

#define SVCAUTH_WRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_wrap))(auth, xdrs, xfunc, xwhere))
#define SVCAUTH_UNWRAP(auth, xdrs, xfunc, xwhere) \
     ((*((auth)->svc_ah_ops->svc_ah_unwrap))(auth, xdrs, xfunc, xwhere))
#define SVCAUTH_DESTROY(auth) \
     ((*((auth)->svc_ah_ops->svc_ah_destroy))(auth))

__BEGIN_DECLS
extern enum auth_stat _authenticate(struct svc_req *, struct rpc_msg *);
extern int svc_auth_reg(int, enum auth_stat (*)(struct svc_req *,
			  struct rpc_msg *));

__END_DECLS

#endif
