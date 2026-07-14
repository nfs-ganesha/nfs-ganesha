// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *             : M. Mohan Kumar <mohan@in.ibm.com>
 *
 * --------------------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *
 */

#include <stdlib.h>
#include "config.h"
#include <sys/utsname.h>
#include "gsh_config.h"
#include "nfs_core.h"
#include "abstract_atomic.h"
#include "nsm.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"

pthread_t nsm_notify_thread_p;
bool run_nsm_notify_thread;
pthread_mutex_t nsm_mutex;
pthread_cond_t nsm_cond;
CLIENT *nsm_clnt;
unsigned long nsm_count;
char *nodename;

struct glist_head local_nlm_info_list = GLIST_HEAD_INIT(local_nlm_info_list);
struct glist_head callback_wait_list = GLIST_HEAD_INIT(callback_wait_list);
struct local_nlm_info *nlm_callback_entry;
struct local_nlm_info *nlm_rpcbind_entry;
int cur_nsm_state;
int recov_nsm_state;

/* retry timeout default to the moon and back */
static const struct timespec tout = { 3, 0 };

#ifdef _INTERNAL_STATD
static const struct timespec tout5 = { 5, 0 };
#endif

bool nsm_connect(void)
{
	struct utsname utsname;

	if (nsm_clnt != NULL)
		return true;

	if (uname(&utsname) == -1) {
		LogCrit(COMPONENT_NLM, "uname failed with errno %d (%s)", errno,
			strerror(errno));
		return false;
	}

	nodename = gsh_strdup(utsname.nodename, MEM_COMP_PROTOCOL);

	nsm_clnt = clnt_ncreate("localhost", SM_PROG, SM_VERS, "tcp");

	if (CLNT_FAILURE(nsm_clnt)) {
		char *err = rpc_sperror(&nsm_clnt->cl_error, "failed");

		LogEventLimited(COMPONENT_NLM, "connect to statd %s", err);
		free_sperror(err);
		CLNT_DESTROY(nsm_clnt);
		nsm_clnt = NULL;
		gsh_free(nodename, MEM_COMP_PROTOCOL);
		nodename = NULL;
	}

	return nsm_clnt != NULL;
}

void nsm_disconnect(bool force)
{
	if ((nsm_count == 0 || force) && nsm_clnt != NULL) {
		CLNT_DESTROY(nsm_clnt);
		nsm_clnt = NULL;
		gsh_free(nodename, MEM_COMP_PROTOCOL);
		nodename = NULL;
	}
}

static void nsm_cc_free(struct clnt_req *cc, size_t size, const char *file,
			int line, const char *function)
{
	gsh_free(cc, MEM_COMP_PROTOCOL);
}

static bool nsm_monitor_noretry(state_nsm_client_t *host)
{
	struct clnt_req *cc;
	char *t;
	struct mon nsm_mon;
	struct sm_stat_res res;
	enum clnt_stat ret;

	if (host == NULL)
		return true;

	PTHREAD_MUTEX_lock(&host->ssc_mutex);

	if (atomic_fetch_int32_t(&host->ssc_monitored)) {
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return true;
	}

	memset(&nsm_mon, 0, sizeof(nsm_mon));
	nsm_mon.mon_id.mon_name = host->ssc_nlm_caller_name;
	nsm_mon.mon_id.my_id.my_prog = NLMPROG;
	nsm_mon.mon_id.my_id.my_vers = NLM4_VERS;
	nsm_mon.mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;
	/* nothing to put in the private data */
	LogDebug(COMPONENT_NLM, "Monitor %s", host->ssc_nlm_caller_name);

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogEventLimited(COMPONENT_NLM, "Monitor %s nsm_connect failed",
				nsm_mon.mon_id.mon_name);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon.mon_id.my_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc), MEM_COMP_PROTOCOL);
	clnt_req_fill(cc, nsm_clnt, authnone_ncreate(), SM_MON,
		      (xdrproc_t)xdr_mon, &nsm_mon, (xdrproc_t)xdr_sm_stat_res,
		      &res);
	cc->cc_free_cb = nsm_cc_free;
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogEventLimited(COMPONENT_NLM, "Monitor %s SM_MON %s",
				nsm_mon.mon_id.mon_name, t);
		free_sperror(t);

		clnt_req_release(cc);
		nsm_disconnect(true);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}
	clnt_req_release(cc);

	if (res.res_stat != STAT_SUCC) {
		LogCrit(COMPONENT_NLM, "Monitor %s SM_MON failed (%d)",
			nsm_mon.mon_id.mon_name, res.res_stat);

		nsm_disconnect(true);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	nsm_count++;
	atomic_store_int32_t(&host->ssc_monitored, true);

	LogDebug(COMPONENT_NLM, "Monitored %s for nodename %s",
		 nsm_mon.mon_id.mon_name, nodename);

	PTHREAD_MUTEX_unlock(&nsm_mutex);
	PTHREAD_MUTEX_unlock(&host->ssc_mutex);
	return true;
}

bool nsm_monitor(state_nsm_client_t *host)
{
#ifdef _INTERNAL_STATD
	assert(!NFS_pcp.internal_statd);
#endif

	/* If someone restarts nsm service, nsm_monitor_noretry may fail
	 * and would tear down the old structures. A retry should work!
	 * So let us retry once if there is a failure.
	 */
	if (nsm_monitor_noretry(host))
		return true;

	return nsm_monitor_noretry(host);
}

bool nlm_monitor(state_nlm_client_t *host)
{
#ifdef _INTERNAL_STATD
	if (NFS_pcp.internal_statd) {
		/* Add to recovery database */
		struct local_nlm_info info;

		memset(&info, 0, sizeof(info));
		info.recovery_type = NLM_CLIENT_ENTRY;
		info.client_name = host->slc_nlm_caller_name;
		info.client_address = host->slc_client_addr;
		info.server_address = host->slc_server_addr;
		/* Always use UDP */
		if (info.client_address.ss_family == AF_INET)
			info.nconf = netconfig_udpv4;
		else
			info.nconf = netconfig_udpv6;

		host->slc_monitored = nlm_add_entry(&info);

		return host->slc_monitored;
	}
#endif

	/* If someone restarts nsm service, nsm_monitor_noretry may fail
	 * and would tear down the old structures. A retry should work!
	 * So let us retry once if there is a failure.
	 */
	if (nsm_monitor_noretry(host->slc_nsm_client))
		return true;

	return nsm_monitor_noretry(host->slc_nsm_client);
}

static bool nsm_unmonitor_noretry(state_nsm_client_t *host)
{
	struct clnt_req *cc;
	char *t;
	struct sm_stat res;
	struct mon_id nsm_mon_id;
	enum clnt_stat ret;

	if (host == NULL)
		return true;

	PTHREAD_MUTEX_lock(&host->ssc_mutex);

	if (!atomic_fetch_int32_t(&host->ssc_monitored)) {
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return true;
	}

	nsm_mon_id.mon_name = host->ssc_nlm_caller_name;
	nsm_mon_id.my_id.my_prog = NLMPROG;
	nsm_mon_id.my_id.my_vers = NLM4_VERS;
	nsm_mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogEventLimited(COMPONENT_NLM,
				"Unmonitor %s nsm_connect failed",
				nsm_mon_id.mon_name);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon_id.my_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc), MEM_COMP_PROTOCOL);
	clnt_req_fill(cc, nsm_clnt, authnone_ncreate(), SM_UNMON,
		      (xdrproc_t)xdr_mon_id, &nsm_mon_id,
		      (xdrproc_t)xdr_sm_stat, &res);
	cc->cc_free_cb = nsm_cc_free;
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogEventLimited(COMPONENT_NLM, "Unmonitor %s SM_UNMON %s",
				nsm_mon_id.mon_name, t);
		free_sperror(t);

		clnt_req_release(cc);
		nsm_disconnect(true);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}
	clnt_req_release(cc);

	atomic_store_int32_t(&host->ssc_monitored, false);
	nsm_count--;

	LogDebug(COMPONENT_NLM, "Unmonitored %s for nodename %s",
		 nsm_mon_id.mon_name, nodename);

	nsm_disconnect(false);

	PTHREAD_MUTEX_unlock(&nsm_mutex);
	PTHREAD_MUTEX_unlock(&host->ssc_mutex);
	return true;
}

bool nsm_unmonitor(state_nsm_client_t *host)
{
#ifdef _INTERNAL_STATD
	if (NFS_pcp.internal_statd) {
		/* Skip unmonitor, handled for nlm client */
		return true;
	}
#endif

	if (!nfs_param.core_param.unmonitor_on_shutdown && admin_shutdown)
		return true;

	/* If someone restarts nsm service, nsm_unmonitor_noretry may
	 * fail and would tear down the old structures. A retry should
	 * work!  So let us retry once if there is a failure.
	 */
	if (nsm_unmonitor_noretry(host))
		return true;

	return nsm_unmonitor_noretry(host);
}

bool nlm_unmonitor(state_nlm_client_t *host)
{
#ifdef _INTERNAL_STATD
	if (NFS_pcp.internal_statd) {
		struct local_nlm_info info;

		memset(&info, 0, sizeof(info));
		info.recovery_type = NLM_CLIENT_ENTRY;
		info.client_name = host->slc_nlm_caller_name;
		info.client_address = host->slc_client_addr;
		info.server_address = host->slc_server_addr;
		info.nconf = host->slc_nconf;

		if (!admin_shutdown)
			return nlm_rm_entry(&info);
		else
			return true;
	}
#endif

	if (!nfs_param.core_param.unmonitor_on_shutdown && admin_shutdown)
		return true;

	/* If someone restarts nsm service, nsm_unmonitor_noretry may
	 * fail and would tear down the old structures. A retry should
	 * work!  So let us retry once if there is a failure.
	 */
	if (nsm_unmonitor_noretry(host->slc_nsm_client))
		return true;

	return nsm_unmonitor_noretry(host->slc_nsm_client);
}

void nsm_unmonitor_all(void)
{
	struct clnt_req *cc;
	char *t;
	struct sm_stat res;
	struct my_id nsm_id;
	enum clnt_stat ret;

#ifdef _INTERNAL_STATD
	if (NFS_pcp.internal_statd)
		return;
#endif

	nsm_id.my_prog = NLMPROG;
	nsm_id.my_vers = NLM4_VERS;
	nsm_id.my_proc = NLMPROC4_SM_NOTIFY;

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogEventLimited(COMPONENT_NLM,
				"Unmonitor all nsm_connect failed");
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		return;
	}

	/* Set this after we call nsm_connect() */
	nsm_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc), MEM_COMP_PROTOCOL);
	clnt_req_fill(cc, nsm_clnt, authnone_ncreate(), SM_UNMON_ALL,
		      (xdrproc_t)xdr_my_id, &nsm_id, (xdrproc_t)xdr_sm_stat,
		      &res);
	cc->cc_free_cb = nsm_cc_free;
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogEventLimited(COMPONENT_NLM, "Unmonitor all %s", t);
		free_sperror(t);
		nsm_disconnect(true);
	} else {
		nsm_disconnect(false);
	}
	clnt_req_release(cc);

	PTHREAD_MUTEX_unlock(&nsm_mutex);
}

#ifdef _INTERNAL_STATD

/*******************************************************************************
 *
 * INTERNAL IMPLEMENTATION OF STATUS MONITOR
 *
 ******************************************************************************/

/*******************************************************************************
 * @brief The SMMON proc SM_NULL function
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */

int smmon_proc_null(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	LogFullDebug(COMPONENT_DISPATCH, "REQUEST PROCESSING: Calling SM_NULL");
	/* 0 is success */
	return 0;
}

/**
 * smmon_free: Frees the result structure allocated for any smmon proc
 *
 * Frees the result structure allocated for any smmon proc. Does Nothing in
 * fact and a common routine is used because none of the smmon procs have
 * anything in their return that needs freeing.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void smmon_free(nfs_res_t *res)
{
	/* Nothing to do */
}

/*******************************************************************************
 * @brief The SMMON proc SM_STAT function
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */
int smmon_proc_stat(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	LogFullDebug(COMPONENT_DISPATCH, "REQUEST PROCESSING: Calling SM_STAT");
	/* 0 is success */
	return 0;
}

/*******************************************************************************
 * @brief The SMMON proc SM_MON function
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */

int smmon_proc_mon(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	LogFullDebug(COMPONENT_DISPATCH, "REQUEST PROCESSING: Calling SM_MON");

	LogEvent(COMPONENT_NLM,
		 "Client %s sent an SM_MON, not supported ignoring",
		 op_ctx->client->hostaddr_str);

	res->sm_stat_res.res_stat = STAT_FAIL;
	res->sm_stat_res.state = 0;
	return NFS_REQ_ERROR;
}

/*******************************************************************************
 * @brief The SMMON proc SM_UNMON function
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */

int smmon_proc_unmon(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "REQUEST PROCESSING: Calling SM_UNMON");

	/* Since this is only special case local host NLM, we just never bother
	 * to unmonitor. For one thing, we can't actually find the recovery
	 * record because UNMON doesn't have the priv field.
	 */

	res->sm_unmon_res = cur_nsm_state;
	return NFS_REQ_OK;
}

void req_call_init(struct local_nlm_info *info);

void fill_forward_sm_notify(nfs_arg_t *arg, struct svc_req *req,
			    sockaddr_t *peer)
{
	struct local_nlm_info *info = gsh_calloc(1, sizeof(*info),
						 MEM_COMP_PROTOCOL);
	sockaddr_t *local = svc_getrpclocal(req->rq_xprt);
	struct display_buffer dspbuf;

	info->mem_comp = MEM_COMP_PROTOCOL;
	info->client_name = gsh_strdup(arg->notify_args.my_name,
				       info->mem_comp);
	info->nsm_state = arg->notify_args.state;
	info->client_address = *peer;
	info->client_address_str = gsh_calloc(SOCK_NAME_MAX, sizeof(char),
					      info->mem_comp);
	info->recovery_type = NSM_FORWARD_NOTIFY;
	/* If NSM_Port was configured, it is expected to be the same for all
	 * cluster members. Otherwise, the default is 0 which is the right
	 * value to indicate the port must be found with RPCBINND/PORTMAP.
	 * Note that when Ganeaha is creating sockets it will bind with this
	 * port, or bind to a library selected port if 0, but the resulting
	 * port is only set in the sockaddr_t, the config value is not changed.
	 */
	info->info_port = NFS_pcp.port[P_STATD];

	dspbuf.b_size = SOCK_NAME_MAX;
	dspbuf.b_current = info->client_address_str;
	dspbuf.b_start = info->client_address_str;

	display_sockip(&dspbuf, &info->client_address);

	info->server_address = *local;
	info->server_address_str = gsh_calloc(SOCK_NAME_MAX, sizeof(char),
					      info->mem_comp);

	dspbuf.b_size = SOCK_NAME_MAX;
	dspbuf.b_current = info->server_address_str;
	dspbuf.b_start = info->server_address_str;

	display_sockip(&dspbuf, &info->server_address);

	LogDebug(COMPONENT_NLM, "Forward SM_NOTIFY from %s to %s",
		 info->server_address_str, info->client_address_str);

	info->nconf = nfs_Get_netconfig(req->rq_xprt->xp_netid);

	/* Add to head of wait list - so we send immediately. */
	glist_add(&callback_wait_list, &info->infolist);
}

void forward_sm_notify(nfs_arg_t *arg, struct svc_req *req)
{
	struct glist_head *glist;

	/* Exit quick if no cluster members */
	if (glist_empty(&nfs_param.core_param.cluster_members))
		return;

	PTHREAD_MUTEX_lock(&nsm_mutex);

	glist_for_each(glist, &nfs_param.core_param.cluster_members) {
		struct base_client_entry *bce;

		bce = container_of(glist, struct base_client_entry, cle_list);

		fill_forward_sm_notify(arg, req, &bce->cidr->ip_addr);
	}

	PTHREAD_COND_signal(&nsm_cond);

	PTHREAD_MUTEX_unlock(&nsm_mutex);
}

/*******************************************************************************
 * @brief The SMMON proc SM_NOTIFY function
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */

int smmon_proc_notify(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	state_status_t state_status = STATE_SUCCESS;
	state_nsm_client_t *nsm_client;

	LogFullDebug(COMPONENT_DISPATCH,
		     "REQUEST PROCESSING: Calling SM_NOTIFY");

	/* Now find the nsm_client using the provided caller_name and
	 * op_ctx->client.
	 */
	nsm_client = get_nsm_client(CARE_NOT, arg->notify_args.my_name);

	if (nsm_client != NULL) {
		/* Now that we have an nsm_client, we can compare caller's
		 * gsh_client with ssc_client.
		 */
		if (op_ctx->client != nsm_client->ssc_client &&
		    isFullDebug(COMPONENT_NLM)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer db = { sizeof(str), str, str };

			display_sockaddr(&db, op_ctx->caller_addr);

			LogFullDebug(
				COMPONENT_NLM,
				"SM_NOTIFY from client %p:%s resulted in ssc_client %p:%s",
				op_ctx->client, str, nsm_client->ssc_client,
				nsm_client->ssc_client->hostaddr_str);
		}

		/* Cast the state number into a state pointer to protect
		 * locks from a client that has rebooted from being released
		 * by this SM_NOTIFY.
		 */
		LogFullDebug(COMPONENT_NLM, "Starting cleanup");
		state_status = state_nlm_notify(nsm_client, true,
						arg->notify_args.state);

		if (state_status != STATE_SUCCESS) {
			/** @todo FSF: Deal with error
			 */
		}

		LogFullDebug(COMPONENT_NLM, "Cleanup complete");

		dec_nsm_client_ref(nsm_client);
	}

	if (!client_match(COMPONENT_NLM, "for Cluster Self list",
			  op_ctx->caller_addr,
			  &nfs_param.core_param.cluster_members, NULL)) {
		/* SM_NOTIFY did not come from another cluster member, forward
		 * it to all cluster members.
		 */
		forward_sm_notify(arg, req);
	}

	LogDebug(COMPONENT_DISPATCH, "REQUEST RESULT: SM_NOTIFY DONE");

	return NFS_REQ_OK;
}

bool local_nlm_info_client_create(struct local_nlm_info *info)
{
	struct my_id *my_id = &info->mon.mon_id.my_id;
	struct netbuf cli_netbuf = { sizeof(sockaddr_t), sizeof(sockaddr_t),
				     &info->client_address };
	struct netbuf srv_netbuf = { sizeof(sockaddr_t), sizeof(sockaddr_t),
				     &info->server_address };
	/* For NLM notify callback, which might be TCP, the RPCBIND/PORTMAP
	 * call has filled in info_port.
	 */
	int port = htons(info->info_port);
	sockaddr_t *addr = &info->client_address;

	/* Need to set port in client_address for complete sendback
	 * address
	 */
	switch (info->client_address.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)addr)->sin_port = port;
		break;

	case AF_INET6:
		((struct sockaddr_in6 *)addr)->sin6_port = port;
		break;

	default:
		break;
		// handle unexpected case...
	}

	LogFullDebug(
		COMPONENT_NLM,
		"clnt_tli_ncreate netid=%s client=%s port=%d server=%s info=%p",
		info->nconf->nc_netid, info->client_address_str,
		info->info_port, info->server_address_str, info);

	info->rpc_client = clnt_tli_ncreate_opt(RPC_ANYFD, info->nconf,
						&srv_netbuf, &cli_netbuf,
						my_id->my_prog, my_id->my_vers,
						0, 0, !IPV6_V6ONLY);

	if (CLNT_FAILURE(info->rpc_client)) {
		char *err = rpc_sperror(&info->rpc_client->cl_error, "failed");

		LogEventLimited(COMPONENT_NLM, "connect to client %s", err);

		free_sperror(err);
		CLNT_DESTROY(info->rpc_client);
		info->rpc_client = NULL;
		return false;
	}

	return true;
}

void send_local_nlm_info(struct local_nlm_info *info);

static void local_nlm_info_cb(struct clnt_req *req)
{
	struct local_nlm_info *info;
	struct my_id *my_id;
	char *err;

	info = container_of(req, struct local_nlm_info, cc);
	my_id = &info->mon.mon_id.my_id;

	if (isFullDebug(COMPONENT_NLM)) {
		err = rpc_sperror(&info->cc.cc_error, "");

		LogFullDebug(COMPONENT_NLM,
			     "cc_process_cb for %s status %s info %p",
			     info->client_address_str, err, info);

		free_sperror(err);
	}

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* Remove from wait list - now nsm_notify_thread won't wait for
	 * reply anymore.
	 */
	glist_del(&info->infolist);

	/* For most requests, we're done, but if we just called PORTMAP, we have
	 * more to do with this local_nlm_info. Note that this extra step will
	 * ONLY happen for NLM_CLIENT_ENTRY recovery_type.
	 *
	 * For NLM_SM_MON_ENTRY recovery_type, we will have directly set up the
	 * callback and done that.
	 */

	switch (req->cc_error.re_status) {
	case RPC_SUCCESS:
		if (my_id->my_prog == RPCBPROG) {
#ifdef USE_RPCBIND_V34
			/** @todo FSF
			 * Once we support this, we must get the port from the
			 * RPCBPROC_GETADDR response...
			 */
			compile error;
#endif
			/* We just called PORTMAP and succeeded, now set up to
			 * call the real procedure.
			 *
			 * Get the port from the PORTMAP response.
			 */
			info->info_port = info->res.res_pmap_getport;

			/* Reset so we are done with this request, but don't
			 * destroy the cc yet.
			 */
			clnt_req_reset(&info->cc);

			/* Done with the PORTMAP CLIENT */
			CLNT_DESTROY(info->rpc_client);
			info->rpc_client = NULL;
			info->cc.cc_clnt = NULL;

			/* Now go back to send the actual request.
			 * This can release the mutex and retake it if a bad
			 * error occures, but we are safe.
			 */
			send_local_nlm_info(info);

			PTHREAD_MUTEX_unlock(&nsm_mutex);

			return;
		}

		break;

	case RPC_TIMEDOUT:
		/* Retry these errors. This can release the mutex and retake it
		 * if a bad error occures, but we are safe.
		 */
		if (info->cc.cc_refreshes-- == 0 ||
		    clnt_req_refresh(&info->cc) != RPC_SUCCESS) {
			char *why;

			if (info->cc.cc_refreshes == 0)
				why = "Retry Exhausted";
			else
				why = "Refresh request after timeout failed";

			err = rpc_sperror(&info->cc.cc_error, why);

			LogEventLimited(COMPONENT_NLM, "%s", err);

			free_sperror(err);
			break;
		}

		send_local_nlm_info(info);

		PTHREAD_MUTEX_unlock(&nsm_mutex);

		return;

	default:
		err = rpc_sperror(&info->cc.cc_error, "failed");

		LogEventLimited(COMPONENT_NLM, "Request failed %s", err);

		free_sperror(err);
		break;
	}

	/* If we got here, we're done with this local_nlm_info */

	/* Remove from waitlist before disposing. */
	glist_del(&info->infolist);

	PTHREAD_MUTEX_unlock(&nsm_mutex);

	/* Done with the client */
	CLNT_DESTROY(info->rpc_client);
	info->rpc_client = NULL;

	/* We're done with the request, this will end up freeing this
	 * local_nlm_info.
	 */
	clnt_req_release(req);
}

void local_nlm_info_free(struct local_nlm_info *info)
{
#ifndef NDEBUG

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* The entry should NOT be on any lists */
	assert(glist_null(&info->infolist));

	PTHREAD_MUTEX_unlock(&nsm_mutex);
#endif

	if (info->rpc_client != NULL)
		CLNT_DESTROY(info->rpc_client);

	/* Free all the strings that might have been allocated */
	gsh_free(info->mon.mon_id.my_id.my_name, info->mem_comp);
	gsh_free(info->mon.mon_id.mon_name, info->mem_comp);
	gsh_free(info->client_name, info->mem_comp);
	gsh_free(info->client_address_str, info->mem_comp);
	gsh_free(info->server_address_str, info->mem_comp);

	gsh_free(info, info->mem_comp);
}

static void local_nlm_info_free_caller(struct clnt_req *req, size_t size,
				       const char *file, int line,
				       const char *function)
{
	struct local_nlm_info *info;

	info = container_of(req, struct local_nlm_info, cc);

	local_nlm_info_free(info);
}

void req_call_init(struct local_nlm_info *info)
{
	clnt_req_fill(&info->cc, NULL, authnone_ncreate(), 0, NULL, &info->args,
		      NULL, &info->res);

	/* Need to override the callback function */
	info->cc.cc_process_cb = local_nlm_info_cb;

	/* Need to override the free function */
	info->cc.cc_free_cb = (clnt_req_freer)local_nlm_info_free_caller;
}

bool req_call(struct local_nlm_info *info)
{
	char *err, *message;
	struct clnt_req *cc = &info->cc;
	struct my_id *my_id = &info->mon.mon_id.my_id;

	if (cc->cc_clnt == NULL) {
		/* Replace the information for the call with the actual
		 * information for the current call.
		 */
		cc->cc_clnt = info->rpc_client;
		cc->cc_proc = my_id->my_proc;
		cc->cc_call.proc = info->xargs;
		cc->cc_reply.proc = info->xres;

		/* Now set up the request */
		LogFullDebug(COMPONENT_NLM, "clnt_req_setup info %p", info);
		info->cc.cc_error.re_status = clnt_req_setup(&info->cc, tout5);

		if (info->cc.cc_error.re_status != RPC_SUCCESS) {
			message = "clnt_req_setup";
			goto error;
		}

		/* Need to override the callback function */
		info->cc.cc_process_cb = local_nlm_info_cb;

		/* Need to override the free function */
		info->cc.cc_free_cb =
			(clnt_req_freer)local_nlm_info_free_caller;
	}

	LogFullDebug(COMPONENT_NLM, "CLNT_CALL_BACK info %p", info);
	info->cc.cc_error.re_status = CLNT_CALL_BACK(&info->cc);

	switch (info->cc.cc_error.re_status) {
	case RPC_SUCCESS:
		LogFullDebug(COMPONENT_NLM, "Success");
		/* Call was fired, it will either time out or complete. */
		return true;

	case RPC_CANTSEND:
		/* Retry, by returning true, this request will be scheduled for
		 * timeout and that will cause retry.
		 */
		LogFullDebug(COMPONENT_NLM, "Can't Send");
		return true;

	default:
		break;
		/* error, continue on below */
	}

	message = "CLNT_CALL_BACK";

error:

	err = rpc_sperror(&info->cc.cc_error, "failed");

	LogEvent(COMPONENT_NLM, "%s %s", message, err);

	free_sperror(err);

	/* Remove it from the waitlist before destroying it. */
	glist_del(&info->infolist);

	/* Must drop the mutex for the clnt_req_release call below */
	PTHREAD_MUTEX_unlock(&nsm_mutex);

	/* Something bad happened, give up on this one */
	clnt_req_release(&info->cc);

	/* Must retake the mutex */
	PTHREAD_MUTEX_lock(&nsm_mutex);

	return false;
}

/*******************************************************************************
 * @brief Ganesha restart send SM_NOTIFY
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 */

void send_sm_notify(struct local_nlm_info *info)
{
	struct my_id *my_id = &info->mon.mon_id.my_id;

	/* Handle an NLM Client Entry we monitored
	 *
	 * We have an entry with IP address and Caller Name
	 *
	 * All we need to send SM_NOTIFY is actually the IP address.
	 * We need our "my_name" and state number.
	 */
	if (is_loopback(&info->client_address)) {
		/* Local NLM entry - ignore */
		glist_del(&info->infolist);
		local_nlm_info_free(info);
		return;
	}

	if (my_id->my_prog == 0) {
		/* Very first time processing this local_nlm_info. */
		LogFullDebug(COMPONENT_NLM, "Init info %p", info);
		req_call_init(info);
	}

	if (info->info_port == 0 || info->info_port == PMAPPORT) {
		/* We need to use PORTMAP (and maybe RPCBIND later) to find
		 * the client's Status Monitor. So set up that call first.
		 */
		my_id->my_prog = RPCBPROG;
		info->info_port = PMAPPORT;

		switch (my_id->my_vers) {
		case 0:
#ifdef USE_RPCBIND_V34
			LogDebug(COMPONENT_NLM,
				 "Starting with RPCBIND Version 4 for client %s",
				 info->client_address_str);
			my_id->my_vers = RPCBVERS4;
			my_id->my_proc = RPCBPROC_GETADDR;
			memset(&info->args.rpcb_args, 0,
			       sizeof(info->args.rpcb_args));
			info->args.rpcb_args.r_prog = SM_PROG;
			info->args.rpcb_args.r_vers = SM_VERS;
			info->xargs = (xdrproc_t)xdr_rpcb;
			info->xres = (xdrproc_t)xdr_wrapstring;
			break;

		case RPCBVERS4:
			LogDebug(
				COMPONENT_NLM,
				"RPCBIND Version 4 didn't work, try RPCBIND Version 3 for client %s",
				info->client_address_str);
			my_id->my_vers = RPCBVERS;
			break;

		case RPCBVERS:
			LogDebug(
				COMPONENT_NLM,
				"RPCBIND Version 3 didn't work, try PORTMAP (Version 2) for client %s",
				info->client_address_str);
#else
			LogDebug(COMPONENT_NLM,
				 "Starting with PORTMAP for client %s info %p",
				 info->client_address_str, info);
#endif
			my_id->my_vers = PMAPVERS;
			my_id->my_proc = PMAPPROC_GETPORT;
			info->args.pmap_args.pm_prog = SM_PROG;
			info->args.pmap_args.pm_vers = SM_VERS;
			info->args.pmap_args.pm_prot = IPPROTO_UDP;
			info->args.pmap_args.pm_port = 0;
			info->xargs = (xdrproc_t)xdr_pmap;
			info->xres = (xdrproc_t)xdr_uint32_t;
			break;

		case PMAPVERS:
			LogDebug(
				COMPONENT_NLM,
				"Retry PORTMAP (Version 2) for client %s info %p",
				info->client_address_str, info);
			/* Keep PORTMAP until we give up */
			break;

		default:
			/* Can't get here... */
			LogFatal(COMPONENT_NLM, "Invalid local_nlm_info");
		}
	} else {
		/* Now send SM_NOTIFY */
		my_id->my_prog = SM_PROG;
		my_id->my_vers = SM_VERS;
		my_id->my_proc = SM_NOTIFY;
		info->xargs = (xdrproc_t)xdr_notify;
		info->xres = (xdrproc_t)xdr_void;

		if (info->recovery_type == NLM_CLIENT_ENTRY) {
			info->args.notify_args.my_name = nodename;
			info->args.notify_args.state = recov_nsm_state;
		} else if (info->recovery_type == NSM_FORWARD_NOTIFY) {
			/* Now send SM_NOTIFY */
			info->args.notify_args.my_name = info->client_name;
			info->args.notify_args.state = info->nsm_state;
		} else {
			LogFatal(COMPONENT_NLM, "Invalid recovery type %d",
				 info->recovery_type);
		}
	}

	/* Get a CLIENT for the request */
	if (info->rpc_client == NULL) {
		/* We haven't set up a client yet. */
		if (!local_nlm_info_client_create(info)) {
			/* Since it failed, let's just retry in 5 seconds. If it
			 * never succeeds, retry will be abandoned when grace
			 * period runs out.
			 */
			goto retry;
		}
	}

	/* Request options are all setup, actually set up the request */
	if (!req_call(info)) {
		/* Oops, problem, we're done with this entry */
		return;
	}

	/* Setup with timeout callback, so just remove from infolist. */
	glist_del(&info->infolist);
	return;

retry:

	/* And put the request at the tail of the wait list with a due time 5
	 * seconds from now.
	 */
	info->reply_due = time(NULL) + 5;

	glist_del(&info->infolist);
	glist_add_tail(&callback_wait_list, &info->infolist);
}

void send_local_nlm_info(struct local_nlm_info *info)
{
	if (info->recovery_type == NLM_CLIENT_ENTRY ||
	    info->recovery_type == NSM_FORWARD_NOTIFY) {
		/* Handle SM_NOTIFY to remote NLM client */
		send_sm_notify(info);
	} else {
		LogFatal(COMPONENT_NLM, "Unexpected recovery_type %d for %p",
			 info->recovery_type, info);
	}
}

void *nsm_notify_thread(void *unused)
{
	struct local_nlm_info *info, sinfo;
	time_t now;

	SetNameFunction("NOTIFY");
	rcu_register_thread();

	/* Add NSM_STATE_ENTRY to new recovery database */
	sinfo.recovery_type = NSM_STATE_ENTRY;
	sinfo.nsm_state = cur_nsm_state;

	if (!nlm_add_entry(&sinfo)) {
		LogFatal(COMPONENT_NLM,
			 "Could not add NSM_STATE_ENTRY to recovery database");
	}

	PTHREAD_MUTEX_lock(&nsm_mutex);

	if (nodename == NULL) {
		struct utsname utsname;

		if (uname(&utsname) == -1) {
			int save_errno = errno;

			LogFatal(COMPONENT_NLM,
				 "uname failed with errno %d (%s)", save_errno,
				 strerror(save_errno));
		}

		nodename = gsh_strdup(utsname.nodename, MEM_COMP_PROTOCOL);
	}

	/* Start by blasting out all the SM_NOTIFY calls */
	while (!glist_empty(&local_nlm_info_list)) {
		/* Pick first entry */
		info = glist_first_entry(&local_nlm_info_list,
					 struct local_nlm_info, infolist);

		if (info->recovery_type != NLM_CLIENT_ENTRY) {
			/* Invalid recovery_type */
			LogCrit(COMPONENT_NLM,
				"Invalid recovery entry %d for entry %p",
				info->recovery_type, info);

			/* Remove it from the list and discard it. */
			glist_del(&info->infolist);
			local_nlm_info_free(info);
			continue;
		}

		/* Fire the request which will put it at the tail of the wait
		 * queue. This CAN release the mutex and retake it, but this
		 * loop is safe from that.
		 */
		send_local_nlm_info(info);
	}

	/* Now wait for all the SM_NOTIFY to be complete or grace period to be
	 * over.
	 */

	now = time(NULL);

	/* Retry any overdue callbacks */
	while (run_nsm_notify_thread || !glist_empty(&callback_wait_list)) {
		/* Refire request, which will replace it at the tail of
		 * the wait the queue.
		 */

		/* Get the next waiter */
		info = glist_first_entry(&callback_wait_list,
					 struct local_nlm_info, infolist);

		if (info == NULL) {
			/* Check if we should exit */
			if (!run_nsm_notify_thread)
				break;

			/* Nothing left to wait for, idle to handle SM_NOTIFY
			 * forward.
			 */
			LogFullDebug(COMPONENT_NLM, "Wait for more NSM work");
			PTHREAD_COND_wait(&nsm_cond, &nsm_mutex);

			/* See if we have work to do or it's time to exit. */
			continue;
		}

		if (info->reply_due > now) {
			struct timespec delay = { info->reply_due, 0 };

			/* Use a condition variable for a timed sleep that may
			 * be interrupted by more work to do or a signal to
			 * terminate.*/
			LogFullDebug(COMPONENT_NLM, "Wait %d seconds for retry",
				     (int)(info->reply_due - now));
			PTHREAD_COND_timedwait(&nsm_cond, &nsm_mutex, &delay);

			now = time(NULL);

			/* And go see if there's anything left after sleep and
			 * taking the lock again.
			 */
			continue;
		}

		// todo - if grace period is over, just drain the list

		/* Re-fire the request which will put it at the tail of the wait
		 * queue. This CAN release the mutex and retake it, but this
		 * loop is safe from that.
		 */
		send_local_nlm_info(info);
	}

	/* All done, drop the mutex and exit. */
	PTHREAD_MUTEX_unlock(&nsm_mutex);

	rcu_unregister_thread();
	return NULL;
}

void process_local_nlm_info(void)
{
	int rc;
	pthread_attr_t attr_thr;

	/* Init for thread parameter (mostly for scheduling) */
	PTHREAD_ATTR_init(&attr_thr);
	PTHREAD_ATTR_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
	PTHREAD_ATTR_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);

	rc = PTHREAD_create(&nsm_notify_thread_p, &attr_thr, nsm_notify_thread,
			    NULL);

	if (rc != 0) {
		LogFatal(COMPONENT_NLM,
			 "Could not create nsm_notify_thread, error = %d (%s)",
			 errno, strerror(errno));
	}

	/* Keep the nsm_notify_thread running if we have cluster members
	 * identified to forward SM_NOTIFY to.
	 */
	run_nsm_notify_thread =
		!glist_empty(&nfs_param.core_param.cluster_members);
}

#endif
