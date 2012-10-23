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
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.24 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#include "config.h"

#include <sys/ioctl.h>
#include  "fsal.h"
#include "fsal_internal.h"
#include "gpfs_methods.h"
#include "fsal_convert.h"
#include <libgen.h> /* used for 'dirname' */
#include "abstract_mem.h"

#include <pthread.h>
#include <string.h>
#include <sys/fsuid.h>

#include "include/gpfs.h"

#ifdef _USE_NFS4_ACL
#define ACL_DEBUG_BUF_SIZE 256
#endif                          /* _USE_NFS4_ACL */

/* credential lifetime (1h) */
uint32_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;


#ifdef _USE_NFS4_ACL
static fsal_status_t fsal_internal_testAccess_acl(const struct req_op_context * p_context,   /* IN */
                                                  fsal_aceperm_t v4mask,  /* IN */
                                                  struct attrlist * p_object_attributes   /* IN */ );

static fsal_status_t fsal_check_access_by_handle(int mntfd,              /* IN */
                                 const struct req_op_context * p_context, /* IN */
                                 struct gpfs_file_handle * p_handle,     /* IN */
                                 mode_t mode,                           /* IN */
                                 fsal_accessflags_t v4mask,             /* IN */
                                 struct attrlist * p_object_attributes); /* IN */

extern fsal_status_t fsal_cred_2_gpfs_cred(struct user_cred *p_fsalcred,
                                           struct xstat_cred_t *p_gpfscred);

extern fsal_status_t fsal_mode_2_gpfs_mode(mode_t fsal_mode,
                                           fsal_accessflags_t v4mask,
                                           unsigned int *p_gpfsmode,
                                           bool is_dir);
#endif                          /* _USE_NFS4_ACL */

static fsal_status_t fsal_internal_testAccess_no_acl(const struct req_op_context *p_context, /* IN */
                                              fsal_accessflags_t access_type,       /* IN */
                                              struct attrlist * p_object_attributes);/* IN */

#if 0 //???   not needed for now
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

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        fs_specific_initinfo_t * fs_specific_info)
{

  /* sanity check */
  if(!fsal_info || !fs_common_info || !fs_specific_info)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* inits FS call semaphore */
  if(fsal_info->max_fs_calls > 0)
    {
      int rc;

      limit_calls = true;

      rc = semaphore_init(&sem_fs_calls, fsal_info->max_fs_calls);

      if(rc != 0)
        return fsalstat(ERR_FSAL_SERVERFAULT, rc);

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
    return fsalstat(ERR_FSAL_NOTSUPP, 0);

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

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, accesscheck_support);
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

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif


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

fsal_status_t fsal_internal_handle2fd(int dirfd,
                                      struct gpfs_file_handle *phandle,
                                      int *pfd, int oflags)
{
  fsal_status_t status;

  if(!phandle || !pfd)
    return fsalstat(ERR_FSAL_FAULT, 0);

  status = fsal_internal_handle2fd_at(dirfd, phandle, pfd, oflags);

  if(FSAL_IS_ERROR(status))
    return(status);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_close:
 * Close by fd
 *
 * \param fd (input):
 *        Open file descriptor
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_close(int fd, void *owner, int cflags)
{
  int rc = 0;
  struct close_file_arg carg;

  carg.mountdirfd = fd;
  carg.close_fd = fd;
  carg.close_flags = cflags;
  carg.close_owner = owner;

  rc = gpfs_ganesha(OPENHANDLE_CLOSE_FILE, &carg);

  LogFullDebug(COMPONENT_FSAL, "OPENHANDLE_CLOSE_FILE returned: rc %d", rc);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
                        struct gpfs_file_handle *phandle, int *pfd, int oflags)
{
  int rc = 0;
  struct open_arg oarg;

  if(!phandle || !pfd)
    return fsalstat(ERR_FSAL_FAULT, 0);

  oarg.mountdirfd = dirfd;
  oarg.handle = phandle;
  oarg.flags = oflags;

  rc = gpfs_ganesha(OPENHANDLE_OPEN_BY_HANDLE, &oarg);

  LogFullDebug(COMPONENT_FSAL, "OPENHANDLE_OPEN_BY_HANDLE returned: rc %d", rc);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  *pfd = rc;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
fsal_status_t
fsal_internal_get_handle(const char              *p_fsalpath, /* IN */
                         struct gpfs_file_handle *p_handle)   /* OUT */
{
  int rc;
  struct name_handle_arg harg;

  if(!p_handle || !p_fsalpath)
    return fsalstat(ERR_FSAL_FAULT, 0);

  harg.handle = p_handle;
  harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.handle->handle_version = OPENHANDLE_VERSION;
  harg.name = p_fsalpath;
  harg.dfd = AT_FDCWD;
  harg.flag = 0;

  LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", p_fsalpath);

  rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

fsal_status_t fsal_internal_get_handle_at(int dfd, const char *p_fsalname, /* IN */
                                          struct gpfs_file_handle *p_handle)/* OUT */
{
  int rc;
  struct name_handle_arg harg;

  if(!p_handle)
    return fsalstat(ERR_FSAL_FAULT, 0);

  harg.handle = p_handle;
  harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.handle->handle_version = OPENHANDLE_VERSION;
  harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.name = p_fsalname;
  harg.dfd = dfd;
  harg.flag = 0;

  LogFullDebug(COMPONENT_FSAL, "Lookup handle at for %d", dfd);

  rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
 fsal_status_t fsal_internal_get_fh(int dirfd, /* IN  */
                                    struct gpfs_file_handle * p_dir_fh,      /* IN  */
                                    const char * p_fsalname,      /* IN  */
                                    struct gpfs_file_handle * p_out_fh)      /* OUT */
{
  int rc;
  struct get_handle_arg harg;

  if(!p_out_fh || !p_dir_fh || !p_fsalname)
    return fsalstat(ERR_FSAL_FAULT, 0);

  harg.mountdirfd = dirfd;
  harg.dir_fh = p_dir_fh;
  harg.out_fh = p_out_fh;
  harg.out_fh->handle_size = OPENHANDLE_HANDLE_LEN;
  harg.out_fh->handle_version = OPENHANDLE_VERSION;
  harg.out_fh->handle_key_size = OPENHANDLE_KEY_LEN;
  harg.len = strlen(p_fsalname);
  harg.name = p_fsalname;

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle for %s",
               p_fsalname);

  rc = gpfs_ganesha(OPENHANDLE_GET_HANDLE, &harg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
fsal_status_t fsal_internal_fd2handle(int fd, struct gpfs_file_handle * p_handle)
{
  int rc;
  struct name_handle_arg harg;

  if(!p_handle)
    return fsalstat(ERR_FSAL_FAULT, 0);

  harg.handle = p_handle;
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
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
fsal_status_t fsal_internal_link_fh(int dirfd,
                                    struct gpfs_file_handle * p_target_handle,
                                    struct gpfs_file_handle * p_dir_handle,
                                    const char * p_link_name)
{
  int rc;
  struct link_fh_arg linkarg;

  if(!p_link_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  linkarg.mountdirfd = dirfd;
  linkarg.len = strlen(p_link_name);
  linkarg.name = p_link_name;
  linkarg.dir_fh = p_dir_handle;
  linkarg.dst_fh = p_target_handle;

  rc = gpfs_ganesha(OPENHANDLE_LINK_BY_FH, &linkarg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
fsal_status_t fsal_internal_stat_name(int dirfd,
                                    struct gpfs_file_handle * p_dir_handle,
                                    const char * p_stat_name,
                                    struct stat *buf)
{
  int rc;
  struct stat_name_arg statarg;

  if(!p_stat_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  statarg.mountdirfd = dirfd;
  statarg.len = strlen(p_stat_name);
  statarg.name = p_stat_name;
  statarg.handle = p_dir_handle;
  statarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_STAT_BY_NAME, &statarg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
fsal_status_t fsal_internal_unlink(int dirfd,
                                   struct gpfs_file_handle * p_dir_handle,
                                   const char * p_stat_name,
                                   struct stat *buf)
{
  int rc;
  struct stat_name_arg statarg;

  if(!p_stat_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  statarg.mountdirfd = dirfd;
  statarg.len = strlen(p_stat_name);
  statarg.name = p_stat_name;
  statarg.handle = p_dir_handle;
  statarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_UNLINK_BY_NAME, &statarg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
fsal_status_t fsal_internal_create(int dirfd,
                                   struct gpfs_file_handle * p_dir_handle,
                                   const char * p_stat_name,
                                   mode_t mode, dev_t dev,
                                   struct gpfs_file_handle * p_new_handle,
                                   struct stat *buf)
{
  int rc;
  struct create_name_arg crarg;

  if(!p_stat_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  crarg.mountdirfd = dirfd;
  crarg.mode = mode;
  crarg.dev = dev;
  crarg.len = strlen(p_stat_name);
  crarg.name = p_stat_name;
  crarg.dir_fh = p_dir_handle;
  crarg.new_fh = p_new_handle;
  crarg.new_fh->handle_size = OPENHANDLE_HANDLE_LEN;
  crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
  crarg.new_fh->handle_version = OPENHANDLE_VERSION;
  crarg.buf = buf;

  rc = gpfs_ganesha(OPENHANDLE_CREATE_BY_NAME, &crarg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
fsal_status_t fsal_internal_rename_fh(int dirfd,
                                    struct gpfs_file_handle * p_old_handle,
                                    struct gpfs_file_handle * p_new_handle,
                                    const char * p_old_name,
                                    const char * p_new_name)
{
  int rc;
  struct rename_fh_arg renamearg;

  if(!p_old_name || !p_new_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  renamearg.mountdirfd = dirfd;
  renamearg.old_len = strlen(p_old_name);
  renamearg.old_name = p_old_name;
  renamearg.new_len = strlen(p_new_name);
  renamearg.new_name = p_new_name;
  renamearg.old_fh = p_old_handle;
  renamearg.new_fh = p_new_handle;

  rc = gpfs_ganesha(OPENHANDLE_RENAME_BY_FH, &renamearg);

  if(rc < 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_readlink_by_handle:
 * Reads the contents of the link
 *
 *
 * \return status of operation
 */

fsal_status_t fsal_readlink_by_handle(int dirfd,
                                      struct gpfs_file_handle * p_handle,
                                      char *__buf, size_t *maxlen)
{
  int rc;
  struct readlink_fh_arg readlinkarg;

  readlinkarg.mountdirfd = dirfd;
  readlinkarg.handle = p_handle;
  readlinkarg.buffer = __buf;
  readlinkarg.size = *maxlen;

  rc = gpfs_ganesha(OPENHANDLE_READLINK_BY_FH, &readlinkarg);

  if(rc < 0)
      return fsalstat(rc, 0);

  if(rc < *maxlen)
  {
    __buf[rc] = '\0';
    *maxlen = rc;
  }
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
      LogDebug(COMPONENT_FSAL, "GPFS get version failed with rc %d", rc);
  else
      LogDebug(COMPONENT_FSAL, "GPFS get version %d", rc);

  return rc;
}

/* Check the access by using NFS4 ACL if it exists. Otherwise, use mode. */
fsal_status_t fsal_internal_testAccess(const struct req_op_context *p_context, /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct attrlist * p_object_attributes /* IN */ )
{
  /* sanity checks. */
  if(!p_object_attributes)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* The root user ignores the mode/uid/gid of the file */
  if(p_context->creds->caller_uid == 0)
    return fsalstat(ERR_FSAL_NO_ERROR, 0);

#ifdef _USE_NFS4_ACL
  /* If ACL exists and given access type is ace4 mask, use ACL to check access. */
  LogDebug(COMPONENT_FSAL, "pattr=%p, pacl=%p, is_ace4_mask=%d, access_type=%x",
           p_object_attributes, p_object_attributes ? p_object_attributes->acl : 0,
           IS_FSAL_ACE4_MASK_VALID(access_type),
           access_type);

  if(p_object_attributes && p_object_attributes->acl &&
     IS_FSAL_ACE4_MASK_VALID(access_type))
    {
      return fsal_internal_testAccess_acl(p_context, FSAL_ACE4_MASK(access_type),
                                          p_object_attributes);
    }
#endif

  /* Use mode to check access. */
  return fsal_internal_testAccess_no_acl(p_context, FSAL_MODE_MASK(access_type),
                                         p_object_attributes);

  LogDebug(COMPONENT_FSAL, "invalid access_type = 0X%x",
           access_type);

  return fsalstat(ERR_FSAL_ACCESS, 0);
}

/* Check the access at the file system. It is called when Use_Test_Access = 0. */
fsal_status_t fsal_internal_access(int mntfd,                            /* IN */
                                const struct req_op_context *p_context,   /* IN */
                                struct gpfs_file_handle * p_handle,      /* IN */
                                fsal_accessflags_t access_type,          /* IN */
                                struct attrlist * p_object_attributes)   /* IN */
{
  fsal_status_t status;
  fsal_accessflags_t v4mask = 0;
  mode_t mode = 0;

  /* sanity checks. */
  if(!p_context || !p_handle)
    return fsalstat(ERR_FSAL_FAULT, 0);

  if(IS_FSAL_ACE4_MASK_VALID(access_type))
    v4mask = FSAL_ACE4_MASK(access_type);

  if(IS_FSAL_MODE_MASK_VALID(access_type))
    mode = FSAL_MODE_MASK(access_type);

  LogDebug(COMPONENT_FSAL, "requested v4mask=0x%x, mode=0x%x", v4mask, mode);

#ifdef _USE_NFS4_ACL
  status = fsal_check_access_by_handle(mntfd, p_context, p_handle, mode, v4mask,
                                       p_object_attributes);

  if(isFullDebug(COMPONENT_FSAL))
  {
    fsal_status_t status2;
	status2 = fsal_internal_testAccess(p_context, access_type,
	                                   p_object_attributes);
	if(status2.major != status.major)
	{
	  LogFullDebug(COMPONENT_FSAL,
	               "access error: access result major %d, test_access result major %d",
                   status.major, status2.major);
	}
	else
	  LogFullDebug(COMPONENT_FSAL,
	               "access ok: access and test_access produced the same result");
  }
#else
  status = fsal_internal_testAccess(p_context, access_type,
                                    p_object_attributes);
#endif

  return status;
}

/* Get NFS4 ACL as well as stat. For now, get stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_get_xstat_by_handle(int dirfd,
                                       struct gpfs_file_handle *p_handle,
                                       gpfsfsal_xstat_t *p_buffxstat)
{
  int rc;
  struct xstat_arg xstatarg;
#ifdef _USE_NFS4_ACL
  gpfs_acl_t *pacl_gpfs;
#endif                          /* _USE_NFS4_ACL */

  if(!p_handle || !p_buffxstat)
      return fsalstat(ERR_FSAL_FAULT, 0);

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
  xstatarg.handle = p_handle;
#ifdef _USE_NFS4_ACL
  xstatarg.acl = pacl_gpfs;
#else
  xstatarg.acl = NULL;
#endif
  xstatarg.attr_changed = 0;
  xstatarg.buf = &p_buffxstat->buffstat;

  rc = gpfs_ganesha(OPENHANDLE_GET_XSTAT, &xstatarg);
  LogDebug(COMPONENT_FSAL, "gpfs_ganesha: GET_XSTAT returned, fd %d rc %d",
                                                                 dirfd, rc);

  if(rc < 0)
    {
      if(errno == ENODATA)
        {
          /* For the special file that do not have ACL, GPFS returns ENODATA.
           * In this case, return okay with stat. */
          p_buffxstat->attr_valid = XATTR_STAT;
          LogFullDebug(COMPONENT_FSAL, "retrieved only stat, not acl");
          return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }
      else
        {
          /* Handle other errors. */
          LogFullDebug(COMPONENT_FSAL, "fsal_get_xstat_by_handle returned errno:%d -- %s",
                       errno, strerror(errno));
          return fsalstat(posix2fsal_error(errno), errno);
        }
    }

#ifdef _USE_NFS4_ACL
  p_buffxstat->attr_valid = XATTR_STAT | XATTR_ACL;
#else
  p_buffxstat->attr_valid = XATTR_STAT;
#endif

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Set NFS4 ACL as well as stat. For now, set stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_set_xstat_by_handle(int dirfd,
                                       const struct req_op_context *p_context,
                                       struct gpfs_file_handle * p_handle, int attr_valid,
                                       int attr_changed, gpfsfsal_xstat_t *p_buffxstat)
{
  int rc, errsv;
  struct xstat_arg xstatarg;

  if(!p_handle || !p_buffxstat)
      return fsalstat(ERR_FSAL_FAULT, 0);

  xstatarg.attr_valid = attr_valid;
  xstatarg.mountdirfd = dirfd;
  xstatarg.handle = p_handle;
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
    return fsalstat(posix2fsal_error(errsv), errsv);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* trucate by handle */
fsal_status_t fsal_trucate_by_handle(int dirfd,
                                     const struct req_op_context *p_context,
                                     struct gpfs_file_handle * p_handle,
                                     u_int64_t size)
{
  int attr_valid;
  int attr_changed;
  gpfsfsal_xstat_t buffxstat;

  if(!p_handle || !p_context)
      return fsalstat(ERR_FSAL_FAULT, 0);

  attr_valid = XATTR_STAT;
  attr_changed = XATTR_SIZE;
  buffxstat.buffstat.st_size = size;

  return fsal_set_xstat_by_handle(dirfd, p_context, p_handle, attr_valid,
                                 attr_changed, &buffxstat);
}

#ifdef _USE_NFS4_ACL
static bool fsal_check_ace_owner(uid_t uid, const struct req_op_context *p_context)
{
  return (p_context->creds->caller_uid == uid);
}

static bool fsal_check_ace_group(gid_t gid, const struct req_op_context *p_context)
{
  int i;

  if(p_context->creds->caller_gid == gid)
    return true;

  for(i = 0; i < p_context->creds->caller_glen; i++)
    {
      if(p_context->creds->caller_garray[i] == gid)
        return true;
    }

  return false;
}

static bool fsal_check_ace_matches(fsal_ace_t *pace,
                                             const struct req_op_context *p_context,
                                             bool is_owner,
                                             bool is_group)
{
  bool result = false;
  char *cause = "";

  if (IS_FSAL_ACE_SPECIAL_ID(*pace))
    switch(pace->who.uid)
      {
        case FSAL_ACE_SPECIAL_OWNER:
          if(is_owner)
            {
              result = true;
              cause = "special owner";
            }
        break;

        case FSAL_ACE_SPECIAL_GROUP:
          if(is_group)
            {
              result = true;
              cause = "special group";
            }
        break;

        case FSAL_ACE_SPECIAL_EVERYONE:
          result = true;
          cause = "special everyone";
        break;

        default:
        break;
      }
  else if (IS_FSAL_ACE_GROUP_ID(*pace))
    {
      if(fsal_check_ace_group(pace->who.gid, p_context))
        {
          result = true;
          cause = "group";
        }
    }
  else
    {
      if(fsal_check_ace_owner(pace->who.uid, p_context))
        {
          result = true;
          cause = "owner";
        }
    }

  LogDebug(COMPONENT_FSAL,
           "result: %d, cause: %s, flag: 0x%X, who: %d",
           result, cause, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return result;
}

static bool fsal_check_ace_applicable(fsal_ace_t *pace,
                                                const struct req_op_context *p_context,
                                                bool is_dir,
                                                bool is_owner,
                                                bool is_group)
{
  bool is_applicable = false;
  bool is_file = !is_dir;

  /* To be applicable, the entry should not be INHERIT_ONLY. */
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogDebug(COMPONENT_FSAL, "Not applicable, "
               "inherit only");
      return false;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to file");
          return false;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to dir");
          return false;
        }
    }

  /* The user should match who value. */
  is_applicable = fsal_check_ace_matches(pace, p_context, is_owner, is_group);
  if(is_applicable)
    LogDebug(COMPONENT_FSAL, "Applicable, flag=0X%x",
             pace->flag);
  else
    LogDebug(COMPONENT_FSAL, "Not applicable to given user");

  return is_applicable;
}

static void fsal_print_inherit_flags(fsal_ace_t *pace, char *p_buf)
{
  if(!pace || !p_buf)
    return;

  memset(p_buf, 0, ACL_DEBUG_BUF_SIZE);

  sprintf(p_buf, "Inherit:%s,%s,%s,%s",
          IS_FSAL_ACE_FILE_INHERIT(*pace)? "file":"",
          IS_FSAL_ACE_DIR_INHERIT(*pace) ? "dir":"",
          IS_FSAL_ACE_INHERIT_ONLY(*pace)? "inherit_only":"",
          IS_FSAL_ACE_NO_PROPAGATE(*pace)? "no_propagate":"");
}

static void fsal_print_ace(int ace_number, fsal_ace_t *pace)
{
  char inherit_flags[ACL_DEBUG_BUF_SIZE];
  char ace_buf[ACL_DEBUG_BUF_SIZE];

  if(!pace)
    return;

  memset(inherit_flags, 0, ACL_DEBUG_BUF_SIZE);
  memset(ace_buf, 0, ACL_DEBUG_BUF_SIZE);

  /* Get inherit flags if any. */
  fsal_print_inherit_flags(pace, inherit_flags);

  /* Print the entire ACE. */
  sprintf(ace_buf, "ACE %d %s %s %s %d %s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s %s",
          ace_number,
          /* ACE type. */
          IS_FSAL_ACE_ALLOW(*pace)? "allow":
          IS_FSAL_ACE_DENY(*pace) ? "deny":
          IS_FSAL_ACE_AUDIT(*pace)? "audit": "?",
          /* ACE who and its type. */
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_OWNER(*pace))    ? "owner@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_GROUP(*pace))    ? "group@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_EVERYONE(*pace)) ? "everyone@":"",
          IS_FSAL_ACE_SPECIAL_ID(*pace)						 ? "specialid":
          IS_FSAL_ACE_GROUP_ID(*pace) 						 ? "gid": "uid",
          GET_FSAL_ACE_WHO(*pace),
          /* ACE mask. */
          IS_FSAL_ACE_READ_DATA(*pace)           ? "read":"",
          IS_FSAL_ACE_WRITE_DATA(*pace)          ? "write":"",
          IS_FSAL_ACE_EXECUTE(*pace)             ? "execute":"",
          IS_FSAL_ACE_ADD_SUBDIRECTORY(*pace)    ? "append":"",
          IS_FSAL_ACE_READ_NAMED_ATTR(*pace)     ? "read_named_attr":"",
          IS_FSAL_ACE_WRITE_NAMED_ATTR(*pace)    ? "write_named_attr":"",
          IS_FSAL_ACE_DELETE_CHILD(*pace)        ? "delete_child":"",
          IS_FSAL_ACE_READ_ATTR(*pace)           ? "read_attr":"",
          IS_FSAL_ACE_WRITE_ATTR(*pace)          ? "write_attr":"",
          IS_FSAL_ACE_DELETE(*pace)              ? "delete":"",
          IS_FSAL_ACE_READ_ACL(*pace)            ? "read_acl":"",
          IS_FSAL_ACE_WRITE_ACL(*pace)           ? "write_acl":"",
          IS_FSAL_ACE_WRITE_OWNER(*pace)         ? "write_owner":"",
          IS_FSAL_ACE_SYNCHRONIZE(*pace)         ? "synchronize":"",
          /* ACE Inherit flags. */
          IS_FSAL_ACE_INHERIT(*pace)? inherit_flags: "");
  LogDebug(COMPONENT_FSAL, "%s", ace_buf);
}

static void fsal_print_v4mask(fsal_aceperm_t v4mask)
{
  fsal_ace_t ace;
  fsal_ace_t *pace = &ace;
  char v4mask_buf[ACL_DEBUG_BUF_SIZE];

  pace->perm = v4mask;
  memset(v4mask_buf, 0, ACL_DEBUG_BUF_SIZE);

  sprintf(v4mask_buf, "v4mask %s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
	  IS_FSAL_ACE_READ_DATA(*pace)           ? "read":"",
	  IS_FSAL_ACE_WRITE_DATA(*pace)          ? "write":"",
	  IS_FSAL_ACE_EXECUTE(*pace)             ? "execute":"",
	  IS_FSAL_ACE_ADD_SUBDIRECTORY(*pace)    ? "append":"",
	  IS_FSAL_ACE_READ_NAMED_ATTR(*pace)     ? "read_named_attr":"",
	  IS_FSAL_ACE_WRITE_NAMED_ATTR(*pace)    ? "write_named_attr":"",
	  IS_FSAL_ACE_DELETE_CHILD(*pace)        ? "delete_child":"",
	  IS_FSAL_ACE_READ_ATTR(*pace)           ? "read_attr":"",
	  IS_FSAL_ACE_WRITE_ATTR(*pace)          ? "write_attr":"",
	  IS_FSAL_ACE_DELETE(*pace)              ? "delete":"",
	  IS_FSAL_ACE_READ_ACL(*pace)            ? "read_acl":"",
	  IS_FSAL_ACE_WRITE_ACL(*pace)           ? "write_acl":"",
	  IS_FSAL_ACE_WRITE_OWNER(*pace)         ? "write_owner":"",
	  IS_FSAL_ACE_SYNCHRONIZE(*pace)         ? "synchronize":"");
  LogDebug(COMPONENT_FSAL, "%s", v4mask_buf);
}

static fsal_status_t fsal_internal_testAccess_acl(
                                  const struct req_op_context * p_context,/* IN */
                                  fsal_aceperm_t v4mask,                 /* IN */
                                  struct attrlist * p_object_attributes)  /* IN */
{
  fsal_aceperm_t missing_access;
  uid_t uid;
  gid_t gid;
  fsal_acl_t *pacl = NULL;
  fsal_ace_t *pace = NULL;
  int ace_number = 0;
  bool is_dir = false;
  bool is_owner = false;
  bool is_group = false;

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "Nothing was requested");
      return fsalstat(ERR_FSAL_NO_ERROR, 0);
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == DIRECTORY);

  LogDebug(COMPONENT_FSAL,
           "file acl=%p, file uid=%d, file gid= %d",
           pacl,uid, gid);
  LogDebug(COMPONENT_FSAL,
           "user uid=%d, user gid= %d, v4mask=0x%X",
           p_context->creds->caller_uid,
           p_context->creds->caller_gid,
           v4mask);

  if(isFullDebug(COMPONENT_FSAL))
    fsal_print_v4mask(v4mask);

  is_owner = fsal_check_ace_owner(uid, p_context);
  is_group = fsal_check_ace_group(gid, p_context);

  /* Always grant READ_ACL, WRITE_ACL and READ_ATTR, WRITE_ATTR to the file
   * owner. */
  if(is_owner)
    {
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_READ_ACL);
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_READ_ATTR);
      if(!missing_access)
        {
          LogDebug(COMPONENT_FSAL, "Met owner privileges");
          return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      LogDebug(COMPONENT_FSAL,
               "ace type 0x%X perm 0x%X flag 0x%X who %d",
               pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      ace_number += 1;

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogDebug(COMPONENT_FSAL, "allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, p_context, is_dir, is_owner, is_group))
            {
              if(IS_FSAL_ACE_ALLOW(*pace))
                {
                  LogDebug(COMPONENT_FSAL,
                           "allow perm 0x%X remainingPerms 0x%X",
                           pace->perm, missing_access);

                  missing_access &= ~(pace->perm & missing_access);
                  if(!missing_access)
                    {
                      LogDebug(COMPONENT_FSAL, "access granted");
                      if(isFullDebug(COMPONENT_FSAL))
                        {
                          if(pacl->naces != ace_number)
                            fsal_print_ace(ace_number, pace);
                        }
                      return fsalstat(ERR_FSAL_NO_ERROR, 0);
                    }
                }
             else if(pace->perm & missing_access)
               {
                 LogDebug(COMPONENT_FSAL, "access denied");
                 if(isFullDebug(COMPONENT_FSAL))
                   {
                     if(pacl->naces != ace_number)
                       fsal_print_ace(ace_number, pace);
                   }
                 return fsalstat(ERR_FSAL_ACCESS, 0);
               }
            }
        }
    }

  if(missing_access)
    {
      LogDebug(COMPONENT_FSAL, "access denied");
      return fsalstat(ERR_FSAL_ACCESS, 0);
    }
  else
    {
      LogDebug(COMPONENT_FSAL, "access granted");
      return fsalstat(ERR_FSAL_NO_ERROR, 0);
    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t fsal_check_access_by_handle(int dirfd,              /* IN */
                                 const struct req_op_context * p_context, /* IN */
                                 struct gpfs_file_handle * p_handle,     /* IN */
                                 mode_t mode,                            /* IN */
                                 fsal_accessflags_t v4mask,              /* IN */
                                 struct attrlist * p_object_attributes)  /* IN */

{
  int rc;
  struct xstat_cred_t gpfscred;
  fsal_status_t status;
  struct xstat_access_arg accessarg;
  unsigned int supported;
  unsigned int gpfs_mode = 0;
  bool is_dir = false;

  if(!p_handle)
    return fsalstat(ERR_FSAL_FAULT, 0);

  is_dir = (p_object_attributes->type == DIRECTORY);

  /* Convert fsal credential to gpfs credential. */
  status = fsal_cred_2_gpfs_cred(p_context->creds, &gpfscred);
  if(FSAL_IS_ERROR(status))
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* Convert fsal mode to gpfs mode. */
  status = fsal_mode_2_gpfs_mode(mode, v4mask, &gpfs_mode, is_dir);
  if(FSAL_IS_ERROR(status))
    return fsalstat(ERR_FSAL_FAULT, 0);

  accessarg.mountdirfd = dirfd;
  accessarg.handle = p_handle;
  accessarg.acl = NULL;  /* Not used. */
  accessarg.cred = (struct xstat_cred_t *) &gpfscred;
  accessarg.posix_mode = gpfs_mode;
  accessarg.access = v4mask;
  accessarg.supported = &supported;

  LogDebug(COMPONENT_FSAL,
           "v4mask=0x%X, mode=0x%X, uid=%d, gid=%d",
           v4mask, gpfs_mode, gpfscred.principal, gpfscred.group);

  if(isFullDebug(COMPONENT_FSAL))
    fsal_print_v4mask(v4mask);

  rc = gpfs_ganesha(OPENHANDLE_CHECK_ACCESS, &accessarg);
  LogDebug(COMPONENT_FSAL, "gpfs_ganesha: CHECK_ACCESS returned, rc = %d", rc);

  if(rc < 0)
  {
    LogDebug(COMPONENT_FSAL, "access denied");
    return fsalstat(posix2fsal_error(errno), errno);
  }

  LogDebug(COMPONENT_FSAL, "access granted");

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif                          /* _USE_NFS4_ACL */

static fsal_status_t fsal_internal_testAccess_no_acl(const struct req_op_context *p_context, /* IN */
                                               fsal_accessflags_t access_type,  /* IN */
                                               struct attrlist * p_object_attributes)/* IN */
{
  fsal_accessflags_t missing_access;
  unsigned int is_grp, i;
  uid_t uid;
  gid_t gid;
  mode_t mode;

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    return fsalstat(ERR_FSAL_INVAL, 0);

  /* unsatisfied flags */
  missing_access = access_type;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "Nothing was requested");
      return fsalstat(ERR_FSAL_NO_ERROR, 0);
    }

   uid = p_object_attributes->owner;
   gid = p_object_attributes->group;
   mode = p_object_attributes->mode;

  LogDebug(COMPONENT_FSAL,
               "file Mode=%#o, file uid=%d, file gid= %d",
               mode,uid, gid);
  LogDebug(COMPONENT_FSAL,
               "user uid=%d, user gid= %d, access_type=0X%x",
               p_context->creds->caller_uid,
               p_context->creds->caller_gid,
               access_type);

  /* If the uid of the file matches the uid of the user,
   * then the uid mode bits take precedence. */
  if(p_context->creds->caller_uid == uid)
    {

      LogDebug(COMPONENT_FSAL,
                   "File belongs to user %d", uid);

      if(mode & S_IRUSR)
        missing_access &= ~FSAL_R_OK;

      if(mode & S_IWUSR)
        missing_access &= ~FSAL_W_OK;

      if(mode & S_IXUSR)
        missing_access &= ~FSAL_X_OK;

      /* handle the creation of a new 500 file correctly */
      if((missing_access & FSAL_OWNER_OK) != 0)
        missing_access = 0;

      if(missing_access == 0)
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
      else
        {
          LogDebug(COMPONENT_FSAL,
                       "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                       mode, access_type, missing_access);
          return fsalstat(ERR_FSAL_ACCESS, 0);
        }

    }

  /* missing_access will be nonzero triggering a failure
   * even though FSAL_OWNER_OK is not even a real posix file
   * permission */
  missing_access &= ~FSAL_OWNER_OK;

  /* Test if the file belongs to user's group. */
  is_grp = (p_context->creds->caller_gid == gid);
  if(is_grp)
    LogDebug(COMPONENT_FSAL,
                 "File belongs to user's group %d",
                 p_context->creds->caller_gid);

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->creds->caller_glen; i++)
      {
        is_grp = (p_context->creds->caller_garray[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_FSAL,
                       "File belongs to user's alt group %d",
                       p_context->creds->caller_garray[i]);
        if(is_grp)
          break;
      }

  /* If the gid of the file matches the gid of the user or
   * one of the alternatve gids of the user, then the uid mode
   * bits take precedence. */
  if(is_grp)
    {
      if(mode & S_IRGRP)
        missing_access &= ~FSAL_R_OK;

      if(mode & S_IWGRP)
        missing_access &= ~FSAL_W_OK;

      if(mode & S_IXGRP)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
      else
        return fsalstat(ERR_FSAL_ACCESS, 0);

    }

  /* If the user uid is not 0, the uid does not match the file's, and
   * the user's gids do not match the file's gid, we apply the "other"
   * mode bits to the user. */
  if(mode & S_IROTH)
    missing_access &= ~FSAL_R_OK;

  if(mode & S_IWOTH)
    missing_access &= ~FSAL_W_OK;

  if(mode & S_IXOTH)
    missing_access &= ~FSAL_X_OK;

  if(missing_access == 0)
    return fsalstat(ERR_FSAL_NO_ERROR, 0);
  else {
    LogDebug(COMPONENT_FSAL,
                 "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                 mode, access_type, missing_access);
    return fsalstat(ERR_FSAL_ACCESS, 0);
  }

}

/**
 *  fsal_error_is_event:
 *  Indicates if an FSAL error should be posted as an event
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 */
bool fsal_error_is_event(fsal_status_t status)
{

  switch (status.major)
    {

    case ERR_FSAL_IO:
    case ERR_FSAL_STALE:
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
bool fsal_error_is_info(fsal_status_t status)
{
  switch (status.major)
    {
    case ERR_FSAL_NOTDIR:
    case ERR_FSAL_NOMEM:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_EXIST:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_INVAL:
    case ERR_FSAL_FBIG:
    case ERR_FSAL_NOSPC:
    case ERR_FSAL_MLINK:
    case ERR_FSAL_NAMETOOLONG:
    case ERR_FSAL_STALE:
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
