// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * @brief 9P protocol parameter tables
 *
 */

#include "config.h"
#include "9p.h"
#include "config_parsing.h"
#include "gsh_config.h"

/* 9P parameters, settable in the 9P stanza. */
struct _9p_param _9p_param;

static struct config_item _9p_params[] = {
	CONF_ITEM_UI32("Nb_Worker", 1, 1024*128, NB_WORKER_THREAD_DEFAULT,
		       _9p_param, nb_worker),
	CONF_ITEM_UI16("_9P_TCP_Port", 1, UINT16_MAX, _9P_TCP_PORT,
		       _9p_param, _9p_tcp_port),
	CONF_ITEM_UI16("_9P_RDMA_Port", 1, UINT16_MAX, _9P_RDMA_PORT,
		       _9p_param, _9p_rdma_port),
	CONF_ITEM_UI32("_9P_TCP_Msize", 1024, UINT32_MAX, _9P_TCP_MSIZE,
		       _9p_param, _9p_tcp_msize),
	CONF_ITEM_UI32("_9P_RDMA_Msize", 1024, UINT32_MAX, _9P_RDMA_MSIZE,
		       _9p_param, _9p_rdma_msize),
	CONF_ITEM_UI16("_9P_RDMA_Backlog", 1, UINT16_MAX, _9P_RDMA_BACKLOG,
		       _9p_param, _9p_rdma_backlog),
	CONF_ITEM_UI16("_9P_RDMA_Inpool_size", 1, UINT16_MAX,
		       _9P_RDMA_INPOOL_SIZE, _9p_param, _9p_rdma_inpool_size),
	CONF_ITEM_UI16("_9P_RDMA_Outpool_Size", 1, UINT16_MAX,
		       _9P_RDMA_OUTPOOL_SIZE,
		       _9p_param, _9p_rdma_outpool_size),
	CONFIG_EOL
};

static void *_9p_param_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL)
		return &_9p_param;
	else
		return NULL;
}

struct config_block _9p_param_blk = {
	.dbus_interface_name = "org.ganesha.nfsd.config.9p",
	.blk_desc.name = "_9P",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = _9p_param_init,
	.blk_desc.u.blk.params = _9p_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};
