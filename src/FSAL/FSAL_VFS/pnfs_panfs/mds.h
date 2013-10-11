/*
 * Copyright © from 2012 Panasas Inc.
 * Author: Boaz Harrosh <bharrosh@panasas.com>
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
 * License along with this library; if not, email to the Free Software
 * Foundation, Inc., licensing@fsf.org
 *
 * -------------
 */

/**
 * @file   mds.h
 * @author Boaz Harrosh <bharrosh@panasas.com>
 *
 * @brief Declare mds.c externals
 *
 * This file is edited with the LINUX coding style: (Will be enforced)
 *	- Tab characters of 8 spaces wide
 *	- Lines not longer then 80 chars
 *	- etc ... (See linux Documentation/CodingStyle.txt)
 */

#include "fsal.h"

/*============================== initialization ==============================*/
/* Need to call this to initialize export_ops for pnfs */
void export_ops_pnfs(struct export_ops *ops);
/* Need to call this to initialize obj_ops for pnfs */
void handle_ops_pnfs(struct fsal_obj_ops *ops);

/* Start the up calls thread for LAYOUT RECALLS*/
int pnfs_panfs_init(int root_fd, void **pnfs_data /*OUT*/);
/* Stop and clean the up calls thread*/
void pnfs_panfs_fini(void *pnfs_data);
