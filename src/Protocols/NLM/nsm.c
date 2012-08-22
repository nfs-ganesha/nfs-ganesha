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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
unsigned long nsm_count;
char * nodename;

bool_t nsm_connect()
{
  struct utsname utsname;

  if(nsm_clnt != NULL)
    return TRUE;

  if(uname(&utsname) == -1)
    {
      LogCrit(COMPONENT_NLM,
              "uname failed with errno %d (%s)",
              errno, strerror(errno));
      return FALSE;
    }

  nodename = gsh_malloc(strlen(utsname.nodename)+1);
  if(nodename == NULL)
    {
      LogCrit(COMPONENT_NLM,
              "failed to allocate memory for nodename");
      return FALSE;
    }

  strcpy(nodename, utsname.nodename);

  nsm_clnt = Clnt_create("localhost", SM_PROG, SM_VERS, "tcp");

  if(nsm_clnt == NULL)
    {
      LogCrit(COMPONENT_NLM,
              "failed to connect to statd");
      gsh_free(nodename);
      nodename = NULL;
    }

  return nsm_clnt != NULL;
}

void nsm_disconnect()
{
  if(nsm_count == 0 && nsm_clnt != NULL)
    {
      Clnt_destroy(nsm_clnt);
      nsm_clnt = NULL;
      gsh_free(nodename);
      nodename = NULL;
    }
}

bool_t nsm_monitor(state_nsm_client_t *host)
{
  enum clnt_stat     ret;
  struct mon         nsm_mon;
  struct sm_stat_res res;
  struct timeval     tout = { 25, 0 };

  if(host == NULL)
    return TRUE;

  P(host->ssc_mutex);

  if(atomic_fetch_int32_t(&host->ssc_monitored))
    {
      V(host->ssc_mutex);
      return TRUE;
    }

  nsm_mon.mon_id.mon_name      = host->ssc_nlm_caller_name;
  nsm_mon.mon_id.my_id.my_prog = NLMPROG;
  nsm_mon.mon_id.my_id.my_vers = NLM4_VERS;
  nsm_mon.mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;
  /* nothing to put in the private data */
  LogDebug(COMPONENT_NLM,
           "Monitor %s",
           host->ssc_nlm_caller_name);

  P(nsm_mutex);

  /* create a connection to nsm on the localhost */
  if(!nsm_connect())
    {
      LogCrit(COMPONENT_NLM,
              "Can not monitor %s clnt_create returned NULL",
              nsm_mon.mon_id.mon_name);
      V(nsm_mutex);
      V(host->ssc_mutex);
      return FALSE;
    }

  /* Set this after we call nsm_connect() */
  nsm_mon.mon_id.my_id.my_name = nodename;

  ret = clnt_call(nsm_clnt,
                  SM_MON,
                  (xdrproc_t) xdr_mon,
                  (caddr_t) & nsm_mon,
                  (xdrproc_t) xdr_sm_stat_res,
                  (caddr_t) & res,
                  tout);

  if(ret != RPC_SUCCESS)
    {
      LogCrit(COMPONENT_NLM,
              "Can not monitor %s SM_MON ret %d %s",
              nsm_mon.mon_id.mon_name, ret, clnt_sperror(nsm_clnt, ""));
      nsm_disconnect();
      V(nsm_mutex);
      V(host->ssc_mutex);
      return FALSE;
    }

  if(res.res_stat != STAT_SUCC)
    {
      LogCrit(COMPONENT_NLM,
              "Can not monitor %s SM_MON status %d",
              nsm_mon.mon_id.mon_name, res.res_stat);
      nsm_disconnect();
      V(nsm_mutex);
      V(host->ssc_mutex);
      return FALSE;
    }

  nsm_count++;
  atomic_store_int32_t(&host->ssc_monitored, TRUE);
  LogDebug(COMPONENT_NLM,
           "Monitored %s for nodename %s", nsm_mon.mon_id.mon_name, nodename);

  V(nsm_mutex);
  V(host->ssc_mutex);
  return TRUE;
}

bool_t nsm_unmonitor(state_nsm_client_t *host)
{
  enum clnt_stat ret;
  struct sm_stat res;
  struct mon_id  nsm_mon_id;
  struct timeval tout = { 25, 0 };

  if(host == NULL)
    return TRUE;

  P(host->ssc_mutex);

  if(!atomic_fetch_int32_t(&host->ssc_monitored))
    {
      V(host->ssc_mutex);
      return TRUE;
    }

  nsm_mon_id.mon_name      = host->ssc_nlm_caller_name;
  nsm_mon_id.my_id.my_prog = NLMPROG;
  nsm_mon_id.my_id.my_vers = NLM4_VERS;
  nsm_mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;

  P(nsm_mutex);

  /* create a connection to nsm on the localhost */
  if(!nsm_connect())
    {
      LogCrit(COMPONENT_NLM,
              "Can not unmonitor %s clnt_create returned NULL",
              nsm_mon_id.mon_name);
      V(nsm_mutex);
      V(host->ssc_mutex);
      return FALSE;
    }

  /* Set this after we call nsm_connect() */
  nsm_mon_id.my_id.my_name = nodename;

  ret = clnt_call(nsm_clnt,
                  SM_UNMON,
                  (xdrproc_t) xdr_mon_id,
                  (caddr_t) & nsm_mon_id,
                  (xdrproc_t) xdr_sm_stat,
                  (caddr_t) & res,
                  tout);

  if(ret != RPC_SUCCESS)
    {
      LogCrit(COMPONENT_NLM,
              "Can not unmonitor %s SM_MON ret %d %s",
              nsm_mon_id.mon_name, ret, clnt_sperror(nsm_clnt, ""));
      nsm_disconnect();
      V(nsm_mutex);
      V(host->ssc_mutex);
      return FALSE;
    }

  atomic_store_int32_t(&host->ssc_monitored, FALSE);
  nsm_count--;

  LogDebug(COMPONENT_NLM,
           "Unonitored %s for nodename %s", nsm_mon_id.mon_name, nodename);

  nsm_disconnect();

  V(nsm_mutex);
  V(host->ssc_mutex);
  return TRUE;
}

void nsm_unmonitor_all(void)
{
  enum clnt_stat ret;
  struct sm_stat res;
  struct my_id   nsm_id;
  struct timeval tout = { 25, 0 };

  nsm_id.my_prog = NLMPROG;
  nsm_id.my_vers = NLM4_VERS;
  nsm_id.my_proc = NLMPROC4_SM_NOTIFY;

  P(nsm_mutex);

  /* create a connection to nsm on the localhost */
  if(!nsm_connect())
    {
      LogCrit(COMPONENT_NLM,
              "Can not unmonitor all clnt_create returned NULL");
      V(nsm_mutex);
      return;
    }

  /* Set this after we call nsm_connect() */
  nsm_id.my_name = nodename;

  ret = clnt_call(nsm_clnt,
                  SM_UNMON_ALL,
                  (xdrproc_t) xdr_my_id,
                  (caddr_t) & nsm_id,
                  (xdrproc_t) xdr_sm_stat,
                  (caddr_t) & res,
                  tout);

  if(ret != RPC_SUCCESS)
    {
      LogCrit(COMPONENT_NLM,
              "Can not unmonitor all ret %d %s",
              ret, clnt_sperror(nsm_clnt, ""));
    }

  nsm_disconnect();
  V(nsm_mutex);
}
