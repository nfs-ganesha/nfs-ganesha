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



struct sockbuf {
    int sock;
    char *data;
    int written;
    int avail;
    int alloced;
    int save_errno;
};


void sockbuf_init (struct sockbuf *s, int sock);
void sockbuf_free (struct sockbuf *s);
int sockbuf_error (struct sockbuf *s);
int sockbuf_recv (struct sockbuf *s, void *out, int len);
int sockbuf_peek (struct sockbuf *s, void *out, int len);
int sockbuf_flush (struct sockbuf *s);
int sockbuf_send (struct sockbuf *s, const void *in, int len);

