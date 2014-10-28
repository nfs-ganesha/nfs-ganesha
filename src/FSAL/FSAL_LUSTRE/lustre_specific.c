/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "lustre_methods.h"
#include "fsal_handle.h"
#include <stdbool.h>

/**
 *
 * \file    fake_shook_glue.c
 *
 */

const char myname[] = "LUSTRE";

fsal_status_t lustre_shook_restore(struct fsal_obj_handle *obj_hdl,
				   bool do_truncate,
				   int *trunc_done)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
