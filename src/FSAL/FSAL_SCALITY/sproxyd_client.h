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

/* sproxyd_client.h
 */

#ifndef __SPROXYD_CLIENT_H__
#define __SPROXYD_CLIENT_H__

int
sproxyd_head(struct scality_fsal_export* export,
	     const char *id,
	     size_t *lenp);

int
sproxyd_read(struct scality_fsal_export* export,
	     struct scality_fsal_obj_handle *obj,
	     uint64_t offset,
	     size_t size, char *buf);

int
sproxyd_delete(struct scality_fsal_export* export,
	       const char *id);

char *
sproxyd_new_key(void);

int
sproxyd_put(struct scality_fsal_export* export,
	    const char *id,
	    char *buf,
	    size_t size);

#endif /* __SPROXYD_CLIENT_H__ */
