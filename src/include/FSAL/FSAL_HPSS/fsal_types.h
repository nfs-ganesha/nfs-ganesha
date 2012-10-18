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
 * \file    fsal_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 * \brief   File System Abstraction Layer types and constants.
 *
 *
 *
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

/*
 * FS relative includes
 */
#include <hpss_version.h>

#if HPSS_MAJOR_VERSION == 5

#include <u_signed64.h>         /* for cast64 function */
#include <hpss_api.h>

#elif (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)

#include <u_signed64.h>         /* for cast64 function */
#include <hpss_api.h>
#include <acct_hpss.h>

#endif

/*
 * labels in the config file
 */

# define CONF_LABEL_FS_SPECIFIC   "HPSS"

/* -------------------------------------------
 *      HPSS dependant definitions
 * ------------------------------------------- */

#include "fsal_glue_const.h"

#define fsal_handle_t hpssfsal_handle_t
#define fsal_op_context_t hpssfsal_op_context_t
#define fsal_file_t hpssfsal_file_t
#define fsal_dir_t hpssfsal_dir_t
#define fsal_export_context_t hpssfsal_export_context_t
#define fsal_cookie_t hpssfsal_cookie_t
#define fs_specific_initinfo_t hpssfs_specific_initinfo_t

/* Filesystem handle */

typedef union {
 struct 
  {

    /* The object type */
    fsal_nodetype_t obj_type;

    /* The hpss handle */
    ns_ObjHandle_t ns_handle;
  } data ;
  char pad[FSAL_HANDLE_T_SIZE];
} hpssfsal_handle_t;

/** FSAL security context */

#if HPSS_MAJOR_VERSION == 5

typedef struct
{

  time_t last_update;
  hsec_UserCred_t hpss_usercred;

} hpssfsal_cred_t;

#elif (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)

typedef struct
{

  time_t last_update;
  sec_cred_t hpss_usercred;

} hpssfsal_cred_t;

#endif

typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */
  exportlist_t        * fe_export;

  ns_ObjHandle_t fileset_root_handle;
  unsigned int default_cos;

} hpssfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(pexport_context->default_cos)

typedef struct
{
  hpssfsal_export_context_t *export_context;    /* Must be the first entry in this structure */
  hpssfsal_cred_t credential;
  msectimer_t latency;
  unsigned int count;
} hpssfsal_op_context_t;

#if (HPSS_MAJOR_VERSION == 5)
#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.hpss_usercred.SecPwent.Uid )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.hpss_usercred.SecPwent.Gid )
#elif (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)
#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.hpss_usercred.Uid )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.hpss_usercred.Gid )
#endif

/** directory stream descriptor */
typedef struct
{
  hpssfsal_op_context_t context;        /* credential for readdir operations */
  hpssfsal_handle_t dir_handle; /* directory handle */
  int reserved;                 /* not used */
} hpssfsal_dir_t;

/** FSAL file descriptor */

#if (HPSS_MAJOR_VERSION == 5)

typedef struct
{
  int filedes;                  /* file descriptor. */
  gss_token_t fileauthz;        /* data access credential. */
} hpssfsal_file_t;

#elif (HPSS_MAJOR_VERSION == 6)

typedef struct
{
  int filedes;                  /* file descriptor. */
  hpss_authz_token_t fileauthz; /* data access credential. */
} hpssfsal_file_t;

#elif (HPSS_MAJOR_VERSION == 7)

typedef struct
{
  int filedes;                  /* file descriptor. */
} hpssfsal_file_t;

#endif

//#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->filedes )

/** HPSS specific init info */

#if (HPSS_MAJOR_VERSION == 5)

typedef struct
{

  /* specifies the behavior for each init value : */
  struct behaviors
  {
    fsal_initflag_t
        /* client API configuration */
    PrincipalName, KeytabPath,
        /* Other specific configuration */
    CredentialLifetime, ReturnInconsistentDirent;

  } behaviors;

  /* client API configuration info : */
  api_config_t hpss_config;

  /* other configuration info */
  fsal_uint_t CredentialLifetime;
  fsal_uint_t ReturnInconsistentDirent;

} hpssfs_specific_initinfo_t;

#elif (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)

typedef struct
{

  /* specifies the behavior for each init value : */
  struct behaviors
  {
    fsal_initflag_t
        /* client API configuration */
    AuthnMech, NumRetries, BusyDelay, BusyRetries, MaxConnections, DebugPath,
        /* Other specific configuration */
    Principal, KeytabPath, CredentialLifetime, ReturnInconsistentDirent;

  } behaviors;

  /* client API configuration info : */
  api_config_t hpss_config;

  /* other configuration info */
  char Principal[FSAL_MAX_NAME_LEN+1];
  char KeytabPath[FSAL_MAX_PATH_LEN];

  fsal_uint_t CredentialLifetime;
  fsal_uint_t ReturnInconsistentDirent;

} hpssfs_specific_initinfo_t;

#endif

/** directory cookie : OffsetOut parameter of hpss_ReadRawAttrsHandle. */
typedef union {
  u_signed64 data ;
  char pad[FSAL_COOKIE_T_SIZE];
} hpssfsal_cookie_t;

//#define FSAL_READDIR_FROM_BEGINNING  (cast64(0))

#if HPSS_LEVEL >= 730
#define HAVE_XATTR_CREATE 1
#endif


#endif                          /* _FSAL_TYPES_SPECIFIC_H */
