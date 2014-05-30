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
 * -------------
 */

/**
 *
 * \file    fsal_internal.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.24 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include  "fsal.h"
#include "fsal_internal.h"
#include "SemN.h"
#include "fsal_convert.h"
#include <libgen.h>             /* used for 'dirname' */
#include "abstract_mem.h"
#include "FSAL/access_check.h"

#include <pthread.h>
#include <string.h>
#include <sys/fsuid.h>

#include "gpfs.h"

#ifdef _USE_NFS4_ACL
#define ACL_DEBUG_BUF_SIZE 256
#endif                          /* _USE_NFS4_ACL */

/* credential lifetime (1h) */
fsal_uint_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;

/* filesystem info for HPSS */
static fsal_staticfsinfo_t default_gpfs_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  _POSIX_LINK_MAX,              /* max links */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* max pathlen */
  TRUE,                         /* no_trunc */
  FALSE,                         /* chown restricted */
  FALSE,                        /* case insensitivity */
  TRUE,                         /* case preserving */
  FSAL_EXPTYPE_PERSISTENT,      /* FH expire type */
  TRUE,                         /* hard link support */
  TRUE,                         /* symlink support */
  TRUE,                         /* lock management */
  TRUE,                         /* lock owners */
  TRUE,                         /* async blocking locks */
  TRUE,                         /* named attributes */
  TRUE,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  TRUE,                         /* can change times */
  TRUE,                         /* homogenous */
  GPFS_SUPPORTED_ATTRIBUTES,    /* supported attributes */
  1048576,                      /* maxread size DONT USE 0 */
  1048576,                      /* maxwrite size DONT USE 0 */
  0,                            /* default umask */
  0,                            /* cross junctions */
  0400,                         /* default access rights for xattrs: root=RW, owner=R */
  1,                            /* default share reservation support in FSAL */
  0                             /* default share reservation support with open owners in FSAL */
};

/* variables for limiting the calls to the filesystem */
static int limit_calls = FALSE;
semaphore_t sem_fs_calls;

/* threads keys for stats */
static pthread_key_t key_stats;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

static void free_pthread_specific_stats(void *buff)
{
  gsh_free(buff);
}

/* init keys */
static void init_keys(void)
{
  if(pthread_key_create(&key_stats, free_pthread_specific_stats) == -1)
    LogMajor(COMPONENT_FSAL,
             "Could not create thread specific stats (pthread_key_create) err %d (%s)",
             errno, strerror(errno));

  return;
}                               /* init_keys */

/**
 * fsal_increment_nbcall:
 * Updates fonction call statistics.
 *
 * \param function_index (input):
 *        Index of the function whom number of call is to be incremented.
 * \param status (input):
 *        Status the function returned.
 *
 * \return Nothing.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* verify index */

  if(function_index >= FSAL_NB_FUNC)
    return;

  /* first, we init the keys if this is the first time */

  if(pthread_once(&once_key, init_keys) != 0)
    {
      LogMajor(COMPONENT_FSAL,
               "Could not create thread specific stats (pthread_once) err %d (%s)",
               errno, strerror(errno));
      return;
    }

  /* we get the specific value */

  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */

  if(bythread_stat == NULL)
    {
      int i;

      bythread_stat = gsh_malloc(sizeof(fsal_statistics_t));

      if(bythread_stat == NULL)
        {
          LogCrit(COMPONENT_FSAL,
                  "Could not allocate memory for FSAL statistics err %d (%s)",
                  ENOMEM, strerror(ENOMEM));
          /* we don't have real memory, bail */
          return;
        }

      /* inits the struct */

      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  /* we increment the values */

  if(bythread_stat)
    {
      bythread_stat->func_stats.nb_call[function_index]++;

      if(!FSAL_IS_ERROR(status))
        bythread_stat->func_stats.nb_success[function_index]++;
      else if(fsal_is_retryable(status))
        bythread_stat->func_stats.nb_err_retryable[function_index]++;
      else
        bythread_stat->func_stats.nb_err_unrecover[function_index]++;
    }

  return;
}

/**
 * fsal_internal_getstats:
 * (For internal use in the FSAL).
 * Retrieve call statistics for current thread.
 *
 * \param output_stats (output):
 *        Pointer to the call statistics structure.
 *
 * \return Nothing.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      LogMajor(COMPONENT_FSAL,
               "Could not create thread specific stats (pthread_once) err %d (%s)",
               errno, strerror(errno));
      return;
    }

  /* we get the specific value */
  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */
  if(bythread_stat == NULL)
    {
      int i;

      if((bythread_stat =
          gsh_malloc(sizeof(fsal_statistics_t))) == NULL)
      {
        /* we don't have working memory, bail */
        LogCrit(COMPONENT_FSAL,
                "Could not allocate memory for FSAL statistics");
        return;
      }

      /* inits the struct */
      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  if(output_stats)
    (*output_stats) = (*bythread_stat);

  return;

}

/**
 * Set credential lifetime.
 * (For internal use in the FSAL).
 * Set the period for thread's credential renewal.
 *
 * \param lifetime_in (input):
 *        The period for thread's credential renewal.
 *
 * \return Nothing.
 */
void fsal_internal_SetCredentialLifetime(fsal_uint_t lifetime_in)
{
  CredentialLifetime = lifetime_in;
}

/**
 *  Used to limit the number of simultaneous calls to Filesystem.
 */
void TakeTokenFSCall()
{
  /* no limits */
  if(limit_calls == FALSE)
    return;

  /* there is a limit */
  semaphore_P(&sem_fs_calls);

}

void ReleaseTokenFSCall()
{
  /* no limits */
  if(limit_calls == FALSE)
    return;

  /* there is a limit */
  semaphore_V(&sem_fs_calls);

}

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        fs_specific_initinfo_t * fs_specific_info)
{

  /* sanity check */
  if(!fsal_info || !fs_common_info || !fs_specific_info)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* inits FS call semaphore */
  if(fsal_info->max_fs_calls > 0)
    {
      int rc;

      limit_calls = TRUE;

      rc = semaphore_init(&sem_fs_calls, fsal_info->max_fs_calls);

      if(rc != 0)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);

      LogDebug(COMPONENT_FSAL,
               "FSAL INIT: Max simultaneous calls to filesystem is limited to %u.",
               fsal_info->max_fs_calls);
    }
  else
    {
      LogDebug(COMPONENT_FSAL,
               "FSAL INIT: Max simultaneous calls to filesystem is unlimited.");
    }

  /* setting default values. */
  global_fs_info = default_gpfs_info;

  if(isFullDebug(COMPONENT_FSAL))
    {
      display_fsinfo(&default_gpfs_info);
    }

  /* Analyzing fs_common_info struct */

  if((fs_common_info->behaviors.maxfilesize != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxlink != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxnamelen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxpathlen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.no_trunc != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_insensitive != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_preserving != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.named_attr != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.lease_time != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.supported_attrs != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.homogenous != FSAL_INIT_FS_DEFAULT))
    ReturnCode(ERR_FSAL_NOTSUPP, 0);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, symlink_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, link_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_owner);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_async_block);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, share_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, share_support_owner);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes constant = 0x%llX.",
               GPFS_SUPPORTED_ATTRIBUTES);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes default = 0x%llX.",
               default_gpfs_info.supported_attrs);

  LogFullDebug(COMPONENT_FSAL,
               "FSAL INIT: Supported attributes mask = 0x%llX.",
               global_fs_info.supported_attrs);

  fsal_save_ganesha_credentials();

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/*********************************************************************
 *
 *  GPFS FSAL char device driver interaces
 *
 ********************************************************************/

/**
 * fsal_internal_handle2fd:
 * Open a file by handle within an export.
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param phandle (input):
 *        Opaque filehandle
 * \param pfd (output):
 *        File descriptor openned by the function
 * \param oflags (input):
 *        Flags to open the file with
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t * phandle, int *pfd, int oflags)
{
  int dirfd = 0;
  fsal_status_t status;

  if(!phandle || !pfd || !p_context || !p_context->export_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  status = fsal_internal_handle2fd_at(dirfd, phandle, pfd, oflags);

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_open);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_handle2fd_at:
 * Open a file by handle from in an open directory
 *
 * \param dirfd (input):
 *        Open file descriptor of parent directory
 * \param phandle (input):
 *        Opaque filehandle
 * \param pfd (output):
 *        File descriptor openned by the function
 * \param oflags (input):
 *        Flags to open the file with
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_handle2fd_at(int dirfd,
                                         fsal_handle_t * phandle, int *pfd, int oflags)
{
  int rc = 0;
  struct open_arg oarg;

  if(!phandle || !pfd)
    ReturnCode(ERR_FSAL_FAULT, 0);

  oarg.mountdirfd = dirfd;
  oarg.handle = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)phandle)->data.handle;
  oarg.flags = oflags;

  rc = gpfs_ganesha(OPENHANDLE_OPEN_BY_HANDLE, &oarg);

  LogFullDebug(COMPONENT_FSAL, "OPENHANDLE_OPEN_BY_HANDLE returned: rc %d", rc);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  *pfd = rc;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_get_handle:
 * Create a handle from a file path
 *
 * \param pcontext (input):
 *        A context pointer for the root of the current export
 * \param p_fsalpath (input):
 *        Full path to the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_get_handle(fsal_op_context_t * p_context,   /* IN */
                                       fsal_path_t * p_fsalpath,        /* IN */
                                       fsal_handle_t * p_handle /* OUT */ )
{
  int rc;
  gpfsfsal_handle_t *p_gpfs_handle = (gpfsfsal_handle_t *)p_handle;
  struct name_handle_arg harg;

  if(!p_context || !p_handle || !p_fsalpath)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
  memset(p_gpfs_handle, 0, sizeof(*p_gpfs_handle));
#endif

  harg.handle = (struct gpfs_file_handle *) &p_gpfs_handle->data.handle;
  harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.handle->handle_version = OPENHANDLE_VERSION;
  harg.name = p_fsalpath->path;
  harg.dfd = AT_FDCWD;
  harg.flag = 0;

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle for %s",
               p_fsalpath->path);

  rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_get_handle_at:
 * Create a handle from a directory pointer and filename
 *
 * \param dfd (input):
 *        Open directory handle
 * \param p_fsalname (input):
 *        Name of the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_get_handle_at(int dfd,      /* IN */
                                          fsal_name_t * p_fsalname,    /* IN */
                                          fsal_handle_t * p_handle,    /* OUT */
                                          fsal_op_context_t * p_context /* IN */ )
{
  int rc;
  gpfsfsal_handle_t *p_gpfs_handle = (gpfsfsal_handle_t *)p_handle;
  struct name_handle_arg harg;

  if(!p_handle || !p_fsalname)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
  memset(p_gpfs_handle, 0, sizeof(*p_gpfs_handle));
#endif

  harg.handle = (struct gpfs_file_handle *) &p_gpfs_handle->data.handle;
  harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.handle->handle_version = OPENHANDLE_VERSION;
  harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.name = p_fsalname->name;
  harg.dfd = dfd;
  harg.flag = 0;

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle at for %s",
               p_fsalname->name);
  rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_get_handle:
 * Create a handle from a directory handle and filename
 *
 * \param pcontext (input):
 *        A context pointer for the root of the current export
 * \param p_dir_handle (input):
 *        The handle for the parent directory
 * \param p_fsalname (input):
 *        Name of the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
 fsal_status_t fsal_internal_get_fh(fsal_op_context_t * p_context, /* IN  */
                                    fsal_handle_t * p_dir_fh,      /* IN  */
                                    fsal_name_t * p_fsalname,      /* IN  */
                                    fsal_handle_t * p_out_fh)      /* OUT */
{
  int dirfd, rc;
  struct get_handle_arg harg;
  gpfsfsal_handle_t *p_gpfs_dir_fh = (gpfsfsal_handle_t *)p_dir_fh;
  gpfsfsal_handle_t *p_gpfs_out_fh = (gpfsfsal_handle_t *)p_out_fh;

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  if(!p_out_fh || !p_dir_fh || !p_fsalname)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
  memset(p_gpfs_out_fh, 0, sizeof(*p_gpfs_out_fh));
#endif

  harg.mountdirfd = dirfd;
  harg.dir_fh = (struct gpfs_file_handle *) &p_gpfs_dir_fh->data.handle;
  harg.out_fh = (struct gpfs_file_handle *) &p_gpfs_out_fh->data.handle;
  harg.out_fh->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.out_fh->handle_version = OPENHANDLE_VERSION;
  harg.out_fh->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.len = p_fsalname->len;
  harg.name = p_fsalname->name;

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle for %s",
               p_fsalname->name);

  rc = gpfs_ganesha(OPENHANDLE_GET_HANDLE, &harg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_fd2handle:
 * convert an fd to a handle
 *
 * \param fd (input):
 *        Open file descriptor for target file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_fd2handle(int fd, fsal_handle_t * handle)
{
  int rc;
  struct name_handle_arg harg;
  gpfsfsal_handle_t * p_handle = (gpfsfsal_handle_t *)handle;

  if(!p_handle || !&p_handle->data.handle)
     ReturnCode(ERR_FSAL_FAULT, 0);

  LogEvent(COMPONENT_FSAL,
               "fsal_internal_fd2handle called.");
/* Function not called If it ever is will need context changes. */
  ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
  memset(p_handle, 0, sizeof(*p_handle));
#endif

  harg.handle = (struct gpfs_file_handle *) &p_handle->data.handle;
  harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.handle->handle_version = OPENHANDLE_VERSION;
  harg.name = NULL;
  harg.dfd = fd;
  harg.flag = 0;

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle by fd for %d",
               fd);

  rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_link_fh:
 * Create a link based on a file fh, dir fh, and new name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_target_handle (input):
 *          file handle of target file
 * \param p_dir_handle (input):
 *          file handle of source directory
 * \param name (input):
 *          name for the new file
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_link_fh(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_target_handle,
                                    fsal_handle_t * p_dir_handle,
                                    fsal_name_t * p_link_name)
{
  int rc;
  int dirfd = 0;
  struct link_fh_arg linkarg;

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  if(!p_link_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  linkarg.mountdirfd = dirfd;
  linkarg.len = p_link_name->len;
  linkarg.name = p_link_name->name;
  linkarg.dir_fh = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_dir_handle)->data.handle;
  linkarg.dst_fh = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_target_handle)->data.handle;

  rc = gpfs_ganesha(OPENHANDLE_LINK_BY_FH, &linkarg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }


  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_internal_stat_name:
 * Stat a file by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to stat
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_stat_name(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_dir_handle,
                                    fsal_name_t * p_stat_name,
                                    struct stat *buf)
{
  int rc;
  int dirfd = 0;
  struct stat_name_arg statarg;

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

#ifdef _VALGRIND_MEMCHECK
  memset(buf, 0, sizeof(*buf));
#endif

  if(!p_stat_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  statarg.mountdirfd = dirfd;
  statarg.len = p_stat_name->len;
  statarg.name = p_stat_name->name;
  statarg.handle = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_dir_handle)->data.handle;
  statarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_STAT_BY_NAME, &statarg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_internal_unlink:
 * Unlink a file/directory by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to unlink
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_unlink(fsal_op_context_t * p_context,
                                   fsal_handle_t * p_dir_handle,
                                   fsal_name_t * p_stat_name,
                                   struct stat *buf)
{
  int rc;
  int dirfd = 0;
  struct stat_name_arg statarg;

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  if(!p_stat_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  statarg.mountdirfd = dirfd;
  statarg.len = p_stat_name->len;
  statarg.name = p_stat_name->name;
  statarg.handle = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_dir_handle)->data.handle;
  statarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_UNLINK_BY_NAME, &statarg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_internal_create:
 * Create a file/directory by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to create
 * \param mode & dev (input):
 *          file type and dev for mknode
 * \param fh & stat (outut):
 *          file handle of new file and attributes
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_create(fsal_op_context_t * p_context,
                                   fsal_handle_t * p_dir_handle,
                                   fsal_name_t * p_stat_name,
                                   mode_t mode, dev_t dev,
                                   fsal_handle_t * p_new_handle,
                                   struct stat *buf)
{
  int rc;
  int dirfd = 0;
  struct create_name_arg crarg;
#ifdef _VALGRIND_MEMCHECK
  gpfsfsal_handle_t * p_handle = (gpfsfsal_handle_t *)p_new_handle;
#endif

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  if(!p_stat_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
  memset(p_handle, 0, sizeof(*p_handle));
#endif

  crarg.mountdirfd = dirfd;
  crarg.mode = mode;
  crarg.dev = dev;
  crarg.len = p_stat_name->len;
  crarg.name = p_stat_name->name;
  crarg.dir_fh = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_dir_handle)->data.handle;
  crarg.new_fh = (struct gpfs_file_handle *) &p_new_handle->data.handle;
  crarg.new_fh->handle_size = OPENHANDLE_HANDLE_LEN;
  crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
  crarg.new_fh->handle_version = OPENHANDLE_VERSION;
  crarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_CREATE_BY_NAME, &crarg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_rename_fh:
 * Rename old file name to new name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_old_handle (input):
 *          file handle of old file
 * \param p_new_handle (input):
 *          file handle of new directory
 * \param name (input):
 *          name for the old file
 * \param name (input):
 *          name for the new file
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_rename_fh(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_old_handle,
                                    fsal_handle_t * p_new_handle,
                                    fsal_name_t * p_old_name,
                                    fsal_name_t * p_new_name)
{
  int rc;
  int dirfd = 0;
  struct rename_fh_arg renamearg;

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  if(!p_old_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(!p_new_name->name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  renamearg.mountdirfd = dirfd;
  renamearg.old_len = p_old_name->len;
  renamearg.old_name = p_old_name->name;
  renamearg.new_len = p_new_name->len;
  renamearg.new_name = p_new_name->name;
  renamearg.old_fh = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_old_handle)->data.handle;
  renamearg.new_fh = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_new_handle)->data.handle;

  rc = gpfs_ganesha(OPENHANDLE_RENAME_BY_FH, &renamearg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errno), errno);
  }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_readlink_by_handle:
 * Reads the contents of the link
 *
 *
 * \return status of operation
 */

fsal_status_t fsal_readlink_by_handle(fsal_op_context_t * p_context,
                                      fsal_handle_t * p_handle, char *__buf, int maxlen)
{
  int rc;
  int dirfd = 0;
  struct readlink_fh_arg readlinkarg;
  gpfsfsal_handle_t *p_gpfs_fh = (gpfsfsal_handle_t *)p_handle;

#ifdef _VALGRIND_MEMCHECK
  memset(__buf, 0, maxlen);
#endif

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  readlinkarg.mountdirfd = dirfd;
  readlinkarg.handle = (struct gpfs_file_handle *) &p_gpfs_fh->data.handle;
  readlinkarg.buffer = __buf;
  readlinkarg.size = maxlen;

  rc = gpfs_ganesha(OPENHANDLE_READLINK_BY_FH, &readlinkarg);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    Return(rc, 0, INDEX_FSAL_readlink);
  }


  if(rc < maxlen)
    __buf[rc] = '\0';

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_stat_by_handle:
 * get the stat value
 *
 *
 * \return status of operation
 */

/* Get NFS4 ACL as well as stat. For now, get stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_get_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t * p_handle, gpfsfsal_xstat_t *p_buffxstat)
{
  int rc;
  int errsv;
  int dirfd = 0;
  struct xstat_arg xstatarg;
#ifdef _USE_NFS4_ACL
  gpfs_acl_t *pacl_gpfs;
#endif                          /* _USE_NFS4_ACL */

  if(!p_handle || !p_context || !p_context->export_context || !p_buffxstat)
      ReturnCode(ERR_FSAL_FAULT, 0);

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

#ifdef _VALGRIND_MEMCHECK
  memset(p_buffxstat, 0, sizeof(*p_buffxstat));
#endif

#ifdef _USE_NFS4_ACL
  /* Initialize acl header so that GPFS knows what we want. */
  pacl_gpfs = (gpfs_acl_t *) p_buffxstat->buffacl;
  pacl_gpfs->acl_level = 0;
  pacl_gpfs->acl_version = GPFS_ACL_VERSION_NFS4;
  pacl_gpfs->acl_type = GPFS_ACL_TYPE_NFS4;
  pacl_gpfs->acl_len = GPFS_ACL_BUF_SIZE;
#endif                          /* _USE_NFS4_ACL */

#ifdef _USE_NFS4_ACL
  xstatarg.attr_valid = XATTR_STAT | XATTR_ACL;
#else
xstatarg.attr_valid = XATTR_STAT;
#endif
  xstatarg.mountdirfd = dirfd;
  xstatarg.handle = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_handle)->data.handle;
#ifdef _USE_NFS4_ACL
  xstatarg.acl = pacl_gpfs;
#else
  xstatarg.acl = NULL;
#endif
  xstatarg.attr_changed = 0;
  xstatarg.buf = &p_buffxstat->buffstat;

  rc = gpfs_ganesha(OPENHANDLE_GET_XSTAT, &xstatarg);
  errsv = errno;
  LogDebug(COMPONENT_FSAL, "gpfs_ganesha: GET_XSTAT returned, rc = %d", rc);

  if(rc < 0)
    {
      if (errsv == EUNATCH)
        LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");

      if(errsv == ENODATA)
        {
          /* For the special file that do not have ACL, GPFS returns ENODATA.
           * In this case, return okay with stat. */
          p_buffxstat->attr_valid = XATTR_STAT;
          LogFullDebug(COMPONENT_FSAL, "retrieved only stat, not acl");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
      else
        {
          /* Handle other errors. */
          LogFullDebug(COMPONENT_FSAL, "fsal_get_xstat_by_handle returned errno:%d -- %s",
                       errsv, strerror(errsv));
          ReturnCode(posix2fsal_error(errsv), errsv);
        }
    }

#ifdef _USE_NFS4_ACL
  p_buffxstat->attr_valid = XATTR_STAT | XATTR_ACL;
#else
  p_buffxstat->attr_valid = XATTR_STAT;
#endif

  ReturnCode(ERR_FSAL_NO_ERROR, 0);  
}

/* Set NFS4 ACL as well as stat. For now, set stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_set_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t * p_handle, int attr_valid,
                                       int attr_changed, gpfsfsal_xstat_t *p_buffxstat)
{
  int rc, errsv;
  int dirfd = 0;
  struct xstat_arg xstatarg;

  if(!p_handle || !p_context || !p_context->export_context || !p_buffxstat)
      ReturnCode(ERR_FSAL_FAULT, 0);

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  xstatarg.attr_valid = attr_valid;
  xstatarg.mountdirfd = dirfd;
  xstatarg.handle = (struct gpfs_file_handle *) &((gpfsfsal_handle_t *)p_handle)->data.handle;
  xstatarg.acl = (gpfs_acl_t *) p_buffxstat->buffacl;
  xstatarg.attr_changed = attr_changed;
  xstatarg.buf = &p_buffxstat->buffstat;

  /* We explicitly do NOT do setfsuid/setfsgid here because truncate, even to
   * enlarge a file, doesn't actually allocate blocks. GPFS implements sparse
   * files, so blocks of all 0 will not actually be allocated.
   */
  rc = gpfs_ganesha(OPENHANDLE_SET_XSTAT, &xstatarg);
  errsv = errno;

  LogDebug(COMPONENT_FSAL, "gpfs_ganesha: SET_XSTAT returned, rc = %d", rc);

  if(rc < 0)
  {
    if (errno == EUNATCH)
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
    ReturnCode(posix2fsal_error(errsv), errsv);
  }


  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* trucate by handle */
fsal_status_t fsal_trucate_by_handle(fsal_op_context_t * p_context,
                                     fsal_handle_t * p_handle,
                                     u_int64_t size)
{
  int attr_valid;
  int attr_changed;
  gpfsfsal_xstat_t buffxstat;

  if(!p_handle || !p_context || !p_context->export_context)
      ReturnCode(ERR_FSAL_FAULT, 0);

  attr_valid = XATTR_STAT;
  attr_changed = XATTR_SIZE;
  buffxstat.buffstat.st_size = size;

  return fsal_set_xstat_by_handle(p_context, p_handle, attr_valid,
                                 attr_changed, &buffxstat);
}

/**
 *  fsal_error_is_event:
 *  Indicates if an FSAL error should be posted as an event
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 *            
 */            
fsal_boolean_t fsal_error_is_event(fsal_status_t status)
{

  switch (status.major)
    {

    case ERR_FSAL_IO:
      return TRUE;

    default:
      return FALSE;
    }
}

/**
 *  fsal_error_is_info:
 *  Indicates if an FSAL error should be posted as an INFO level debug msg.
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 */
fsal_boolean_t fsal_error_is_info(fsal_status_t status)
{
  switch (status.major)
    {
#if 0
    /* Log infrastructure redesign can enable this error logging under
       different category; Disabling for Producition level code. */
    case ERR_FSAL_PERM:
    case ERR_FSAL_NOT_OPENED:
    case ERR_FSAL_ACCESS:
    case ERR_FSAL_FILE_OPEN:
    case ERR_FSAL_DELAY:
    case ERR_FSAL_NOTEMPTY:
    case ERR_FSAL_DQUOT:
    case ERR_FSAL_NOSPC:
    case ERR_FSAL_EXIST:
    case ERR_FSAL_NAMETOOLONG:
    case ERR_FSAL_STALE:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_NOTDIR:
#endif

    case ERR_FSAL_NOMEM:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_INVAL:
    case ERR_FSAL_FBIG:
    case ERR_FSAL_MLINK:
    case ERR_FSAL_NOTSUPP:
    case ERR_FSAL_OVERFLOW:
    case ERR_FSAL_DEADLOCK:
    case ERR_FSAL_INTERRUPT:
    case ERR_FSAL_SERVERFAULT:
      return TRUE;

    default:
      return FALSE;
    }
}
/** 
*  fsal_internal_version; 
* 
* \return GPFS version 
*/ 
int fsal_internal_version() 
{ 
  int rc; 
 
  rc = gpfs_ganesha(OPENHANDLE_GET_VERSION, &rc); 
 
  if(rc < 0)
    {
      if (errno == EUNATCH)
        LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
      LogDebug(COMPONENT_FSAL, "GPFS get version failed with rc %d", rc);
    }
  else 
    LogDebug(COMPONENT_FSAL, "GPFS get version %d", rc);
 
  return rc; 
} 

/**
 * GPFSFSAL_start_grace:
 * Interface to manipulate GPFS' grace period
 *
 * grace_period:  -1 == Grace period ends immediately
 *                 0 == Use GPFS' Default (ie. 60 secs)
 *                 n == number of seconds
 */
fsal_status_t
GPFSFSAL_start_grace(fsal_op_context_t *p_context,      /* IN */
                                   int  grace_period)   /* IN */
{
        int                     rc = 0;
        struct grace_period_arg gpa;

        if (!p_context || grace_period < -1)
                Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_start_grace);

        grace_period /= 2;
        if(!grace_period)
          LogCrit(COMPONENT_FSAL, "Grace period %d too short, changing to 1", grace_period);
        
        gpa.mountdirfd =
            ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;
        gpa.grace_sec = grace_period ? grace_period : 1;

        LogFullDebug(COMPONENT_FSAL, "mountdirfd = %d, grace period = %d",
                gpa.mountdirfd, gpa.grace_sec);

        rc = gpfs_ganesha(OPENHANDLE_GRACE_PERIOD, &gpa);

        LogFullDebug(COMPONENT_FSAL,
                "OPENHANDLE_GRACE_PERIOD returned: rc = %d", rc);

        if (rc < 0)
        {
          if (errno == EUNATCH)
            LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
          ReturnCode(posix2fsal_error(errno), errno);
        }

        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
