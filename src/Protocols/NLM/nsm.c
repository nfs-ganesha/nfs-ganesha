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

#include "config.h"
#include <sys/utsname.h>
#include "abstract_atomic.h"
#include "gsh_rpc.h"
#include "nsm.h"
#include "sal_data.h"

pthread_mutex_t nsm_mutex = PTHREAD_MUTEX_INITIALIZER;
CLIENT *nsm_clnt;
AUTH *nsm_auth;
unsigned long nsm_count;
char *nodename;

/* retry timeout default to the moon and back */
static const struct timespec tout = { 3, 0 };

bool nsm_connect(void)
{
	struct utsname utsname;

	if (nsm_clnt != NULL)
		return true;

	if (uname(&utsname) == -1) {
		LogCrit(COMPONENT_NLM,
			"uname failed with errno %d (%s)",
			errno, strerror(errno));
		return false;
	}

	nodename = gsh_strdup(utsname.nodename);

	nsm_clnt = clnt_ncreate("localhost", SM_PROG, SM_VERS, "tcp");

	if (CLNT_FAILURE(nsm_clnt)) {
		char *err = rpc_sperror(&nsm_clnt->cl_error, "failed");

		LogCrit(COMPONENT_NLM, "connect to statd %s", err);
		gsh_free(err);
		CLNT_DESTROY(nsm_clnt);
		nsm_clnt = NULL;
		gsh_free(nodename);
		nodename = NULL;
	}

	/* split auth (for authnone, idempotent) */
	nsm_auth = authnone_ncreate();

	return nsm_clnt != NULL;
}

void nsm_disconnect(void)
{
	if (nsm_count == 0 && nsm_clnt != NULL) {
		CLNT_DESTROY(nsm_clnt);
		nsm_clnt = NULL;
		AUTH_DESTROY(nsm_auth);
		nsm_auth = NULL;
		gsh_free(nodename);
		nodename = NULL;
	}
}

bool nsm_monitor(state_nsm_client_t *host)
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
		LogCrit(COMPONENT_NLM,
			"Monitor %s nsm_connect failed",
			nsm_mon.mon_id.mon_name);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon.mon_id.my_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc));
	clnt_req_fill(cc, nsm_clnt, nsm_auth, SM_MON,
		      (xdrproc_t) xdr_mon, &nsm_mon,
		      (xdrproc_t) xdr_sm_stat_res, &res);
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_NLM,
			"Monitor %s SM_MON %s",
			nsm_mon.mon_id.mon_name, t);
		gsh_free(t);

		clnt_req_release(cc);
		nsm_disconnect();
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}
	clnt_req_release(cc);

	if (res.res_stat != STAT_SUCC) {
		LogCrit(COMPONENT_NLM,
			"Monitor %s SM_MON failed (%d)",
			nsm_mon.mon_id.mon_name, res.res_stat);

		nsm_disconnect();
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	nsm_count++;
	atomic_store_int32_t(&host->ssc_monitored, true);

	LogDebug(COMPONENT_NLM,
		 "Monitored %s for nodename %s",
		 nsm_mon.mon_id.mon_name, nodename);

	PTHREAD_MUTEX_unlock(&nsm_mutex);
	PTHREAD_MUTEX_unlock(&host->ssc_mutex);
	return true;
}

bool nsm_unmonitor(state_nsm_client_t *host)
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
		LogCrit(COMPONENT_NLM,
			"Unmonitor %s nsm_connect failed",
			nsm_mon_id.mon_name);
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon_id.my_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc));
	clnt_req_fill(cc, nsm_clnt, nsm_auth, SM_UNMON,
		      (xdrproc_t) xdr_mon_id, &nsm_mon_id,
		      (xdrproc_t) xdr_sm_stat, &res);
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	nsm_count--;
	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_NLM,
			"Unmonitor %s SM_UNMON %s",
			nsm_mon_id.mon_name, t);
		gsh_free(t);

		clnt_req_release(cc);
		nsm_disconnect();
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		PTHREAD_MUTEX_unlock(&host->ssc_mutex);
		return false;
	}
	clnt_req_release(cc);

	atomic_store_int32_t(&host->ssc_monitored, false);

	LogDebug(COMPONENT_NLM, "Unmonitored %s for nodename %s",
		 nsm_mon_id.mon_name, nodename);

	nsm_disconnect();

	PTHREAD_MUTEX_unlock(&nsm_mutex);
	PTHREAD_MUTEX_unlock(&host->ssc_mutex);
	return true;
}

void nsm_unmonitor_all(void)
{
	struct clnt_req *cc;
	char *t;
	struct sm_stat res;
	struct my_id nsm_id;
	enum clnt_stat ret;

	nsm_id.my_prog = NLMPROG;
	nsm_id.my_vers = NLM4_VERS;
	nsm_id.my_proc = NLMPROC4_SM_NOTIFY;

	PTHREAD_MUTEX_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogCrit(COMPONENT_NLM,
			"Unmonitor all nsm_connect failed");
		PTHREAD_MUTEX_unlock(&nsm_mutex);
		return;
	}

	/* Set this after we call nsm_connect() */
	nsm_id.my_name = nodename;

	cc = gsh_malloc(sizeof(*cc));
	clnt_req_fill(cc, nsm_clnt, nsm_auth, SM_UNMON_ALL,
		      (xdrproc_t) xdr_my_id, &nsm_id,
		      (xdrproc_t) xdr_sm_stat, &res);
	ret = clnt_req_setup(cc, tout);
	if (ret == RPC_SUCCESS) {
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		t = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_NLM,
			"Unmonitor all %s",
			t);
		gsh_free(t);
	}
	clnt_req_release(cc);

	nsm_disconnect();
	PTHREAD_MUTEX_unlock(&nsm_mutex);
}
