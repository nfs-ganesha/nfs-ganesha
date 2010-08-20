/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include "nlm_send_reply.h"
#include "nlm4.h"
#include "nfs_proto_functions.h"

nlm_reply_proc_t nlm_reply_proc[] = {

  [NLMPROC4_GRANTED_MSG] = {
                            .inproc = (xdrproc_t) xdr_nlm4_testargs,
                            .outproc = (xdrproc_t) xdr_void,
                            }
  ,
  [NLMPROC4_TEST_RES] = {
                         .inproc = (xdrproc_t) xdr_nlm4_testres,
                         .outproc = (xdrproc_t) xdr_void,
                         }
  ,
  [NLMPROC4_LOCK_RES] = {
                         .inproc = (xdrproc_t) xdr_nlm4_res,
                         .outproc = (xdrproc_t) xdr_void,
                         }
  ,
  [NLMPROC4_CANCEL_RES] = {
                           .inproc = (xdrproc_t) xdr_nlm4_res,
                           .outproc = (xdrproc_t) xdr_void,
                           }
  ,
  [NLMPROC4_UNLOCK_RES] = {
                           .inproc = (xdrproc_t) xdr_nlm4_res,
                           .outproc = (xdrproc_t) xdr_void,
                           }
  ,
};

/* Client routine  to send the asynchrnous response */
int nlm_send_reply(int proc, char *host, void *inarg, void *outarg)
{
  CLIENT *clnt;
  struct timeval tout = { 5, 0 };
  xdrproc_t inproc = NULL, outproc = NULL;
  int retval;

  clnt = clnt_create(host, NLMPROG, NLM4_VERS, "tcp");
  if(!clnt)
    {
      LogMajor(COMPONENT_NFSPROTO, "%s: Cannot create connection to %s client\n",
               __func__, host);
      return -1;
    }
  inproc = nlm_reply_proc[proc].inproc;
  outproc = nlm_reply_proc[proc].outproc;

  retval = clnt_call(clnt, proc, inproc, inarg, outproc, outarg, tout);
  if(retval != RPC_SUCCESS)
    {
      LogMajor(COMPONENT_NFSPROTO, "%s: Client procedure call %d failed\n", __func__, proc);
    }

  clnt_destroy(clnt);
  return retval;
}
