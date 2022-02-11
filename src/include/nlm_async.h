/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#ifndef NLM_ASYNC_H
#define NLM_ASYNC_H

#include <pthread.h>

#include "sal_data.h"

extern pthread_mutex_t nlm_async_resp_mutex;
extern pthread_cond_t nlm_async_resp_cond;

int nlm_async_callback_init(void);

int nlm_send_async_res_nlm4(state_nlm_client_t *host, state_async_func_t func,
			    nfs_res_t *pres);

int nlm_send_async_res_nlm4test(state_nlm_client_t *host,
				state_async_func_t func, nfs_res_t *pres);

/* Client routine  to send the asynchrnous response, key is used to wait for
 * a response
 */
int nlm_send_async(int proc, state_nlm_client_t *host, void *inarg, void *key);

void nlm_signal_async_resp(void *key);

#endif				/* NLM_ASYNC_H */
