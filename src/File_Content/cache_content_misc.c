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
 * \file    cache_content_misc.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 07:29:11 $
 * \version $Revision: 1.14 $
 * \brief   Management of the file content cache: miscellaneous functions.
 *
 * cache_content_misc.c :  Management of the file content cache, miscellaneous functions.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#define NAME_MAX         255
#include <sys/statvfs.h>        /* For statfs */
#endif                          /* _SOLARIS */

#include "fsal.h"
#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"
#include "nfs_exports.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <libgen.h>

#ifdef _LINUX
#include <sys/vfs.h>            /* For statfs */
#endif

#ifdef _APPLE
#include <sys/param.h>          /* For Statfs */
#include <sys/mount.h>
#endif

unsigned int cache_content_dir_errno;

/* HashFileID4 : creates a 16bits hash of the 64bits fileid4 buffer.
 *
 * @param fileid4 [IN] 64bits fileid to be hashed.
 */
short HashFileID4(u_int64_t fileid4)
{
  int i;
  short hash_val = 0;

  for(i = 0; i <= 56; i += 8)
    {
#define ALPHABET_LEN      16
#define PRIME_16BITS   65521

      hash_val = (hash_val * ALPHABET_LEN + ((fileid4 >> i) & 0xFF)) % PRIME_16BITS;
    }

  return hash_val;
}

/**
 *
 * cache_content_create_name: Creates a name in the local fs for a cached entry.
 * 
 * Creates a name in the local fs for a cached entry
 * and creates the directories that whil contain this file.
 *
 * @param path [OUT] buffer to be used for storing the name.
 * @param type [IN] type of pathname to be created.
 * @param pentry_inode [OUT] Entry in Cache inode layer related to the file content entry.
 * @param pclient [IN] resources allocated for the file content client.
 * 
 * @return CACHE_CONTENT_SUCCESS if operation is a success, other values show an error.
 *
 */
cache_content_status_t cache_content_create_name(char *path,
                                                 cache_content_nametype_t type,
                                                 fsal_op_context_t * pcontext,
                                                 cache_entry_t * pentry_inode,
                                                 cache_content_client_t * pclient)
{
  fsal_status_t fsal_status;
  u_int64_t fileid4;            /* Don't want to include nfs_prot.h at this level */
  fsal_handle_t *pfsal_handle = NULL;
  cache_inode_status_t cache_status;
  char entrydir[MAXPATHLEN];
  int i, nb_char;
  short hash_val;

  if((pfsal_handle = cache_inode_get_fsal_handle(pentry_inode, &cache_status)) == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

      return CACHE_CONTENT_BAD_CACHE_INODE_ENTRY;
    }

  /* Get the digest for the handle, for computing an entry name */
  fsal_status = FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                  FSAL_DIGEST_FILEID4, pfsal_handle, (caddr_t) & fileid4);

  if(FSAL_IS_ERROR(fsal_status))
    return CACHE_CONTENT_FSAL_ERROR;

  /* computes a 16bits hash of the 64bits fileid4 buffer */
  hash_val = HashFileID4(fileid4);

  /* for limiting the number of entries into each datacache directory
   * we create 256 subdirectories on 2 levels, depending on the entry's fileid.
   */
  nb_char = snprintf(entrydir, MAXPATHLEN, "%s/export_id=%d", pclient->cache_dir, 0);

  for(i = 0; i <= 8; i += 8)
    {
      /* concatenation of hashval */
      nb_char += snprintf((char *)(entrydir + nb_char), MAXPATHLEN - nb_char,
                          "/%02hhX", (char)((hash_val >> i) & 0xFF));

      /* creating the directory if necessary */
      if((mkdir(entrydir, 0750) != 0) && (errno != EEXIST))
        {
          return CACHE_CONTENT_LOCAL_CACHE_ERROR;
        }
    }

  /* Create files for caching the entry: index file */
  switch (type)
    {
    case CACHE_CONTENT_DATA_FILE:
      snprintf(path, MAXPATHLEN, "%s/node=%llx.data", entrydir,
               (unsigned long long)fileid4);
      break;

    case CACHE_CONTENT_INDEX_FILE:
      snprintf(path, MAXPATHLEN, "%s/node=%llx.index", entrydir,
               (unsigned long long)fileid4);
      break;

    case CACHE_CONTENT_DIR:
      snprintf(path, MAXPATHLEN, "%s/export_id=%d", pclient->cache_dir, 0);
      break;

    default:
      return CACHE_CONTENT_INVALID_ARGUMENT;
    }

  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_create_name */

/**
 *
 * cache_content_get_export_id: gets an export id from an export dirname. 
 *
 * Gets an export id from an export dirname. 
 *
 * @param dirname [IN] The dirname for the export_id dirname.
 *
 * @return -1 if failed, the export_id if successfull.
 *
 */
int cache_content_get_export_id(char *dirname)
{
  int exportid;

  if(strncmp(dirname, "export_id=", strlen("export_id=")))
    return -1;

  if(sscanf(dirname, "export_id=%d", &exportid) == 0)
    return -1;
  else
    return exportid;
}                               /* cache_content_get_export_id */

/**
 *
 * cache_content_get_inum: gets an inode number fronm a cache filename.
 *
 * Gets an inode number fronm a cache filename.
 *
 * @param filename [IN] The filename to be parsed.
 *
 * @return 0 if failed, the inum if successfull.
 *
 */
u_int64_t cache_content_get_inum(char *filename)
{
  unsigned long long inum;
  char buff[MAXNAMLEN];
  char *bname = NULL;

  /* splits the dirent->d_name into path and filename */
  strncpy(buff, filename, MAXNAMLEN);
  bname = basename(buff);

  if(strncmp(bname, "node=", strlen("node=")))
    return 0;

  if(strncmp(bname + strlen(bname) - 5, "index", NAME_MAX))
    return 0;

  if(sscanf(bname, "node=%llx.index", &inum) == 0)
    return 0;
  else
    return (u_int64_t) inum;
}                               /* cache_content_get_inum */

/**
 *
 * cache_content_get_datapath :
 * recovers the path for a file of a specified inum. 
 *
 * @param basepath [IN] path to the root of the directory in the cache for the related export entry
 * @param inum     [IN] inode number for the file whose size is to be recovered. 
 * @param path     [OUT] the absolute path of the file (must be at least a MAXPATHLEN length string). 
 *
 * @return 0 if OK, or -1 is failed. 
 *
 */

int cache_content_get_datapath(char *basepath, u_int64_t inum, char *datapath)
{
  short hash_val;

  hash_val = HashFileID4(inum);

  snprintf(datapath, MAXPATHLEN, "%s/%02hhX/%02hhX/node=%llx.data", basepath,
           (char)((hash_val) & 0xFF),
           (char)((hash_val >> 8) & 0xFF), (unsigned long long)inum);

  LogFullDebug(COMPONENT_CACHE_CONTENT, "cache_content_get_datapath : datapath ----> %s",
                  datapath);

  /* it is ok, we now return 0 */
  return 0;
}                               /* cache_content_get_datapath */

/**
 *
 * cache_content_recover_size: recovers the size of a data cached file. 
 *
 * Recovers the size of a data cached file. 
 *
 * @param basepath [IN] path to the root of the directory in the cache for the related export entry
 * @param inum     [IN] inode number for the file whose size is to be recovered. 
 *
 * @return the recovered size (as a off_t) or -1 is failed. 
 *
 */

off_t cache_content_recover_size(char *basepath, u_int64_t inum)
{
  char path[MAXPATHLEN];
  struct stat buffstat;

  cache_content_get_datapath(basepath, inum, path);

  if(stat(path, &buffstat) != 0)
    {
      LogCrit(COMPONENT_CACHE_CONTENT,
          "Failure in cache_content_recover_size while stat on local cache: path=%s errno = %u",
           path, errno);

      return -1;
    }

  LogFullDebug(COMPONENT_CACHE_CONTENT, "path ----> %s %"PRIu64, path, buffstat.st_size);

  /* Stat is ok, we now return the size */
  return buffstat.st_size;
}                               /* cache_content_recover_size */

/**
 *
 * cache_content_get_cached_size: recovers the size of a data cached file.
 *
 * Recovers the size of a data cached file.
 *
 * @param pentry [IN] related pentry
 *
 * @return the recovered size (as a off_t) or -1 is failed.
 *
 */
off_t cache_content_get_cached_size(cache_content_entry_t * pentry)
{
  struct stat buffstat;

  if(stat(pentry->local_fs_entry.cache_path_data, &buffstat) != 0)
    {
      LogCrit(COMPONENT_CACHE_CONTENT,
          "Failure in cache_content_get_cached_size while stat on local cache: path=%s errno = %u",
           pentry->local_fs_entry.cache_path_index, errno);

      return -1;
    }

  /* Stat is ok, we now return the size */

  return buffstat.st_size;

}                               /* cache_content_get_cached_size */

/**
 *
 * cache_content_error_convert: Converts a cache_content_status to a cache_inode_status.
 *
 *  Converts a cache_content_status to a cache_inode_status.
 *
 * @param status [IN] File content status to be converted.
 *
 * @return a cache_inode_status_t resulting from the conversion.
 *
 */
cache_inode_status_t cache_content_error_convert(cache_content_status_t status)
{
  cache_inode_status_t converted_status;

  switch (status)
    {
    case CACHE_CONTENT_SUCCESS:
      converted_status = CACHE_INODE_SUCCESS;
      break;

    case CACHE_CONTENT_INVALID_ARGUMENT:
      converted_status = CACHE_INODE_INVALID_ARGUMENT;
      break;

    case CACHE_CONTENT_BAD_CACHE_INODE_ENTRY:
      converted_status = CACHE_INODE_INVALID_ARGUMENT;
      break;

    case CACHE_CONTENT_ENTRY_EXISTS:
      converted_status = CACHE_INODE_ENTRY_EXISTS;
      break;

    case CACHE_CONTENT_FSAL_ERROR:
      converted_status = CACHE_INODE_FSAL_ERROR;
      break;

    case CACHE_CONTENT_LOCAL_CACHE_ERROR:
      converted_status = CACHE_INODE_CACHE_CONTENT_ERROR;
      break;

    case CACHE_CONTENT_MALLOC_ERROR:
      converted_status = CACHE_INODE_MALLOC_ERROR;
      break;

    case CACHE_CONTENT_LRU_ERROR:
      converted_status = CACHE_INODE_LRU_ERROR;
      break;

    default:
      converted_status = CACHE_INODE_INVALID_ARGUMENT;
      break;
    }

  return converted_status;
}                               /* cache_content_error_convert */

/**
 *
 * cache_content_fsal_seek_convert: converts a fsal_seek_t to unix offet. 
 *
 * Converts a fsal_seek_t to unix offet. Non absolulte fsal_seek_t will produce an error. 
 *
 * @param seek [IN] FSAL Seek descriptor.
 * @param pstatus [OUT] pointer to the status. 
 *
 * @return the converted value.
 * 
 */
off_t cache_content_fsal_seek_convert(fsal_seek_t seek, cache_content_status_t * pstatus)
{
  off_t offset = 0;

  if(seek.whence != FSAL_SEEK_SET)
    *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
  else
    {
      *pstatus = CACHE_CONTENT_SUCCESS;
      offset = (off_t) seek.offset;
    }

  return offset;
}                               /* cache_content_fsal_seek_convert */

/**
 *
 * cache_content_fsal_size_convert: converts a fsal_size_t to unix size. 
 *
 * Converts a fsal_seek_t to unix size.
 *
 * @param seek [IN] FSAL Seek descriptor.
 * @param pstatus [OUT] pointer to the status. 
 *
 * @return the converted value.
 * 
 */
size_t cache_content_fsal_size_convert(fsal_size_t size, cache_content_status_t * pstatus)
{
  size_t taille;

  *pstatus = CACHE_CONTENT_SUCCESS;
  taille = (size_t) size;

  return taille;
}                               /* cache_content_fsal_size_convert */

/**
 *
 * cache_content_prepare_directories: do the mkdir to set the data cache directories
 *
 * do the mkdir to set the data cache directories.
 *
 * @param pexportlist [IN] export list
 * @param pstatus [OUT] pointer to the status. 
 *
 * @return the status for the operation
 * 
 */
cache_content_status_t cache_content_prepare_directories(exportlist_t * pexportlist,
                                                         char *cache_dir,
                                                         cache_content_status_t * pstatus)
{
  exportlist_t *pexport = NULL;
  char cache_sub_dir[MAXPATHLEN];

  /* Does the cache root directory exist ? */
  if(access(cache_dir, F_OK) == -1)
    {
      /* Create the cache root directory */
      if(mkdir(cache_dir, 0750) == -1)
        return CACHE_CONTENT_LOCAL_CACHE_ERROR;
    }

  /* Create the sub directories if needed */
  for(pexport = pexportlist; pexport != NULL; pexport = pexport->next)
    {
      /* Create a directory only if the export entry is to be datya cached */
      if(pexport->options & EXPORT_OPTION_USE_DATACACHE)
        {
          snprintf(cache_sub_dir, MAXPATHLEN, "%s/export_id=%d", cache_dir, 0);

          if(access(cache_sub_dir, F_OK) == -1)
            {
              /* Create the cache  directory */
              if(mkdir(cache_sub_dir, 0750) == -1)
                return CACHE_CONTENT_LOCAL_CACHE_ERROR;
            }
        }
    }

  /* If this point is reached, everything went ok */
  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_prepare_directories */

/**
 *
 * cache_content_valid: validates an entry to update its garbagge status.
 *
 * Validates an error to update its garbagge status.
 * Entry is supposed to be locked when this function is called !!
 *
 * @param pentry [INOUT] entry to be validated.
 * @param op [IN] can be set to CACHE_INODE_OP_GET or CACHE_INODE_OP_SET to show the type of operation done.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return CACHE_INODE_SUCCESS if successful \n
 * @return CACHE_INODE_LRU_ERROR if an errorr occured in LRU management.
 *
 */
cache_content_status_t cache_content_valid(cache_content_entry_t * pentry,
                                           cache_content_op_t op,
                                           cache_content_client_t * pclient)
{
  /* /!\ NOTE THIS CAREFULLY: entry is supposed to be locked when this function is called !! */

#ifndef _NO_BUDDY_SYSTEM
  buddy_stats_t __attribute__ ((__unused__)) bstats;
#endif

  if(pentry == NULL)
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* Update internal md */
  pentry->internal_md.valid_state = VALID;

  switch (op)
    {
    case CACHE_CONTENT_OP_GET:
      pentry->internal_md.read_time = time(NULL);
      break;

    case CACHE_CONTENT_OP_SET:
      pentry->internal_md.mod_time = time(NULL);
      pentry->internal_md.refresh_time = pentry->internal_md.mod_time;
      pentry->local_fs_entry.sync_state = FLUSH_NEEDED;
      break;

    case CACHE_CONTENT_OP_FLUSH:
      pentry->internal_md.mod_time = time(NULL);
      pentry->internal_md.refresh_time = pentry->internal_md.mod_time;
      pentry->local_fs_entry.sync_state = SYNC_OK;
      break;
    }

  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_valid */

/**
 *
 * cache_content_check_threshold: check datacache filesystem's threshold.
 *
 * @param datacache_path [IN] the datacache filesystem's path.
 * @param threshold_min [IN] the low watermark for the filesystem (in percent).
 * @param threshold_max [IN] the high watermark for the filesystem (in percent).
 * @param p_bool_overflow [OUT] boolean that indicates whether the FS
 *                      overcomes the high watermark.
 * @param p_blocks_to_lwm [OUT] if bool_overflow is set to true,
 *                      this indicates the number of blocks to be purged
 *                      in order to reach the low watermark.
 *
 * @return CACHE_CONTENT_SUCCESS if successful \n
 * @return CACHE_CONTENT_INVALID_ARGUMENT if some argument has an unexpected value\n 
 * @return CACHE_CONTENT_LOCAL_CACHE_ERROR if an error occured while
 *                    getting informations from datacache filesystem.
 */

cache_content_status_t cache_content_check_threshold(char *datacache_path,
                                                     unsigned int threshold_min,
                                                     unsigned int threshold_max,
                                                     int *p_bool_overflow,
                                                     unsigned long *p_blocks_to_lwm)
{
  char fspath[MAXPATHLEN];
#ifdef _SOLARIS
  struct statvfs info_fs;
#else
  struct statfs info_fs;
#endif
  unsigned long total_user_blocs, dispo_hw, dispo_lw;
  double tx_used, hw, lw;

  /* defensive checks */

  if(!datacache_path || !p_bool_overflow || !p_blocks_to_lwm
     || (threshold_min > threshold_max) || (threshold_max > 100))
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* cross mountpoint */

  snprintf(fspath, MAXPATHLEN, "%s/.", datacache_path);

  /* retieve FS info */

  if(statfs(fspath, &info_fs) != 0)
    {
      LogCrit(COMPONENT_CACHE_CONTENT, "Error getting local filesystem info: path=%s errno=%u", fspath,
                 errno);
      return CACHE_CONTENT_LOCAL_CACHE_ERROR;
    }

  /* Compute thresholds and total block count.
   * Those formulas are based on the df's code:
   * used = f_blocks - available_to_root
   *      = f_blocks - f_bfree
   * total = used + available
   *       = f_blocks - f_bfree + f_bavail
   */
  hw = (double)threshold_max;   /* cast to double */
  lw = (double)threshold_min;   /* cast to double */

  total_user_blocs = (info_fs.f_blocks + info_fs.f_bavail - info_fs.f_bfree);
  dispo_hw = (unsigned long)(((100.0 - hw) * total_user_blocs) / 100.0);
  dispo_lw = (unsigned long)(((100.0 - lw) * total_user_blocs) / 100.0);

  tx_used = 100.0 * ((double)info_fs.f_blocks - (double)info_fs.f_bfree) /
      ((double)info_fs.f_blocks + (double)info_fs.f_bavail - (double)info_fs.f_bfree);

  LogEvent(COMPONENT_CACHE_CONTENT,
                  "Datacache: %s: %.2f%% used, low_wm = %.2f%%, high_wm = %.2f%%",
                  datacache_path, tx_used, lw, hw);

  /* threshold test */

  /* if the threshold is under high watermark, nothing to do */

  if(tx_used < hw)
    {
      *p_bool_overflow = FALSE;
      *p_blocks_to_lwm = 0;
      LogEvent(COMPONENT_CACHE_CONTENT, "Datacache: no purge needed");
    }
  else
    {
      *p_bool_overflow = TRUE;
      *p_blocks_to_lwm = dispo_lw - info_fs.f_bavail;
      LogEvent(COMPONENT_CACHE_CONTENT,
                      "Datacache: need to purge %lu blocks for reaching low WM",
                      *p_blocks_to_lwm);
    }

  return CACHE_CONTENT_SUCCESS;

}

/**
 *
 * cache_content_local_cache_opendir: Open a local cache directory associated to an export entry.
 *
 * @param cache_dir  [IN] the path to the directory associated with the export entry
 * @param pdirectory [OUT] pointer to trhe openend directory 
 *
 * @return the handle to the directory or NULL is failed
 */
int cache_content_local_cache_opendir(char *cache_dir,
                                      cache_content_dirinfo_t * pdirectory)
{
  pdirectory->level0_dir = NULL;
  pdirectory->level1_dir = NULL;
  pdirectory->level2_dir = NULL;
  pdirectory->level1_cnt = 0;
  pdirectory->cookie0 = NULL;
  pdirectory->cookie1 = NULL;
  pdirectory->cookie2 = NULL;
  strcpy(pdirectory->level0_path, "");
  strcpy(pdirectory->level1_name, "");
  strcpy(pdirectory->level2_name, "");

  /* opens the top level directory */
  if((pdirectory->level0_dir = opendir(cache_dir)) == NULL)
    {
      cache_content_dir_errno = errno;
      return FALSE;
    }
  else
    {
      cache_content_dir_errno = 0;
      strncpy(pdirectory->level0_path, cache_dir, MAXPATHLEN);
    }

  pdirectory->level1_cnt = 0;
  return TRUE;
}                               /* cache_content_local_cache_opendir */

/**
 *
 * cache_content_test_cached: Tests if a given pentry_inode has already an associated data cache
 *
 * Tests if a given pentry_inode has already an associated data cache. This is useful to recover data from 
 * a data cache built by a former server instance.
 *
 * @param pentry_inode [IN] entry in cache_inode layer for this file.
 * @param pclient      [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext     [IN] the related FSAL Context
 * @pstatus           [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS if entry is found, CACHE_CONTENT_NOT_FOUND if not found
 */
cache_content_status_t cache_content_test_cached(cache_entry_t * pentry_inode,
                                                 cache_content_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_content_status_t * pstatus)
{
  char cache_path_index[MAXPATHLEN];

  if(pstatus == NULL)
    return CACHE_CONTENT_INVALID_ARGUMENT;

  if(pentry_inode == NULL || pclient == NULL || pcontext == NULL)
    {
      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Build the cache index path */
  if((*pstatus = cache_content_create_name(cache_path_index,
                                           CACHE_CONTENT_INDEX_FILE,
                                           pcontext,
                                           pentry_inode,
                                           pclient)) != CACHE_CONTENT_SUCCESS)
    {
      return *pstatus;
    }

  /* Check if the file exists */
  if(access(cache_path_index, F_OK) == 0)
    {
      /* File is accessible and exists */
      *pstatus = CACHE_CONTENT_SUCCESS;
      return CACHE_CONTENT_SUCCESS;
    }

  /* No access */
  *pstatus = CACHE_CONTENT_NOT_FOUND;
  return CACHE_CONTENT_NOT_FOUND;

}                               /* cache_content_test_cached */

/**
 *
 * cache_content_local_cache_dir_iter: iterate on a local cache directory to get the entry one by one
 *
 * @param directory  [IN] the directory to be read
 * @param index      [IN] thread index for multithreaded flushes (first has index 0)
 * @param mod        [IN] modulus for multithreaded flushes (number of threads)
 * @param pdir_entry [OUT] found dir_entry 
 *
 * @return TRUE if OK, FALSE if NOK.
 */
int cache_content_local_cache_dir_iter(cache_content_dirinfo_t * directory,
                                       struct dirent *pdir_entry,
                                       unsigned int index, unsigned int mod)
{
  int rc_readdir = 0;

  /* sanity check */
  if(directory == NULL || pdir_entry == NULL)
    {
      cache_content_dir_errno = EFAULT;
      return FALSE;
    }

  do
    {

      errno = 0;

      /* if the lowest level directory is not opened,
       * proceed a readdir, on the topper level directory,
       * and so on.
       */
      if(directory->level2_dir != NULL)
        {
          rc_readdir =
              readdir_r(directory->level2_dir, pdir_entry, &(directory->cookie2));

          if(rc_readdir == 0 && directory->cookie2 != NULL)
            {
              char d_name_save[MAXNAMLEN];

              /* go to the next loop if the entry is . or .. */
              if(!strcmp(".", pdir_entry->d_name) || !strcmp("..", pdir_entry->d_name))
                continue;

              LogFullDebug(COMPONENT_CACHE_CONTENT,"iterator --> %s/%s/%s/%s", directory->level0_path,
                     directory->level1_name, directory->level2_name, pdir_entry->d_name);

              /* the d_name must actually be the relative path from
               * the cache directory path, so that a file can be
               * accessed using <rootpath>/<d_name> path.
               */
              strncpy(d_name_save, pdir_entry->d_name, MAXNAMLEN);
              snprintf(pdir_entry->d_name, MAXNAMLEN, "%s/%s/%s",
                       directory->level1_name, directory->level2_name, d_name_save);

              return TRUE;
            }
          else
            {
              /* test if it is an error or an end of dir */
              if(errno != 0)
                {
                  cache_content_dir_errno = errno;
                  return TRUE;
                }
              else
                {
                  /* the lowest level entry dir is finished,
                   * must proceed a readdir on the topper level
                   */
                  closedir(directory->level2_dir);
                  directory->level2_dir = NULL;
                  /* go to next loop */
                }
            }
        }
      /* continue directory at level 1 */
      else if(directory->level1_dir != NULL)
        {
          if(mod <= 1)
            {
              /* list all dirs */
              rc_readdir =
                  readdir_r(directory->level1_dir, pdir_entry, &(directory->cookie1));
              directory->level1_cnt += 1;
            }
          else
            {
              rc_readdir =
                  readdir_r(directory->level1_dir, pdir_entry, &(directory->cookie1));
              directory->level1_cnt++;

              LogFullDebug(COMPONENT_CACHE_CONTENT,
                  "---> directory->level1_cnt=%u mod=%u index=%u modulocalcule=%u name=%s",
                   directory->level1_cnt, mod, index, directory->level1_cnt % mod,
                   pdir_entry->d_name);

              /* skip entry if  cnt % mod == index */
              if((directory->level1_cnt % mod != index))
                continue;
            }

          if(rc_readdir == 0 && directory->cookie1 != NULL)
            {
              char dirpath[MAXPATHLEN];

              /* go to the next loop if this is the . or .. entry */
              if(!strcmp(".", pdir_entry->d_name) || !strcmp("..", pdir_entry->d_name))
                continue;

              strncpy(directory->level2_name, pdir_entry->d_name, MAXNAMLEN);

              /* must now open the entry as the level2 directory */
              snprintf(dirpath, MAXPATHLEN, "%s/%s/%s",
                       directory->level0_path,
                       directory->level1_name, directory->level2_name);

              if((directory->level2_dir = opendir(dirpath)) == NULL)
                {
                  cache_content_dir_errno = errno;
                  return FALSE;
                }
            }
          else
            {
              /* test if it is an error or an end of dir */
              if(errno != 0)
                {
                  cache_content_dir_errno = errno;
                  return TRUE;
                }
              else
                {
                  /* the lowest level entry dir is finished,
                   * must proceed a readdir on the topper level
                   */
                  closedir(directory->level1_dir);
                  directory->level1_dir = NULL;
                }
            }
        }
      /* continue directory at level 0 */
      else if(directory->level0_dir != NULL)
        {

          rc_readdir =
              readdir_r(directory->level0_dir, pdir_entry, &(directory->cookie0));

          if(rc_readdir == 0 && directory->cookie0 != NULL)
            {
              char dirpath[MAXPATHLEN];

              /* go to the next loop if this is the . or .. entry */
              if(!strcmp(".", pdir_entry->d_name) || !strcmp("..", pdir_entry->d_name))
                continue;

              strncpy(directory->level1_name, pdir_entry->d_name, MAXNAMLEN);

              /* must now open the entry as the level1 directory */
              snprintf(dirpath, MAXPATHLEN, "%s/%s",
                       directory->level0_path, directory->level1_name);

              directory->level1_cnt = 0;

              if((directory->level1_dir = opendir(dirpath)) == NULL)
                {
                  cache_content_dir_errno = errno;
                  return TRUE;
                }
            }
          else
            {
              /* test if it is an error or an end of dir */
              if(errno != 0)
                {
                  cache_content_dir_errno = errno;
                  return TRUE;
                }
              else
                {
                  /* we are at the end of the level directory
                   * return End of Dir
                   */
                  cache_content_dir_errno = 0;
                  return FALSE;
                }
            }
        }
      else
        {
          /* invalid base directory descriptor */
          cache_content_dir_errno = EINVAL;
          return TRUE;
        }

    }
  while(1);

  cache_content_dir_errno = -1;
  /* should never happen */
  return TRUE;

}                               /* cache_content_local_cache_dir_iter */

/**
 *
 * cache_content_local_cache_closedir: Close a local cache directory associated to an export entry.
 *
 * @param directory[IN] the handle to the directory to be closed
 *
 * @return nothing (void function)
 */

void cache_content_local_cache_closedir(cache_content_dirinfo_t * directory)
{
  if(directory != NULL)
    {
      if(directory->level2_dir != NULL)
        {
          closedir(directory->level2_dir);
          directory->level2_dir = NULL;
        }

      if(directory->level1_dir != NULL)
        {
          closedir(directory->level1_dir);
          directory->level1_dir = NULL;
        }

      if(directory->level0_dir != NULL)
        {
          closedir(directory->level0_dir);
          directory->level0_dir = NULL;
        }
    }
}                               /* cache_content_local_cache_closedir */
