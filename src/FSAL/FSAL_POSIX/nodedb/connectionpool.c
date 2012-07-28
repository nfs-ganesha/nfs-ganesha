/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
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
 * ------------- 
 */


#include <pthread.h>


#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include "sockbuf.h"
#include "connection.h"



/* pool size */
#define MAX_POOL_SIZE        40

struct connection;


struct locked_connection {
    pthread_mutex_t mutex;
    struct connection *conn;
};

struct connection_pool {
    struct locked_connection lockedconn[MAX_POOL_SIZE];
    int round_robin;
};


struct connection_pool *connection_pool_new (void)
{
    int i;
    struct connection_pool *r;
    r = (struct connection_pool *) malloc (sizeof (*r));
    memset (r, '\0', sizeof (*r));
    for (i = 0; i < MAX_POOL_SIZE; i++)
        pthread_mutex_init (&r->lockedconn[i].mutex, NULL);
    return r;
}

void connection_pool_free (struct connection_pool *c)
{
    int i;
    for (i = 0; i < MAX_POOL_SIZE; i++) {
        pthread_mutex_lock (&c->lockedconn[i].mutex);
        if (c->lockedconn[i].conn)
            connection_free (c->lockedconn[i].conn);
        pthread_mutex_unlock (&c->lockedconn[i].mutex);
        pthread_mutex_destroy (&c->lockedconn[i].mutex);
    }
    memset (c, '\0', sizeof (*c));
    free (c);
}

/* re-use the pool -- we try not add new connections unless the others are busy */
struct locked_connection *connection_pool_get_locked_ (struct connection_pool *c, int start)
{
    int i, r;

    i = start;

/* Try 1: try use an already-initialized idle connection: */
    do {
        if (!pthread_mutex_trylock (&c->lockedconn[i].mutex)) {
            if (c->lockedconn[i].conn)
                return &c->lockedconn[i];
            pthread_mutex_unlock (&c->lockedconn[i].mutex);
        }
    } while ((i = (i + 1) % MAX_POOL_SIZE) != start);

    assert (i == start);

/* Try 2: try initialize a *new* connection: */
    do {
        if (!pthread_mutex_trylock (&c->lockedconn[i].mutex)) {
            if (!c->lockedconn[i].conn)
                c->lockedconn[i].conn = connection_new ();
            return &c->lockedconn[i];
        }
    } while ((i = (i + 1) % MAX_POOL_SIZE) != start);

    assert (i == start);

/* Try 3: everyone in the pool is in use by another thread, thus wait on an existing connection: */
    r = pthread_mutex_lock (&c->lockedconn[start].mutex);
    assert (!r);
    if (!c->lockedconn[start].conn)
        c->lockedconn[start].conn = connection_new ();
    return &c->lockedconn[start];
}

struct locked_connection *connection_pool_get_locked (struct connection_pool *c)
{
    int start;
    c->round_robin = (c->round_robin + 1) % MAX_POOL_SIZE;      /* threads overwriting each other is ok here */
    start = c->round_robin;
    return connection_pool_get_locked_ (c, start);
}

void locked_connection_unlock (struct locked_connection *c)
{
    pthread_mutex_unlock (&c->mutex);
}


