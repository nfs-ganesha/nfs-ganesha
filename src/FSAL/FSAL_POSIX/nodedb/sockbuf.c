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


#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#include "sockbuf.h"


void sockbuf_init (struct sockbuf *s, int sock)
{
    memset (s, '\0', sizeof (*s));
    s->sock = sock;
}

void sockbuf_free (struct sockbuf *s)
{
    if (s->data)
        free (s->data);
    memset (s, '\0', sizeof (*s));
}

/* -1 means remote close connection */
int sockbuf_error (struct sockbuf *s)
{
    return s->save_errno;
}

int sockbuf_peek (struct sockbuf *s, void *out, int len)
{
    if (s->save_errno)
        return 1;

    while (s->avail - s->written < len) {
        int c;
        if (s->avail == s->alloced) {
            s->alloced += 1024;
            s->data = (char *) realloc (s->data, s->alloced);
        }
        c = recv (s->sock, s->data + s->avail, s->alloced - s->avail, 0);
        if (c > 0) {
            s->avail += c;
        } else {
            s->save_errno = c ? errno : -1;
            return 1;
        }
    }

    memcpy (out, s->data + s->written, len);

    return 0;
}

int sockbuf_recv (struct sockbuf *s, void *out, int len)
{
    if (s->save_errno)
        return 1;

    if (sockbuf_peek (s, out, len))
        return 1;

    s->written += len;

    if (s->written == s->avail)
        s->written = s->avail = 0;

    return 0;
}

int sockbuf_flush (struct sockbuf *s)
{
    if (s->save_errno)
        return 1;

    while (s->avail > s->written) {
        int c;
        c = send (s->sock, s->data + s->written, s->avail - s->written, 0);
        if (c > 0) {
            s->written += c;
        } else {
            s->save_errno = c ? errno : -1;
            return 1;
        }
    }

    s->written = s->avail = 0;

    return 0;
}

int sockbuf_send (struct sockbuf *s, const void *in, int len)
{
    if (s->save_errno)
        return 1;

    if (s->avail + len > s->alloced) {
        while (s->avail + len > s->alloced)
            s->alloced += 1024;
        s->data = (char *) realloc (s->data, s->alloced);
    }

    memcpy (s->data + s->avail, in, len);
    s->avail += len;

    if (s->avail - s->written >= 4096) {
        int c;
        c = send (s->sock, s->data + s->written, s->avail - s->written, 0);
        if (c > 0) {
            s->written += c;
        } else {
            s->save_errno = c ? errno : -1;
            return 1;
        }
        if (s->written == s->avail)
            s->written = s->avail = 0;
    }

    return 0;
}




