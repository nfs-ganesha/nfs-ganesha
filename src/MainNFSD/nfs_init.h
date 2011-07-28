/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    nfs_init.h
 * \author  $Author: leibovic $
 * \brief   NFSd initialization prototypes.
 *
 */

#ifndef _NFS_INIT_H
#define _NFS_INIT_H

#include "cache_content.h"
#include "log_macros.h"
#include "nfs_core.h"

/* setting this variable to TRUE causes datacache
 * to be flushed by garbage collector.
 */
extern unsigned int force_flush_by_signal;

typedef struct __nfs_start_info
{
  int flush_datacache_mode;
  int dump_default_config;
  unsigned int nb_flush_threads;
  cache_content_flush_behaviour_t flush_behaviour;
  int lw_mark_trigger;
} nfs_start_info_t;

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
void nfs_prereq_init(char *program_name, char *host_name, int debug_level, char *log_path);

/**
 * nfs_set_param_default:
 * Set p_nfs_param structure to default parameters.
 */
void nfs_set_param_default();

/**
 * nfs_set_param_from_conf:
 * Load parameters from config file.
 */
int nfs_set_param_from_conf(nfs_start_info_t * p_start_info);

/**
 * nfs_check_param_consistency:
 * Checks parameters concistency (limits, ...)
 */
int nfs_check_param_consistency();

/**
 * nfs_start:
 * start NFS service
 */
void nfs_start(nfs_start_info_t * p_start_info);

int nfs_get_fsalpathlib_conf(char *configPath,  path_str_t * PathLib, unsigned int *plen) ;

#endif
