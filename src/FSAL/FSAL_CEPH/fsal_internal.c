/*
 * Copyright (C) 2010 The Linx Box Corporation
 * Contributor : Adam C. Emerson
 *
 * Some Portions Copyright CEA/DAM/DIF  (2008)
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
 *
 * \file    fsal_internal.c
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
#include "nfs4.h"
#include "HashTable.h"

#include <pthread.h>

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;
cephfs_specific_initinfo_t global_spec_info;

#define SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_FILEID   | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME  )

#ifdef _PNFS_MDS
static layouttype4 layout_type_list[] = {LAYOUT4_NFSV4_1_FILES};
#endif /* _PNFS_MDS */

/* filesystem info for your filesystem */
static fsal_staticfsinfo_t default_ceph_info = {
  .maxfilesize = UINT64_MAX,
  .maxlink = 1024,
  .maxnamelen = FSAL_MAX_NAME_LEN,
  .maxpathlen = FSAL_MAX_PATH_LEN,
  .no_trunc = TRUE,
  .chown_restricted = TRUE,
  .case_insensitive = FALSE,
  .case_preserving = TRUE,
  .fh_expire_type = FSAL_EXPTYPE_PERSISTENT,
  .link_support = TRUE,
  .symlink_support = TRUE,
  .lock_support = FALSE,
  .lock_support_owner = FALSE,
  .lock_support_async_block = FALSE,
  .named_attr = TRUE,
  .unique_handles = TRUE,
  .lease_time = {10, 0},
  .acl_support = FSAL_ACLSUPPORT_DENY,
  .cansettime = TRUE,
  .homogenous = TRUE,
  .supported_attrs = SUPPORTED_ATTRIBUTES,
  .maxread = 0x400000,
  .maxwrite = 0x400000,
  .umask = 0,
  .auth_exportpath_xdev = 0,
  .xattr_access_rights = 0400,
  .accesscheck_support = 0,
  .share_support = 0,
  .share_support_owner = 0,
#ifdef _PNFS_MDS
  .pnfs_supported = TRUE,
  .layout_blksize = 0x400000,
  .max_segment_count = 1,
  .loc_buffer_size = 256,
  .dsaddr_buffer_size = 5120
#endif /* _PNFS_MDS */
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

#define SET_INTEGER_PARAM( cfg, p_init_info, _field )         \
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

#define SET_BITMAP_PARAM( cfg, p_init_info, _field )          \
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

#define SET_BOOLEAN_PARAM( cfg, p_init_info, _field )         \
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
   Check the access from an existing fsal_attrib_list_t or struct stat
*/
/* XXX : ACL */
fsal_status_t fsal_internal_testAccess(cephfsal_op_context_t* context,
                                       fsal_accessflags_t access_type,
                                       struct stat * st,
                                       fsal_attrib_list_t * object_attributes)
{
  fsal_accessflags_t missing_access;
  unsigned int is_grp, i;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_accessmode_t mode;
  fsal_uid_t userid = context->credential.user;
  fsal_uid_t groupid = context->credential.group;

  /* sanity checks. */

  if((!object_attributes && !st) || !context)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    ReturnCode(ERR_FSAL_INVAL, 0);

  /* test root access */

  if(userid == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);

  /* unsatisfied flags */

  missing_access = FSAL_MODE_MASK(access_type); /* only modes, no ACLs here */

  if(object_attributes)
    {
      uid = object_attributes->owner;
      gid = object_attributes->group;
      mode = object_attributes->mode;
    }
  else
    {
      uid = st->st_uid;
      gid = st->st_gid;
      mode = unix2fsal_mode(st->st_mode);
    }

  /* Test if file belongs to user. */

  if(userid == uid)
    {

      LogFullDebug(COMPONENT_FSAL, "File belongs to user %d", uid);

      if(mode & FSAL_MODE_RUSR)
        missing_access &= ~FSAL_R_OK;

      if(mode & FSAL_MODE_WUSR)
        missing_access &= ~FSAL_W_OK;

      if(mode & FSAL_MODE_XUSR)
        missing_access &= ~FSAL_X_OK;

      if((missing_access & FSAL_OWNER_OK) != 0)
        missing_access = 0;

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

  is_grp = (groupid == gid);

  if(is_grp)
    LogFullDebug(COMPONENT_FSAL, "File belongs to user's group %d",
                      groupid);


  /* Test if file belongs to alt user's groups */

  if(!is_grp)
    {
      for(i = 0; i < context->credential.nbgroups; i++)
        {
          is_grp = context->credential.alt_groups[i] == gid;

          if(is_grp)
            LogFullDebug(COMPONENT_FSAL,
                         "File belongs to user's alt group %d",
                         context->credential.alt_groups[i]);

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

#ifdef _PNFS_MDS
  default_ceph_info.fs_layout_types.fattr4_fs_layout_types_val
    = layout_type_list;
  default_ceph_info.fs_layout_types.fattr4_fs_layout_types_len
    = 1;
#endif /* _PNFS_MDS */

  /* setting default values. */
  global_fs_info = default_ceph_info;

  LogDebug(COMPONENT_FSAL, "{");
  LogDebug(COMPONENT_FSAL, "  maxfilesize  = %llX    ",
           default_ceph_info.maxfilesize);
  LogDebug(COMPONENT_FSAL, "  maxlink  = %lu   ",
           default_ceph_info.maxlink);
  LogDebug(COMPONENT_FSAL, "  maxnamelen  = %lu  ",
           default_ceph_info.maxnamelen);
  LogDebug(COMPONENT_FSAL, "  maxpathlen  = %lu  ",
           default_ceph_info.maxpathlen);
  LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ",
           default_ceph_info.no_trunc);
  LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
           default_ceph_info.chown_restricted);
  LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
           default_ceph_info.case_insensitive);
  LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
           default_ceph_info.case_preserving);
  LogDebug(COMPONENT_FSAL, "  fh_expire_type  = %hu ",
           default_ceph_info.fh_expire_type);
  LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
           default_ceph_info.link_support);
  LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
           default_ceph_info.symlink_support);
  LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
           default_ceph_info.lock_support);
  LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
           global_fs_info.lock_support_owner);
  LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
           global_fs_info.lock_support_async_block);
  LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
           default_ceph_info.named_attr);
  LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
           default_ceph_info.unique_handles);
  LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
           default_ceph_info.acl_support);
  LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
           default_ceph_info.cansettime);
  LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
           default_ceph_info.homogenous);
  LogDebug(COMPONENT_FSAL, "  supported_attrs  = %llX  ",
           default_ceph_info.supported_attrs);
  LogDebug(COMPONENT_FSAL, "  maxread  = %llX     ",
           default_ceph_info.maxread);
  LogDebug(COMPONENT_FSAL, "  maxwrite  = %llX     ",
           default_ceph_info.maxwrite);
  LogDebug(COMPONENT_FSAL, "  umask  = %X ", default_ceph_info.umask);
  LogDebug(COMPONENT_FSAL, "}");

  /* Analyzing fs_common_info struct */

#ifdef _PNFS_MDS
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
     (fs_common_info->behaviors.homogenous != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.fs_layout_types != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.layout_blksize != FSAL_INIT_FS_DEFAULT))
#else /* !_PNFS_MDS */
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
#endif /* !_PNFS_MDS */
    {
      ReturnCode(ERR_FSAL_NOTSUPP, 0);
    }

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, symlink_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, link_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_owner);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_async_block);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);
#ifdef _PNFS_MDS
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, pnfs_supported);
#endif /* _PNFS_MDS */

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes constant = 0x%llX.",
               SUPPORTED_ATTRIBUTES);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes default = 0x%llX.",
               default_ceph_info.supported_attrs);

  LogDebug(COMPONENT_FSAL,
                    "FSAL INIT: Supported attributes mask = 0x%llX.",
                    global_fs_info.supported_attrs);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
