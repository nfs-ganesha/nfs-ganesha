/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
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

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file sal_shared.h
 * @brief Data structures for state management.
 */

#ifndef SAL_SHARED_H
#define SAL_SHARED_H

/**
 * @brief Type of state
 */

enum state_type {
	STATE_TYPE_NONE,
	STATE_TYPE_SHARE,
	STATE_TYPE_DELEG,
	STATE_TYPE_LOCK,
	STATE_TYPE_LAYOUT,
	STATE_TYPE_NLM_LOCK,
	STATE_TYPE_NLM_SHARE,
	STATE_TYPE_COPY_OFFLOAD,
	STATE_TYPE_MAX,
};

#endif /* SAL_SHARED_H */
/** @} */
