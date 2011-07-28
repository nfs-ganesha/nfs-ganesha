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
 * ---------------------------------------
 */

/**
 * \file    cache_content_emergency_flush.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   Emergency flush for forcing flush of data cached files to FSAL.

 *
 * cache_content_emergency_flush.c : Emergency flush for forcing flush of data cached files to FSAL.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#include <sys/statvfs.h>
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

#ifdef _LINUX
#include <sys/vfs.h>            /* For statfs */
#endif

#ifdef _APPLE
#include <sys/param.h>          /* For Statfs */
#include <sys/mount.h>
#endif

extern unsigned int cache_content_dir_errno;

/**
 *
 * cache_content_emergency_flush: Flushes the content of a file in the local cache to the FSAL data.
 *
 * Flushes the content of a file in the local cache to the FSAL data.
 * This routine should be called only from the cache_inode layer.
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is
 * locked and will prevent from concurent accesses.
 *
 * @param cachedir     [IN]    cachedir the filesystem where the cache resides
 * @param flushhow     [IN]    should we delete local files or not ?
 * @param lw_mark_trig [IN]    shpuld we purge until low water mark is reached ?
 * @param grace_period [IN]    grace_period The grace period for a file before being considered for deletion
 * @param p_nb_flushed [INOUT] current flushed count
 * @param p_nb_errors  [INOUT] current flush errors
 * @param p_nb_orphans [INOUT] current orphan files detected
 * @param pcontext     [INOUT] pcontext the FSAL context for this operation
 * @param pstatys      [OUT]   the status of the operation.
 *
 * @return CACHE_CONTENT_SUCCESS if successful, an error otherwise.
 *
 */

cache_content_status_t cache_content_emergency_flush(char *cachedir,
                                                     cache_content_flush_behaviour_t
                                                     flushhow,
                                                     unsigned int lw_mark_trigger_flag,
                                                     time_t grace_period,
                                                     unsigned int index, unsigned int mod,
                                                     unsigned int *p_nb_flushed,
                                                     unsigned int *p_nb_too_young,
                                                     unsigned int *p_nb_errors,
                                                     unsigned int *p_nb_orphans,
                                                     fsal_op_context_t * pcontext,
                                                     cache_content_status_t * pstatus)
{
  int rc;
  fsal_handle_t fsal_handle;
  fsal_status_t fsal_status;
  cache_content_dirinfo_t directory;
  struct dirent dir_entry;
  FILE *stream = NULL;
  char buff[CACHE_INODE_DUMP_LEN + 1];
  int inum;
  char indexpath[MAXPATHLEN];
  char datapath[MAXPATHLEN];
  fsal_path_t fsal_path;
  fsal_mdsize_t strsize = MAXPATHLEN + 1;
  struct stat buffstat;
  time_t max_acmtime = 0;
#ifdef _USE_PROXY
  fsal_u64_t fileid;
#endif
  cache_content_flush_behaviour_t local_flushhow = flushhow;
  unsigned int passcounter = 0;
#ifdef _SOLARIS
  struct statvfs info_fs;
#else
  struct statfs info_fs;
#endif
  unsigned long total_user_blocs;
  unsigned long dispo_hw;
  unsigned long dispo_lw;
  double tx_used;

  /* TODO: I'm not really sure how these work at all as I don't see an
     assignment later, just that these get used */
  double hw = 0;
  double lw = 0;

  *pstatus = CACHE_CONTENT_SUCCESS;

  if(cache_content_local_cache_opendir(cachedir, &directory) == FALSE)
    {
      LogCrit(COMPONENT_CACHE_CONTENT, "cache_content_emergency_flush can't open directory %s, errno=%u (%s)",
                 cachedir, cache_content_dir_errno, strerror(cache_content_dir_errno));
      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
      return *pstatus;
    }

  while(cache_content_local_cache_dir_iter(&directory, &dir_entry, index, mod))
    {
      if((lw_mark_trigger_flag == TRUE)
         && (local_flushhow == CACHE_CONTENT_FLUSH_AND_DELETE))
        {
          passcounter += 1;

          if(passcounter == 100)
            {
              passcounter = 0;

              if(statfs(cachedir, &info_fs) != 0)
                {
                  LogCrit(COMPONENT_CACHE_CONTENT,"Error getting local filesystem info: path=%s errno=%u",
                             cachedir, errno);
                  return CACHE_CONTENT_LOCAL_CACHE_ERROR;
                }
              /* Compute thresholds and total block count.
               * Those formulas are based on the df's code:
               * used = f_blocks - available_to_root
               *      = f_blocks - f_bfree
               * total = used + available
               *       = f_blocks - f_bfree + f_bavail
               */
              total_user_blocs = (info_fs.f_blocks + info_fs.f_bavail - info_fs.f_bfree);
              dispo_hw = (unsigned long)(((100.0 - hw) * total_user_blocs) / 100.0);
              dispo_lw = (unsigned long)(((100.0 - lw) * total_user_blocs) / 100.0);

              tx_used = 100.0 * ((double)info_fs.f_blocks - (double)info_fs.f_bfree) /
                  ((double)info_fs.f_blocks + (double)info_fs.f_bavail -
                   (double)info_fs.f_bfree);

              LogEvent(COMPONENT_CACHE_CONTENT,
                              "Datacache: %s: %.2f%% used, low_wm = %.2f%%, high_wm = %.2f%%",
                              cachedir, tx_used, lw, hw);

              if(tx_used < nfs_param.cache_layers_param.dcgcpol.lwmark_df)
                {
                  /* No need to purge more, downgrade to sync mode */
                  local_flushhow = CACHE_CONTENT_FLUSH_SYNC_ONLY;
                  LogEvent(COMPONENT_CACHE_CONTENT,
                                  "Datacache: Low Water is reached, I stop purging but continue on syncing");
                }
            }
        }

      /* Manage only index files */
      if(!strcmp(dir_entry.d_name + strlen(dir_entry.d_name) - 5, "index"))
        {
          if((inum = cache_content_get_inum(dir_entry.d_name)) == -1)
            {
              LogCrit(COMPONENT_CACHE_CONTENT, "Bad file name %s found in cache", dir_entry.d_name);
              continue;
            }

          /* read the content of the index file, for having the FSAL handle */

          snprintf(indexpath, MAXPATHLEN, "%s/%s", cachedir, dir_entry.d_name);

          if((stream = fopen(indexpath, "r")) == NULL)
            return CACHE_CONTENT_LOCAL_CACHE_ERROR;

          /* BUG: what happens if any of these fail? */
          #define XSTR(s) STR(s)
          #define STR(s) #s
          rc = fscanf(stream, "internal:read_time=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);
          rc = fscanf(stream, "internal:mod_time=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);
          rc = fscanf(stream, "internal:export_id=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);
          rc = fscanf(stream, "file: FSAL handle=%" XSTR(CACHE_INODE_DUMP_LEN) "s", buff);
          #undef STR
          #undef XSTR

          if(sscanHandle(&fsal_handle, buff) < 0)
            {
              /* expected = 2*sizeof(fsal_handle_t) in hexa representation */
              LogCrit(COMPONENT_CACHE_CONTENT,
                  "Invalid FSAL handle in index file %s: unexpected length %u (expected=%u)",
                   indexpath, (unsigned int)strlen(buff),
                   (unsigned int)(2 * sizeof(fsal_handle_t)));
              continue;
            }

          /* Now close the stream */
          fclose(stream);

          cache_content_get_datapath(cachedir, inum, datapath);

          /* Stat the data file to now if it is eligible or not */
          if(stat(datapath, &buffstat) == -1)
            {
              LogCrit(COMPONENT_CACHE_CONTENT,
                  "Can't stat file %s errno=%u(%s), continuing with next entries...",
                   datapath, errno, strerror(errno));
              continue;
            }

          /* Get the max into atime, mtime, ctime */
          max_acmtime = 0;

          if(buffstat.st_atime > max_acmtime)
            max_acmtime = buffstat.st_atime;
          if(buffstat.st_mtime > max_acmtime)
            max_acmtime = buffstat.st_mtime;
          if(buffstat.st_ctime > max_acmtime)
            max_acmtime = buffstat.st_ctime;

          LogFullDebug(COMPONENT_CACHE_CONTENT,
              "date=%d max_acmtime=%d ,time( NULL ) - max_acmtime = %d, grace_period = %d",
               (int)time(NULL), (int)max_acmtime, (int)(time(NULL) - max_acmtime), (int)grace_period);

          if(time(NULL) - max_acmtime < grace_period)
            {
              /* update stats, if provided */
              if(p_nb_too_young != NULL)
                *p_nb_too_young += 1;

              LogDebug(COMPONENT_CACHE_CONTENT, "File %s is too young to die, preserving it...", datapath);
              continue;
            }

          if(isFullDebug(COMPONENT_CACHE_CONTENT))
            {
              LogFullDebug(COMPONENT_CACHE_CONTENT, "=====> local=%s FSAL HANDLE=", datapath);
              print_buff(COMPONENT_CACHE_CONTENT, (char *)&fsal_handle, sizeof(fsal_handle));
            }

          fsal_status = FSAL_str2path(datapath, strsize, &fsal_path);
#if defined(  _USE_PROXY ) && defined( _BY_FILEID )
          fileid = cache_content_get_inum(dir_entry.d_name);

          LogFullDebug(COMPONENT_CACHE_CONTENT, "====> Fileid = %llu %llx", fileid, fileid);

          if(!FSAL_IS_ERROR(fsal_status))
            {
              fsal_status = FSAL_rcp_by_fileid(&fsal_handle,
                                               fileid,
                                               pcontext,
                                               &fsal_path, FSAL_RCP_LOCAL_TO_FS);
            }
#else
          if(!FSAL_IS_ERROR(fsal_status))
            {
              fsal_status = FSAL_rcp(&fsal_handle,
                                     pcontext, &fsal_path, FSAL_RCP_LOCAL_TO_FS);
            }
#endif

          if(FSAL_IS_ERROR(fsal_status))
            {
              if((fsal_status.major == ERR_FSAL_NOENT) ||
                 (fsal_status.major == ERR_FSAL_STALE))
                {
                  LogDebug(COMPONENT_CACHE_CONTENT,
                      "Cached entry %x doesn't exist anymore in FSAL, removing....",
                       inum);

                  /* update stats, if provided */
                  if(p_nb_orphans != NULL)
                    *p_nb_orphans += 1;

                  /* Remove the index file from the data cache */
                  if(unlink(indexpath))
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,"Can't unlink flushed index %s, errno=%u(%s)", indexpath,
                                 errno, strerror(errno));
                      return CACHE_CONTENT_LOCAL_CACHE_ERROR;
                    }

                  /* Remove the data file from the data cache */
                  if(unlink(datapath))
                    {
		      LogCrit(COMPONENT_CACHE_CONTENT,"Can't unlink flushed index %s, errno=%u(%s)", datapath,
                                 errno, strerror(errno));
                      return CACHE_CONTENT_LOCAL_CACHE_ERROR;
                    }
                }
              else
                {
                  /* update stats, if provided */
                  if(p_nb_errors != NULL)
                    *p_nb_errors += 1;

                  LogCrit(COMPONENT_CACHE_CONTENT,
                      "Can't flush file #%x, fsal_status.major=%u fsal_status.minor=%u",
                       inum, fsal_status.major, fsal_status.minor);
                }
            }
          else
            {
              /* success */
              /* update stats, if provided */
              if(p_nb_flushed != NULL)
                *p_nb_flushed += 1;

              switch (local_flushhow)
                {
                case CACHE_CONTENT_FLUSH_AND_DELETE:
                  /* Remove the index file from the data cache */
                  if(unlink(indexpath))
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,"Can't unlink flushed index %s, errno=%u(%s)", indexpath,
                                 errno, strerror(errno));
                      return CACHE_CONTENT_LOCAL_CACHE_ERROR;
                    }

                  /* Remove the data file from the data cache */
                  if(unlink(datapath))
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,"Can't unlink flushed index %s, errno=%u(%s)", datapath,
                                 errno, strerror(errno));
                      return CACHE_CONTENT_LOCAL_CACHE_ERROR;
                    }
                  break;

                case CACHE_CONTENT_FLUSH_SYNC_ONLY:
                  /* do nothing */
                  break;
                }               /* switch */
            }                   /* else */
        }                       /* if */
    }                           /* while */

  cache_content_local_cache_closedir(&directory);

  return *pstatus;
}                               /* cache_content_emergency_flush */
