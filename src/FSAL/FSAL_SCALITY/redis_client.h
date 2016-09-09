/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Scality Inc., 2016
 * Author: Guillaume Gimenez ploki@blackmilk.fr
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
 * -------------
 */

/* redis_client.h
 */

#ifndef __SCALITY_REDIS_CLIENT_H__
#define __SCALITY_REDIS_CLIENT_H__

int
redis_get_object(char *buf, int buf_sz, char *obj, int obj_sz);

int
redis_get_handle_key(const char *obj, char *buf, int buf_sz);

int
redis_create_handle_key(const char *obj, char *buf, int buf_sz);

void redis_remove(const char *obj);

int
redis_get_seekloc_marker(const char *obj,  fsal_cookie_t whence,
			 char *marker_buf, int marker_buf_len);

int
redis_set_seekloc_marker(const char *obj, const char *marker,
			 fsal_cookie_t *whencep);

#endif /* __SCALITY_REDIS_CLIENT_H__ */

