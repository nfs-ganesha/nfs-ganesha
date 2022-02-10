/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @file fsal_init.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Module initialization
 */

/**
 * @brief Initializer macro
 *
 * Every FSAL module has an initializer.  any function labeled as
 * MODULE_INIT will be called in order after the module is loaded and
 * before dlopen returns.  This is where you register your fsal.
 *
 * The initializer function should use register_fsal to initialize
 * public data and get the default operation vectors, then override
 * them with module-specific methods.
 */

#define MODULE_INIT __attribute__((constructor))

/**
 * @brief Finalizer macro
 *
 * Every FSAL module *must* have a destructor to free any resources.
 * the function should assert() that the module can be safely unloaded.
 * However, the core should do the same check prior to attempting an
 * unload. The function must be defined as void foo(void), i.e. no args
 * passed and no returns evaluated.
 */

#define MODULE_FINI __attribute__((destructor))
