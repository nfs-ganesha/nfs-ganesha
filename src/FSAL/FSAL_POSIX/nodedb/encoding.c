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


#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct file_data;
struct stat;
struct nodedb;
struct connection;

#include "sockbuf.h"
#include "nodedb.h"
#include "connection.h"
#include "ops.h"
#include "encoding.h"

void encode_flush (struct connection *conn)
{
    sockbuf_flush (&conn->output);
}

static int decode_magic (struct connection *conn, enum param_magic m)
{
    unsigned char i = PARAM_MAGIC_BOGUS;
    sockbuf_peek (&conn->input, &i, sizeof (i));
    if ((int) i == (int) m) {
        sockbuf_recv (&conn->input, &i, sizeof (i));
        return 0;
    }
    return 1;
}

static void encode_magic (struct connection *conn, enum param_magic m)
{
    unsigned char i;
    i = (unsigned char) (int) m;
    sockbuf_send (&conn->output, &i, sizeof (i));
}

void _decode_struct (struct connection *conn, void **in, int len)
{
    if (conn->decode_error)
        return;
    if (decode_null (conn)) {
        *in = NULL;
        return;
    }
    if (decode_magic (conn, PARAM_MAGIC_STRUCT)) {
        conn->decode_error = MARSHAL_ERROR_MISMATCH_PARAM_TYPE;
        conn->decode_errortext = conn->progress;
        return;
    }
    sockbuf_recv (&conn->input, *in, len);
}

void _encode_struct (struct connection *conn, const void *out, int len)
{
    if (conn->encode_error)
        return;
    if (!out) {
        encode_null (conn);
    } else {
        encode_magic (conn, PARAM_MAGIC_STRUCT);
        sockbuf_send (&conn->output, out, len);
    }
}

void decode_stat (struct connection *conn, struct stat **v)
{
    _decode_struct (conn, (void **) v, sizeof (**v));
}

void encode_stat (struct connection *conn, const struct stat *v)
{
    _encode_struct (conn, (void *) v, sizeof (*v));
}

void decode_file_data (struct connection *conn, struct file_data **v)
{
    _decode_struct (conn, (void **) v, sizeof (**v));
#if 0
    if (v && *v)
        printf ("decode filedata - %llu:%llu  %llu:%llu\n", (*v)->handle.handleid, (*v)->handle.timestamp, (*v)->devid, (*v)->inode);
#endif
}

void encode_file_data (struct connection *conn, const struct file_data *v)
{
#if 0
    if (v)
        printf ("encode filedata - %llu:%llu  %llu:%llu\n", v->handle.handleid, v->handle.timestamp, v->devid, v->inode);
#endif
    _encode_struct (conn, (void *) v, sizeof (*v));
}

void decode_handle_data (struct connection *conn, struct handle_data **v)
{
    _decode_struct (conn, (void **) v, sizeof (**v));
#if 0
    if (v && *v)
        printf ("decode handle - %llu:%llu\n", (*v)->handleid, (*v)->timestamp);
#endif
}

void encode_handle_data (struct connection *conn, const struct handle_data *v)
{
#if 0
    if (v)
        printf ("encode handle - %llu:%llu\n", v->handleid, v->timestamp);
#endif
    _encode_struct (conn, (void *) v, sizeof (*v));
}

void decode_char_p (struct connection *conn, char **p)
{
    int len;
    *p = NULL;
    if (conn->decode_error)
        return;
    if (decode_magic (conn, PARAM_MAGIC_STRING)) {
        conn->decode_error = MARSHAL_ERROR_MISMATCH_PARAM_TYPE;
        conn->decode_errortext = conn->progress;
        return;
    }
    sockbuf_recv (&conn->input, &len, sizeof (len));
    if (sockbuf_error (&conn->input))
        return;
    assert (len >= 0);
    *p = (char *) malloc (len + 1);
    memset (*p, '\0', len + 1);         /* precautionary - partial reads? */
    sockbuf_recv (&conn->input, *p, len);
}

void encode_char_p (struct connection *conn, const char *p)
{
    int len;
    if (conn->encode_error)
        return;
    len = strlen (p);
    encode_magic (conn, PARAM_MAGIC_STRING);
    sockbuf_send (&conn->output, &len, sizeof (len));
    sockbuf_send (&conn->output, p, len);
}

void decode_int (struct connection *conn, int *p)
{
    if (conn->decode_error)
        return;
    if (decode_magic (conn, PARAM_MAGIC_INT)) {
        conn->decode_error = MARSHAL_ERROR_MISMATCH_PARAM_TYPE;
        conn->decode_errortext = conn->progress;
        return;
    }
    sockbuf_recv (&conn->input, p, sizeof (*p));
}

void encode_int (struct connection *conn, const int *p)
{
    if (conn->encode_error)
        return;
    encode_magic (conn, PARAM_MAGIC_INT);
    sockbuf_send (&conn->output, p, sizeof (*p));
}

void decode_op (struct connection *conn, enum ops *p)
{
    int op;
    if (conn->decode_error)
        return;
    if (decode_magic (conn, PARAM_MAGIC_OP)) {
        conn->decode_error = MARSHAL_ERROR_OP;
        conn->decode_errortext = "op";
        return;
    }
    sockbuf_recv (&conn->input, &op, sizeof (op));
    *p = op;
}

void encode_op (struct connection *conn, enum ops p)
{
    int op;
    if (conn->encode_error)
        return;
    encode_magic (conn, PARAM_MAGIC_OP);
    op = (int) p;
    sockbuf_send (&conn->output, &op, sizeof (op));
}

void decode_endvars (struct connection *conn)
{
    if (conn->decode_error)
        return;
    if (decode_magic (conn, PARAM_MAGIC_ENDVAR)) {
        conn->decode_error = MARSHAL_ERROR_MISMATCH_PARAM_TYPE;
        conn->decode_errortext = "";
        return;
    }
}

void encode_endvars (struct connection *conn)
{
    if (conn->encode_error)
        return;
    encode_magic (conn, PARAM_MAGIC_ENDVAR);
}

int decode_null (struct connection *conn)
{
    if (conn->decode_error)
        return 1;
    return !decode_magic (conn, PARAM_MAGIC_NULL);
}

void encode_null (struct connection *conn)
{
    if (conn->encode_error)
        return;
    encode_magic (conn, PARAM_MAGIC_NULL);
}

int decode_unknown (struct connection *conn)
{
    if (conn->decode_error)
        return 1;
    return !decode_magic (conn, PARAM_MAGIC_UKNOWN_OP);
}

void encode_unknown (struct connection *conn)
{
    if (conn->encode_error)
        return;
    encode_magic (conn, PARAM_MAGIC_UKNOWN_OP);
}

int decode_error(struct connection *conn)
{
    return conn->decode_error;
}

int encode_error(struct connection *conn)
{
    return conn->encode_error;
}

void free_char_p (char **p)
{
    if (*p) {
        free (*p);
        *p = NULL;
    }
}



