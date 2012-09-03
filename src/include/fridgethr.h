/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

#ifndef FRIDGETHR_H
#define FRIDGETHR_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "nfs_core.h"

typedef struct fridge_entry
{
    pthread_t id;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    bool frozen;
    void *arg;
    struct timespec timeout;
    struct timeval tp;
    /* XXX */
    struct fridge_entry *prev;
    struct fridge_entry *next;
} fridge_entry_t;

typedef struct thr_fridge
{
    pthread_mutex_t mtx;
    fridge_entry_t *entry;
    uint32_t thr_max;
    uint32_t thr_hiwat;
    uint32_t stacksize;
    uint32_t expiration_delay_s;
    pthread_attr_t attr;
    uint32_t nthreads;
    uint32_t flags;
} thr_fridge_t;

#define FRIDGETHR_FLAG_NONE  0x0000

int fridgethr_init(thr_fridge_t *);

int fridgethr_get(thr_fridge_t *, pthread_t *,
                  void *(*func)(void*), void *arg);

void *fridgethr_freeze(thr_fridge_t *fr);

#endif /* FRIDGETHR_H */
