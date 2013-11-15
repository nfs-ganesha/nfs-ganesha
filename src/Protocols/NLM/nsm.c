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
#include "ganesha_rpc.h"
#include "nsm.h"
#include "sal_data.h"

pthread_mutex_t nsm_mutex = PTHREAD_MUTEX_INITIALIZER;
CLIENT *nsm_clnt;
AUTH *nsm_auth;
unsigned long nsm_count;
char *nodename;

bool nsm_connect()
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
	if (nodename == NULL) {
		LogCrit(COMPONENT_NLM,
			"failed to allocate memory for nodename");
		return false;
	}

	nsm_clnt = gsh_clnt_create("localhost", SM_PROG, SM_VERS, "tcp");

	if (nsm_clnt == NULL) {
		LogCrit(COMPONENT_NLM, "failed to connect to statd");
		gsh_free(nodename);
		nodename = NULL;
	}

	/* split auth (for authnone, idempotent) */
	nsm_auth = authnone_create();

	return nsm_clnt != NULL;
}

void nsm_disconnect()
{
	if (nsm_count == 0 && nsm_clnt != NULL) {
		gsh_clnt_destroy(nsm_clnt);
		nsm_clnt = NULL;
		AUTH_DESTROY(nsm_auth);
		nsm_auth = NULL;
		gsh_free(nodename);
		nodename = NULL;
	}
}

bool nsm_monitor(state_nsm_client_t *host)
{
	enum clnt_stat ret;
	struct mon nsm_mon;
	struct sm_stat_res res;
	struct timeval tout = { 25, 0 };

	if (host == NULL)
		return true;

	pthread_mutex_lock(&host->ssc_mutex);

	if (atomic_fetch_int32_t(&host->ssc_monitored)) {
		pthread_mutex_unlock(&host->ssc_mutex);
		return true;
	}

	nsm_mon.mon_id.mon_name = host->ssc_nlm_caller_name;
	nsm_mon.mon_id.my_id.my_prog = NLMPROG;
	nsm_mon.mon_id.my_id.my_vers = NLM4_VERS;
	nsm_mon.mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;
	/* nothing to put in the private data */
	LogDebug(COMPONENT_NLM, "Monitor %s", host->ssc_nlm_caller_name);

	pthread_mutex_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogCrit(COMPONENT_NLM,
			"Can not monitor %s clnt_create returned NULL",
			nsm_mon.mon_id.mon_name);
		pthread_mutex_unlock(&nsm_mutex);
		pthread_mutex_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon.mon_id.my_id.my_name = nodename;

	ret = clnt_call(nsm_clnt,
			nsm_auth,
			SM_MON,
			(xdrproc_t) xdr_mon,
			&nsm_mon,
			(xdrproc_t) xdr_sm_stat_res,
			&res,
			tout);

	if (ret != RPC_SUCCESS) {
		LogCrit(COMPONENT_NLM,
			"Can not monitor %s SM_MON ret %d %s",
			nsm_mon.mon_id.mon_name,
			ret,
			clnt_sperror(nsm_clnt, ""));

		nsm_disconnect();
		pthread_mutex_unlock(&nsm_mutex);
		pthread_mutex_unlock(&host->ssc_mutex);
		return false;
	}

	if (res.res_stat != STAT_SUCC) {
		LogCrit(COMPONENT_NLM,
			"Can not monitor %s SM_MON status %d",
			nsm_mon.mon_id.mon_name, res.res_stat);

		nsm_disconnect();
		pthread_mutex_unlock(&nsm_mutex);
		pthread_mutex_unlock(&host->ssc_mutex);
		return false;
	}

	nsm_count++;
	atomic_store_int32_t(&host->ssc_monitored, true);

	LogDebug(COMPONENT_NLM,
		 "Monitored %s for nodename %s",
		 nsm_mon.mon_id.mon_name, nodename);

	pthread_mutex_unlock(&nsm_mutex);
	pthread_mutex_unlock(&host->ssc_mutex);
	return true;
}

bool nsm_unmonitor(state_nsm_client_t *host)
{
	enum clnt_stat ret;
	struct sm_stat res;
	struct mon_id nsm_mon_id;
	struct timeval tout = { 25, 0 };

	if (host == NULL)
		return true;

	pthread_mutex_lock(&host->ssc_mutex);

	if (!atomic_fetch_int32_t(&host->ssc_monitored)) {
		pthread_mutex_unlock(&host->ssc_mutex);
		return true;
	}

	nsm_mon_id.mon_name = host->ssc_nlm_caller_name;
	nsm_mon_id.my_id.my_prog = NLMPROG;
	nsm_mon_id.my_id.my_vers = NLM4_VERS;
	nsm_mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;

	pthread_mutex_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogCrit(COMPONENT_NLM,
			"Can not unmonitor %s clnt_create returned NULL",
			nsm_mon_id.mon_name);
		pthread_mutex_unlock(&nsm_mutex);
		pthread_mutex_unlock(&host->ssc_mutex);
		return false;
	}

	/* Set this after we call nsm_connect() */
	nsm_mon_id.my_id.my_name = nodename;

	ret = clnt_call(nsm_clnt,
			nsm_auth,
			SM_UNMON,
			(xdrproc_t) xdr_mon_id,
			&nsm_mon_id,
			(xdrproc_t) xdr_sm_stat,
			&res,
			tout);

	if (ret != RPC_SUCCESS) {
		LogCrit(COMPONENT_NLM,
			"Can not unmonitor %s SM_MON ret %d %s",
			nsm_mon_id.mon_name,
			ret,
			clnt_sperror(nsm_clnt, ""));

		nsm_disconnect();
		pthread_mutex_unlock(&nsm_mutex);
		pthread_mutex_unlock(&host->ssc_mutex);
		return false;
	}

	atomic_store_int32_t(&host->ssc_monitored, false);
	nsm_count--;

	LogDebug(COMPONENT_NLM, "Unonitored %s for nodename %s",
		 nsm_mon_id.mon_name, nodename);

	nsm_disconnect();

	pthread_mutex_unlock(&nsm_mutex);
	pthread_mutex_unlock(&host->ssc_mutex);
	return true;
}

void nsm_unmonitor_all(void)
{
	enum clnt_stat ret;
	struct sm_stat res;
	struct my_id nsm_id;
	struct timeval tout = { 25, 0 };

	nsm_id.my_prog = NLMPROG;
	nsm_id.my_vers = NLM4_VERS;
	nsm_id.my_proc = NLMPROC4_SM_NOTIFY;

	pthread_mutex_lock(&nsm_mutex);

	/* create a connection to nsm on the localhost */
	if (!nsm_connect()) {
		LogCrit(COMPONENT_NLM,
			"Can not unmonitor all clnt_create returned NULL");
		pthread_mutex_unlock(&nsm_mutex);
		return;
	}

	/* Set this after we call nsm_connect() */
	nsm_id.my_name = nodename;

	ret = clnt_call(nsm_clnt,
			nsm_auth,
			SM_UNMON_ALL,
			(xdrproc_t) xdr_my_id,
			&nsm_id,
			(xdrproc_t) xdr_sm_stat,
			&res,
			tout);

	if (ret != RPC_SUCCESS) {
		LogCrit(COMPONENT_NLM,
			"Can not unmonitor all ret %d %s",
			ret,
			clnt_sperror(nsm_clnt, ""));
	}

	nsm_disconnect();
	pthread_mutex_unlock(&nsm_mutex);
}
