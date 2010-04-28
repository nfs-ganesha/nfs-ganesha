/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

/**
 * \file    pnfs.h
 * \author  $Author: deniel $
 * \date    $Date: 2010/01/27 12:44:15 $
 * \brief   Management of the pNFS features.
 *
 * pnfs.h : Management of the pNFS features.
 *
 *
 */

#ifndef _PNFS_H
#define _PNFS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#ifdef _USE_MFSL
#include "mfsl.h"
#endif
#include "log_functions.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"

typedef union pnfs_parameter__
{
  pnfs_layoutfile_parameter_t layoutfile;
} pnfs_parameter_t;

typedef union pnfs_file__
{
  pnfs_ds_file_t ds_file;
} pnfs_file_t;

#endif                          /* _PNFS_H */
