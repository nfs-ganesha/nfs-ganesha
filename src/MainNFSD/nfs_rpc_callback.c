// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright (C) 2012, The Linux Box Corporation
 * Copyright (c) 2012-2018 Red Hat, Inc. and/or its affiliates.
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *               William Allen Simpson <william.allen.simpson@gmail.com>
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
#include "log.h"
#include "nfs_rpc_callback.h"
#include "nfs4.h"
#ifdef _HAVE_GSSAPI
#include "gss_credcache.h"
#endif /* _HAVE_GSSAPI */
#include "sal_data.h"
#include "sal_functions.h"
#include <misc/timespec.h>

const struct __netid_nc_table netid_nc_table[9] = {
	{
	"-", _NC_ERR, 0}, {
	"tcp", _NC_TCP, AF_INET}, {
	"tcp6", _NC_TCP6, AF_INET6}, {
	"rdma", _NC_RDMA, AF_INET}, {
	"rdma6", _NC_RDMA6, AF_INET6}, {
	"sctp", _NC_SCTP, AF_INET}, {
	"sctp6", _NC_SCTP6, AF_INET6}, {
	"udp", _NC_UDP, AF_INET}, {
	"udp6", _NC_UDP6, AF_INET6},};

/* retry timeout default to the moon and back */
static const struct timespec tout = { 3, 0 };

/**
 * @brief Initialize the callback credential cache
 *
 * @param[in] ccache Location of credential cache
 */

#ifdef _HAVE_GSSAPI
static inline void nfs_rpc_cb_init_ccache(const char *ccache)
{
	int code;

	if (mkdir(ccache, 0700) < 0) {
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

	code =
		gssd_refresh_krb5_machine_credential(nfs_host_name,
			NULL, nfs_param.krb5_param.svc.principal);

	if (code)
		LogWarn(COMPONENT_INIT,
			"gssd_refresh_krb5_machine_credential failed (%d:%d)",
			code, errno);
}
#endif /* _HAVE_GSSAPI */

/**
 * @brief Initialize callback subsystem
 */
void nfs_rpc_cb_pkginit(void)
{
#ifdef _HAVE_GSSAPI
	/* ccache */
	nfs_rpc_cb_init_ccache(nfs_param.krb5_param.ccache_dir);

	/* sanity check GSSAPI */
	if (gssd_check_mechs() != 0)
		LogCrit(COMPONENT_INIT,
			"sanity check: gssd_check_mechs() failed");
#endif /* _HAVE_GSSAPI */
}

/**
 * @brief Shutdown callback subsystem
 */
void nfs_rpc_cb_pkgshutdown(void)
{
	/* return */
}

/**
 * @brief Convert a netid label
 *
 * @todo This is automatically redundant, but in fact upstream TI-RPC is
 * not up-to-date with RFC 5665, will fix (Matt)
 *
 * @param[in] netid The netid label dictating the protocol
 *
 * @return The numerical protocol identifier.
 */

nc_type nfs_netid_to_nc(const char *netid)
{
	if (!strcmp(netid, netid_nc_table[_NC_TCP6].netid))
		return _NC_TCP6;

	if (!strcmp(netid, netid_nc_table[_NC_TCP].netid))
		return _NC_TCP;

	if (!strcmp(netid, netid_nc_table[_NC_UDP6].netid))
		return _NC_UDP6;

	if (!strcmp(netid, netid_nc_table[_NC_UDP].netid))
		return _NC_UDP;

	if (!strcmp(netid, netid_nc_table[_NC_RDMA6].netid))
		return _NC_RDMA6;

	if (!strcmp(netid, netid_nc_table[_NC_RDMA].netid))
		return _NC_RDMA;

	if (!strcmp(netid, netid_nc_table[_NC_SCTP6].netid))
		return _NC_SCTP6;

	if (!strcmp(netid, netid_nc_table[_NC_SCTP].netid))
		return _NC_SCTP;

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
	int code;
	char *uaddr2 = gsh_strdup(uaddr);
	char *dot, *p1, *p2;
	uint16_t port;

	assert(clientid->cid_minorversion == 0);

	/* find the two port bytes and terminate the string */
	dot = strrchr(uaddr2, '.');

	if (dot == NULL)
		goto out;

	p2 = dot + 1;
	*dot = '\0';

	dot = strrchr(uaddr2, '.');

	if (dot == NULL)
		goto out;

	p1 = dot + 1;
	*dot = '\0';

	port = htons((atoi(p1) << 8) | atoi(p2));

	/* At this point, the port has been extracted and uaddr2 is now
	 * terminated without the port portion and is thus suitable for
	 * passing to inet_pton as is and will support a variety of formats.
	 */

	memset(&clientid->cid_cb.v40.cb_addr.ss, 0, sizeof(sockaddr_t));

	switch (clientid->cid_cb.v40.cb_addr.nc) {
	case _NC_TCP:
	case _NC_RDMA:
	case _NC_SCTP:
	case _NC_UDP:
	{
		/* IPv4 (ws inspired) */
		struct sockaddr_in *sin = ((struct sockaddr_in *)
					   &clientid->cid_cb.v40.cb_addr.ss);

		sin->sin_family = AF_INET;
		sin->sin_port = port;
		code = inet_pton(AF_INET, uaddr2, &sin->sin_addr);

		if (code != 1)
			LogWarn(COMPONENT_NFS_CB, "inet_pton failed (%d %s)",
				code, uaddr);
		else
			LogDebug(COMPONENT_NFS_CB,
				 "client callback addr:port %s:%d",
				 uaddr2, ntohs(port));

		break;

	}
	case _NC_TCP6:
	case _NC_RDMA6:
	case _NC_SCTP6:
	case _NC_UDP6:
	{
		/* IPv6 (ws inspired) */
		struct sockaddr_in6 *sin6 = ((struct sockaddr_in6 *)
					     &clientid->cid_cb.v40.cb_addr.ss);

		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = port;
		code = inet_pton(AF_INET6, uaddr2, &sin6->sin6_addr);

		if (code != 1)
			LogWarn(COMPONENT_NFS_CB,
				"inet_pton failed (%d %s)", code, uaddr);
		else
			LogDebug(COMPONENT_NFS_CB,
				 "client callback addr:port %s:%d",
				 uaddr2, ntohs(port));

		break;

	}
	default:
		/* unknown netid */
		break;
	};

out:

	gsh_free(uaddr2);
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
	if (strlcpy(clientid->cid_cb.v40.cb_client_r_addr, addr4->r_addr,
		    sizeof(clientid->cid_cb.v40.cb_client_r_addr))
	    >= sizeof(clientid->cid_cb.v40.cb_client_r_addr)) {
		LogCrit(COMPONENT_CLIENTID, "Callback r_addr %s too long",
			addr4->r_addr);
	}
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
	int nfd;
	int code;

	assert(clientid->cid_minorversion == 0);

	*fd = -1;
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
		return EINVAL;
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
		return EINVAL;
	}

	nfd = socket(domain, sock_type, protocol);
	if (nfd < 0) {
		code = errno;
		LogWarn(COMPONENT_NFS_CB,
			"socket failed %d (%s)", code, strerror(code));
		return code;
	}

	code = connect(nfd,
		       (struct sockaddr *)&clientid->cid_cb.v40.cb_addr.ss,
		       sock_size);

	if (code < 0) {
		code = errno;
		LogWarn(COMPONENT_NFS_CB, "connect fail errno %d (%s)",
			code, strerror(code));
		close(nfd);
		return code;
	}

	*proto = protocol;
	*fd = nfd;

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
	switch (flavor) {
	case RPCSEC_GSS:
	case AUTH_SYS:
	case AUTH_NONE:
		return true;
	default:
		return false;
	};
}

/**
 * @brief Kerberos OID
 *
 * This value comes from kerberos source, gssapi_krb5.c (Umich).
 */

#ifdef _HAVE_GSSAPI
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
	const char *qualifier = "nfs@";
	int qualifier_len = strlen(qualifier);
	void *sin;

	if (len < SOCK_NAME_MAX)
		return NULL;

	switch (chan->type) {
	case RPC_CHAN_V40:
		sin = &chan->source.clientid->cid_cb.v40.cb_addr.ss;
		break;
	default:
		return NULL;
	}

	memcpy(buf, qualifier, qualifier_len + 1);

	if (sprint_sockip(sin, buf + qualifier_len, len - qualifier_len))
		return buf;

	return NULL;
}
#endif /* _HAVE_GSSAPI */

/**
 * @brief Set up GSS on a callback channel
 *
 * @param[in,out] chan Channel on which to set up GSS
 * @param[in]     cred GSS Credential
 *
 * @return	auth->ah_error; check AUTH_FAILURE or AUTH_SUCCESS.
 */

#ifdef _HAVE_GSSAPI
static inline AUTH *nfs_rpc_callback_setup_gss(rpc_call_channel_t *chan,
					       nfs_client_cred_t *cred)
{
	AUTH *result;
	char hprinc[MAXPATHLEN + 1];
	char *principal = nfs_param.krb5_param.svc.principal;
	int32_t code;

	assert(cred->flavor == RPCSEC_GSS);

	/* MUST RFC 3530bis, section 3.3.3 */
	chan->gss_sec.svc = cred->auth_union.auth_gss.svc;
	chan->gss_sec.qop = cred->auth_union.auth_gss.qop;

	/* the GSSAPI k5 mech needs to find an unexpired credential
	 * for nfs/hostname in an accessible k5ccache */
	code = gssd_refresh_krb5_machine_credential(nfs_host_name,
						    NULL, principal);

	if (code) {
		LogWarn(COMPONENT_NFS_CB,
			"gssd_refresh_krb5_machine_credential failed (%d:%d)",
			code, errno);
		goto out_err;
	}

	if (!format_host_principal(chan, hprinc, sizeof(hprinc))) {
		code = errno;
		LogCrit(COMPONENT_NFS_CB, "format_host_principal failed");
		goto out_err;
	}

	chan->gss_sec.cred = GSS_C_NO_CREDENTIAL;
	chan->gss_sec.req_flags = 0;

	if (chan->gss_sec.svc != RPCSEC_GSS_SVC_NONE) {
		/* no more lipkey, spkm3 */
		chan->gss_sec.mech = (gss_OID) & krb5oid;
		chan->gss_sec.req_flags = GSS_C_MUTUAL_FLAG;	/* XXX */
		result = authgss_ncreate_default(chan->clnt, hprinc,
						 &chan->gss_sec);
	} else {
		result = authnone_ncreate();
	}

	return result;

out_err:
	result = authnone_ncreate_dummy();
	result->ah_error.re_status = RPC_SYSTEMERROR;
	result->ah_error.re_errno = code;
	return result;
}
#endif /* _HAVE_GSSAPI */

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
	rpc_call_channel_t *chan = &clientid->cid_cb.v40.cb_chan;
	char *err;
	struct netbuf raddr;
	int fd;
	int proto;
	int code;

	assert(!chan->clnt);
	assert(clientid->cid_minorversion == 0);

	/* XXX we MUST error RFC 3530bis, sec. 3.3.3 */
	if (!supported_auth_flavor(clientid->cid_credential.flavor))
		return EINVAL;

	chan->type = RPC_CHAN_V40;
	chan->source.clientid = clientid;

	code = nfs_clid_connected_socket(clientid, &fd, &proto);
	if (code) {
		LogWarn(COMPONENT_NFS_CB, "Failed creating socket");
		return code;
	}

	raddr.buf = &clientid->cid_cb.v40.cb_addr.ss;

	switch (proto) {
	case IPPROTO_TCP:
		raddr.maxlen = raddr.len = sizeof(struct sockaddr_in);
		chan->clnt = clnt_vc_ncreatef(fd, &raddr,
					      clientid->cid_cb.v40.cb_program,
					      NFS_CB /* Errata ID: 2291 */,
					      0, 0,
					      CLNT_CREATE_FLAG_CLOSE |
					      CLNT_CREATE_FLAG_CONNECT);
		break;
	case IPPROTO_UDP:
		raddr.maxlen = raddr.len = sizeof(struct sockaddr_in6);
		chan->clnt = clnt_dg_ncreatef(fd, &raddr,
					      clientid->cid_cb.v40.cb_program,
					      NFS_CB /* Errata ID: 2291 */,
					      0, 0,
					      CLNT_CREATE_FLAG_CLOSE);
		break;
	default:
		break;
	}

	if (CLNT_FAILURE(chan->clnt)) {
		err = rpc_sperror(&chan->clnt->cl_error, "failed");

		LogDebug(COMPONENT_NFS_CB, "%s", err);
		gsh_free(err);
		CLNT_DESTROY(chan->clnt);
		chan->clnt = NULL;
		close(fd);
		return EINVAL;
	}

	/* channel protection */
	switch (clientid->cid_credential.flavor) {
#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		chan->auth = nfs_rpc_callback_setup_gss(chan,
						&clientid->cid_credential);
		break;
#endif /* _HAVE_GSSAPI */
	case AUTH_SYS:
		chan->auth = authunix_ncreate_default();
		break;
	case AUTH_NONE:
		chan->auth = authnone_ncreate();
		break;
	default:
		return EINVAL;
	}

	if (AUTH_FAILURE(chan->auth)) {
		err = rpc_sperror(&chan->auth->ah_error, "failed");

		LogDebug(COMPONENT_NFS_CB, "%s", err);
		gsh_free(err);
		AUTH_DESTROY(chan->auth);
		chan->auth = NULL;
		CLNT_DESTROY(chan->clnt);
		chan->clnt = NULL;
		return EINVAL;
	}
	return 0;
}

/**
 * @brief Dispose of a channel
 *
 * The caller should hold the channel mutex.
 *
 * @param[in] chan The channel to dispose of
 */
static void _nfs_rpc_destroy_chan(rpc_call_channel_t *chan)
{
	assert(chan);

	/* clean up auth, if any */
	if (chan->auth) {
		AUTH_DESTROY(chan->auth);
		chan->auth = NULL;
	}

	/* channel has a dedicated RPC client */
	if (chan->clnt) {
		/* destroy it */
		CLNT_DESTROY(chan->clnt);
		chan->clnt = NULL;
	}

	chan->last_called = 0;
}

/**
 * Call the NFSv4 client's CB_NULL procedure.
 *
 * @param[in] chan    Channel on which to call
 * @param[in] timeout The timeout for client call
 * @param[in] locked  True if the channel is already locked
 *
 * @return Client status.
 */

static enum clnt_stat rpc_cb_null(rpc_call_channel_t *chan, bool locked)
{
	struct clnt_req *cc;
	enum clnt_stat stat;

	/* XXX TI-RPC does the signal masking */
	if (!locked)
		PTHREAD_MUTEX_lock(&chan->mtx);

	if (!chan->clnt) {
		stat = RPC_INTR;
		goto unlock;
	}

	cc = gsh_malloc(sizeof(*cc));
	clnt_req_fill(cc, chan->clnt, chan->auth, CB_NULL,
		      (xdrproc_t) xdr_void, NULL,
		      (xdrproc_t) xdr_void, NULL);
	stat = clnt_req_setup(cc, tout);
	if (stat == RPC_SUCCESS) {
		cc->cc_refreshes = 1;
		stat = CLNT_CALL_WAIT(cc);
	}
	clnt_req_release(cc);

	/* If a call fails, we have to assume path down, or equally fatal
	 * error.  We may need back-off. */
	if (stat != RPC_SUCCESS)
		_nfs_rpc_destroy_chan(chan);

 unlock:
	if (!locked)
		PTHREAD_MUTEX_unlock(&chan->mtx);

	return stat;
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

int nfs_rpc_create_chan_v41(SVCXPRT *xprt, nfs41_session_t *session,
			    int num_sec_parms, callback_sec_parms4 *sec_parms)
{
	rpc_call_channel_t *chan = &session->cb_chan;
	char *err;
	int i;
	int code = 0;
	bool authed = false;

	PTHREAD_MUTEX_lock(&chan->mtx);

	if (chan->clnt) {
		/* Something better later. */
		code = EEXIST;
		goto out;
	}

	chan->type = RPC_CHAN_V41;
	chan->source.session = session;

	assert(xprt);

	if (svc_get_xprt_type(xprt) == XPRT_RDMA) {
		LogWarn(COMPONENT_NFS_CB,
			"refusing to create back channel over RDMA for now");
		code = EINVAL;
		goto out;
	}

	/* connect an RPC client
	 * Use version 1 per errata ID 2291 for RFC 5661
	 */
	chan->clnt = clnt_vc_ncreate_svc(xprt, session->cb_program,
					 NFS_CB /* Errata ID: 2291 */,
					 CLNT_CREATE_FLAG_NONE);

	if (CLNT_FAILURE(chan->clnt)) {
		err = rpc_sperror(&chan->clnt->cl_error, "failed");

		LogDebug(COMPONENT_NFS_CB, "%s", err);
		gsh_free(err);
		CLNT_DESTROY(chan->clnt);
		chan->clnt = NULL;
		code = EINVAL;
		goto out;
	}

	for (i = 0; i < num_sec_parms; ++i) {
		if (sec_parms[i].cb_secflavor == AUTH_NONE) {
			chan->auth = authnone_ncreate();
			authed = true;
			break;
		} else if (sec_parms[i].cb_secflavor == AUTH_SYS) {
			struct authunix_parms *sys_parms =
			    &sec_parms[i].callback_sec_parms4_u.cbsp_sys_cred;

			chan->auth = authunix_ncreate(sys_parms->aup_machname,
						      sys_parms->aup_uid,
						      sys_parms->aup_gid,
						      sys_parms->aup_len,
						      sys_parms->aup_gids);
			if (AUTH_SUCCESS(chan->auth)) {
				authed = true;
				break;
			}
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
		err = rpc_sperror(&chan->auth->ah_error, "failed");

		LogDebug(COMPONENT_NFS_CB, "%s", err);
		gsh_free(err);
		AUTH_DESTROY(chan->auth);
		chan->auth = NULL;
	}

	if (!authed) {
		code = EPERM;
		LogMajor(COMPONENT_NFS_CB, "No working auth in sec_params.");
		goto out;
	}

	atomic_set_uint32_t_bits(&session->flags, session_bc_up);

 out:
	if (code != 0) {
		LogWarn(COMPONENT_NFS_CB,
			"can not create back channel, code %d", code);
		if (chan->clnt)
			_nfs_rpc_destroy_chan(chan);
	}

	PTHREAD_MUTEX_unlock(&chan->mtx);

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
	rpc_call_channel_t *chan;
	struct glist_head *glist;
	nfs41_session_t *session;

	if (clientid->cid_minorversion == 0) {
		chan = &clientid->cid_cb.v40.cb_chan;
		if (!chan->clnt) {
			if (nfs_rpc_create_chan_v40(clientid, flags)) {
				chan = NULL;
			}
		}
		return chan;
	}

	/* Get the first working back channel we have */
	chan = NULL;
	pthread_mutex_lock(&clientid->cid_mutex);
	glist_for_each(glist, &clientid->cid_cb.v41.cb_session_list) {
		session = glist_entry(glist, nfs41_session_t, session_link);
		if (atomic_fetch_uint32_t(&session->flags) & session_bc_up) {
			chan = &session->cb_chan;
			break;
		}
	}
	pthread_mutex_unlock(&clientid->cid_mutex);

	return chan;
}

/**
 * @brief Dispose of a channel
 *
 * @param[in] chan The channel to dispose of
 */
void nfs_rpc_destroy_chan(rpc_call_channel_t *chan)
{
	assert(chan);

	PTHREAD_MUTEX_lock(&chan->mtx);

	_nfs_rpc_destroy_chan(chan);

	PTHREAD_MUTEX_unlock(&chan->mtx);
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

struct _rpc_call *alloc_rpc_call(void)
{
	struct _rpc_call *call = gsh_calloc(1, sizeof(struct _rpc_call));

	(void) atomic_inc_uint64_t(&nfs_health_.enqueued_reqs);

	return call;
}

/**
 * @brief Free an RPC call
 *
 * @param[in] call The call to free
 */
void free_rpc_call(rpc_call_t *call)
{
	free_argop(call->cbt.v_u.v4.args.argarray.argarray_val);
	free_resop(call->cbt.v_u.v4.res.resarray.resarray_val);

	clnt_req_release(&call->call_req);
}

/**
 * @brief Free the RPC call context
 *
 * @param[in] cc The call context to free
 */
static void nfs_rpc_call_free(struct clnt_req *cc, size_t unused)
{
	rpc_call_t *call = container_of(cc, struct _rpc_call, call_req);

	gsh_free(call);
	(void) atomic_inc_uint64_t(&nfs_health_.dequeued_reqs);
}

/**
 * @brief Call response processing
 *
 * @param[in] cc  The RPC call request context
 */
static void nfs_rpc_call_process(struct clnt_req *cc)
{
	rpc_call_t *call = container_of(cc, rpc_call_t, call_req);

	/* always TCP for retries, cc_refreshes only for AUTH_REFRESH()
	 */
	if (cc->cc_error.re_status == RPC_AUTHERROR
	 && cc->cc_refreshes-- > 0
	 && AUTH_REFRESH(cc->cc_auth, NULL)) {
		if (clnt_req_refresh(cc) == RPC_SUCCESS) {
			cc->cc_error.re_status = CLNT_CALL_BACK(cc);
			return;
		}
	}

	call->states |= NFS_CB_CALL_FINISHED;

	if (call->call_hook)
		call->call_hook(call);

	free_rpc_call(call);
}

/**
 * @brief Dispatch a call
 *
 * @param[in,out] call  The call to dispatch
 * @param[in]     flags The flags governing call
 *
 * @return enum clnt_stat.
 */

enum clnt_stat nfs_rpc_call(rpc_call_t *call, uint32_t flags)
{
	struct clnt_req *cc = &call->call_req;

	call->states = NFS_CB_CALL_DISPATCH;

	/* XXX TI-RPC does the signal masking */
	PTHREAD_MUTEX_lock(&call->chan->mtx);

	clnt_req_fill(cc, call->chan->clnt, call->chan->auth, CB_COMPOUND,
		      (xdrproc_t) xdr_CB_COMPOUND4args, &call->cbt.v_u.v4.args,
		      (xdrproc_t) xdr_CB_COMPOUND4res, &call->cbt.v_u.v4.res);
	cc->cc_size = sizeof(nfs_request_t);
	cc->cc_free_cb = nfs_rpc_call_free;

	if (!call->chan->clnt) {
		cc->cc_error.re_status = RPC_INTR;
		goto unlock;
	}
	if (clnt_req_setup(cc, tout) == RPC_SUCCESS) {
		cc->cc_process_cb = nfs_rpc_call_process;
		cc->cc_error.re_status = CLNT_CALL_BACK(cc);
	}

	/* If a call fails, we have to assume path down, or equally fatal
	 * error.  We may need back-off. */
	if (cc->cc_error.re_status != RPC_SUCCESS) {
		_nfs_rpc_destroy_chan(call->chan);
		call->states |= NFS_CB_CALL_ABORTED;
	}

 unlock:
	PTHREAD_MUTEX_unlock(&call->chan->mtx);

	/* any broadcast or signalling done in completion function */
	return cc->cc_error.re_status;
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
 * @param[in] session The session on whose back channel we make the call
 * @param[in] op      The operation to add
 * @param[in] refer   Referral data, NULL if none
 * @param[in] slot    Slot number to use
 *
 * @return The constructed call or NULL.
 */
static rpc_call_t *construct_v41(nfs41_session_t *session,
				 nfs_cb_argop4 *op,
				 struct state_refer *refer,
				 slotid4 slot, slotid4 highest_slot)
{
	rpc_call_t *call = alloc_rpc_call();
	nfs_cb_argop4 sequenceop;
	CB_SEQUENCE4args *sequence = &sequenceop.nfs_cb_argop4_u.opcbsequence;
	const uint32_t minor = session->clientid_record->cid_minorversion;

	call->chan = &session->cb_chan;
	cb_compound_init_v4(&call->cbt, 2, minor, 0, NULL, 0);

	memset(sequence, 0, sizeof(CB_SEQUENCE4args));
	sequenceop.argop = NFS4_OP_CB_SEQUENCE;

	memcpy(sequence->csa_sessionid, session->session_id,
	       NFS4_SESSIONID_SIZE);
	sequence->csa_sequenceid = session->bc_slots[slot].sequence;
	sequence->csa_slotid = slot;
	sequence->csa_highest_slotid = highest_slot;
	sequence->csa_cachethis = false;

	if (refer) {
		referring_call_list4 *list;
		referring_call4 *ref_call = NULL;

		list = gsh_calloc(1, sizeof(referring_call_list4));

		ref_call = gsh_malloc(sizeof(referring_call4));

		sequence->csa_referring_call_lists.csarcl_len = 1;
		sequence->csa_referring_call_lists.csarcl_val = list;
		memcpy(list->rcl_sessionid, refer->session,
		       sizeof(NFS4_SESSIONID_SIZE));
		list->rcl_referring_calls.rcl_referring_calls_len = 1;
		list->rcl_referring_calls.rcl_referring_calls_val = ref_call;
		ref_call->rc_sequenceid = refer->sequence;
		ref_call->rc_slotid = refer->slot;
	} else {
		sequence->csa_referring_call_lists.csarcl_len = 0;
		sequence->csa_referring_call_lists.csarcl_val = NULL;
	}
	cb_compound_add_op(&call->cbt, &sequenceop);
	cb_compound_add_op(&call->cbt, op);

	return call;
}

/**
 * @brief Free a CB sequence for v41
 *
 * @param[in] call The call to free
 */
static void release_v41(rpc_call_t *call)
{
	nfs_cb_argop4 *argarray_val =
		call->cbt.v_u.v4.args.argarray.argarray_val;
	CB_SEQUENCE4args *sequence =
		&argarray_val[0].nfs_cb_argop4_u.opcbsequence;
	referring_call_list4 *call_lists =
		sequence->csa_referring_call_lists.csarcl_val;

	if (call_lists == NULL)
		return;

	gsh_free(call_lists->rcl_referring_calls.rcl_referring_calls_val);
	gsh_free(call_lists);
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

	PTHREAD_MUTEX_lock(&session->cb_mutex);
 retry:
	for (cur = 0;
	     cur < MIN(session->back_channel_attrs.ca_maxrequests,
		       session->nb_slots);
	     ++cur) {

		if (!(session->bc_slots[cur].in_use) && (!found)) {
			found = true;
			*slot = cur;
			*highest_slot = cur;
		}

		if (session->bc_slots[cur].in_use)
			*highest_slot = cur;
	}

	if (!found && wait) {
		struct timespec ts;
		bool woke = false;

		clock_gettime(CLOCK_REALTIME, &ts);
		timespec_addms(&ts, 100);

		woke = (pthread_cond_timedwait(&session->cb_cond,
					       &session->cb_mutex,
					       &ts) != ETIMEDOUT);
		if (woke) {
			wait = false;
			goto retry;
		}
	}

	if (found) {
		session->bc_slots[*slot].in_use = true;
		++session->bc_slots[*slot].sequence;
		assert(*slot < session->back_channel_attrs.ca_maxrequests);
	}

	PTHREAD_MUTEX_unlock(&session->cb_mutex);
	return found;
}

/**
 * @brief Release a reserved callback slot and wake waiters
 *
 * @param[in,out] session The session holding slot to release
 * @param[in]     slot    Slot to release
 * @param[in]     bool    Whether the operation was ever sent
 */

static void release_cb_slot(nfs41_session_t *session, slotid4 slot, bool sent)
{
	PTHREAD_MUTEX_lock(&session->cb_mutex);
	session->bc_slots[slot].in_use = false;
	if (!sent)
		--session->bc_slots[slot].sequence;
	pthread_cond_broadcast(&session->cb_cond);
	PTHREAD_MUTEX_unlock(&session->cb_mutex);
}

static int nfs_rpc_v41_single(nfs_client_id_t *clientid, nfs_cb_argop4 *op,
		       struct state_refer *refer,
		       void (*completion)(rpc_call_t *),
		       void *completion_arg)
{
	struct glist_head *glist;
	int ret = ENOTCONN;
	bool wait = false;

restart:
	pthread_mutex_lock(&clientid->cid_mutex);
	glist_for_each(glist, &clientid->cid_cb.v41.cb_session_list) {
		nfs41_session_t *scur, *session;
		slotid4 slot = 0;
		slotid4 highest_slot = 0;
		rpc_call_t *call = NULL;

		scur = glist_entry(glist, nfs41_session_t, session_link);

		/*
		 * This is part of the infinite loop avoidance. When we
		 * attempt to use a session and that fails, we clear the
		 * session_bc_up flag.  Then, we can avoid that session until
		 * the backchannel has been reestablished.
		 */
		if (!(atomic_fetch_uint32_t(&scur->flags) & session_bc_up)) {
			LogDebug(COMPONENT_NFS_CB, "bc is down");
			continue;
		}

		/*
		 * We get a slot before we try to get a reference to the
		 * session, which is odd, but necessary, as we can't hold
		 * the cid_mutex when we go to put the session reference.
		 */
		if (!(find_cb_slot(scur, wait, &slot, &highest_slot))) {
			LogDebug(COMPONENT_NFS_CB, "can't get slot");
			continue;
		}

		/*
		 * Get a reference to the session.
		 *
		 * @todo: We don't really need to do the hashtable lookup
		 * here since we have a pointer, but it's currently the only
		 * safe way to get a reference.
		 */
		if (!nfs41_Session_Get_Pointer(scur->session_id, &session)) {
			release_cb_slot(scur, slot, false);
			continue;
		}

		assert(session == scur);

		/* Drop mutex since we have a session ref */
		pthread_mutex_unlock(&clientid->cid_mutex);

		call = construct_v41(session, op, refer, slot, highest_slot);

		call->call_hook = completion;
		call->call_arg = completion_arg;
		ret = nfs_rpc_call(call, NFS_RPC_CALL_NONE);
		if (ret == 0)
			return 0;

		/*
		 * Tear down channel since there is likely something
		 * wrong with it.
		 */
		LogDebug(COMPONENT_NFS_CB, "nfs_rpc_call failed: %d",
				ret);
		atomic_clear_uint32_t_bits(&session->flags, session_bc_up);

		release_v41(call);
		free_rpc_call(call);

		release_cb_slot(session, slot, false);
		dec_session_ref(session);
		goto restart;
	}
	pthread_mutex_unlock(&clientid->cid_mutex);

	/* If it didn't work, then try again and wait on a slot */
	if (ret && !wait) {
		wait = true;
		goto restart;
	}

	return ret;
}

/**
 * @brief Free information associated with any 'single' call
 */

void nfs41_release_single(rpc_call_t *call)
{
	release_cb_slot(call->chan->source.session,
			call->cbt.v_u.v4.args.argarray.argarray_val[0]
			.nfs_cb_argop4_u.opcbsequence.csa_slotid, true);
	dec_session_ref(call->chan->source.session);
	release_v41(call);
}

/**
 * @brief test the state of callback channel for a clientid using NULL.
 * @return  enum clnt_stat
 */

enum clnt_stat nfs_test_cb_chan(nfs_client_id_t *clientid)
{
	rpc_call_channel_t *chan;
	enum clnt_stat stat;
	int retries = 1;

	/* create (fix?) channel */
	do {
		chan = nfs_rpc_get_chan(clientid, NFS_RPC_FLAG_NONE);
		if (!chan) {
			LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
			return RPC_SYSTEMERROR;
		}

		if (!chan->clnt) {
			LogCrit(COMPONENT_NFS_CB,
				"nfs_rpc_get_chan failed (no clnt)");
			return RPC_SYSTEMERROR;
		}

		if (!chan->auth) {
			LogCrit(COMPONENT_NFS_CB,
				"nfs_rpc_get_chan failed (no auth)");
			return RPC_SYSTEMERROR;
		}

		/* try the CB_NULL proc -- inline here, should be ok-ish */
		stat = rpc_cb_null(chan, false);
		LogDebug(COMPONENT_NFS_CB,
			"rpc_cb_null on client %p returns %d", clientid, stat);

		/* RPC_INTR indicates that we should refresh the
		 * channel and retry */
	} while (stat == RPC_INTR && retries-- > 0);

	return stat;
}

static int nfs_rpc_v40_single(nfs_client_id_t *clientid, nfs_cb_argop4 *op,
		       void (*completion)(rpc_call_t *),
		       void *completion_arg)
{
	rpc_call_channel_t *chan;
	rpc_call_t *call;
	int rc;

	/* Attempt a recall only if channel state is UP */
	if (get_cb_chan_down(clientid)) {
		LogCrit(COMPONENT_NFS_CB,
			"Call back channel down, not issuing a recall");
		return ENOTCONN;
	}

	chan = nfs_rpc_get_chan(clientid, NFS_RPC_FLAG_NONE);
	if (!chan) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
		/* TODO: move this to nfs_rpc_get_chan ? */
		set_cb_chan_down(clientid, true);
		return ENOTCONN;
	}
	if (!chan->clnt) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
		set_cb_chan_down(clientid, true);
		return ENOTCONN;
	}
	if (!chan->auth) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no auth)");
		set_cb_chan_down(clientid, true);
		return ENOTCONN;
	}

	call = alloc_rpc_call();
	call->chan = chan;
	cb_compound_init_v4(&call->cbt, 1, 0,
			    clientid->cid_cb.v40.cb_callback_ident, NULL, 0);
	cb_compound_add_op(&call->cbt, op);
	call->call_hook = completion;
	call->call_arg = completion_arg;

	rc = nfs_rpc_call(call, NFS_RPC_CALL_NONE);
	if (rc)
		free_rpc_call(call);
	return rc;
}

/**
 * @brief Send CB_COMPOUND with a single operation
 *
 * In the case of v4.1+, this actually sends two opearations, a CB_SEQUENCE
 * and the supplied operation.  It works as a convenience function to handle
 * the details of callback management, finding a connection with a working
 * back channel, and so forth.
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
 * @param[in] c_arg          Argument provided to completion hook
 *
 * @return POSIX error codes.
 */
int nfs_rpc_cb_single(nfs_client_id_t *clientid, nfs_cb_argop4 *op,
		       struct state_refer *refer,
		       void (*completion)(rpc_call_t *),
		       void *c_arg)
{
	if (clientid->cid_minorversion == 0)
		return nfs_rpc_v40_single(clientid, op, completion, c_arg);
	return nfs_rpc_v41_single(clientid, op, refer, completion, c_arg);
}
