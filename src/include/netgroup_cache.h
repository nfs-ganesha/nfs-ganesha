/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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

/* Returns true if the given host is in the given netgroup.  Uses
 * innetgr() to accomplish the task, and also caches such results.
 */

#ifndef NETGROUP_CACHE_H
#define NETGROUP_CACHE_H
void ng_cache_init(void);
void ng_clear_cache(void);
bool ng_innetgr(const char *group, const char *host);
#endif
