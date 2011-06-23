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
 * \file    nfs41_session.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.95 $
 * \brief   Management of NFSv4.1 sessions
 *
 * nfs41_session.h : Management of the NFSv4.1 sessions
 *
 *
 */

#ifndef _NFS41_SESSION_H
#define _NFS41_SESSION_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>


#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#ifdef _USE_MFSL
#include "mfsl.h"
#endif
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#define NFS41_SESSION_PER_CLIENT 3
#define NFS41_NB_SLOTS           3
#define NFS41_DRC_SIZE          32768

typedef struct nfs41_session_slot__
{
  sequenceid4 sequence;
  pthread_mutex_t lock;
  char cached_result[NFS41_DRC_SIZE];
  unsigned int cache_used;
} nfs41_session_slot_t;

typedef struct nfs41_session__
{
  clientid4 clientid;
  uint32_t sequence;
  uint32_t session_flags;
  char session_id[NFS4_SESSIONID_SIZE];
  channel_attrs4 fore_channel_attrs;
  channel_attrs4 back_channel_attrs;
  nfs41_session_slot_t slots[NFS41_NB_SLOTS];
} nfs41_session_t;

#endif                          /* _NFS41_SESSION_H */
