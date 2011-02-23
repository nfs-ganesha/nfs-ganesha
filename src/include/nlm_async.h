/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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

#ifndef NLM_ASYNC_H
#define NLM_ASYNC_H

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include <pthread.h>

#include "nfs_proto_functions.h"

extern pthread_mutex_t nlm_async_resp_mutex;
extern pthread_cond_t nlm_async_resp_cond;

typedef struct nlm_async_res
{
  char *caller_name;
  nfs_res_t pres;
} nlm_async_res_t;

typedef void (nlm_callback_func) (void *arg);
extern int nlm_async_callback_init();
void nlm_async_callback(nlm_callback_func * func, void *arg);
extern int nlm_async_callback_init();

extern nlm_async_res_t *nlm_build_async_res_nlm4(char *caller_name, nfs_res_t * pres);

extern nlm_async_res_t *nlm_build_async_res_nlm4test(char *caller_name, nfs_res_t * pres);

typedef struct
{
  xdrproc_t inproc;
  xdrproc_t outproc;
} nlm_reply_proc_t;

/* Client routine  to send the asynchrnous response, key is used to wait for a response */
extern int nlm_send_async(int proc, char *host, void *inarg, void *key);
extern void nlm_signal_async_resp(void *key);

#endif                          /* NLM_ASYNC_H */
