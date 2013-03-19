// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_internal.c
// Description: FSAL internal operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  
 * USA
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
#include "FSAL/access_check.h"

#include <pthread.h>
#include <string.h>

#ifdef _USE_NFS4_ACL
#define ACL_DEBUG_BUF_SIZE 256
#endif                          /* _USE_NFS4_ACL */

#include "pt_ganesha.h"

/* credential lifetime (1h) */
fsal_uint_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;

/* filesystem info for HPSS */
static fsal_staticfsinfo_t default_ptfs_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  _POSIX_LINK_MAX,              /* max links */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* max pathlen */
  TRUE,                         /* no_trunc */
  TRUE,                         /* chown restricted */
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
  PTFS_SUPPORTED_ATTRIBUTES,    /* supported attributes */
  0,                            /* maxread size */
  0,                            /* maxwrite size */
  0,                            /* default umask */
  0,                            /* cross junctions */
  0400,                         /* default access rights for xattrs: root=RW, 
                                 * owner=R */
  0,                            /* default share reservation support in FSAL */
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
             "Could not create thread specific stats (pthread_key_create) " 
             "err %d (%s)",
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
void
fsal_increment_nbcall(int           function_index,
		      fsal_status_t status)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* verify index */

  if(function_index >= FSAL_NB_FUNC)
    return;

  /* first, we init the keys if this is the first time */

  if(pthread_once(&once_key, init_keys) != 0) {
    LogMajor(COMPONENT_FSAL,
             "Could not create thread specific stats (pthread_once) " 
             "err %d (%s)",
             errno, strerror(errno));
    return;
  }

  /* we get the specific value */

  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */

  if(bythread_stat == NULL) {
    int i;

    bythread_stat = 
      (fsal_statistics_t *) gsh_malloc(sizeof(fsal_statistics_t));

    if(bythread_stat == NULL) {
      LogCrit(COMPONENT_FSAL,
              "Could not allocate memory for FSAL statistics err %d (%s)",
              ENOMEM, strerror(ENOMEM));
      /* we don't have real memory, bail */
      return;
    }

    /* inits the struct */

    for(i = 0; i < FSAL_NB_FUNC; i++) {
      bythread_stat->func_stats.nb_call[i] = 0;
      bythread_stat->func_stats.nb_success[i] = 0;
      bythread_stat->func_stats.nb_err_retryable[i] = 0;
      bythread_stat->func_stats.nb_err_unrecover[i] = 0;
    }

    /* set the specific value */
    pthread_setspecific(key_stats, (void *)bythread_stat);

  }

  /* we increment the values */

  if(bythread_stat) {
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
void
fsal_internal_getstats(fsal_statistics_t * output_stats)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0) {
    LogMajor(COMPONENT_FSAL,
             "Could not create thread specific stats (pthread_once) " 
             "err %d (%s)",
             errno, strerror(errno));
    return;
  }

  /* we get the specific value */
  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */
  if(bythread_stat == NULL) {
    int i;

    if((bythread_stat =
       (fsal_statistics_t *) gsh_malloc(sizeof(fsal_statistics_t))) == NULL) {
       /* we don't have working memory, bail */
       LogCrit(COMPONENT_FSAL,
               "Could not allocate memory for FSAL statistics err %d (%s)",
               ENOMEM, strerror(ENOMEM));
       return;
    }

    /* inits the struct */
    for(i = 0; i < FSAL_NB_FUNC; i++) {
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
void
fsal_internal_SetCredentialLifetime(fsal_uint_t lifetime_in)
{
  CredentialLifetime = lifetime_in;
}

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t
fsal_internal_init_global(fsal_init_info_t       * fsal_info,
			  fs_common_initinfo_t   * fs_common_info,
			  fs_specific_initinfo_t * fs_specific_info)
{

  /* sanity check */
  if(!fsal_info || !fs_common_info || !fs_specific_info)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* Process the FSAL_PT specific configuration parameters */
  ptfs_specific_initinfo_t * p_ptfs_info = (ptfs_specific_initinfo_t *)
                                           fs_specific_info;
  if (p_ptfs_info->internal_handle_timeout != 0) {
    LogDebug(COMPONENT_FSAL, "Setting polling_thread_handle_timeout_sec to: %d",
             p_ptfs_info->internal_handle_timeout);
    polling_thread_handle_timeout_sec = p_ptfs_info->internal_handle_timeout;
  } else {
    LogDebug(COMPONENT_FSAL, "Leaving polling_thread_handle_timeout_sec at: %d",
              polling_thread_handle_timeout_sec);
  }

  /* inits FS call semaphore */
  if(fsal_info->max_fs_calls > 0) {
    int rc;

    limit_calls = TRUE;

    rc = semaphore_init(&sem_fs_calls, fsal_info->max_fs_calls);

    if(rc != 0)
      ReturnCode(ERR_FSAL_SERVERFAULT, rc);

    LogDebug(COMPONENT_FSAL,
             "FSAL INIT: Max simultaneous calls to filesystem is limited " 
             "to %u.",
             fsal_info->max_fs_calls);
  } else {
    LogDebug(COMPONENT_FSAL,
             "FSAL INIT: Max simultaneous calls to filesystem is " 
             "unlimited.");
  }

  /* setting default values. */
  global_fs_info = default_ptfs_info;

  if(isFullDebug(COMPONENT_FSAL)) {
    display_fsinfo(&default_ptfs_info);
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
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes constant = 0x%llX.",
               PTFS_SUPPORTED_ATTRIBUTES);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes default = 0x%llX.",
               default_ptfs_info.supported_attrs);

  LogFullDebug(COMPONENT_FSAL,
               "FSAL INIT: Supported attributes mask = 0x%llX.",
               global_fs_info.supported_attrs);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

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
fsal_status_t
fsal_internal_handle2fd(fsal_op_context_t * p_context,
			fsal_handle_t     * phandle,
			int               * pfd,
			int                 oflags)
{
  int dirfd = 0;
  fsal_status_t status;

  if(!phandle || !pfd || !p_context || !p_context->export_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

  dirfd = ((ptfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  status = fsal_internal_handle2fd_at(p_context, dirfd, phandle, pfd, oflags);

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
fsal_status_t
fsal_internal_handle2fd_at(fsal_op_context_t * p_context,
			   int                 dirfd,
			   fsal_handle_t     * phandle,
			   int               * pfd,
			   int                 oflags)
{
  ptfsal_handle_t *p_fsi_handle = (ptfsal_handle_t *)phandle;
  int open_rc = 0;
  int stat_rc;
  char fsi_name[PATH_MAX];

  FSI_TRACE(FSI_DEBUG, "FSI - handle2fd_at\n");
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

  if(!phandle || !pfd)
    ReturnCode(ERR_FSAL_FAULT, 0);

  FSI_TRACE(FSI_DEBUG, "Handle Type: %d", 
            p_fsi_handle->data.handle.handle_type);
  if (p_fsi_handle->data.handle.handle_type != FSAL_TYPE_DIR) {
    FSI_TRACE(FSI_DEBUG, "FSI - handle2fdat - opening regular file\n");
    open_rc = ptfsal_open_by_handle(p_context, phandle, oflags, 0777);
  } else {
    stat_rc =  fsi_get_name_from_handle(p_context, 
                                        p_fsi_handle->data.handle.f_handle, 
                                        fsi_name,
                                        NULL);
    if(stat_rc < 0)
    {
      FSI_TRACE(FSI_DEBUG, "Handle to name failed handle %s", 
                p_fsi_handle->data.handle.f_handle);
      ReturnCode(posix2fsal_error(errno), errno);
    }
    FSI_TRACE(FSI_DEBUG, "NAME: %s", fsi_name);
    open_rc = ptfsal_opendir(p_context, fsi_name, NULL, 0); 
  }

  FSI_TRACE(FSI_DEBUG, "File Descriptor = %d\n", open_rc);

  if(open_rc < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  *pfd = open_rc;

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
fsal_status_t
fsal_internal_get_handle(fsal_op_context_t * p_context,  /* IN */
			 fsal_path_t       * p_fsalpath, /* IN */
			 fsal_handle_t     * p_handle    /* OUT */)
{
  int rc;
  fsi_stat_struct buffstat;
  ptfsal_handle_t *p_fsi_handle = (ptfsal_handle_t *)p_handle;
  uint64_t * handlePtr;

  FSI_TRACE(FSI_NOTICE, "FSI - get_handle for path %s\n", p_fsalpath->path);

  if(!p_context || !p_handle || !p_fsalpath)
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset(p_fsi_handle, 0, sizeof(ptfsal_handle_t));
  rc = ptfsal_stat_by_name(p_context, p_fsalpath, &buffstat);

  FSI_TRACE(FSI_DEBUG, "Stat call return %d", rc);
  if (rc)
  {
    ReturnCode(ERR_FSAL_NOENT, errno);
  }
  memset(p_handle, 0, sizeof(ptfsal_handle_t));
  memcpy(&p_fsi_handle->data.handle.f_handle, 
         &buffstat.st_persistentHandle.handle, FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
  p_fsi_handle->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
  p_fsi_handle->data.handle.handle_version = OPENHANDLE_VERSION;
  p_fsi_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
  p_fsi_handle->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);

  handlePtr = (uint64_t *) p_fsi_handle->data.handle.f_handle; 
  FSI_TRACE(FSI_NOTICE,"FSI - fsal_internal_get_handle[0x%lx %lx %lx %lx] type %x\n", 
            handlePtr[0], handlePtr[1], handlePtr[2], handlePtr[3],
            p_fsi_handle->data.handle.handle_type);

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
fsal_status_t
fsal_internal_get_handle_at(fsal_op_context_t * p_context,  /* IN */ 
			    int                 dfd,        /* IN */
			    fsal_name_t       * p_fsalname, /* IN */
			    fsal_handle_t     * p_handle    /* OUT*/)
{
  fsi_stat_struct buffstat;
  int stat_rc;
  ptfsal_handle_t *p_fsi_handle = (ptfsal_handle_t *)p_handle;
  ptfsal_handle_t fsi_handle;
  fsal_path_t fsal_path;

  FSI_TRACE(FSI_DEBUG, "FSI - get_handle_at for %s \n",  p_fsalname->name);

  if(!p_handle || !p_fsalname)
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset(p_fsi_handle, 0, sizeof(ptfsal_handle_t));

  LogFullDebug(COMPONENT_FSAL,
               "Lookup handle at for %s",
               p_fsalname->name);

  FSI_TRACE(FSI_DEBUG, 
            "FSI - gethandleat OPENHANDLE_NAME_TO_HANDLE [%s] dfd %d\n",
            p_fsalname->name,dfd);

  memset(&fsal_path, 0, sizeof(fsal_path_t));
  memcpy(&fsal_path.path, p_fsalname->name, 
         sizeof(fsal_path.path));
  stat_rc = ptfsal_stat_by_name(p_context, &fsal_path, &buffstat);

  if(stat_rc == 0) {
    memcpy(&p_fsi_handle->data.handle.f_handle, 
           &buffstat.st_persistentHandle.handle, 
           sizeof(fsi_handle.data.handle.f_handle));
    p_fsi_handle->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
    p_fsi_handle->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);
    p_fsi_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
    p_fsi_handle->data.handle.handle_version = OPENHANDLE_VERSION;
    FSI_TRACE(FSI_DEBUG, "Handle=%s", p_fsi_handle->data.handle.f_handle);    
  } else {
    ReturnCode(ERR_FSAL_NOENT, errno);
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
fsal_status_t
fsal_internal_fd2handle(fsal_op_context_t * p_context,
			int                 fd,
			fsal_handle_t     * handle)
{
  ptfsal_handle_t *p_handle = (ptfsal_handle_t *)handle;

  FSI_TRACE(FSI_DEBUG, "FSI - fd2handle\n");

  if(!p_handle || !&p_handle->data.handle)
    ReturnCode(ERR_FSAL_FAULT, 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_link_at:
 * Create a link based on a file descriptor, dirfd, and new name
 *
 * \param srcfd (input):
 *          file descriptor of source file
 * \param dirfd (input):
 *          file descriptor of target directory
 * \param name (input):
 *          name for the new file
 *
 * \return status of operation
 */
fsal_status_t
fsal_internal_link_at(int    srcfd,
		      int    dirfd,
		      char * name)
{
  FSI_TRACE(FSI_DEBUG, "FSI - link_at\n");

  if(!name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal_readlink_by_handle:
 * Reads the contents of the link
 *
 *
 * \return status of operation
 */
fsal_status_t
fsal_readlink_by_handle(fsal_op_context_t * p_context,
			fsal_handle_t     * p_handle,
			char              * __buf,
			int maxlen)
{
  int rc;

  FSI_TRACE(FSI_DEBUG, "Begin - readlink_by_handle\n");

  memset(__buf, 0, maxlen);
  rc = ptfsal_readlink(p_handle, p_context,  __buf);

  if(rc < 0)
      Return(rc, 0, INDEX_FSAL_readlink);

  FSI_TRACE(FSI_DEBUG, "End - readlink_by_handle\n");
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_stat_by_handle:
 * get the stat value
 *
 *
 * \return status of operation
 */
fsal_status_t
fsal_stat_by_handle(fsal_op_context_t * p_context,
		    fsal_handle_t     * p_handle,
		    struct stat64     * buf)
{
  FSI_TRACE(FSI_DEBUG, "FSI - stat_by_handle\n");

  if(!p_handle || !p_context || !p_context->export_context)
      ReturnCode(ERR_FSAL_FAULT, 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* Get NFS4 ACL as well as stat. For now, get stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t
fsal_get_xstat_by_handle(fsal_op_context_t * p_context,
			 fsal_handle_t     * p_handle,
			 ptfsal_xstat_t    * p_buffxstat)
{
  int dirfd = 0;
#ifdef _USE_NFS4_ACL
  gpfs_acl_t *pacl_ptfs;
#endif                          /* _USE_NFS4_ACL */

  FSI_TRACE(FSI_DEBUG, "FSI - get_xstat_by_handle\n");

  if(!p_handle || !p_context || !p_context->export_context || !p_buffxstat)
      ReturnCode(ERR_FSAL_FAULT, 0);

  memset(p_buffxstat, 0, sizeof(ptfsal_xstat_t));

  dirfd = ((ptfsal_op_context_t *)p_context)->export_context->mount_root_fd;

  // figure out what to return
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* Set NFS4 ACL as well as stat. For now, set stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t
fsal_set_xstat_by_handle(fsal_op_context_t * p_context,
			 fsal_handle_t     * p_handle,
			 int                 attr_valid,
			 int                 attr_changed,
			 ptfsal_xstat_t    * p_buffxstat)
{
  FSI_TRACE(FSI_DEBUG, "FSI - set_xstat_by_handle\n");

  if(!p_handle || !p_context || !p_context->export_context || !p_buffxstat)
      ReturnCode(ERR_FSAL_FAULT, 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
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
    case ERR_FSAL_PERM:
    case ERR_FSAL_NOT_OPENED:
    case ERR_FSAL_ACCESS:
    case ERR_FSAL_FILE_OPEN:
    case ERR_FSAL_DELAY:
    case ERR_FSAL_NOTEMPTY:
    case ERR_FSAL_DQUOT:
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
    case ERR_FSAL_STALE:
      return TRUE;

    default:
      return FALSE;
    }
}
