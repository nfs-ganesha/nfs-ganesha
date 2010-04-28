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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#ifdef _USE_POSIX
#define NAME_MAX  255
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#endif                          /* _USE_POSIX */
#endif                          /* _SOLARIS */

/*
 * FS relative includes
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

/*
 * labels in the config file
 */

#define CONF_LABEL_FS_SPECIFIC   "POSIX"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define FSAL_MAX_NAME_LEN   NAME_MAX
#define FSAL_MAX_PATH_LEN   PATH_MAX

#define FSAL_NGROUPS_MAX  32

/* prefered readdir size */
//#define FSAL_READDIR_SIZE 2048
#define FSAL_READDIR_SIZE 4096

/** object POSIX infos */
typedef struct
{
  dev_t devid;
  ino_t inode;
  int nlink;
  time_t ctime;
  fsal_nodetype_t ftype;
} fsal_posixdb_fileinfo_t;

/** object name.  */

typedef struct fsal_name__
{
  char name[FSAL_MAX_NAME_LEN];
  unsigned int len;
} fsal_name_t;

/** object path.  */

typedef struct fsal_path__
{
  char path[FSAL_MAX_PATH_LEN];
  unsigned int len;
} fsal_path_t;

#define FSAL_NAME_INITIALIZER {"",0}
#define FSAL_PATH_INITIALIZER {"",0}

static fsal_name_t __attribute__ ((__unused__)) FSAL_DOT =
{
".", 1};

static fsal_name_t __attribute__ ((__unused__)) FSAL_DOT_DOT =
{
"..", 2};

typedef struct
{
  fsal_u64_t id;
  int ts;                       /* timestamp */
  fsal_posixdb_fileinfo_t info; /* info from the database, related to the object on the FS */
} fsal_handle_t;  /**< FS object handle.            */

/** Authentification context.    */

typedef struct fsal_cred__
{
  uid_t user;
  gid_t group;
  fsal_count_t nbgroups;
  gid_t alt_groups[FSAL_NGROUPS_MAX];
} fsal_cred_t;

/** fs specific init info */
#include "posixdb.h"

typedef void *fsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(*pexport_context)

typedef struct
{
  fsal_cred_t credential;
  fsal_export_context_t *export_context;
  fsal_posixdb_conn *p_conn;
} fsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct fs_specific_initinfo__
{
  fsal_posixdb_conn_params_t dbparams;
} fs_specific_initinfo_t;

/**< directory cookie */
typedef struct fsal_cookie__
{
  off_t cookie;
} fsal_cookie_t;

static fsal_cookie_t __attribute__ ((__unused__)) FSAL_READDIR_FROM_BEGINNING =
{
0};

typedef struct fsal_lockdesc__
{
  struct flock flock;
} fsal_lockdesc_t;

/* Directory stream descriptor. */

typedef struct fsal_dir__
{
  DIR *p_dir;
  fsal_op_context_t context;    /* credential for accessing the directory */
  fsal_path_t path;
  fsal_handle_t handle;
#ifdef _USE_POSIXDB_READDIR_BLOCK
  fsal_posixdb_child *p_dbentries;
  int dbentries_count;        /**< if -1 then do not try to fill p_dbentries */
#endif
} fsal_dir_t;

#ifdef _FSAL_POSIX_USE_STREAM
typedef struct fsal_file__
{
  FILE *p_file;
  int ro;                       /* read only file ? */
} fsal_file_t;

#define FSAL_FILENO( p_fsal_file )  ( fileno( (p_fsal_file)->p_file ) )

#else
typedef struct fsal_file__
{
  int filefd;
  int ro;                       /* read only file ? */
} fsal_file_t;

#define FSAL_FILENO(p_fsal_file)  ((p_fsal_file)->filefd )

#endif                          /* _FSAL_POSIX_USE_STREAM */

#endif                          /* _FSAL_TYPES__SPECIFIC_H */
