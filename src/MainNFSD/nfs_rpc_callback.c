/*
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @file nfs_rpc_callback.c
 * @author Matt Benjamin <matt@linuxbox.com>
 * @author Lee Dobryden <lee@linuxbox.com>
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief RPC callback dispatch package
 *
 * This module implements APIs for submission, and dispatch of NFSv4.0
 * and NFSv4.1 callbacks.
 *
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_req_queue.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "nfs4.h"
#include "gss_credcache.h"
#include "sal_data.h"
#include <misc/timespec.h>

/**
 * @brief Pool for allocating callbacks.
 */
static pool_t *rpc_call_pool;

static void _nfs_rpc_destroy_chan(rpc_call_channel_t *chan);

/**
 * @brief Initialize the callback credential cache
 *
 * @param[in] ccache Location of credential cache
 */

static inline void nfs_rpc_cb_init_ccache(const char *ccache)
{
	int code = 0;

	if (mkdir(ccache, 700) < 0) {
		if (errno == EEXIST)
			LogEvent(COMPONENT_INIT,
				 "Callback creds directory (%s) already exists",
				 ccache);
		else
			LogWarn(COMPONENT_INIT,
				"Could not create credential cache directory: %s (%s)",
				ccache, strerror(errno));
	}
	ccachesearch[0] = nfs_param.krb5_param.ccache_dir;

	code = gssd_refresh_krb5_machine_credential(host_name, NULL,
						    nfs_param.krb5_param.svc.
						    principal);
	if (code) {
		LogWarn(COMPONENT_INIT,
			"gssd_refresh_krb5_machine_credential failed (%d:%d)",
			code, errno);
		goto out;
	}

 out:
	return;
}

/**
 * @brief Initialize callback subsystem
 */

void nfs_rpc_cb_pkginit(void)
{
	/* Create a pool of rpc_call_t */
	rpc_call_pool = pool_init("RPC Call Pool",
				  sizeof(rpc_call_t), pool_basic_substrate,
				  NULL, nfs_rpc_init_call, NULL);
	if (!(rpc_call_pool))
		LogFatal(COMPONENT_INIT,
			"Error while allocating rpc call pool");

	/* ccache */
	nfs_rpc_cb_init_ccache(nfs_param.krb5_param.ccache_dir);

	/* sanity check GSSAPI */
	if (gssd_check_mechs() != 0)
		LogCrit(COMPONENT_INIT,
			"sanity check: gssd_check_mechs() failed");

	return;
}

/**
 * @brief Shutdown callback subsystem
 */
void nfs_rpc_cb_pkgshutdown(void)
{
	return;
}

/**
 * @brief Convert a netid label
 *
 * @todo This is automatically redundant, but in fact upstream TI-RPC is
 * not up-to-date with RFC 5665, will fix (Matt)
 *
 * @copyright 2012, Linux Box Corp
 *
 * @param[in] netid The netid label dictating the protocol
 *
 * @return The numerical protocol identifier.
 */

nc_type nfs_netid_to_nc(const char *netid)
{
	if (!strncmp(netid, netid_nc_table[_NC_TCP].netid,
		     netid_nc_table[_NC_TCP].netid_len))
		return _NC_TCP;

	if (!strncmp(netid, netid_nc_table[_NC_TCP6].netid,
		     netid_nc_table[_NC_TCP6].netid_len))
		return _NC_TCP6;

	if (!strncmp(netid, netid_nc_table[_NC_UDP].netid,
		     netid_nc_table[_NC_UDP].netid_len))
		return _NC_UDP;

	if (!strncmp(netid, netid_nc_table[_NC_UDP6].netid,
		     netid_nc_table[_NC_UDP6].netid_len))
		return _NC_UDP6;

	if (!strncmp(netid, netid_nc_table[_NC_RDMA].netid,
		     netid_nc_table[_NC_RDMA].netid_len))
		return _NC_RDMA;

	if (!strncmp(netid, netid_nc_table[_NC_RDMA6].netid,
		     netid_nc_table[_NC_RDMA6].netid_len))
		return _NC_RDMA6;

	if (!strncmp(netid, netid_nc_table[_NC_SCTP].netid,
		     netid_nc_table[_NC_SCTP].netid_len))
		return _NC_SCTP;

	if (!strncmp(netid, netid_nc_table[_NC_SCTP6].netid,
		     netid_nc_table[_NC_SCTP6].netid_len))
		return _NC_SCTP6;

	return _NC_ERR;
}

/**
 * @brief Convert string format address to sockaddr
 *
 * This function takes the host.port format used in the NFSv4.0
 * clientaddr4 and converts it to a POSIX sockaddr structure stored in
 * the callback information of the clientid.
 *
 * @param[in,out] clientid The clientid in which to store the sockaddr
 * @param[in]     uaddr    na_r_addr from the clientaddr4
 */

static inline void setup_client_saddr(nfs_client_id_t *clientid,
				      const char *uaddr)
{
	char addr_buf[SOCK_NAME_MAX + 1];
	uint32_t bytes[11];
	int code;

	assert(clientid->cid_minorversion == 0);

	memset(&clientid->cid_cb.v40.cb_addr.ss, 0,
	       sizeof(struct sockaddr_storage));

	switch (clientid->cid_cb.v40.cb_addr.nc) {
	case _NC_TCP:
	case _NC_RDMA:
	case _NC_SCTP:
	case _NC_UDP:
		/* IPv4 (ws inspired) */
		if (sscanf(uaddr,
			   "%u.%u.%u.%u.%u.%u",
			   &bytes[1], &bytes[2], &bytes[3], &bytes[4],
			   &bytes[5], &bytes[6]) == 6) {
			struct sockaddr_in *sin = ((struct sockaddr_in *)
						   &clientid->cid_cb.v40.
						   cb_addr.ss);
			snprintf(addr_buf, sizeof(addr_buf), "%u.%u.%u.%u",
				 bytes[1], bytes[2], bytes[3], bytes[4]);
			sin->sin_family = AF_INET;
			sin->sin_port = htons((bytes[5] << 8) | bytes[6]);
			code = inet_pton(AF_INET, addr_buf, &sin->sin_addr);
			if (code != 1) {
				LogWarn(COMPONENT_NFS_CB,
					"inet_pton failed (%d %s)", code,
					addr_buf);
			} else {
				LogDebug(COMPONENT_NFS_CB,
					 "client callback addr:port %s:%d",
					 addr_buf, ntohs(sin->sin_port));
			}
		}
		break;
	case _NC_TCP6:
	case _NC_RDMA6:
	case _NC_SCTP6:
	case _NC_UDP6:
		/* IPv6 (ws inspired) */
		if (sscanf(uaddr,
			   "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x.%u.%u",
			   &bytes[1], &bytes[2], &bytes[3], &bytes[4],
			   &bytes[5], &bytes[6], &bytes[7], &bytes[8],
			   &bytes[9], &bytes[10]) == 10) {
			struct sockaddr_in6 *sin6 = ((struct sockaddr_in6 *)
						     &clientid->cid_cb.v40.
						     cb_addr.ss);

			snprintf(addr_buf, sizeof(addr_buf),
				 "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x", bytes[1],
				 bytes[2], bytes[3], bytes[4], bytes[5],
				 bytes[6], bytes[7], bytes[8]);
			code = inet_pton(AF_INET6, addr_buf, &sin6->sin6_addr);
			sin6->sin6_port = htons((bytes[9] << 8) | bytes[10]);
			sin6->sin6_family = AF_INET6;
			if (code != 1) {
				LogWarn(COMPONENT_NFS_CB,
					"inet_pton failed (%d %s)", code,
					addr_buf);
			} else {
				LogDebug(COMPONENT_NFS_CB,
					 "client callback addr:port %s:%d",
					 addr_buf, ntohs(sin6->sin6_port));
			}
		}
		break;
	default:
		/* unknown netid */
		break;
	};
}

/**
 * @brief Set the callback location for an NFSv4.0 clientid
 *
 * @param[in,out] clientid The clientid in which to set the location
 * @param[in]     addr4    The client's supplied callback address
 */

void nfs_set_client_location(nfs_client_id_t *clientid,
			     const clientaddr4 *addr4)
{
	assert(clientid->cid_minorversion == 0);
	clientid->cid_cb.v40.cb_addr.nc = nfs_netid_to_nc(addr4->r_netid);
	strlcpy(clientid->cid_cb.v40.cb_client_r_addr, addr4->r_addr,
		SOCK_NAME_MAX);
	setup_client_saddr(clientid, clientid->cid_cb.v40.cb_client_r_addr);
}

/**
 * @brief Get the fd of an NFSv4.0 callback connection
 *
 * @param[in]  clientid The clientid to query
 * @param[out] fd       The file descriptor
 * @param[out] proto    The protocol used on this connection
 *
 * @return 0 or values of errno.
 */

static inline int32_t nfs_clid_connected_socket(nfs_client_id_t *clientid,
						int *fd, int *proto)
{
	int domain, sock_type, protocol, sock_size;
	int nfd, code = 0;

	assert(clientid->cid_minorversion == 0);

	*fd = 0;
	*proto = -1;

	switch (clientid->cid_cb.v40.cb_addr.nc) {
	case _NC_TCP:
	case _NC_TCP6:
		sock_type = SOCK_STREAM;
		protocol = IPPROTO_TCP;
		break;
	case _NC_UDP6:
	case _NC_UDP:
		sock_type = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
		break;
	default:
		code = EINVAL;
		goto out;
	}
	switch (clientid->cid_cb.v40.cb_addr.ss.ss_family) {
	case AF_INET:
		domain = PF_INET;
		sock_size = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		domain = PF_INET6;
		sock_size = sizeof(struct sockaddr_in6);
		break;
	default:
		code = EINVAL;
		goto out;
	}
	nfd = socket(domain, sock_type, protocol);
	if (nfd < 0) {
		code = errno;
		LogWarn(COMPONENT_NFS_CB,
			"socket failed %d (%s)", code, strerror(code));
		goto out;
	}
	code = connect(nfd,
		       (struct sockaddr *)&clientid->cid_cb.v40.cb_addr.ss,
		       sock_size);
	if (code < 0) {
		code = errno;
		LogWarn(COMPONENT_NFS_CB, "connect fail errno %d (%s)",
			code, strerror(code));
		close(nfd);
		goto out;
	}
	*proto = protocol;
	*fd = nfd;

 out:
	return code;
}

/* end refactorable RPC code */

/**
 * @brief Check if an authentication flavor is supported
 *
 * @param[in] flavor RPC authentication flavor
 *
 * @retval true if supported.
 * @retval false if not.
 */

static inline bool supported_auth_flavor(int flavor)
{
	bool code = false;

	switch (flavor) {
	case RPCSEC_GSS:
	case AUTH_SYS:
	case AUTH_NONE:
		code = true;
		break;
	default:
		break;
	};

	return code;
}

/**
 * @brief Kerberos OID
 *
 * This value comes from kerberos source, gssapi_krb5.c (Umich).
 */

gss_OID_desc krb5oid = { 9, "\052\206\110\206\367\022\001\002\002" };

/**
 * @brief Format a principal name for an RPC call channel
 *
 * @param[in]  chan Call channel
 * @param[out] buf  Buffer to hold formatted name
 * @param[in]  len  Size of buffer
 *
 * @return The principle or NULL.
 */

static inline char *format_host_principal(rpc_call_channel_t *chan, char *buf,
					  size_t len)
{
	char addr_buf[SOCK_NAME_MAX + 1];
	const char *host = NULL;
	char *princ = NULL;

	switch (chan->type) {
	case RPC_CHAN_V40:
		{
			nfs_client_id_t *clientid = chan->source.clientid;
			switch (clientid->cid_cb.v40.cb_addr.ss.ss_family) {
			case AF_INET:
				{
					struct sockaddr_in *sin =
					    (struct sockaddr_in *)
					    &clientid->cid_cb.v40.cb_addr.ss;
					host =
					    inet_ntop(AF_INET, &sin->sin_addr,
						      addr_buf,
						      INET_ADDRSTRLEN);
					break;
				}
			case AF_INET6:
				{
					struct sockaddr_in6 *sin6 =
					    (struct sockaddr_in6 *)
					    &clientid->cid_cb.v40.cb_addr.ss;
					host =
					    inet_ntop(AF_INET6,
						      &sin6->sin6_addr,
						      addr_buf,
						      INET6_ADDRSTRLEN);
					break;
				}
			default:
				break;
			}
			break;
		}
	case RPC_CHAN_V41:
		/* XXX implement */
		goto out;
		break;
	}

	if (host) {
		snprintf(buf, len, "nfs@%s", host);
		princ = buf;
	}

 out:
	return princ;
}

/**
 * @brief Set up GSS on a callback channel
 *
 * @param[in,out] chan Channel on which to set up GSS
 * @param[in]     cred GSS Credential
 */

static inline void nfs_rpc_callback_setup_gss(rpc_call_channel_t *chan,
					      nfs_client_cred_t *cred)
{
	AUTH *auth;
	char hprinc[MAXPATHLEN + 1];
	int32_t code = 0;

	assert(cred->flavor == RPCSEC_GSS);

	/* MUST RFC 3530bis, section 3.3.3 */
	chan->gss_sec.svc = cred->auth_union.auth_gss.svc;
	chan->gss_sec.qop = cred->auth_union.auth_gss.qop;

	/* the GSSAPI k5 mech needs to find an unexpired credential
	 * for nfs/hostname in an accessible k5ccache */
	code =
	    gssd_refresh_krb5_machine_credential(host_name, NULL,
						 nfs_param.krb5_param.svc.
						 principal);
	if (code) {
		LogWarn(COMPONENT_NFS_CB,
			"gssd_refresh_krb5_machine_credential failed (%d:%d)",
			code, errno);
		goto out;
	}

	if (!format_host_principal(chan, hprinc, sizeof(hprinc))) {
		LogCrit(COMPONENT_NFS_CB, "format_host_principal failed");
		goto out;
	}

	chan->gss_sec.cred = GSS_C_NO_CREDENTIAL;
	chan->gss_sec.req_flags = 0;

	if (chan->gss_sec.svc != RPCSEC_GSS_SVC_NONE) {
		/* no more lipkey, spkm3 */
		chan->gss_sec.mech = (gss_OID) & krb5oid;
		chan->gss_sec.req_flags = GSS_C_MUTUAL_FLAG;	/* XXX */
		auth = authgss_create_default(chan->clnt, hprinc,
					      &chan->gss_sec);
		/* authgss_create and authgss_create_default return NULL on
		 * failure, don't assign NULL to clnt->cl_auth */
		if (auth)
			chan->auth = auth;
	}

 out:
	return;
}

/**
 * @brief Create a channel for an NFSv4.0 client
 *
 * @param[in] clientid Client record
 * @param[in] flags     Currently unused
 *
 * @return Status code.
 */

int nfs_rpc_create_chan_v40(nfs_client_id_t *clientid, uint32_t flags)
{
	struct netbuf raddr;
	int fd, proto, code = 0;
	rpc_call_channel_t *chan = &clientid->cid_cb.v40.cb_chan;

	assert(!chan->clnt);
	assert(clientid->cid_minorversion == 0);

	/* XXX we MUST error RFC 3530bis, sec. 3.3.3 */
	if (!supported_auth_flavor(clientid->cid_credential.flavor)) {
		code = EINVAL;
		goto out;
	}

	chan->type = RPC_CHAN_V40;
	chan->source.clientid = clientid;

	code = nfs_clid_connected_socket(clientid, &fd, &proto);
	if (code) {
		LogWarn(COMPONENT_NFS_CB, "Failed creating socket");
		goto out;
	}

	raddr.buf = &clientid->cid_cb.v40.cb_addr.ss;

	switch (proto) {
	case IPPROTO_TCP:
		raddr.maxlen = raddr.len = sizeof(struct sockaddr_in);
		chan->clnt = clnt_vc_create(fd, &raddr,
					    clientid->cid_cb.v40.cb_program,
					    NFS_CB /* Errata ID: 2291 */,
					    0, 0);
		break;
	case IPPROTO_UDP:
		raddr.maxlen = raddr.len = sizeof(struct sockaddr_in6);
		chan->clnt = clnt_dg_create(fd, &raddr,
					    clientid->cid_cb.v40.cb_program,
					    NFS_CB /* Errata ID: 2291 */,
					    0, 0);
		break;
	default:
		break;
	}

	if (!chan->clnt) {
		close(fd);
		code = EINVAL;
		goto out;
	}

	/* channel protection */
	switch (clientid->cid_credential.flavor) {
	case RPCSEC_GSS:
		nfs_rpc_callback_setup_gss(chan, &clientid->cid_credential);
		break;
	case AUTH_SYS:
		chan->auth = authunix_create_default();
		if (!chan->auth)
			code = EINVAL;
		code = 0;
		break;
	case AUTH_NONE:
		chan->auth = authnone_ncreate();
		if (!chan->auth)
			code = EINVAL;
		break;
	default:
		code = EINVAL;
		break;
	}

 out:
	return code;
}

/**
 * @brief Create a channel for an NFSv4.1 session
 *
 * This function creates a channel on an NFSv4.1 session, using the
 * given security parameters.  If a channel already exists, it is
 * removed and replaced.
 *
 * @param[in,out] session       The session on which to create the
 *                              back channel
 * @param[in]     num_sec_parms Length of sec_parms list
 * @param[in]     sec_parms     Allowable security parameters
 *
 * @return 0 or POSIX error code.
 */

int nfs_rpc_create_chan_v41(nfs41_session_t *session, int num_sec_parms,
			    callback_sec_parms4 *sec_parms)
{
	int code = 0;
	rpc_call_channel_t *chan = &session->cb_chan;
	int i = 0;
	bool authed = false;
	struct timeval cb_timeout = { 15, 0 };

	pthread_mutex_lock(&chan->mtx);

	if (chan->clnt) {
		/* Something better later. */
		code = EEXIST;
		goto out;
	}

	chan->type = RPC_CHAN_V41;
	chan->source.session = session;

	assert(session->xprt);

	/* connect an RPC client
	 * Use version 1 per errata ID 2291 for RFC 5661
	 */
	chan->clnt = clnt_vc_create_svc(session->xprt, session->cb_program,
					NFS_CB /* Errata ID: 2291 */,
					SVC_VC_CREATE_BOTHWAYS);

	if (!chan->clnt) {
		code = EINVAL;
		goto out;
	}

	for (i = 0; i < num_sec_parms; ++i) {
		if (sec_parms[i].cb_secflavor == AUTH_NONE) {
			chan->auth = authnone_ncreate();
			if (!chan->auth)
				continue;
			authed = true;
			break;
		} else if (sec_parms[i].cb_secflavor == AUTH_SYS) {
			struct authunix_parms *sys_parms =
			    &sec_parms[i].callback_sec_parms4_u.cbsp_sys_cred;

			chan->auth = authunix_create(sys_parms->aup_machname,
						     sys_parms->aup_uid,
						     sys_parms->aup_gid,
						     sys_parms->aup_len,
						     sys_parms->aup_gids);
			if (!chan->auth)
				continue;
			authed = true;
			break;
		} else if (sec_parms[i].cb_secflavor == RPCSEC_GSS) {

			/**
			 * @todo ACE: Come back later and implement
			 * GSS.
			 */
			continue;
		} else {
			LogMajor(COMPONENT_NFS_CB,
				 "Client sent unknown auth type.");
			continue;
		}
	}

	if (!authed) {
		code = EPERM;
		LogMajor(COMPONENT_NFS_CB, "No working auth in sec_params.");
		goto out;
	}

	if (rpc_cb_null(chan, cb_timeout, true) != RPC_SUCCESS) {
#ifdef EBADFD
		code = EBADFD;
#else				/* !EBADFD */
		code = EBADF;
#endif				/* !EBADFD */
		goto out;
	}

	session->flags |= session_bc_up;
	code = 0;

 out:
	if ((code != 0) && chan->clnt)
		_nfs_rpc_destroy_chan(chan);

	pthread_mutex_unlock(&chan->mtx);

	return code;
}

/**
 * @brief Get a backchannel for a clientid
 *
 * This function works for both NFSv4.0 and NFSv4.1.  For NFSv4.0, if
 * the channel isn't up, it tries to create it.
 *
 * @param[in,out] clientid The clientid to use
 * @param[out]    flags    Unused
 *
 * @return The back channel or NULL if none existed or could be
 *         established.
 */
rpc_call_channel_t *nfs_rpc_get_chan(nfs_client_id_t *clientid, uint32_t flags)
{
	rpc_call_channel_t *chan = NULL;

	if (clientid->cid_minorversion == 0) {
		chan = &clientid->cid_cb.v40.cb_chan;
		if (!chan->clnt)
			(void)nfs_rpc_create_chan_v40(clientid, flags);
	} else {		/* 1 and higher */
		struct glist_head *glist = NULL;

		/* Get the first working back channel we have */
		/**@ todo ??? pthread_mutex_lock(&found->cid_mutex); */
		glist_for_each(glist, &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(glist,
							       nfs41_session_t,
							       session_link);
			if (session->flags & session_bc_up)
				chan = &session->cb_chan;
		/**@ todo ??? pthread_mutex_unlock(&found->cid_mutex); */
		}
	}

	return chan;
}

/**
 * @brief Dispose of a v4.0 channel
 *
 * The caller should hold the channel mutex.
 *
 * @param[in] chan The channel to dispose of
 */
void nfs_rpc_destroy_v40_chan(rpc_call_channel_t *chan)
{
	/* channel has a dedicated RPC client */
	if (chan->clnt) {
		/* clean up auth, if any */
		if (chan->auth) {
			AUTH_DESTROY(chan->auth);
			chan->auth = NULL;
		}
		/* destroy it */
		if (chan->clnt)
			clnt_destroy(chan->clnt);
	}
}

/**
 * @brief Dispose of a v41 channel
 *
 * The caller should hold the channel mutex.
 *
 * @param[in] chan The channel to dispose of
 */
void nfs_rpc_destroy_v41_chan(rpc_call_channel_t *chan)
{
	if (chan->auth) {
		AUTH_DESTROY(chan->auth);
		chan->auth = NULL;
	}
	if (chan->clnt)
		chan->clnt->cl_ops->cl_release(chan->clnt,
					       CLNT_RELEASE_FLAG_NONE);
}

/**
 * @brief Dispose of a channel
 *
 * @param[in] chan The channel to dispose of
 */
void _nfs_rpc_destroy_chan(rpc_call_channel_t *chan)
{
	assert(chan);

	switch (chan->type) {
	case RPC_CHAN_V40:
		nfs_rpc_destroy_v40_chan(chan);
		break;
	case RPC_CHAN_V41:
		nfs_rpc_destroy_v41_chan(chan);
		break;
	}

	chan->clnt = NULL;
	chan->last_called = 0;
}

/**
 * @brief Dispose of a channel
 *
 * @param[in] chan The channel to dispose of
 */
void nfs_rpc_destroy_chan(rpc_call_channel_t *chan)
{
	assert(chan);

	pthread_mutex_lock(&chan->mtx);

	_nfs_rpc_destroy_chan(chan);

	pthread_mutex_unlock(&chan->mtx);
}

/**
 * Call the NFSv4 client's CB_NULL procedure.
 *
 * @param[in] chan    Channel on which to call
 * @param[in] timeout Timeout for client call
 * @param[in] locked  True if the channel is already locked
 *
 * @return Client status.
 */

enum clnt_stat rpc_cb_null(rpc_call_channel_t *chan, struct timeval timeout,
			   bool locked)
{
	enum clnt_stat stat = RPC_SUCCESS;

	/* XXX TI-RPC does the signal masking */
	if (!locked)
		pthread_mutex_lock(&chan->mtx);

	if (!chan->clnt) {
		stat = RPC_INTR;
		goto unlock;
	}

	stat = clnt_call(chan->clnt, chan->auth,
			 CB_NULL, (xdrproc_t) xdr_void,
			 NULL, (xdrproc_t) xdr_void, NULL, timeout);

	/* If a call fails, we have to assume path down, or equally fatal
	 * error.  We may need back-off. */
	if (stat != RPC_SUCCESS)
		_nfs_rpc_destroy_chan(chan);

 unlock:
	if (!locked)
		pthread_mutex_unlock(&chan->mtx);

	return stat;
}

/**
 * @brief Free callback arguments
 *
 * @param[in] op The argop to free
 */

static inline void free_argop(nfs_cb_argop4 *op)
{
	gsh_free(op);
}

/**
 * @brief Free callback result
 *
 * @param[in] op The resop to free
 */

static inline void free_resop(nfs_cb_resop4 *op)
{
	gsh_free(op);
}

/**
 * @brief Allocate an RPC call
 *
 * @return The newly allocated call or NULL.
 */

rpc_call_t *alloc_rpc_call(void)
{
	rpc_call_t *call;

	call = pool_alloc(rpc_call_pool, NULL);

	return call;
}

/**
 * @brief Fre an RPC call
 *
 * @param[in] call The call to free
 */
void free_rpc_call(rpc_call_t *call)
{
	free_argop(call->cbt.v_u.v4.args.argarray.argarray_val);
	free_resop(call->cbt.v_u.v4.res.resarray.resarray_val);
	pool_free(rpc_call_pool, call);
}

/**
 * @brief Completion hook
 *
 * If a call has been supplied to handle the result, call the supplied
 * hook. Otherwise, a no-op.
 *
 * @param[in] call  The RPC call
 * @param[in] hook  The call hook
 * @param[in] arg   Supplied arguments
 * @param[in] flags Any flags
 */
static inline void RPC_CALL_HOOK(rpc_call_t *call, rpc_call_hook hook,
				 void *arg, uint32_t flags)
{
	if (call)
		call->call_hook(call, hook, arg, flags);
}

/**
 * @brief Fire off an RPC call
 *
 * @param[in] call           The constructed call
 * @param[in] completion_arg Argument to completion function
 * @param[in] flags          Control flags for call
 *
 * @return 0 or POSIX error codes.
 */
int32_t nfs_rpc_submit_call(rpc_call_t *call, void *completion_arg,
			    uint32_t flags)
{
	int32_t code = 0;
	request_data_t *nfsreq = NULL;
	rpc_call_channel_t *chan = call->chan;

	assert(chan);

	call->completion_arg = completion_arg;
	if (flags & NFS_RPC_CALL_INLINE) {
		code = nfs_rpc_dispatch_call(call, NFS_RPC_CALL_NONE);
	} else {
		nfsreq = nfs_rpc_get_nfsreq(0 /* flags */);
		pthread_mutex_lock(&call->we.mtx);
		call->states = NFS_CB_CALL_QUEUED;
		nfsreq->rtype = NFS_CALL;
		nfsreq->r_u.call = call;
		nfs_rpc_enqueue_req(nfsreq);
		pthread_mutex_unlock(&call->we.mtx);
	}

	return code;
}

/**
 * @brief Dispatch a call
 *
 * @param[in,out] call  The call to dispatch
 * @param[in]     flags Flags governing call
 *
 * @return 0 or POSIX errors.
 */

int32_t nfs_rpc_dispatch_call(rpc_call_t *call, uint32_t flags)
{
	int code = 0;
	struct timeval CB_TIMEOUT = { 15, 0 };	/* XXX */
	rpc_call_hook hook_status = RPC_CALL_COMPLETE;

	/* send the call, set states, wake waiters, etc */
	pthread_mutex_lock(&call->we.mtx);

	switch (call->states) {
	case NFS_CB_CALL_DISPATCH:
	case NFS_CB_CALL_FINISHED:
		/* XXX invalid entry states for nfs_rpc_dispatch_call */
		abort();
	}

	call->states = NFS_CB_CALL_DISPATCH;
	pthread_mutex_unlock(&call->we.mtx);

	/* XXX TI-RPC does the signal masking */
	pthread_mutex_lock(&call->chan->mtx);

	if (!call->chan->clnt) {
		call->stat = RPC_INTR;
		goto unlock;
	}

	call->stat = clnt_call(call->chan->clnt,
			       call->chan->auth, CB_COMPOUND,
			       (xdrproc_t) xdr_CB_COMPOUND4args,
			       &call->cbt.v_u.v4.args,
			       (xdrproc_t) xdr_CB_COMPOUND4res,
			       &call->cbt.v_u.v4.res,
			       CB_TIMEOUT);

	/* If a call fails, we have to assume path down, or equally fatal
	 * error.  We may need back-off. */
	if (call->stat != RPC_SUCCESS) {
		_nfs_rpc_destroy_chan(call->chan);
		hook_status = RPC_CALL_ABORT;
	}

 unlock:
	pthread_mutex_unlock(&call->chan->mtx);

	/* signal waiter(s) */
	pthread_mutex_lock(&call->we.mtx);
	call->states |= NFS_CB_CALL_FINISHED;

	/* broadcast will generally be inexpensive */
	if (call->flags & NFS_RPC_CALL_BROADCAST)
		pthread_cond_broadcast(&call->we.cv);
	pthread_mutex_unlock(&call->we.mtx);

	/* call completion hook */
	RPC_CALL_HOOK(call, hook_status, call->completion_arg,
		      NFS_RPC_CALL_NONE);

	return code;
}

/**
 * @brief Abort a call
 *
 * @param[in] call The call to abort
 *
 * @todo function doesn't seem to do anything.
 *
 * @return But it does it successfully.
 */

int32_t nfs_rpc_abort_call(rpc_call_t *call)
{
	return 0;
}

/**
 * @brief Construct a CB_COMPOUND for v41
 *
 * This function constructs a compound with a CB_SEQUENCE and one
 * other operation.
 *
 * @param[in] session Session on whose back channel we make the call
 * @param[in] op      The operation to add
 * @param[in] refer   Referral data, NULL if none
 * @param[in] slot    Slot number to use
 *
 * @return The constructed call or NULL.
 */
static rpc_call_t *construct_single_call(nfs41_session_t *session,
					 nfs_cb_argop4 *op,
					 struct state_refer *refer,
					 slotid4 slot, slotid4 highest_slot)
{
	rpc_call_t *call = alloc_rpc_call();
	nfs_cb_argop4 sequenceop;
	CB_SEQUENCE4args *sequence = &sequenceop.nfs_cb_argop4_u.opcbsequence;

	if (!call)
		return NULL;

	call->chan = &session->cb_chan;
	cb_compound_init_v4(&call->cbt, 2,
			    session->clientid_record->cid_minorversion, 0, NULL,
			    0);
	memset(sequence, 0, sizeof(CB_SEQUENCE4args));
	sequenceop.argop = NFS4_OP_CB_SEQUENCE;

	memcpy(sequence->csa_sessionid, session->session_id,
	       NFS4_SESSIONID_SIZE);
	sequence->csa_sequenceid = session->cb_slots[slot].sequence;
	sequence->csa_slotid = slot;
	sequence->csa_highest_slotid = highest_slot;
	sequence->csa_cachethis = false;
	if (refer) {
		referring_call_list4 *list =
		    gsh_calloc(1, sizeof(referring_call_list4));
		referring_call4 *ref_call = NULL;
		if (!list) {
			free_rpc_call(call);
			return NULL;
		}
		ref_call = gsh_malloc(sizeof(referring_call4));
		if (!ref_call) {
			gsh_free(list);
			free_rpc_call(call);
			return NULL;
		}
		sequence->
		    csa_referring_call_lists.csa_referring_call_lists_len = 1;
		sequence->
		    csa_referring_call_lists.csa_referring_call_lists_val =
		    list;
		memcpy(list->rcl_sessionid, refer->session,
		       sizeof(NFS4_SESSIONID_SIZE));
		list->rcl_referring_calls.rcl_referring_calls_len = 1;
		list->rcl_referring_calls.rcl_referring_calls_val = ref_call;
		ref_call->rc_sequenceid = refer->sequence;
		ref_call->rc_slotid = refer->slot;
	} else {
		sequence->csa_referring_call_lists.
		    csa_referring_call_lists_len = 0;
		sequence->csa_referring_call_lists.
		    csa_referring_call_lists_val = NULL;
	}
	cb_compound_add_op(&call->cbt, &sequenceop);
	cb_compound_add_op(&call->cbt, op);

	return call;
}

/**
 * @brief Free a CB call and sequence
 *
 * @param[in] call The call to free
 *
 * @return The constructed call or NULL.
 */
static void free_single_call(rpc_call_t *call)
{
	CB_SEQUENCE4args *sequence =
	    (&call->cbt.v_u.v4.args.argarray.argarray_val[0].nfs_cb_argop4_u.
	     opcbsequence);
	if (sequence->csa_referring_call_lists.csa_referring_call_lists_val) {
		if (sequence->csa_referring_call_lists.
		    csa_referring_call_lists_val->
		    rcl_referring_calls.rcl_referring_calls_val) {
			gsh_free(sequence->csa_referring_call_lists.
				 csa_referring_call_lists_val->
				 rcl_referring_calls.
				 rcl_referring_calls_val);
		}
		gsh_free(sequence->csa_referring_call_lists.
			 csa_referring_call_lists_val);
	}
	free_rpc_call(call);
}

/**
 * @brief Find a callback slot
 *
 * Find and reserve a slot, if we can.  If @c wait is set to true, we
 * wait on the condition variable for a limited time.
 *
 * @param[in,out] session      Sesson on which to operate
 * @param[in]     wait         Whether to wait on the condition variable if
 *                             no slot can be found
 * @param[out]    slot         Slot to use
 * @param[out]    highest_slot Highest slot in use
 *
 * @retval false if a slot was not found.
 * @retval true if a slot was found.
 */
static bool find_cb_slot(nfs41_session_t *session, bool wait, slotid4 *slot,
			 slotid4 *highest_slot)
{
	slotid4 cur = 0;
	bool found = false;

	pthread_mutex_lock(&session->cb_mutex);
 retry:
	for (cur = 0;
	     cur < MIN(session->back_channel_attrs.ca_maxrequests,
		       NFS41_NB_SLOTS); ++cur) {
		if (!(session->cb_slots[cur].in_use) && (!found)) {
			found = true;
			*slot = cur;
			*highest_slot = cur;
		}
		if (session->cb_slots[cur].in_use)
			*highest_slot = cur;
	}

	if (!found && wait) {
		struct timespec ts;
		bool woke = false;

		clock_gettime(CLOCK_REALTIME, &ts);
		timespec_addms(&ts, 100);

		woke =
		    (pthread_cond_timedwait
		     (&session->cb_cond, &session->cb_mutex, &ts) != ETIMEDOUT);
		if (woke) {
			wait = false;
			goto retry;
		}
	}

	if (found) {
		session->cb_slots[*slot].in_use = true;
		++session->cb_slots[*slot].sequence;
		assert(*slot < session->back_channel_attrs.ca_maxrequests);
	}
	pthread_mutex_unlock(&session->cb_mutex);

	return found;
}

/**
 * @brief Release a reserved callback slot and wake waiters
 *
 * @param[in,out] session Session holding slot to release
 * @param[in]     slot    Slot to release
 * @param[in]     bool    Whether the operation was ever sent
 */

static void release_cb_slot(nfs41_session_t *session, slotid4 slot, bool sent)
{
	pthread_mutex_lock(&session->cb_mutex);
	session->cb_slots[slot].in_use = false;
	if (!sent)
		--session->cb_slots[slot].sequence;
	pthread_cond_broadcast(&session->cb_cond);
	pthread_mutex_unlock(&session->cb_mutex);
}

/**
 * @brief Send v4.1 CB_COMPOUND with a single operation
 *
 * This actually sends two opearations, a CB_SEQUENCE and the supplied
 * operation.  It works as a convenience function to handle the
 * details of CB_SEQUENCE management, finding a connection with a
 * working back channel, and so forth.
 *
 * @note This should work for most practical purposes, but is not
 * ideal.  What we ought to have is a per-clientid queue that
 * operations can be submitted to that will be sent when a
 * back-channel is re-established, with a per-session queue for
 * operations that were sent but had the back-channel fail before the
 * response was received.
 *
 * @param[in] clientid       Client record
 * @param[in] op             The operation to perform
 * @param[in] refer          Referral tracking info (or NULL)
 * @param[in] completion     Completion function for this operation
 * @param[in] completion_arg Argument provided to completion hook
 * @param[in] free_op        Function to free elements of the op (may be
 *                           NULL.) Only called on error, so it should
 *                           also be called explicitly from the completion
 *                           function.
 *
 * @return POSIX error codes.
 */
int nfs_rpc_v41_single(nfs_client_id_t *clientid, nfs_cb_argop4 *op,
		       struct state_refer *refer,
		       int32_t(*completion) (rpc_call_t *, rpc_call_hook,
					     void *arg, uint32_t flags),
		       void *completion_arg,
		       void (*free_op) (nfs_cb_argop4 *op))
{
	int scan = 0;
	bool sent = false;
	struct glist_head *glist = NULL;

	if (clientid->cid_minorversion < 1)
		return EINVAL;

	for (scan = 0; scan < 2; ++scan) {
		/**@ todo ??? pthread_mutex_lock(&found->cid_mutex); */
		glist_for_each(glist, &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(glist,
							       nfs41_session_t,
							       session_link);
			if (!(session->flags & session_bc_up))
				continue;
			rpc_call_channel_t *chan = &session->cb_chan;
			slotid4 slot = 0;
			slotid4 highest_slot = 0;
			rpc_call_t *call = NULL;
			int code = 0;
			if (!
			    (find_cb_slot
			     (session, scan == 1, &slot, &highest_slot))) {
				continue;
			}
			call =
			    construct_single_call(session, op, refer, slot,
						  highest_slot);
			if (!call) {
				release_cb_slot(session, slot, false);
				return ENOMEM;
			}
			call->call_hook = completion;
			code =
			    nfs_rpc_submit_call(call, completion_arg,
						NFS_RPC_FLAG_NONE);
			if (code != 0) {
				/* Clean up... */
				free_single_call(call);
				release_cb_slot(session, slot, false);
				pthread_mutex_lock(&chan->mtx);
				nfs_rpc_destroy_v41_chan(chan);
				session->flags &= ~session_bc_up;
				pthread_mutex_unlock(&chan->mtx);
			} else {
				sent = true;
				goto out;
			}
		}
		/**@ todo ??? pthread_mutex_unlock(&found->cid_mutex); */
	}

 out:
	return (sent) ? 0 : ENOTCONN;
}

/**
 * @brief Free information associated with any 'single' call
 */

void nfs41_complete_single(rpc_call_t *call, rpc_call_hook hook, void *arg,
			   uint32_t flags)
{
	release_cb_slot(call->chan->source.session,
			call->cbt.v_u.v4.args.argarray.argarray_val[0]
			.nfs_cb_argop4_u.opcbsequence.csa_slotid, true);
	free_single_call(call);
}

/**
 * @brief test the state of callback channel for a clientid using NULL.
 * @return  enum clnt_stat
 */

enum clnt_stat nfs_test_cb_chan(nfs_client_id_t *pclientid)
{
	int32_t tries;
	struct timeval CB_TIMEOUT = {15, 0};
	rpc_call_channel_t *chan;
	enum clnt_stat stat = RPC_SUCCESS;
	assert(pclientid);
	/* create (fix?) channel */
	for (tries = 0; tries < 2; ++tries) {

		chan = nfs_rpc_get_chan(pclientid, NFS_RPC_FLAG_NONE);
		if (!chan) {
			LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
			stat = RPC_SYSTEMERROR;
			goto out;
		}

		if (!chan->clnt) {
			LogCrit(COMPONENT_NFS_CB,
				"nfs_rpc_get_chan failed (no clnt)");
			stat = RPC_SYSTEMERROR;
			goto out;
		}

		/* try the CB_NULL proc -- inline here, should be ok-ish */
		stat = rpc_cb_null(chan, CB_TIMEOUT, false);
		LogDebug(COMPONENT_NFS_CB,
			"rpc_cb_null on client %p returns %d",
			pclientid, stat);

		/* RPC_INTR indicates that we should refresh the
		 *      * channel and retry */
		if (stat != RPC_INTR)
			break;
	}

out:
	return stat;
}
