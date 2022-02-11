/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (c) 2013, The Linux Box Corporation
 *
 * Some portions copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup delayed Delayed Execution
 *
 * This provides a simple system allowing tasks to be submitted along
 * with a delay.

 * This is similar to the thread fridge, however there is a lot of
 * complication in the thread fridge that would make no sense here,
 * and the delay structure here is very different from the thread
 * fridge's task queue.  Someone could merge the two together, but it
 * would make the internal logic rather snarly and the initialization
 * parameters even more recondite.
 *
 * @{
 */

/**
 * @file delayed_exec.h
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Header for the delayed execution system
 */

#ifndef DELAYED_EXEC_H
#define DELAYED_EXEC_H

#include <stdint.h>
#include <stdbool.h>
#include "gsh_types.h"

void delayed_start(void);
void delayed_shutdown(void);
int delayed_submit(void (*)(void *), void *, nsecs_elapsed_t);

#endif				/* DELAYED_EXEC_H */

/** @} */
