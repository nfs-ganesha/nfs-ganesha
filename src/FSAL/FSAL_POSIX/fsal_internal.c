/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#include "fsal.h"
#include "fsal_internal.h"
#include "posixdb_consistency.h"
#include "stuff_alloc.h"
#include "SemN.h"
#include "fsal_convert.h"
#include <libgen.h>             /* used for 'dirname' */

#include <pthread.h>
#include <string.h>
#include <mntent.h>

/* credential lifetime (1h) */
fsal_uint_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;
fsal_posixdb_conn_params_t global_posixdb_params;

/* filesystem info for HPSS */
static fsal_staticfsinfo_t default_posix_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  _POSIX_LINK_MAX,              /* max links */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* min filename */
  TRUE,                         /* no_trunc */
  TRUE,                         /* chown restricted */
  FALSE,                        /* case insensitivity */
  TRUE,                         /* case preserving */
  FSAL_EXPTYPE_PERSISTENT,      /* FH expire type */
  TRUE,                         /* hard link support */
  TRUE,                         /* symlink support */
  FALSE,                        /* lock management */
  FALSE,                        /* lock owners */
  FALSE,                        /* async blocking locks */
  TRUE,                         /* named attributes */
  TRUE,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  TRUE,                         /* can change times */
  TRUE,                         /* homogenous */
  POSIX_SUPPORTED_ATTRIBUTES,   /* supported attributes */
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
      else if(status.major == ERR_FSAL_DELAY)
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

      bythread_stat = (fsal_statistics_t *) Mem_Alloc(sizeof(fsal_statistics_t));

      if(bythread_stat == NULL)
        {
          LogError(COMPONENT_FSAL, ERR_SYS, ERR_MALLOC, Mem_Errno);
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
                                        posixfs_specific_initinfo_t * fs_specific_info)
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
  LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
           default_posix_info.lock_support_owner);
  LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
           default_posix_info.lock_support_async_block);
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
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_owner);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_async_block);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  /* setting global database parameters */
  global_posixdb_params = fs_specific_info->dbparams;

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes constant = 0x%llX.",
               POSIX_SUPPORTED_ATTRIBUTES);

  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes default = 0x%llX.",
               default_posix_info.supported_attrs);

  LogDebug(COMPONENT_FSAL,
           "FSAL INIT: Supported attributes mask = 0x%llX.",
           global_fs_info.supported_attrs);

  /* initialize database cache */
  if(fsal_posixdb_cache_init())
    ReturnCode(ERR_FSAL_FAULT, 0);

  LogDebug(COMPONENT_FSAL, "global_fs_info {");
  LogDebug(COMPONENT_FSAL, "  maxfilesize  = %llX    ",
           global_fs_info.maxfilesize);
  LogDebug(COMPONENT_FSAL, "  maxlink  = %lu   ", global_fs_info.maxlink);
  LogDebug(COMPONENT_FSAL, "  maxnamelen  = %lu  ",
           global_fs_info.maxnamelen);
  LogDebug(COMPONENT_FSAL, "  maxpathlen  = %lu  ",
           global_fs_info.maxpathlen);
  LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ", global_fs_info.no_trunc);
  LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
           global_fs_info.chown_restricted);
  LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
           global_fs_info.case_insensitive);
  LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
           global_fs_info.case_preserving);
  LogDebug(COMPONENT_FSAL, "  fh_expire_type  = %hu ",
           global_fs_info.fh_expire_type);
  LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
           global_fs_info.link_support);
  LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
           global_fs_info.symlink_support);
  LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
           global_fs_info.lock_support);
  LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
           global_fs_info.lock_support_owner);
  LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
           global_fs_info.lock_support_async_block);
  LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
           global_fs_info.named_attr);
  LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
           global_fs_info.unique_handles);
  LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
           global_fs_info.acl_support);
  LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
           global_fs_info.cansettime);
  LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
           global_fs_info.homogenous);
  LogDebug(COMPONENT_FSAL, "  supported_attrs  = %llX  ",
           global_fs_info.supported_attrs);
  LogDebug(COMPONENT_FSAL, "  maxread  = %llX     ",
           global_fs_info.maxread);
  LogDebug(COMPONENT_FSAL, "  maxwrite  = %llX     ",
           global_fs_info.maxwrite);
  LogDebug(COMPONENT_FSAL, "  umask  = %X ", global_fs_info.umask);
  LogDebug(COMPONENT_FSAL, "}");

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/** 
 * @brief convert 'struct stat' to 'fsal_posixdb_fileinfo_t'
 * 
 * @param buffstat 
 * @param info 
 * 
 * @return 
 */
fsal_status_t fsal_internal_posix2posixdb_fileinfo(struct stat *buffstat,
                                                   fsal_posixdb_fileinfo_t * info)
{
  /* sanity check */
  if(!info || !buffstat)
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset(info, 0, sizeof(fsal_posixdb_fileinfo_t));
  info->devid = buffstat->st_dev;
  info->inode = buffstat->st_ino;
  info->nlink = (int)buffstat->st_nlink;
  info->ctime = buffstat->st_ctime;
  info->ftype = posix2fsal_type(buffstat->st_mode);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/*
 *  This function adds an entry in the database
 */
fsal_status_t fsal_internal_posixdb_add_entry(fsal_posixdb_conn * p_conn,
                                              fsal_name_t * p_filename,
                                              fsal_posixdb_fileinfo_t * p_info,
                                              posixfsal_handle_t * p_dir_handle,
                                              posixfsal_handle_t * p_new_handle)
{
  fsal_posixdb_status_t stdb;

  if(!p_info || !p_conn || !p_new_handle)       /* p_filename & p_dir_handle can be NULL for the root */
    ReturnCode(ERR_FSAL_FAULT, 0);

 add:
  stdb = fsal_posixdb_add(p_conn, p_info, p_dir_handle, p_filename, p_new_handle);

  if(stdb.major == ERR_FSAL_POSIXDB_CONSISTENCY)
    {                           /* there is already an entry with this path, but it's an inconsistent one */
      stdb = fsal_posixdb_deleteHandle(p_conn, p_new_handle);
      if(FSAL_POSIXDB_IS_ERROR(stdb))
        return posixdb2fsal_error(stdb);
      goto add;
    }
  else if(FSAL_POSIXDB_IS_ERROR(stdb))
    {
      return posixdb2fsal_error(stdb);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/** 
 * @brief p_path <- p_path + '/' + p_name
 * 
 * @param p_path 
 * @param p_name 
 * 
 * @return 
 */
fsal_status_t fsal_internal_appendFSALNameToFSALPath(fsal_path_t * p_path,
                                                     fsal_name_t * p_name)
{
  char *end;
  if(!p_path || !p_name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  end = p_path->path + p_path->len - 1;
  if(*end != '/')
    {
      if(p_path->len + 1 + p_name->len > FSAL_MAX_PATH_LEN)
        ReturnCode(ERR_FSAL_NAMETOOLONG, 0);
      p_path->len += p_name->len + 1;
      end++;
      *end = '/';
      end++;
      strcpy(end, p_name->name);
    }
  else
    {
      if(p_path->len + p_name->len > FSAL_MAX_PATH_LEN)
        ReturnCode(ERR_FSAL_NAMETOOLONG, 0);
      p_path->len += p_name->len;
      end++;
      strcpy(end, p_name->name);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * Get a valid path associated to an handle.
 * The function selects many paths from the DB and return the first valid one. If is_dir is set, then only 1 path will be constructed from the database.
 */
fsal_status_t fsal_internal_getPathFromHandle(posixfsal_op_context_t * p_context,       /* IN */
                                              posixfsal_handle_t * p_handle,    /* IN */
                                              int is_dir,       /* IN */
                                              fsal_path_t * p_fsalpath, /* OUT */
                                              struct stat *p_buffstat /* OUT */ )
{
  fsal_status_t status;
  fsal_posixdb_status_t statusdb;
  int rc, errsv, count, i;
  fsal_posixdb_fileinfo_t infofs;
  fsal_path_t paths[global_fs_info.maxlink];

  if(!p_context || !p_handle || !p_fsalpath)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* if there is a path in the posixfsal_handle_t variable, then try to use it instead of querying the database for it */
  /* Read the path from the Handle. If it's valid & coherent, then no need to query the database ! */
  /* if !p_buffstat, we don't need to check the path */
  statusdb = fsal_posixdb_getInfoFromHandle(p_context->p_conn,
                                            p_handle,
                                            paths,
                                            (is_dir ? 1 : global_fs_info.maxlink),
                                            &count);
  if(FSAL_POSIXDB_IS_ERROR(statusdb)
     && FSAL_IS_ERROR(status = posixdb2fsal_error(statusdb)))
    return status;

  /* if !p_buffstat, then we do not stat the path to test if file is valid */
  if(p_buffstat)
    {
      for(i = 0; i < count; i++)
        {
          TakeTokenFSCall();
          rc = lstat(paths[i].path, p_buffstat);
          errsv = errno;
          ReleaseTokenFSCall();

          if(rc)
            {
              /* error : delete the bad path from the database */
              char dirc[FSAL_MAX_PATH_LEN];
              char basec[FSAL_MAX_PATH_LEN];
              fsal_path_t parentdir;
              fsal_name_t filename;
              posixfsal_handle_t parenthdl;
              char *dname, *bname;

              /* split /path/to/filename in /path/to & filename */
              strncpy(dirc, paths[i].path, FSAL_MAX_PATH_LEN);
              strncpy(basec, paths[i].path, FSAL_MAX_PATH_LEN);
              dname = dirname(dirc);
              bname = basename(basec);

              status = FSAL_str2path(dname, FSAL_MAX_PATH_LEN, &parentdir);
              status = FSAL_str2name(bname, FSAL_MAX_NAME_LEN, &filename);

              /* get the handle of /path/to */

              status = POSIXFSAL_lookupPath(&parentdir,
					    (fsal_op_context_t *)p_context,
					    (fsal_handle_t *)&parenthdl, NULL);

              if(!FSAL_IS_ERROR(status))
                {
                  statusdb =
                      fsal_posixdb_delete(p_context->p_conn, &parenthdl, &filename, NULL);
                  /* no need to check if there was an error, because it doesn't change the behavior of the function */
                }

            }
          else
            {                   /* no error */
              FSAL_pathcpy(p_fsalpath, &(paths[0]));
              break;
            }
        }
      if(i == count)
        ReturnCode(ERR_FSAL_STALE, 0);

      /* check consistency */
      status = fsal_internal_posix2posixdb_fileinfo(p_buffstat, &infofs);
      if(FSAL_IS_ERROR(status))
        return status;

      if(fsal_posixdb_consistency_check(&(p_handle->data.info), &infofs))
        {
          /* not consistent !! */
          /* delete the stale handle */
          statusdb = fsal_posixdb_deleteHandle(p_context->p_conn, p_handle);
          if(FSAL_POSIXDB_IS_ERROR(statusdb)
             && FSAL_IS_ERROR(status = posixdb2fsal_error(statusdb)))
            return status;

          ReturnCode(ERR_FSAL_STALE, 0);
        }

    }
  else
    {
      /* @TODO : check that there si at liste 1 path */
      FSAL_pathcpy(p_fsalpath, &(paths[0]));
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/** 
 * @brief Get the handle of a file, knowing its name and its parent dir
 * 
 * @param p_context 
 * @param p_parent_dir_handle 
 *    Handle of the parent directory
 * @param p_fsalname 
 *    Name of the object
 * @param p_infofs
 *    Information about the file (taken from the filesystem) to be compared to information stored in database
 * @param p_object_handle 
 *    Handle of the file.
 * 
 * @return
 *    ERR_FSAL_NOERR, if no error
 *    Anothere error code else.
 */
fsal_status_t fsal_internal_getInfoFromName(posixfsal_op_context_t * p_context, /* IN */
                                            posixfsal_handle_t * p_parent_dir_handle,   /* IN */
                                            fsal_name_t * p_fsalname,   /* IN */
                                            fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                            posixfsal_handle_t * p_object_handle)       /* OUT */
{
  fsal_posixdb_status_t stdb;
  fsal_status_t st;

  stdb = fsal_posixdb_getInfoFromName(p_context->p_conn,
                                      p_parent_dir_handle,
                                      p_fsalname, NULL, p_object_handle);
  switch (stdb.major)
    {
    case ERR_FSAL_POSIXDB_NOERR:
      /* No error, the object is in the database */
      /* check consistency */
      if(fsal_posixdb_consistency_check(&(p_object_handle->data.info), p_infofs))
        {
          /* Entry not consistent */
          /* Delete the Handle entry, then add a new one (with a Parent entry) */
          stdb = fsal_posixdb_deleteHandle(p_context->p_conn, p_object_handle);
          if(FSAL_POSIXDB_IS_ERROR(stdb) && FSAL_IS_ERROR(st = posixdb2fsal_error(stdb)))
            return st;
          /* don't break, add a new entry */
        }
      else
        {
          break;
        }
    case ERR_FSAL_POSIXDB_NOENT:
      /* object not in the database, add a new entry */
      st = fsal_internal_posixdb_add_entry(p_context->p_conn,
                                           p_fsalname,
                                           p_infofs,
                                           p_parent_dir_handle, p_object_handle);
      if(FSAL_IS_ERROR(st))
        return st;
      break;
    default:
      /* error */
      if(FSAL_IS_ERROR(st = posixdb2fsal_error(stdb)))
        return st;
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/** 
 * @brief Get the handle of a file from a fsal_posixdb_child array, knowing its name
 * 
 * @param p_context 
 * @param p_parent_dir_handle 
 *    Handle of the parent directory
 * @param p_fsalname 
 *    Name of the object
 * @param p_infofs
 *    Information about the file (taken from the filesystem) to be compared to information stored in database
 * @param p_children
 *    fsal_posixdb_child array (that contains the entries of the directory stored in the db)
 * @param p_object_handle 
 *    Handle of the file.
 * 
 * @return
 *    ERR_FSAL_NOERR, if no error
 *    Anothere error code else.
 */
fsal_status_t fsal_internal_getInfoFromChildrenList(posixfsal_op_context_t * p_context, /* IN */
                                                    posixfsal_handle_t * p_parent_dir_handle,   /* IN */
                                                    fsal_name_t * p_fsalname,   /* IN */
                                                    fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                                    fsal_posixdb_child * p_children,    /* IN */
                                                    unsigned int children_count,        /* IN */
                                                    posixfsal_handle_t * p_object_handle)       /* OUT */
{
  fsal_posixdb_status_t stdb;
  fsal_status_t st;
  int cmp = -1;                 /* when no children is available */
  unsigned int count;

  /* sanity check */
  if(!p_context || !p_parent_dir_handle || !p_fsalname || (!p_children && children_count)
     || !p_object_handle)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* check if the filename is in the list */
  for(count = 0; count < children_count; count++)
    {
      cmp = FSAL_namecmp(p_fsalname, &(p_children[count].name));
      if(!cmp)
        break;
      /* maybe a sorted list could give better result */
    }

  switch (cmp)
    {
    case 0:
      /* Entry found : check consistency */

      if(fsal_posixdb_consistency_check(&(p_children[count].handle.data.info), p_infofs))
        {
          /* Entry not consistent */
          /* Delete the Handle entry, then add a new one (with a Parent entry) */
          stdb =
              fsal_posixdb_deleteHandle(p_context->p_conn, &(p_children[count].handle));

          if(FSAL_POSIXDB_IS_ERROR(stdb) && FSAL_IS_ERROR(st = posixdb2fsal_error(stdb)))
            return st;

          /* don't break, add a new entry */

        }
      else
        {
          memcpy(p_object_handle, &(p_children[count].handle),
                 sizeof(posixfsal_handle_t));
          break;
        }

    default:
      /* not found ! Add it */
      st = fsal_internal_posixdb_add_entry(p_context->p_conn,
                                           p_fsalname,
                                           p_infofs,
                                           p_parent_dir_handle, p_object_handle);
      if(FSAL_IS_ERROR(st))
        return st;
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/*
   Check the access from an existing fsal_attrib_list_t or struct stat
*/
/* XXX : ACL */
fsal_status_t fsal_internal_testAccess(posixfsal_op_context_t * p_context,      /* IN */
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

  missing_access = FSAL_MODE_MASK(access_type); /* only modes, no ACLs here */

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

      if((missing_access & FSAL_OWNER_OK) != 0)
        missing_access = 0;

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        ReturnCode(ERR_FSAL_ACCESS, 0);
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

int fsal_internal_path2fsname(char *rpath, char *fs_spec)
{
  FILE *fp;
  struct mntent mnt;
  struct mntent *pmnt;
  char work[MAXPATHLEN];
  char mntdir[MAXPATHLEN];

  size_t pathlen, outlen;
  int rc = -1;

  pathlen = 0;
  outlen = 0;

  if(!rpath || !fs_spec)
    return -1;

  fp = setmntent(MOUNTED, "r");

  if(fp == NULL)
    return -1;

  while((pmnt = getmntent_r(fp, &mnt, work, MAXPATHLEN)) != NULL)
    {
      /* get the longer path that matches export path */
      if(mnt.mnt_dir != NULL)
        {
          pathlen = strlen(mnt.mnt_dir);

          if((pathlen > outlen) && !strcmp(mnt.mnt_dir, "/"))
            {
              outlen = pathlen;
              strncpy(mntdir, mnt.mnt_dir, MAXPATHLEN);
              strncpy(fs_spec, mnt.mnt_fsname, MAXPATHLEN);
            }
          /* in other cases, the filesystem must be <mountpoint>/<smthg> or <mountpoint>\0 */
          else if((pathlen > outlen) &&
                  !strncmp(rpath, mnt.mnt_dir, pathlen) &&
                  ((rpath[pathlen] == '/') || (rpath[pathlen] == '\0')))
            {
              /* LogFullDebug(COMPONENT_FSAL, "%s is under mountpoint %s, type=%s, fs=%s\n", 
                 rpath, mnt.mnt_dir, mnt.mnt_type, mnt.mnt_fsname); */

              outlen = pathlen;
              strncpy(mntdir, mnt.mnt_dir, MAXPATHLEN);
              strncpy(fs_spec, mnt.mnt_fsname, MAXPATHLEN);
              rc = 0;
            }
        }

    }

  endmntent(fp);
  return rc;
}                               /* fsal_internal_path2fsname */
