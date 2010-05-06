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

#include <rpc/rpc.h>

#include "nsm.h"
#include "nlm4.h"

int nsm_monitor(char *host)
{

  CLIENT *clnt;
  enum clnt_stat ret;
  struct mon nsm_mon;
  struct sm_stat_res res;
  struct timeval tout = { 5, 0 };

  nsm_mon.mon_id.mon_name = strdup(host);
  nsm_mon.mon_id.my_id.my_name = strdup("localhost");
  nsm_mon.mon_id.my_id.my_prog = NLMPROG;
  nsm_mon.mon_id.my_id.my_vers = NLM4_VERS;
  nsm_mon.mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;
  /* nothing to put in the private data */

  /* create a connection to nsm on the localhost */
  clnt = clnt_create("localhost", SM_PROG, SM_VERS, "tcp");
  if(!clnt)
    {
      free(nsm_mon.mon_id.mon_name);
      free(nsm_mon.mon_id.my_id.my_name);
      return -1;
    }

  ret = clnt_call(clnt, SM_MON,
                  (xdrproc_t) xdr_mon, (caddr_t) & nsm_mon,
                  (xdrproc_t) xdr_sm_stat_res, (caddr_t) & res, tout);
  if(ret != RPC_SUCCESS)
    {
      free(nsm_mon.mon_id.mon_name);
      free(nsm_mon.mon_id.my_id.my_name);
      clnt_destroy(clnt);
      return -1;
    }
  if(res.res_stat != STAT_SUCC)
    {
      free(nsm_mon.mon_id.mon_name);
      free(nsm_mon.mon_id.my_id.my_name);
      clnt_destroy(clnt);
      return -1;
    }
  free(nsm_mon.mon_id.mon_name);
  free(nsm_mon.mon_id.my_id.my_name);
  clnt_destroy(clnt);
  return 0;
}

int nsm_unmonitor(char *host)
{

  CLIENT *clnt;
  enum clnt_stat ret;
  struct sm_stat res;
  struct mon_id nsm_mon_id;
  struct timeval tout = { 5, 0 };

  nsm_mon_id.mon_name = strdup(host);
  nsm_mon_id.my_id.my_name = strdup("localhost");
  nsm_mon_id.my_id.my_prog = NLMPROG;
  nsm_mon_id.my_id.my_vers = NLM4_VERS;
  nsm_mon_id.my_id.my_proc = NLMPROC4_SM_NOTIFY;

  /* create a connection to nsm on the localhost */
  clnt = clnt_create("localhost", SM_PROG, SM_VERS, "tcp");
  if(!clnt)
    {
      free(nsm_mon_id.mon_name);
      free(nsm_mon_id.my_id.my_name);
      return -1;
    }

  ret = clnt_call(clnt, SM_UNMON,
                  (xdrproc_t) xdr_mon_id, (caddr_t) & nsm_mon_id,
                  (xdrproc_t) xdr_sm_stat, (caddr_t) & res, tout);
  if(ret != RPC_SUCCESS)
    {
      free(nsm_mon_id.mon_name);
      free(nsm_mon_id.my_id.my_name);
      clnt_destroy(clnt);
      return -1;
    }
  free(nsm_mon_id.mon_name);
  free(nsm_mon_id.my_id.my_name);
  clnt_destroy(clnt);
  return 0;
}

int nsm_unmonitor_all(void)
{
  CLIENT *clnt;
  enum clnt_stat ret;
  struct sm_stat res;
  struct my_id nsm_id;
  struct timeval tout = { 5, 0 };

  nsm_id.my_name = strdup("localhost");
  nsm_id.my_prog = NLMPROG;
  nsm_id.my_vers = NLM4_VERS;
  nsm_id.my_proc = NLMPROC4_SM_NOTIFY;

  /* create a connection to nsm on the localhost */
  clnt = clnt_create("localhost", SM_PROG, SM_VERS, "tcp");
  if(!clnt)
    {
      free(nsm_id.my_name);
      return -1;
    }

  ret = clnt_call(clnt, SM_UNMON_ALL,
                  (xdrproc_t) xdr_my_id, (caddr_t) & nsm_id,
                  (xdrproc_t) xdr_sm_stat, (caddr_t) & res, tout);
  if(ret != RPC_SUCCESS)
    {
      free(nsm_id.my_name);
      clnt_destroy(clnt);
      return -1;
    }
  free(nsm_id.my_name);
  clnt_destroy(clnt);
  return 0;
}
