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

#include  "fsal.h"
#include "fsal_internal.h"
#include "stuff_alloc.h"
#include "SemN.h"
#include "fsal_convert.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>

/* Add missing prototype in vfs/*.h */
int fd_to_handle(int fd, void **hanp, size_t * hlen);

/* credential lifetime (1h) */
fsal_uint_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;

/* filesystem info for HPSS */
static fsal_staticfsinfo_t default_posix_info = {
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
  FALSE,                        /* lock management */
  TRUE,                         /* named attributes */
  TRUE,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  TRUE,                         /* can change times */
  TRUE,                         /* homogenous */
  VFS_SUPPORTED_ATTRIBUTES,     /* supported attributes */
  0,                            /* maxread size */
  0,                            /* maxwrite size */
  0,                            /* default umask */
  0,                            /* cross junctions */
  0400                          /* default access rights for xattrs: root=RW, owner=R */
};

/* variables for limiting the calls to the filesystem */
static int limit_calls = FALSE;
semaphore_t sem_fs_calls;

/* threads keys for stats */
static pthread_key_t key_stats;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init keys */
static void init_keys(void)
{
  if(pthread_key_create(&key_stats, NULL) == -1)
    LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_KEY_CREATE, errno);

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
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */

  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */

  if(bythread_stat == NULL)
    {
      int i;

      bythread_stat = (fsal_statistics_t *) Mem_Alloc(sizeof(fsal_statistics_t));

      if(bythread_stat == NULL)
        {
          LogError(COMPONENT_FSAL, ERR_SYS, ERR_MALLOC, Mem_Errno);
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
      else if(status.major == ERR_FSAL_DELAY)   /* Error is retryable */
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
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */
  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */
  if(bythread_stat == NULL)
    {
      int i;

      if((bythread_stat =
          (fsal_statistics_t *) Mem_Alloc(sizeof(fsal_statistics_t))) == NULL)
        LogError(COMPONENT_FSAL, ERR_SYS, ERR_MALLOC, Mem_Errno);

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

#define SET_INTEGER_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      cfg._field = (p_init_info)->values._field;                  \
      break;                                                      \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( cfg._field > (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( cfg._field < (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

#define SET_BITMAP_PARAM( cfg, p_init_info, _field )              \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a bit AND */                                     \
      cfg._field &= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a bit OR */                                      \
      cfg._field |= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

#define SET_BOOLEAN_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a boolean AND */                                 \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a boolean OR */                                  \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_FS_DEFAULT:                                    \
    default:                                                      \
    /* In the other cases, we keep the default value. */          \
        break;                                                    \
    }

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        vfsfs_specific_initinfo_t * fs_specific_info)
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
  global_fs_info = default_posix_info;

  LogDebug(COMPONENT_FSAL, "{");
  LogDebug(COMPONENT_FSAL, "  maxfilesize  = %llX    ",
           default_posix_info.maxfilesize);
  LogDebug(COMPONENT_FSAL, "  maxlink  = %lu   ",
           default_posix_info.maxlink);
  LogDebug(COMPONENT_FSAL, "  maxnamelen  = %lu  ",
           default_posix_info.maxnamelen);
  LogDebug(COMPONENT_FSAL, "  maxpathlen  = %lu  ",
           default_posix_info.maxpathlen);
  LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ",
           default_posix_info.no_trunc);
  LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
           default_posix_info.chown_restricted);
  LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
           default_posix_info.case_insensitive);
  LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
           default_posix_info.case_preserving);
  LogDebug(COMPONENT_FSAL, "  fh_expire_type  = %hu ",
           default_posix_info.fh_expire_type);
  LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
           default_posix_info.link_support);
  LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
           default_posix_info.symlink_support);
  LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
           default_posix_info.lock_support);
  LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
           default_posix_info.named_attr);
  LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
           default_posix_info.unique_handles);
  LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
           default_posix_info.acl_support);
  LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
           default_posix_info.cansettime);
  LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
           default_posix_info.homogenous);
  LogDebug(COMPONENT_FSAL, "  supported_attrs  = %llX  ",
           default_posix_info.supported_attrs);
  LogDebug(COMPONENT_FSAL, "  maxread  = %llX     ",
           default_posix_info.maxread);
  LogDebug(COMPONENT_FSAL, "  maxwrite  = %llX     ",
           default_posix_info.maxwrite);
  LogDebug(COMPONENT_FSAL, "  umask  = %X ", default_posix_info.umask);
  LogDebug(COMPONENT_FSAL, "}");

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
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  LogFullDebug(COMPONENT_FSAL,
                    "Supported attributes constant = 0x%llX.",
                    VFS_SUPPORTED_ATTRIBUTES);

  LogFullDebug(COMPONENT_FSAL,
                    "Supported attributes default = 0x%llX.",
                    default_posix_info.supported_attrs);

  LogDebug(COMPONENT_FSAL,
                    "FSAL INIT: Supported attributes mask = 0x%llX.",
                    global_fs_info.supported_attrs);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t fsal_internal_handle2fd(vfsfsal_op_context_t * p_context,
                                      vfsfsal_handle_t * phandle, int *pfd, int oflags)
{
  int rc = 0;
  int errsv = 0;


  if(!phandle || !pfd || !p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

#if 0
  {
  char str[1024] ;
  sprint_mem( str, phandle->data.vfs_handle.handle, phandle->data.vfs_handle.handle_bytes ) ;
  printf( "=====> fsal_internal_handle2fd: type=%u bytes=%u|%s\n",  
          phandle->data.vfs_handle.handle_type, phandle->data.vfs_handle.handle_bytes, str ) ;
  }
#endif


  rc =  vfs_open_by_handle( p_context->export_context->mount_root_fd,
			    &phandle->data.vfs_handle, 
                            oflags ) ;
  if(rc == -1)
    {
      errsv = errno;

      ReturnCode(posix2fsal_error(errsv), errsv);
    }

  *pfd = rc;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_handle2fd */

fsal_status_t fsal_internal_fd2handle( vfsfsal_op_context_t * p_context,
                                       int fd, 
				       vfsfsal_handle_t * phandle)
{
  int rc = 0 ;
  int errsv = 0 ; 
  int mnt_id = 0 ;


  phandle->data.vfs_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( ( rc = vfs_fd_to_handle( fd, &phandle->data.vfs_handle, &mnt_id ) ) )
   ReturnCode(posix2fsal_error(errsv), errsv);

#if 0 
  {
    char str[1024] ;
    sprint_mem( str, phandle->data.vfs_handle.handle, phandle->data.vfs_handle.handle_bytes ) ;
    printf( "=====> fsal_internal_fd2handle: type=%u bytes=%u|%s\n",  
            phandle->data.vfs_handle.handle_type, phandle->data.vfs_handle.handle_bytes, str ) ;
  }
#endif

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_fd2handle */

fsal_status_t fsal_internal_Path2Handle(vfsfsal_op_context_t * p_context,       /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        vfsfsal_handle_t * p_handle /* OUT */ )
{
  int objectfd;
  fsal_status_t st;

  if(!p_context || !p_handle || !p_fsalpath)
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset(p_handle, 0, sizeof(vfsfsal_handle_t));

  LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", p_fsalpath->path);

  if((objectfd = open(p_fsalpath->path, O_RDONLY, 0600)) < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  st = fsal_internal_fd2handle(p_context, objectfd, p_handle);
  close(objectfd);
  return st;
}                               /* fsal_internal_Path2Handle */


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
                                          fsal_name_t * p_fsalname,     /* IN */
                                          fsal_handle_t * p_handle      /* OUT
                                                                         */ )
{
  fsal_status_t st;
  int errsrv = 0 ;

  if( !p_fsalname || !p_handle )
    ReturnCode(ERR_FSAL_FAULT, 0);
 
  memset(p_handle, 0, sizeof(vfsfsal_handle_t));

  LogFullDebug(COMPONENT_FSAL, "get handle at for %s", p_fsalname->name);

  p_handle->data.vfs_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( vfs_name_by_handle_at( dfd,  p_fsalname->name, &p_handle->data.vfs_handle ) != 0 )
   {
      errsrv = errno;
      ReturnCode(posix2fsal_error(errsrv), errsrv);
   }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* fsal_internal_get_handle_at */


/*
   Check the access from an existing fsal_attrib_list_t or struct stat
*/
/* XXX : ACL */
fsal_status_t fsal_internal_testAccess(vfsfsal_op_context_t * p_context,        /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat * p_buffstat,        /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  fsal_accessflags_t missing_access;
  unsigned int is_grp, i;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_accessmode_t mode;

  /* sanity checks. */

  if((!p_object_attributes && !p_buffstat) || !p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    ReturnCode(ERR_FSAL_INVAL, 0);

  /* test root access */

  if(p_context->credential.user == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);

  /* unsatisfied flags */

  missing_access = access_type;

  if(p_object_attributes)
    {
      uid = p_object_attributes->owner;
      gid = p_object_attributes->group;
      mode = p_object_attributes->mode;
    }
  else
    {
      uid = p_buffstat->st_uid;
      gid = p_buffstat->st_gid;
      mode = unix2fsal_mode(p_buffstat->st_mode);
    }

  /* Test if file belongs to user. */

  if(p_context->credential.user == uid)
    {

      LogFullDebug(COMPONENT_FSAL, "File belongs to user %d", uid);

      if(mode & FSAL_MODE_RUSR)
        missing_access &= ~FSAL_R_OK;

      if(mode & FSAL_MODE_WUSR)
        missing_access &= ~FSAL_W_OK;

      if(mode & FSAL_MODE_XUSR)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        {
          LogFullDebug(COMPONENT_FSAL,
                            "Mode=%#o, Access=%#o, Rights missing: %#o", mode,
                            access_type, missing_access);
          ReturnCode(ERR_FSAL_ACCESS, 0);
        }

    }

  /* Test if the file belongs to user's group. */

  is_grp = (p_context->credential.group == gid);

  if(is_grp)
    LogFullDebug(COMPONENT_FSAL, "File belongs to user's group %d",
                      p_context->credential.group);


  /* Test if file belongs to alt user's groups */

  if(!is_grp)
    {
      for(i = 0; i < p_context->credential.nbgroups; i++)
        {
          is_grp = (p_context->credential.alt_groups[i] == gid);

          if(is_grp)
            LogFullDebug(COMPONENT_FSAL,
                              "File belongs to user's alt group %d",
                              p_context->credential.alt_groups[i]);

          // exits loop if found
          if(is_grp)
            break;
        }
    }

  /* finally apply group rights */

  if(is_grp)
    {
      if(mode & FSAL_MODE_RGRP)
        missing_access &= ~FSAL_R_OK;

      if(mode & FSAL_MODE_WGRP)
        missing_access &= ~FSAL_W_OK;

      if(mode & FSAL_MODE_XGRP)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        ReturnCode(ERR_FSAL_ACCESS, 0);

    }

  /* test other perms */

  if(mode & FSAL_MODE_ROTH)
    missing_access &= ~FSAL_R_OK;

  if(mode & FSAL_MODE_WOTH)
    missing_access &= ~FSAL_W_OK;

  if(mode & FSAL_MODE_XOTH)
    missing_access &= ~FSAL_X_OK;

  /* XXX ACLs. */

  if(missing_access == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);
  else
    ReturnCode(ERR_FSAL_ACCESS, 0);

}

fsal_status_t fsal_internal_setattrs_symlink(vfsfsal_handle_t * p_filehandle,   /* IN */
                                             vfsfsal_op_context_t * p_context,  /* IN */
                                             fsal_attrib_list_t * p_attrib_set, /* IN */
                                             fsal_attrib_list_t * p_object_attributes)
{
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  *p_object_attributes = *p_attrib_set;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_setattrs_symlink */


