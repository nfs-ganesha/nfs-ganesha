/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

/**
 *
 * \file cache_inode_weakref.h
 * \author Matt Benjamin
 * \brief Generic weak reference package
 *
 * \section DESCRIPTION
 *
 * Manage weak references to cache inode objects (e.g., references from
 * directory entries).
 *
 */

#ifndef _CACHE_INODE_WEAKREF_H
#define _CACHE_INODE_WEAKREF_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "cache_inode.h"
#include "generic_weakref.h"

void cache_inode_weakref_init(void);
gweakref_t cache_inode_weakref_insert(struct cache_entry_t *entry);
cache_entry_t *cache_inode_weakref_get(gweakref_t *ref,
                                       uint32_t flags);
void cache_inode_weakref_delete(gweakref_t *ref);
void cache_inode_weakref_shutdown();

#endif /* _CACHE_INODE_WEAKREF_H */
