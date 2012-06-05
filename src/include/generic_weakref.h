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
 * \file generic_weakref.h
 * \author Matt Benjamin
 * \brief Generic weak reference package
 *
 * \section DESCRIPTION
 *
 * This module defines an infrastructure for enforcement of
 * reference counting guarantees, eviction safety, and access restrictions
 * using ordinary object addresses.
 *
 */

#ifndef _GENERIC_WEAKREF_H
#define _GENERIC_WEAKREF_H


typedef struct gweakref_
{
    void *ptr;
    uint64_t gen;
} gweakref_t;

typedef struct gweakref_table_ gweakref_table_t;

gweakref_table_t *gweakref_init(uint32_t npart, uint32_t cache_sz);
gweakref_t gweakref_insert(gweakref_table_t *wt, void *obj);
void *gweakref_lookup(gweakref_table_t *wt, gweakref_t *ref);
void *gweakref_lookupex(gweakref_table_t *wt, gweakref_t *ref,
                        pthread_rwlock_t **lock);
void gweakref_delete(gweakref_table_t *wt, gweakref_t *ref);
void gweakref_destroy(gweakref_table_t *wt);

#endif /* _GENERIC_WEAKREF_H */
