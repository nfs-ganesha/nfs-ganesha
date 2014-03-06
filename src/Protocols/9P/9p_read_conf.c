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
 * \file    9p_proto_tools.c
 * \brief   9P version
 *
 * 9p_proto_tools.c : _9P_interpretor, protocol's service functions
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "9p.h"
#include "config_parsing.h"

static struct config_item _9p_params[] = {
	CONF_ITEM_UI16("_9P_TCP_Port", 1, 0xFFFF, _9P_TCP_PORT,
		       _9p_param, _9p_tcp_port),
	CONF_ITEM_UI16("_9P_RDMA_Port", 1, 0xFFFF, _9P_RDMA_PORT,
		       _9p_param, _9p_rdma_port),
	CONF_ITEM_UI32("_9P_TCP_Msize", 1024, 0xFFFFFFFF, _9P_TCP_MSIZE,
		       _9p_param, _9p_tcp_msize),
	CONF_ITEM_UI32("_9P_RDMA_Msize", 1024, 0xFFFFFFFF, _9P_RDMA_MSIZE,
		       _9p_param, _9p_rdma_msize),
	CONF_ITEM_UI16("_9P_RDMA_Backlog", 1, 0xFFFF, _9P_RDMA_BACKLOG,
		       _9p_param, _9p_rdma_backlog),
	CONF_ITEM_UI16("_9P_RDMA_Inpool_size", 1, 0xFFFF, _9P_RDMA_INPOOL_SIZE,
		       _9p_param, _9p_rdma_inpool_size),
	CONF_ITEM_UI16("_9P_RDMA_Outpool_Size", 1, 0xFFFF,
		       _9P_RDMA_OUTPOOL_SIZE,
		       _9p_param, _9p_rdma_outpool_size),
	CONFIG_EOL
};

struct config_block _9p_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.9p",
	.blk_desc.name = "_9P",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = _9p_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

int _9p_read_conf(config_file_t in_config, _9p_parameter_t *pparam)
{
	int rc;

	rc = load_config_from_parse(in_config,
				    &_9p_param,
				    pparam,
				    true);
	return (rc == 0)? -2 : ((rc < 0) ? -1 : 1);
}				/* _9p_read_conf */
