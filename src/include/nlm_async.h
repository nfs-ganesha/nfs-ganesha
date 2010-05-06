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

#include "nfs_proto_functions.h"

typedef struct nlm_async_res
{
  char *caller_name;
  nfs_res_t pres;
} nlm_async_res_t;

typedef void (nlm_callback_func) (void *arg);
extern int nlm_async_callback_init();
void nlm_async_callback(nlm_callback_func * func, void *arg);
extern int nlm_async_callback_init();

static inline nlm_async_res_t *nlm_build_async_res(char *caller_name, nfs_res_t * pres)
{
  nlm_async_res_t *arg;
  arg = (nlm_async_res_t *) Mem_Alloc(sizeof(nlm_async_res_t));
  arg->caller_name = strdup(caller_name);
  memcpy(&(arg->pres), pres, sizeof(nfs_res_t));
  return arg;
}

#endif                          /* NLM_ASYNC_H */
