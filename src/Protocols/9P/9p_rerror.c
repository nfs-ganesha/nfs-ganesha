/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_rerror.c
 * \brief   9P version
 *
 * 9p_rerror.c : _9P_interpretor, request RERROR
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "9p.h"

int _9p_rerror(struct _9p_request_data *req9p, void *worker_data, u16 *msgtag,
	       u32 err, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u8 msgtype = *(req9p->_9pmsg + _9P_HDR_SIZE);
	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RERROR);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, err, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	/* Check boundaries. 0 is no_function fallback */
	if (msgtype < _9P_TSTATFS || msgtype > _9P_TWSTAT
	    || _9pfuncdesc[msgtype].service_function == NULL)
		msgtype = 0;

	LogDebug(COMPONENT_9P, "RERROR(%s) tag=%u err=(%u|%s)",
		 _9pfuncdesc[msgtype].funcname, *msgtag, err, strerror(err));

	return 1;
}
