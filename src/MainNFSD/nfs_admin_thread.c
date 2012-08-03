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
 * \file    nfs_admin_thread.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/31 10:08:04 $
 * \version $Revision: 1.6 $
 * \brief   The file that contain the 'admin_thread' routine for the nfsd.
 *
 * nfs_admin_thread.c : The file that contain the 'admin_thread' routine for the nfsd.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "nfs_tcb.h"

#ifdef FIXME_EXPORT_RELOAD
struct glist_head temp_exportlist;
#endif
pthread_cond_t admin_condvar = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex_admin_condvar = PTHREAD_MUTEX_INITIALIZER;
bool_t reload_exports;

void nfs_Init_admin_data(void)
{
  return;
}

void admin_replace_exports()
{
#ifndef FIXME_EXPORT_RELOAD
  LogWarn(COMPONENT_MAIN,
          "Export reload is not supported");
#else
  P(mutex_admin_condvar);
  reload_exports = TRUE;
  if(pthread_cond_signal(&(admin_condvar)) == -1)
      LogCrit(COMPONENT_MAIN,
              "admin_replace_exports - admin cond signal failed , errno = %d (%s)",
              errno, strerror(errno));
  V(mutex_admin_condvar);
#endif
}

/* Skips deleting first entry of export list. */
int rebuild_export_list()
{
#ifndef FIXME_EXPORT_RELOAD
  return 0;
#else
  int status = 0;
  config_file_t config_struct;

  /* If no configuration file is given, then the caller must want to reparse the
   * configuration file from startup. */
  if(config_path[0] == '\0')
    {
      LogCrit(COMPONENT_CONFIG,
              "Error: No configuration file was specified for reloading exports.");
      return 0;
    }

  /* Attempt to parse the new configuration file */
  config_struct = config_ParseFile(config_path);
  if(!config_struct)
    {
      LogCrit(COMPONENT_CONFIG,
              "rebuild_export_list: Error while parsing new configuration file %s: %s",
              config_path, config_GetErrorMsg());
      return 0;
    }

  /* Create the new exports list */
  status = ReadExports(config_struct, &temp_exportlist);
  if(status < 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "rebuild_export_list: Error while parsing export entries");
      return status;
    }
  else if(status == 0)
    {
      LogWarn(COMPONENT_CONFIG,
              "rebuild_export_list: No export entries found in configuration file !!!");
      return 0;
    }

  /* At least one worker thread should exist. Each worker thread has a pointer to
   * the same hash table. */
  if(nfs_export_create_root_entry(&temp_exportlist) != TRUE)
    {
      LogCrit(COMPONENT_MAIN,
              "replace_exports: Error initializing Cache Inode root entries");
      return 0;
    }

  return 1;
#endif
}

static int ChangeoverExports()
{
#ifndef FIXME_EXPORT_RELOAD
  return ENOTSUP;
#else
  /* To be supported again, this routine will need to handle
   * comparing new exports to old exports. Any exports that
   * are just being updated, will need to be updated in place
   * to preserve references to them.
   *
   * Entries that are to be removed will need to be cleaned
   * up, SAL state revoked, maybe some cache_inode work to
   * free cache_inode entries and such.
   */
#endif
}

void *admin_thread(void *UnusedArg)
{
  SetNameFunction("admin_thr");

  while(1)
    {
      P(mutex_admin_condvar);
      while(reload_exports == FALSE)
            pthread_cond_wait(&(admin_condvar), &(mutex_admin_condvar));
      reload_exports = FALSE;
      V(mutex_admin_condvar);

      if (rebuild_export_list() <= 0)
        {
          LogCrit(COMPONENT_MAIN, "Could not reload the exports list.");
          continue;
        }

      if(pause_threads(PAUSE_RELOAD_EXPORTS) == PAUSE_EXIT)
        {
          LogDebug(COMPONENT_MAIN,
                   "Export reload interrupted by shutdown while pausing threads");
          /* Be helpfull and exit
           * (other termination will just blow us away, and that's ok...
           */
          break;
        }

      /* Clear the id mapping cache for gss principals to uid/gid.
       * The id mapping may have changed.
       */
#ifdef _HAVE_GSSAPI
#ifdef _USE_NFSIDMAP
      uidgidmap_clear();
      idmap_clear();
      namemap_clear();
#endif /* _USE_NFSIDMAP */
#endif /* _HAVE_GSSAPI */

      if (ChangeoverExports())
        {
          LogCrit(COMPONENT_MAIN, "ChangeoverExports failed.");
          continue;
        }

      LogEvent(COMPONENT_MAIN,
               "Exports reloaded and active");

      /* wake_workers could return PAUSE_PAUSE, but we don't have to do
       * anything special in that case.
       */
      if(wake_threads(AWAKEN_RELOAD_EXPORTS) == PAUSE_EXIT)
        {
          LogDebug(COMPONENT_MAIN,
                   "Export reload interrupted by shutdown while waking threads");
          /* Be helpfull and exit
           * (other termination will just blow us away, and that's ok...
           */
          break;
        }
    }

  return NULL;
}                               /* admin_thread */
