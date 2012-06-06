/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
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

/* File I/O */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "FSAL/fsal_commonlib.h"

fsal_status_t
pxy_open(struct fsal_obj_handle *obj_hdl,
         fsal_openflags_t openflags)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

fsal_status_t
pxy_read(struct fsal_obj_handle *obj_hdl,
	 fsal_seek_t * seek_descriptor,
	 size_t buffer_size,
	 caddr_t buffer,
	 ssize_t *read_amount,
	 fsal_boolean_t * end_of_file)
{
        ReturnCode(ERR_FSAL_IO, EIO);
}

fsal_status_t
pxy_write(struct fsal_obj_handle *obj_hdl,
	  fsal_seek_t *seek_descriptor,
	  size_t buffer_size,
	  caddr_t buffer,
	  ssize_t *write_amount)
{
        ReturnCode(ERR_FSAL_IO, EIO);
}

fsal_status_t
pxy_commit(struct fsal_obj_handle *obj_hdl,
	   off_t offset,
	   size_t len)
{
        ReturnCode(ERR_FSAL_IO, EIO);
}

fsal_status_t 
pxy_lock_op(struct fsal_obj_handle *obj_hdl,
	    void * p_owner,
	    fsal_lock_op_t lock_op,
	    fsal_lock_param_t   request_lock,
	    fsal_lock_param_t * conflicting_lock)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

fsal_status_t
pxy_share_op(struct fsal_obj_handle *obj_hdl,
	     void *p_owner,
	     fsal_share_param_t request_share)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t
pxy_close(struct fsal_obj_handle *obj_hdl)
{
	ReturnCode(ERR_FSAL_PERM, EPERM);
}

fsal_status_t
pxy_lru_cleanup(struct fsal_obj_handle *obj_hdl,
		lru_actions_t requests)
{
	ReturnCode(ERR_FSAL_PERM, EPERM);
}

fsal_status_t
pxy_rcp(struct fsal_obj_handle *obj_hdl,
        const char *local_path,
        fsal_rcpflag_t transfer_opt)
{
	ReturnCode(ERR_FSAL_PERM, EPERM);
}

