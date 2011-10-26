/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Venkateswararao Jujjuri   jujjuri@gmail.com
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
 * ---------------------------------------
 */

/**
 * \file    nfs_tcb.c
 * \author  $Author: leibovic $
 * \brief   The file that contain thread control block related code
 *
 * nfs_tcb.c : The file that contain thread control block related code
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "nfs_core.h"
#include "nfs_tcb.h"
#include "nlm_list.h"


pthread_mutex_t   tcb_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct glist_head tcb_head;

void tcb_head_init(void)
{
  init_glist(&tcb_head);
}

void tcb_insert(nfs_tcb_t *element)
{
  P(tcb_mutex);
  glist_add_tail(&tcb_head, &element->tcb_list);
  V(tcb_mutex);
}

void tcb_remove(nfs_tcb_t *element)
{
  P(tcb_mutex);
  glist_del(&element->tcb_list);
  V(tcb_mutex);
}
