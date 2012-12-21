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
struct marshal;

enum marshal_error {
    MARSHAL_ERROR_BAD_MAGIC = 1,
    MARSHAL_ERROR_OP,
    MARSHAL_ERROR_MISMATCH_PARAM_TYPE,
    __MARSHAL_ERROR_LAST
};


struct connection {
    struct marshal *marshal;
    struct nodedb *db;
    int sock;
    struct sockbuf input;
    struct sockbuf output;
    enum marshal_error encode_error;
    enum marshal_error decode_error;
    const char *progress;
    const char *decode_errortext;
};

struct connection *connection_new (void);
void connection_free (struct connection *c);





