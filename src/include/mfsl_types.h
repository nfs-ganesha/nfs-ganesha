/*
 *
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
 * \file    mfsl_types.h
 */

#ifndef _MFSL_TYPES_H
#define _MFSL_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_MFSL          "MFSL"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "fsal.h"
#include "err_fsal.h"
#include "err_mfsl.h"

#ifdef _USE_MFSL_NULL
#include "MFSL/MFSL_NULL/mfsl_types.h"
#endif

#ifdef _USE_MFSL_ASYNC
#include "MFSL/MFSL_ASYNC/mfsl_types.h"
#endif

#ifdef _USE_MFSL_AIO
#include "MFSL/MFSL_AIO/mfsl_types.h"
#endif

#ifdef _USE_MFSL_PROXY_RPCSECGSS
#include "MFSL/MFSL_PROXY_RPCSECGSS/mfsl_types.h"
#endif

#ifdef _USE_MFSL_PNFS_FILE
#include "MFSL/MFSL_PNFS_FILE/mfsl_types.h"
#endif

#endif                          /* _MFSL_TYPES_H */
