/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file  nfs_admin_thread.c
 * @brief The admin_thread and support code.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "nfs_tools.h"
#include "log.h"
#include "sal_functions.h"
#include "sal_data.h"
#include "fsal_up.h"
#include "cache_inode_lru.h"
#ifdef USE_DBUS
#include "ganesha_dbus.h"
#endif

extern struct fridgethr *req_fridge; /*< Decoder thread pool */

static exportlist_t *temp_pexportlist;

/**
 * @brief Mutex protecting command and status
 */

static pthread_mutex_t admin_control_mtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Condition variable on which commands and states are
 * signalled.
 */

static pthread_cond_t admin_control_cv = PTHREAD_COND_INITIALIZER;

/**
 * @brief Commands issued to the admin thread
 */

typedef enum {
  admin_none_pending, /*< No command.  The admin thread sets this on
			  startup and after */
  admin_reload_exports, /*< Reload the exports */
  admin_shutdown /*< Shut down Ganesha */
} admin_command_t;

/**
 * @brief Current state of the admin thread
 */

typedef enum {
  admin_stable, /*< The admin thread is not performing an action. */
  admin_reloading, /*< The admin thread is reloading exports */
  admin_shutting_down, /*< The admin thread is shutting down Ganesha */
  admin_halted /*< All threads should exit. */
} admin_status_t;

static admin_command_t admin_command;
static admin_status_t admin_status;

#ifdef USE_DBUS

/**
 * @brief Dbus method for reloading configuration
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_reload(DBusMessageIter *args,
			      DBusMessage *reply)
{
	if (args != NULL) {
		LogWarn(COMPONENT_DBUS,
			"Replace exports take no arguments.");
		return false;
	}

	admin_replace_exports();

	return true;
}

static struct gsh_dbus_method method_reload = {
	.name = "reload",
	.method = admin_dbus_reload,
	.args = {{NULL, NULL, NULL}
	}
};

/**
 * @brief Dbus method for shutting down Ganesha
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_shutdown(DBusMessageIter *args,
				DBusMessage *reply)
{
	if (args != NULL) {
		LogWarn(COMPONENT_DBUS,
			"Shutdown takes no arguments.");
		return false;
	}

	admin_halt();

	return true;
}

static struct gsh_dbus_method method_shutdown = {
	.name = "shutdown",
	.method = admin_dbus_shutdown,
	.args = {{NULL, NULL, NULL}
	}
};

static struct gsh_dbus_method *admin_methods[] = {
	&method_shutdown,
	&method_reload,
	NULL
};

static struct gsh_dbus_interface admin_interface = {
	.name = "org.ganesha.nfsd.admin",
	.props = NULL,
	.methods = admin_methods,
	.signals = NULL
};

static struct gsh_dbus_interface *admin_interfaces[] = {
	&admin_interface,
	NULL
};

#endif /* USE_DBUS */

/**
 * @brief Initialize admin thread control state and DBUS methods.
 */

void nfs_Init_admin_thread(void)
{
	admin_command = admin_none_pending;
	admin_status = admin_stable;
#ifdef USE_DBUS
	gsh_dbus_register_path("admin", admin_interfaces);
#endif /* USE_DBUS */
	LogEvent(COMPONENT_NFS_CB, "Admin thread initialized");
}

static void admin_issue_command(admin_command_t command)
{
  pthread_mutex_lock(&admin_control_mtx);
  while ((admin_command != admin_none_pending) &&
	 ((admin_status != admin_stable) ||
	  (admin_status != admin_halted)))
    {
      pthread_cond_wait(&admin_control_cv, &admin_control_mtx);
    }
  if (admin_status == admin_halted)
    {
      pthread_mutex_unlock(&admin_control_mtx);
      return;
    }
  admin_command = command;
  pthread_cond_broadcast(&admin_control_cv);
  pthread_mutex_unlock(&admin_control_mtx);
}

/**
 * @brief Signal the admin thread to replace the exports
 */

void admin_replace_exports(void)
{
  admin_issue_command(admin_command);
}

/**
 * @brief Signal the admin thread to shut down the system
 */

void admin_halt(void)
{
  admin_issue_command(admin_shutdown);
}

/* Skips deleting first entry of export list. */
int rebuild_export_list(void)
{
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
  status = ReadExports(config_struct, &temp_pexportlist);
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
  if(!nfs_export_create_root_entry(temp_pexportlist))
    {
      LogCrit(COMPONENT_MAIN,
              "replace_exports: Error initializing Cache Inode root entries");
      return 0;
    }

  return 1;
}

static int ChangeoverExports()
{

  exportlist_t *pcurrent = NULL;

  /* Now we know that the configuration was parsed successfully.
   * And that worker threads are no longer accessing the export list.
   * Remove all but the first export entry in the exports list.
   */
  if (nfs_param.pexportlist)
    pcurrent = nfs_param.pexportlist->next;

  while(pcurrent != NULL)
    {
      /* Leave the head so that the list may be replaced later without
       * changing the reference pointer in worker threads. */

      if (pcurrent == nfs_param.pexportlist)
        break;

      nfs_param.pexportlist->next = RemoveExportEntry(pcurrent);
      pcurrent = nfs_param.pexportlist->next;
    }

  /* Allocate memory if needed, could have started with NULL exports */
  if (nfs_param.pexportlist == NULL)
    nfs_param.pexportlist = gsh_malloc(sizeof(exportlist_t));

  if (nfs_param.pexportlist == NULL)
    return ENOMEM;

  /* Changed the old export list head to the new export list head.
   * All references to the exports list should be up-to-date now. */
  memcpy(nfs_param.pexportlist, temp_pexportlist, sizeof(exportlist_t));

  /* We no longer need the head that was created for
   * the new list since the export list is built as a linked list. */
  gsh_free(temp_pexportlist);
  temp_pexportlist = NULL;
  return 0;
}

static void redo_exports(void)
{
  /**
   * @todo If we make this accessible by DBUS we should have a good
   * way of indicating error.
   */
  int rc = 0;

  if (rebuild_export_list() <= 0)
    {
      return;
    }

  /* Do these first, since they can call down into the FSAL and
     provoke callbacks themselves. */

  if (nfs_param.core_param.enable_FSAL_upcalls)
    {
      rc = fsal_up_pause();
      if (rc != 0)
	{
	  LogMajor(COMPONENT_THREAD,
		   "Error pausing upcall system: %d",
		   rc);
	}
      return;
    }

  rc = state_async_pause();
  if (rc != STATE_SUCCESS)
    {
      LogMajor(COMPONENT_THREAD,
	       "Error pausing async state thread: %d",
	       rc);
      return;
    }

  if (worker_pause() != 0)
    {
      LogMajor(COMPONENT_MAIN,
	       "Unable to pause workers.");
      return;
    }

  /* Clear the id mapping cache for gss principals to uid/gid.  The id
   * mapping may have changed.
   */
#ifdef _HAVE_GSSAPI
#ifdef USE_NFSIDMAP
  uidgidmap_clear();
  idmap_clear();
  namemap_clear();
#endif /* USE_NFSIDMAP */
#endif /* _HAVE_GSSAPI */

  if (ChangeoverExports())
    {
      LogCrit(COMPONENT_MAIN, "ChangeoverExports failed.");
      return;
    }

  if (worker_resume() != 0)
    {
      /* It's not as if there's anything you can do if this
	 happens... */
      LogFatal(COMPONENT_MAIN,
	       "Unable to resume workers.");
      return;
    }

  rc = state_async_resume();
  if (rc != STATE_SUCCESS)
    {
      LogFatal(COMPONENT_THREAD,
	       "Error resumeing down upcall system: %d",
	       rc);
    }

  if (nfs_param.core_param.enable_FSAL_upcalls)
    {
      rc = fsal_up_resume();
      if (rc != 0)
	{
	  LogMajor(COMPONENT_THREAD,
		   "Error resuming upcall system: %d",
		   rc);
	}
    }

  LogEvent(COMPONENT_MAIN,
	   "Exports reloaded and active");

}

static void do_shutdown(void)
{
  int rc = 0;

  LogEvent(COMPONENT_MAIN, "NFS EXIT: stopping NFS service");

  if (nfs_param.core_param.enable_FSAL_upcalls)
    {
      LogEvent(COMPONENT_MAIN,
	       "Stopping FSAL UPcall thread");
      rc = fsal_up_shutdown();
      if (rc != 0)
	{
	  LogMajor(COMPONENT_THREAD,
		   "Error shutting down upcall system: %d",
		   rc);
	}
      else
	{
	  LogEvent(COMPONENT_THREAD,
		   "Upcall system shut down.");
	}
    }

  LogEvent(COMPONENT_MAIN,
	   "Stopping state asynchronous request thread");
  rc = state_async_shutdown();
  if (rc != 0)
    {
      LogMajor(COMPONENT_THREAD,
	       "Error shutting down state asynchronous request system: %d",
	       rc);
    }
  else
    {
      LogEvent(COMPONENT_THREAD,
	       "State asynchronous request system shut down.");
    }

  LogEvent(COMPONENT_MAIN, "Stopping request listener threads.");
  nfs_rpc_dispatch_stop();

  LogEvent(COMPONENT_MAIN, "Stopping request decoder threads");
  rc = fridgethr_sync_command(req_fridge,
			      fridgethr_comm_stop,
			      120);

  if (rc == ETIMEDOUT)
    {
      LogMajor(COMPONENT_THREAD,
	       "Shutdown timed out, cancelling threads!");
      fridgethr_cancel(req_fridge);
    }
  else if (rc != 0)
    {
      LogMajor(COMPONENT_THREAD,
	       "Failed to shut down the request thread fridge: %d!",
	       rc);
    }
  else
    {
      LogEvent(COMPONENT_THREAD,
	       "Request threads shut down.");
    }

  LogEvent(COMPONENT_MAIN, "Stopping worker threads");

  rc = worker_shutdown();

  if(rc != 0)
    LogMajor(COMPONENT_THREAD,
	     "Unable to shut down worker threads: %d",
	     rc);
  else
    LogEvent(COMPONENT_THREAD,
	     "Worker threads successfully shut down.");

  rc = reaper_shutdown();
  if (rc != 0)
    {
      LogMajor(COMPONENT_THREAD,
	       "Error shutting down reaper thread: %d",
	       rc);
    }
  else
    {
      LogEvent(COMPONENT_THREAD,
	       "Reaper thread shut down.");
    }

  LogEvent(COMPONENT_MAIN,
	   "Stopping LRU thread.");
  rc = cache_inode_lru_pkgshutdown();
  if (rc != 0)
    {
      LogMajor(COMPONENT_THREAD,
	       "Error shutting down LRU thread: %d",
	       rc);
    }
  else
    {
      LogEvent(COMPONENT_THREAD,
	       "LRU thread system shut down.");
    }

  LogEvent(COMPONENT_MAIN,
	   "Destroying the inode cache.");
  cache_inode_destroyer();
  LogEvent(COMPONENT_MAIN,
	   "Inode cache destroyed.");

  LogEvent(COMPONENT_MAIN,
	   "Destroying the FSAL system.");
  destroy_fsals();
  LogEvent(COMPONENT_MAIN,
	   "FSAL system destroyed.");

  unlink(pidfile_path);
}

void *admin_thread(void *UnusedArg)
{
  SetNameFunction("Admin");

  pthread_mutex_lock(&admin_control_mtx);
  while (admin_command != admin_shutdown)
    {
      /* If we add more commands we can expand this into a
	 switch/case... */
      if (admin_command == admin_reload_exports)
	{
	  admin_command = admin_none_pending;
	  admin_status = admin_reloading;
	  pthread_cond_broadcast(&admin_control_cv);
	  pthread_mutex_unlock(&admin_control_mtx);
	  redo_exports();
	  pthread_mutex_lock(&admin_control_mtx);
	  admin_status = admin_stable;
	  pthread_cond_broadcast(&admin_control_cv);
	}
      if (admin_command != admin_none_pending) {
	continue;
      }
      pthread_cond_wait(&admin_control_cv, &admin_control_mtx);
    }

  admin_command = admin_none_pending;
  admin_status = admin_shutting_down;
  pthread_cond_broadcast(&admin_control_cv);
  pthread_mutex_unlock(&admin_control_mtx);
  do_shutdown();
  pthread_mutex_lock(&admin_control_mtx);
  admin_status = admin_halted;
  pthread_cond_broadcast(&admin_control_cv);
  pthread_mutex_unlock(&admin_control_mtx);

  return NULL;
}                               /* admin_thread */
