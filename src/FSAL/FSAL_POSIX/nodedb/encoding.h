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


struct nodedb;
struct stat;
struct file_data;

enum param_magic {
    PARAM_MAGIC_NULL = 0,
    PARAM_MAGIC_ENDVAR,
    PARAM_MAGIC_OP,
    PARAM_MAGIC_UKNOWN_OP,
    PARAM_MAGIC_STRUCT,
    PARAM_MAGIC_INT,
    PARAM_MAGIC_STRING,


    PARAM_MAGIC_BOGUS = 255,
    __PARAM_MAGIC_LAST
};

void _decode_struct (struct connection *conn, void **in, int len);
void _encode_struct (struct connection *conn, const void *out, int len);

void decode_stat(struct connection *conn, struct stat **stat);
void encode_stat(struct connection *conn, const struct stat *stat);
void decode_file_data(struct connection *conn, struct file_data **file_data);
void encode_file_data(struct connection *conn, const struct file_data *file_data);
void decode_handle_data(struct connection *conn, struct handle_data **handle_data);
void encode_handle_data(struct connection *conn, const struct handle_data *handle_data);

void decode_char_p (struct connection *conn, char **p);
void encode_char_p (struct connection *conn, const char *p);
void decode_int (struct connection *conn, int *p);
void encode_int (struct connection *conn, const int *p);


void decode_op (struct connection *conn, enum ops *p);
void encode_op (struct connection *conn, enum ops p);

void encode_flush (struct connection *conn);

void decode_endvars (struct connection *conn);
void encode_endvars (struct connection *conn);



int decode_error(struct connection *conn);
int encode_error(struct connection *conn);
void encode_null(struct connection *conn);
int decode_null(struct connection *conn);
void encode_unknown(struct connection *conn);


void free_char_p(char **p);


