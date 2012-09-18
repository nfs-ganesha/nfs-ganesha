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

struct thr_fridge;

struct fridge_thr_entry
{
    struct fridge_thr_context
    {
        uint32_t uflags;
        pthread_t id;
        pthread_mutex_t mtx;
        pthread_cond_t cv;
        sigset_t sigmask;
        void *(*func)(void*);
        void *arg;
    } ctx;
    uint32_t flags;
    bool frozen;
    struct timespec timeout;
    struct timeval tp;
    struct glist_head q;
    struct thr_fridge *fr;
};

typedef struct fridge_thr_entry fridge_entry_t;
typedef struct fridge_thr_context fridge_thr_contex_t;

typedef struct thr_fridge
{
    char *s;
    pthread_mutex_t mtx;
    uint32_t thr_max;
    uint32_t stacksize;
    uint32_t expiration_delay_s;
    pthread_attr_t attr;
    uint32_t nthreads;
    struct glist_head idle_q;
    uint32_t nidle;
    uint32_t flags;
} thr_fridge_t;


#define FridgeThr_Flag_None          0x0000
#define FridgeThr_Flag_WaitSync      0x0001
#define FridgeThr_Flag_SyncDone      0x0002

int fridgethr_init(thr_fridge_t *, const char *s);

struct fridge_thr_context *
fridgethr_get(thr_fridge_t *fr, void *(*func)(void*),
              void *arg);

#endif /* FRIDGETHR_H */
