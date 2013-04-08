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
 *
 * All the display functions and error handling.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>             /* for malloc */
#include <ctype.h>              /* for isdigit */
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#include <execinfo.h>
#include <sys/resource.h>

#include "log.h"
#include "rpc/rpc.h"
#include "common_utils.h"
#include "abstract_mem.h"
#include "nfs_core.h"

pthread_rwlock_t log_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* Variables to control log fields */

/**
 * @brief Define an index each of the log fields that are configurable.
 *
 * Ganesha log messages have several "header" fields used in every
 * message. Some of those fields may be configured (mostly display or
 * not display).
 *
 */
enum log_flag_index_t
{
  LF_DATE, /*< Date field. */
  LF_TIME, /*< Time field. */
  LF_EPOCH, /*< Server Epoch field (distinguishes server instance. */
  LF_HOSTAME, /*< Server host name field. */
  LF_PROGNAME, /*< Ganesha program name field. */
  LF_PID, /*< Ganesha process identifier. */
  LF_THREAD_NAME, /*< Name of active thread logging message. */
  LF_FILE_NAME, /*< Source file name message occured in. */
  LF_LINE_NUM, /*< Source line number message occurred in. */
  LF_FUNCTION_NAME, /*< Function name message occurred in. */
  LF_COMPONENT, /*< Log component. */
  LF_LEVEL, /*< Log level. */
};

/**
 * @brief Description of a flag tha controls a log field.
 *
 */
struct log_flag
{
  int      lf_idx;  /*< The log field index this flag controls. */
  bool_t   lf_val;  /*< True/False value for the flag. */
  int      lf_ext;  /*< Extended value for the flag, if it has one. */
  char   * lf_name; /*< Name of the flag. */
};

/**
 * @brief Define a set of possible time and date formats.
 *
 * These values will be stored in lf_ext for the LF_DATE and LF_TIME flags.
 *
 */
enum timedate_formats_t
{
  TD_NONE, /*< No time/date. */
  TD_GANESHA, /*< Legacy Ganesha time and date format. */
  TD_LOCAL, /*< Use strftime local format for time/date. */
  TD_8601, /*< Use ISO 8601 time/date format. */
  TD_SYSLOG, /*< Use a typical syslog time/date format. */
  TD_SYSLOG_USEC, /*< Use a typical syslog time/date format that also includes
                      microseconds. */
  TD_USER, /* Use a user defined time/date format. */
};

/* Define the maximum length of a user time/date format. */
#define MAX_TD_USER_LEN 64
/* Define the maximum overall time/date format length, should have room for both
 * user date and user time format plus room for blanks around them.
 */
#define MAX_TD_FMT_LEN (MAX_TD_USER_LEN * 2 + 4)

/**
 * @brief Actually define the flag variables with their default values.
 *
 * Flags that don't have an extended value will have lf_ext initialized to 0.
 *
 */
struct log_flag tab_log_flag[] =
{
/*                          Extended
 * Index             Flag   Value       Name
 */
  {LF_DATE,          TRUE,  TD_GANESHA, "DATE"},
  {LF_TIME,          TRUE,  TD_GANESHA, "TIME"},
  {LF_EPOCH,         TRUE,  0,          "EPOCH"},
  {LF_HOSTAME,       TRUE,  0,          "HOSTAME"},
  {LF_PROGNAME,      TRUE,  0,          "PROGNAME"},
  {LF_PID,           TRUE,  0,          "PID"},
  {LF_THREAD_NAME,   TRUE,  0,          "THREAD_NAME"},
  {LF_FILE_NAME,     FALSE, 0,          "FILE_NAME"},
  {LF_LINE_NUM,      FALSE, 0,          "LINE_NUM"},
  {LF_FUNCTION_NAME, TRUE,  0,          "FUNCTION_NAME"},
  {LF_COMPONENT,     TRUE,  0,          "COMPONENT"},
  {LF_LEVEL,         TRUE,  0,          "LEVEL"},
};

static int log_to_syslog(struct log_facility   * facility,
                         log_levels_t            level,
                         struct display_buffer * buffer,
                         char                  * compstr,
                         char                  * message);

static int log_to_file(struct log_facility   * facility,
                       log_levels_t            level,
                       struct display_buffer * buffer,
                       char                  * compstr,
                       char                  * message);

static int log_to_stream(struct log_facility   * facility,
                         log_levels_t            level,
                         struct display_buffer * buffer,
                         char                  * compstr,
                         char                  * message);

/**
 * @brief Define the standard log facilities.
 *
 */
struct log_facility facilities[] =
{
  {{NULL, NULL}, {NULL, NULL},
   "SYSLOG", NIV_FULL_DEBUG,
   LH_COMPONENT, log_to_syslog, NULL},
  {{NULL, NULL}, {NULL, NULL},
   "FILE", NIV_FULL_DEBUG,
   LH_ALL, log_to_file, "/var/log/ganesha"},
  {{NULL, NULL}, {NULL, NULL},
   "STDERR", NIV_FULL_DEBUG,
   LH_ALL, log_to_stream, NULL},
  {{NULL, NULL}, {NULL, NULL},
   "STDOUT", NIV_FULL_DEBUG,
   LH_ALL, log_to_stream, NULL},
  {{NULL, NULL}, {NULL, NULL},
   "TEST", NIV_FULL_DEBUG,
   LH_NONE, log_to_stream, NULL},
};

struct glist_head facility_list;
struct glist_head active_facility_list;

struct log_facility * default_facility = &facilities[SYSLOG];

log_header_t max_headers = LH_COMPONENT;

/**
 *
 * @brief Finds a log facility by name
 *
 * @param[in]  name The name of the facility to be found
 *
 * @retval NULL No facility by that name
 * @retval non-NULL Pointer to the facility structure
 *
 */
struct log_facility * find_log_facility(const char * name)
{
  struct glist_head   * glist;
  struct log_facility * facility;

  glist_for_each(glist, &facility_list)
    {
      facility = glist_entry(glist, struct log_facility, lf_list);
      if(!strcasecmp(name, facility->lf_name))
        return facility;
    }

  return NULL;
}

/**
 *
 * @brief Test if facility is active
 *
 * @param[in]  facility The facility to test
 *
 * @retval 1 if active
 * @retval 0 if not active
 *
 */
static inline int is_facility_active(struct log_facility * facility)
{
  return !glist_null(&facility->lf_active);
}

/**
 *
 * @brief Test if facility is registered
 *
 * @param[in]  facility The facility to test
 *
 * @retval 1 if registered
 * @retval 0 if not registered
 *
 */
static inline int is_facility_registered(struct log_facility * facility)
{
  return !glist_null(&facility->lf_list);
}

/**
 *
 * @brief Deactivate a facility
 *
 * Caller must hold log_rwlock in write mode.
 *
 * @param[in]  facility The facility to deactivate
 *
 */
void _deactivate_log_facility(struct log_facility * facility)
{
  if(!is_facility_active(facility))
    return;

  glist_del(&facility->lf_active);

  /* If this facility needed the headers, we need to walk the remaining
   * facilities to determine if any still need headers.
   */
  if(facility->lf_headers == max_headers)
    {
      struct glist_head   * glist;
      struct log_facility * found;

      max_headers = LH_NONE;

      glist_for_each(glist, &active_facility_list)
        {
          found = glist_entry(glist, struct log_facility, lf_active);

          if(found->lf_headers > max_headers)
            max_headers = found->lf_headers;
        }
    }
}

/**
 *
 * @brief Deactivate a facility
 *
 * Takes log_rwlock to be held for write.
 *
 * @param[in]  facility The facility to deactivate
 *
 */
void deactivate_log_facility(struct log_facility * facility)
{
  pthread_rwlock_wrlock(&log_rwlock);
  _deactivate_log_facility(facility);
  pthread_rwlock_unlock(&log_rwlock);
}

/**
 *
 * @brief Activate a facility
 *
 * Requires log_rwlock for write.
 *
 * @param[in]  facility The facility to activate
 *
 */
void _activate_log_facility(struct log_facility * facility)
{
  if(!is_facility_active(facility))
    {
      glist_add_tail(&active_facility_list, &facility->lf_active);

      if(facility->lf_headers > max_headers)
        max_headers = facility->lf_headers;
    }
}

/**
 *
 * @brief Activate a facility
 *
 * Takes log_rwlock for write.
 *
 * @param[in]  facility The facility to activate
 *
 */
void activate_log_facility(struct log_facility * facility)
{
  pthread_rwlock_wrlock(&log_rwlock);
  _activate_log_facility(facility);
  pthread_rwlock_unlock(&log_rwlock);
}

/**
 *
 * @brief Register an additional log facility
 *
 * This function allows registering additional log facilities. The facility
 * may already have been referenced by the configuration by using the FACILITY
 * key to let the config know the facility is available, and the name of the
 * facility to provide a max log level for this facility (thus making the
 * facility active). The effects of the configuration will be transferred to
 * the newly registered facility.
 *
 * @param[in]  facility The facility to register.
 *
 * @retval -1 failure
 * @retval 0  success
 *
 */
int register_log_facility(struct log_facility * facility)
{
  struct log_facility * existing;

  pthread_rwlock_wrlock(&log_rwlock);

  existing = find_log_facility(facility->lf_name);

  if(existing != NULL)
    {
      /* Only allow re-registration of a facility created with FACILITY config
       * key.
       */
      if(existing->lf_func != NULL)
        {
          pthread_rwlock_unlock(&log_rwlock);

          LogMajor(COMPONENT_LOG,
                   "Attempt to re-register log facility %s",
                   existing->lf_name);

          return -1;
        }

      /* Copy the max logging level threshold from the existing facility into
       * the registered facility.
       */
      facility->lf_max_level = existing->lf_max_level;

      /* If existing facility was active, deactivate it and activate registered
       * facility.
       */
      if(is_facility_active(existing))
        {
          _deactivate_log_facility(existing);
          _activate_log_facility(facility);
        }

      /* Finally, remove existing facility from list and free memory. */
      glist_del(&existing->lf_list);
      gsh_free(existing->lf_name);
      gsh_free(existing);
      existing = NULL;
    }

  glist_add_tail(&facility_list, &facility->lf_list);

  pthread_rwlock_unlock(&log_rwlock);

  LogInfo(COMPONENT_LOG,
          "Registered log facility %s",
          facility->lf_name);

  return 0;
}

/**
 *
 * @brief Unregister an additional log facility
 *
 * This function allows unregistering additional log facilities. 
 *
 * @param[in]  facility The facility to unregister.
 *
 * @retval -1 failure
 * @retval 0  success
 *
 */
int unregister_log_facility(struct log_facility * facility)
{
  struct log_facility * existing;

  pthread_rwlock_wrlock(&log_rwlock);

  existing = find_log_facility(facility->lf_name);
  
  if(existing != facility)
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Invalid attempt to un-register log facility %s",
               existing->lf_name);

      return -1;
    }
  else if(!is_facility_registered(existing))
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Log facility %s is not registered",
               existing->lf_name);

      return -1;
    }

  /* Remove the facility from the list of registered facilities. */
  glist_del(&existing->lf_list);

  pthread_rwlock_unlock(&log_rwlock);

  LogInfo(COMPONENT_LOG,
          "Unregistered log facility %s",
          facility->lf_name);

  return 0;
}

/**
 *
 * @brief Create a dummy facility.
 *
 * This function is used when the FACILITY keyword is found in the config file.
 * It creates a facility with no logging function that serves as a place holder
 * to have a max log level specified and activating or deactivating the facility.
 *
 * @param[in]  name The name of the facility to create.
 *
 * @retval NULL failure
 * @retval non-NULL the facility created (or the real facility if it already exists)
 *
 */
struct log_facility * create_null_facility(const char * name)
{
  struct log_facility * facility;

  pthread_rwlock_wrlock(&log_rwlock);

  facility = find_log_facility(name);

  if(facility != NULL)
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogInfo(COMPONENT_LOG,
               "Facility %s already exists",
               name);

      return facility;
    }

  facility = gsh_calloc(1, sizeof(*facility));

  if(facility == NULL)
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Can not allocate a facility for %s",
               name);

      return NULL;
    }

  facility->lf_name = gsh_strdup(name);

  if(facility->lf_name == NULL)
    {
      gsh_free(facility);

      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Can not allocate a facility name for %s",
               name);

      return NULL;
    }

  glist_add_tail(&facility_list, &facility->lf_list);

  pthread_rwlock_unlock(&log_rwlock);

  LogInfo(COMPONENT_LOG,
          "Registered NULL log facility %s",
          facility->lf_name);

  return facility;
}

char const_log_str[LOG_BUFF_LEN];
char date_time_fmt[MAX_TD_FMT_LEN];
char user_date_fmt[MAX_TD_USER_LEN];
char user_time_fmt[MAX_TD_USER_LEN];

/* La longueur d'une chaine */
#define MAX_NUM_FAMILY  50
#define UNUSED_SLOT -1

log_level_t tabLogLevel[] =
{
  {NIV_NULL,       "NIV_NULL",       "NULL",   LOG_NOTICE},
  {NIV_FATAL,      "NIV_FATAL",      "FATAL",  LOG_CRIT},
  {NIV_MAJ,        "NIV_MAJ",        "MAJ",    LOG_CRIT},
  {NIV_CRIT,       "NIV_CRIT",       "CRIT",   LOG_ERR},
  {NIV_WARN,       "NIV_WARN",       "WARN",   LOG_WARNING},
  {NIV_EVENT,      "NIV_EVENT",      "EVENT",  LOG_NOTICE},
  {NIV_INFO,       "NIV_INFO",       "INFO",   LOG_INFO},
  {NIV_DEBUG,      "NIV_DEBUG",      "DEBUG",  LOG_DEBUG},
  {NIV_MID_DEBUG,  "NIV_MID_DEBUG",  "MIDDBG", LOG_DEBUG},
  {NIV_FULL_DEBUG, "NIV_FULL_DEBUG", "FULDBG", LOG_DEBUG}
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

/* Error codes */
errctx_t __attribute__ ((__unused__)) tab_system_err[] =
{
  {SUCCES, "SUCCES", "No Error"},
  {ERR_FAILURE, "FAILURE", "Error occurred"},
  {ERR_EVNT, "EVNT", "Event occurred"},
  {ERR_PTHREAD_KEY_CREATE, "ERR_PTHREAD_KEY_CREATE", "Error in creation of pthread_keys"},
  {ERR_MALLOC, "ERR_MALLOC", "malloc failed"},
  {ERR_SIGACTION, "ERR_SIGACTION", "sigaction failed"},
  {ERR_PTHREAD_ONCE, "ERR_PTHREAD_ONCE", "pthread_once failed"},
  {ERR_FILE_LOG, "ERR_FILE_LOG", "failed to access the log"},
  {ERR_GETHOSTBYNAME, "ERR_GETHOSTBYNAME", "gethostbyname failed"},
  {ERR_MMAP, "ERR_MMAP", "mmap failed"},
  {ERR_SOCKET, "ERR_SOCKET", "socket failed"},
  {ERR_BIND, "ERR_BIND", "bind failed"},
  {ERR_CONNECT, "ERR_CONNECT", "connect failed"},
  {ERR_LISTEN, "ERR_LISTEN", "listen failed"},
  {ERR_ACCEPT, "ERR_ACCEPT", "accept failed"},
  {ERR_RRESVPORT, "ERR_RRESVPORT", "rresvport failed"},
  {ERR_GETHOSTNAME, "ERR_GETHOSTNAME", "gethostname failed"},
  {ERR_GETSOCKNAME, "ERR_GETSOCKNAME", "getsockname failed"},
  {ERR_IOCTL, "ERR_IOCTL", "ioctl failed"},
  {ERR_UTIME, "ERR_UTIME", "utime failed"},
  {ERR_XDR, "ERR_XDR", "An XDR call failed"},
  {ERR_CHMOD, "ERR_CHMOD", "chmod failed"},
  {ERR_SEND, "ERR_SEND", "send failed"},
  {ERR_GETHOSTBYADDR, "ERR_GETHOSTBYADDR", "gethostbyaddr failed"},
  {ERR_PREAD, "ERR_PREAD", "pread failed"},
  {ERR_PWRITE, "ERR_PWRITE", "pwrite failed"},
  {ERR_STAT, "ERR_STAT", "stat failed"},
  {ERR_GETPEERNAME, "ERR_GETPEERNAME", "getpeername failed"},
  {ERR_FORK, "ERR_FORK", "fork failed"},
  {ERR_GETSERVBYNAME, "ERR_GETSERVBYNAME", "getservbyname failed"},
  {ERR_MUNMAP, "ERR_MUNMAP", "munmap failed"},
  {ERR_STATVFS, "ERR_STATVFS", "statvfs failed"},
  {ERR_OPENDIR, "ERR_OPENDIR", "opendir failed"},
  {ERR_READDIR, "ERR_READDIR", "readdir failed"},
  {ERR_CLOSEDIR, "ERR_CLOSEDIR", "closedir failed"},
  {ERR_LSTAT, "ERR_LSTAT", "lstat failed"},
  {ERR_GETWD, "ERR_GETWD", "getwd failed"},
  {ERR_CHDIR, "ERR_CHDIR", "chdir failed"},
  {ERR_CHOWN, "ERR_CHOWN", "chown failed"},
  {ERR_MKDIR, "ERR_MKDIR", "mkdir failed"},
  {ERR_OPEN, "ERR_OPEN", "open failed"},
  {ERR_READ, "ERR_READ", "read failed"},
  {ERR_WRITE, "ERR_WRITE", "write failed"},
  {ERR_UTIMES, "ERR_UTIMES", "utimes failed"},
  {ERR_READLINK, "ERR_READLINK", "readlink failed"},
  {ERR_SYMLINK, "ERR_SYMLINK", "symlink failed"},
  {ERR_SYSTEM, "ERR_SYSTEM", "system failed"},
  {ERR_POPEN, "ERR_POPEN", "popen failed"},
  {ERR_LSEEK, "ERR_LSEEK", "lseek failed"},
  {ERR_PTHREAD_CREATE, "ERR_PTHREAD_CREATE", "pthread_create failed"},
  {ERR_RECV, "ERR_RECV", "recv failed"},
  {ERR_FOPEN, "ERR_FOPEN", "fopen failed"},
  {ERR_GETCWD, "ERR_GETCWD", "getcwd failed"},
  {ERR_SETUID, "ERR_SETUID", "setuid failed"},
  {ERR_RENAME, "ERR_RENAME", "rename failed"},
  {ERR_UNLINK, "ERR_UNLINK", "unlink failed"},
  {ERR_SELECT, "ERR_SELECT", "select failed"},
  {ERR_WAIT, "ERR_WAIT", "wait failed"},
  {ERR_SETSID, "ERR_SETSID", "setsid failed"},
  {ERR_SETGID, "ERR_SETGID", "setgid failed"},
  {ERR_GETGROUPS, "ERR_GETGROUPS", "getgroups failed"},
  {ERR_SETGROUPS, "ERR_SETGROUPS", "setgroups failed"},
  {ERR_UMASK, "ERR_UMASK", "umask failed"},
  {ERR_CREAT, "ERR_CREAT", "creat failed"},
  {ERR_SETSOCKOPT, "ERR_SETSOCKOPT", "setsockopt failed"},
  {ERR_DIRECTIO, "ERR_DIRECTIO", "directio failed"},
  {ERR_GETRLIMIT, "ERR_GETRLIMIT", "getrlimit failed"},
  {ERR_SETRLIMIT, "ERR_SETRLIMIT", "setrlimit"},
  {ERR_TRUNCATE, "ERR_TRUNCATE", "truncate failed"},
  {ERR_PTHREAD_MUTEX_INIT, "ERR_PTHREAD_MUTEX_INIT", "pthread mutex initialization failed."},
  {ERR_PTHREAD_COND_INIT, "ERR_PTHREAD_COND_INIT", "pthread condition initialization failed."},
  {ERR_FCNTL, "ERR_FCNTL", "call to fcntl is failed"},
  {ERR_NULL, "ERR_NULL", ""}
};

/* constants */
static int log_mask = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

/* Array of error families */

static family_t tab_family[MAX_NUM_FAMILY];

/* Global variables */

static char program_name[1024];
static char hostname[256];
static int syslog_opened = 0 ;

//extern nfs_parameter_t nfs_param;

/*
 * Variables specifiques aux threads.
 */

typedef struct ThreadLogContext_t
{
  char                  * thread_name;
  struct display_buffer   dspbuf;
  char                    buffer[LOG_BUFF_LEN+1];

} ThreadLogContext_t;

ThreadLogContext_t emergency_context = {
  "* log emergency *",
  {sizeof(emergency_context.buffer),
   emergency_context.buffer,
   emergency_context.buffer}
};

pthread_mutex_t emergency_mutex = PTHREAD_MUTEX_INITIALIZER;

/* threads keys */
static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

#define LogChanges(format, args...) \
  do { \
    if (LogComponents[COMPONENT_LOG].comp_log_level == NIV_FULL_DEBUG) \
      DisplayLogComponentLevel(COMPONENT_LOG, (char *) __FILE__, __LINE__, \
                               (char *) __FUNCTION__, \
                               NIV_NULL, "LOG: " format, ## args ); \
  } while (0)

cleanup_list_element *cleanup_list = NULL;

void RegisterCleanup(cleanup_list_element *clean)
{
  clean->next = cleanup_list;
  cleanup_list = clean;
}

void Cleanup(void)
{
  cleanup_list_element *c = cleanup_list;
  while(c != NULL)
    {
      c->clean();
      c = c->next;
    }
}

void Fatal(void)
{
  Cleanup();
  exit(2);
}

/* Feel free to add what you want here. This is a collection
 * of debug info that will be printed when a log message is
 * printed that matches or exceeds the severity level of
 * component LOG_MESSAGE_DEBUGINFO. */
extern uint32_t open_fd_count;

#define BT_MAX 256

char *get_debug_info(int *size)
{
  int                      bt_count, i, b_left;
  long                     bt_data[BT_MAX];
  char                  ** bt_str;
  struct display_buffer    dspbuf;

  struct rlimit rlim = {
    .rlim_cur = RLIM_INFINITY,
    .rlim_max = RLIM_INFINITY
  };

  bt_count = backtrace((void **)&bt_data, BT_MAX);

  if (bt_count > 0)
    bt_str = backtrace_symbols((void **)&bt_data, bt_count);
  else
    return NULL;

  if (bt_str == NULL || *bt_str == NULL)
    return NULL;

  // Form a single printable string from array of backtrace symbols
  dspbuf.b_size = 256; /* Account for rest of string. */

  for(i=0; i < bt_count; i++)
    dspbuf.b_size += strlen(bt_str[i]) + 1; /* account for \n */

  dspbuf.b_start   = gsh_malloc(dspbuf.b_size);
  dspbuf.b_current = dspbuf.b_start;

  if(dspbuf.b_start == NULL)
    {
      free(bt_str);
      return NULL;
    }

  b_left = display_cat(&dspbuf,
                       "\nDEBUG INFO -->\n"
                       "backtrace:\n");

  for(i=0; i < bt_count && b_left > 0; i++)
    {
      b_left = display_cat(&dspbuf, bt_str[i]);

      if(b_left > 0)
        b_left = display_cat(&dspbuf, "\n");
    }

  getrlimit(RLIMIT_NOFILE, &rlim);

  if(b_left > 0)
    b_left = display_printf(&dspbuf,
                            "\n"
                            "open_fd_count        = %-6d\n"
                            "rlimit_cur           = %-6ld\n"
                            "rlimit_max           = %-6ld\n"
                            "<--DEBUG INFO\n\n",
                            open_fd_count,
                            rlim.rlim_cur,
                            rlim.rlim_max);

  if (size != NULL)
    *size = display_buffer_len(&dspbuf);

  free(bt_str);
  
  return dspbuf.b_start;
}

void print_debug_info_fd(int fd)
{
  int    size;
  char * str = get_debug_info(&size);
  int    rc = 0;    // dumb variable to catch write return

  if (str != NULL)
    {
      rc = write(fd, str, size);
      gsh_free(str);
    }
}

void print_debug_info_file(FILE *flux)
{
  char *str = get_debug_info(NULL);

  if (str != NULL)
    {
      fputs(str, flux);
      gsh_free(str);
    }
}

void print_debug_info_syslog(int level)
{
  int size;
  char *debug_str = get_debug_info(&size);
  char *end_c=debug_str, *first_c=debug_str;

  if (debug_str != NULL) {
    while(*end_c != '\0' && (end_c - debug_str) <= size)
      {
        if (*end_c == '\n' || *end_c == '\0')
          {
            *end_c = '\0';
            if ((end_c - debug_str) != 0)
              syslog(tabLogLevel[level].syslog_level, "%s", first_c);
            first_c = end_c+1;
          }
        end_c++;
      }
    gsh_free(debug_str);
  }
}

#ifdef _DONT_HAVE_LOCALTIME_R

/* Localtime is not reentrant...
 * So we are obliged to have a mutex for calling it.
 * pffff....
 */
static pthread_mutex_t mutex_localtime = PTHREAD_MUTEX_INITIALIZER;

/* thread-safe and PORTABLE version of localtime */

static struct tm *Localtime_r(const time_t * p_time, struct tm *p_tm)
{
  struct tm *p_tmp_tm;

  if(!p_tm)
    {
      errno = EFAULT;
      return NULL;
    }

  pthread_mutex_lock(&mutex_localtime);

  p_tmp_tm = localtime(p_time);

  /* copy the result */
  (*p_tm) = (*p_tmp_tm);

  pthread_mutex_unlock(&mutex_localtime);

  return p_tm;
}
#else
# define Localtime_r localtime_r
#endif

/* Init of pthread_keys */
static void init_keys(void)
{
  if(pthread_key_create(&thread_key, NULL) == -1)
    LogCrit(COMPONENT_LOG,
            "init_keys - pthread_key_create returned %d (%s)",
            errno, strerror(errno));
}                               /* init_keys */



/**
 * GetThreadContext :
 * manages pthread_keys.
 */
static ThreadLogContext_t *Log_GetThreadContext(int ok_errors)
{
  ThreadLogContext_t *context;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      if (ok_errors)
        LogCrit(COMPONENT_LOG_EMERG,
                "Log_GetThreadContext - pthread_once returned %d (%s)",
                errno, strerror(errno));
      return &emergency_context;
    }

  context = (ThreadLogContext_t *) pthread_getspecific(thread_key);

  /* we allocate the thread key if this is the first time */
  if(context == NULL)
    {
      /* allocates thread structure */
      context = gsh_malloc(sizeof(ThreadLogContext_t));

      if(context == NULL)
        {
          if (ok_errors)
            LogCrit(COMPONENT_LOG_EMERG,
                    "Log_GetThreadContext - malloc returned %d (%s)",
                    errno, strerror(errno));
          return &emergency_context;
        }

      /* inits thread structures */
      context->thread_name      = emergency_context.thread_name;
      context->dspbuf.b_size    = sizeof(context->buffer);
      context->dspbuf.b_start   = context->buffer;
      context->dspbuf.b_current = context->buffer;
      
      /* set the specific value */
      pthread_setspecific(thread_key, context);

      if (ok_errors)
        LogFullDebug(COMPONENT_LOG_EMERG, "malloc => %p",
                     context);
    }

  return context;

}                               /* Log_GetThreadContext */

static inline const char *Log_GetThreadFunction(int ok_errors)
{
  ThreadLogContext_t *context = Log_GetThreadContext(ok_errors);

  return context->thread_name;
}

/*
 * Convert a numeral log level in ascii to
 * the numeral value. 
 */
int ReturnLevelAscii(const char *LevelInAscii)
{
  int i = 0;

  for(i = 0; i < ARRAY_SIZE(tabLogLevel); i++)
    if(!strcasecmp(tabLogLevel[i].str, LevelInAscii) ||
       !strcasecmp(tabLogLevel[i].str + 4, LevelInAscii) ||
       !strcasecmp(tabLogLevel[i].short_str, LevelInAscii))
      return tabLogLevel[i].value;

  /* If nothing found, return -1 */
  return -1;
}                               /* ReturnLevelAscii */

int ReturnComponentAscii(const char *ComponentInAscii)
{
  log_components_t component;

  for(component = COMPONENT_ALL; component < COMPONENT_COUNT; component++)
    {
      if(!strcasecmp(LogComponents[component].comp_name,
                     ComponentInAscii) ||
         !strcasecmp(LogComponents[component].comp_name + 10,
                     ComponentInAscii))
        {
          return component;
        }
    }

  return -1;
}

char *ReturnLevelInt(int level)
{
  int i = 0;

  for(i = 0; i < ARRAY_SIZE(tabLogLevel); i++)
    if(tabLogLevel[i].value == level)
      return tabLogLevel[i].str;

  /* If nothing is found, return NULL. */
  return NULL;
}                               /* ReturnLevelInt */

/*
 * Set the name of this program.
 */
void SetNamePgm(char *nom)
{

  /* This function isn't thread-safe because the name of the program
   * is common among all the threads. */
  if(strmaxcpy(program_name, nom, sizeof(program_name)) == -1)
    LogFatal(COMPONENT_LOG,
             "Program name %s too long", nom);
}                               /* SetNamePgm */

/*
 * Set the hostname.
 */
void SetNameHost(char *name)
{
  if(strmaxcpy(hostname, name, sizeof(hostname)) == -1)
    LogFatal(COMPONENT_LOG,
             "Host name %s too long", name);
}                               /* SetNameHost */

/* Set the function name in progress. */
void SetNameFunction(char *nom)
{
  ThreadLogContext_t *context = Log_GetThreadContext(0);
  if(context == NULL)
    return;
  if(context->thread_name != emergency_context.thread_name)
    gsh_free(context->thread_name);
  context->thread_name = gsh_strdup(nom);
}                               /* SetNameFunction */

/*
 * Five functions to manage debug level throug signal
 * handlers.
 *
 * InitLogging
 * IncrementLevelDebug
 * DecrementLevelDebug
 * SetLevelDebug
 * ReturnLevelDebug
 */

void SetComponentLogLevel(log_components_t component, int level_to_set)
{
  if (component == COMPONENT_ALL)
    {
      SetLevelDebug(level_to_set);
      return;
    }

  if(level_to_set < NIV_NULL)
    level_to_set = NIV_NULL;

  if(level_to_set >= NB_LOG_LEVEL)
    level_to_set = NB_LOG_LEVEL - 1;

  if(LogComponents[component].comp_env_set)
    {
      LogWarn(COMPONENT_CONFIG,
              "LOG %s level %s from config is ignored because %s"
              " was set in environment",
              LogComponents[component].comp_name,
              ReturnLevelInt(level_to_set),
              ReturnLevelInt(LogComponents[component].comp_log_level));
      return;
    }

  if (LogComponents[component].comp_log_level != level_to_set)
    {
      LogChanges("Changing log level of %s from %s to %s",
                 LogComponents[component].comp_name,
                 ReturnLevelInt(LogComponents[component].comp_log_level),
                 ReturnLevelInt(level_to_set));
      LogComponents[component].comp_log_level = level_to_set;
    }
}

inline int ReturnLevelDebug()
{
  return LogComponents[COMPONENT_ALL].comp_log_level;
}                               /* ReturnLevelDebug */

void _SetLevelDebug(int level_to_set)
{
  int i;

  if(level_to_set < NIV_NULL)
    level_to_set = NIV_NULL;

  if(level_to_set >= NB_LOG_LEVEL)
    level_to_set = NB_LOG_LEVEL - 1;

  for (i = COMPONENT_ALL; i < COMPONENT_FAKE; i++)
      LogComponents[i].comp_log_level = level_to_set;
}                               /* _SetLevelDebug */

void SetLevelDebug(int level_to_set)
{
  _SetLevelDebug(level_to_set);

  LogChanges("Setting log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}

struct log_flag * StrToFlag(const char * str)
{
  int i = 0;

  for(i = 0; i < ARRAY_SIZE(tab_log_flag); i++)
    if(!strcasecmp(tab_log_flag[i].lf_name, str))
      return &tab_log_flag[i];

  /* If nothing found, return NULL */
  return NULL;
}

void set_const_log_str()
{
  struct display_buffer dspbuf = {sizeof(const_log_str),
                                  const_log_str,
                                  const_log_str};
  struct display_buffer tdfbuf = {sizeof(date_time_fmt),
                                  date_time_fmt,
                                  date_time_fmt};
  int                   b_left = display_start(&dspbuf);

  const_log_str[0] = '\0';

  if(b_left > 0 && tab_log_flag[LF_EPOCH].lf_val)
    b_left = display_printf(&dspbuf, ": epoch %08x ", ServerEpoch);

  if(b_left > 0 && tab_log_flag[LF_HOSTAME].lf_val)
    b_left = display_printf(&dspbuf, ": %s ", hostname);

  if(b_left > 0 && tab_log_flag[LF_PROGNAME].lf_val)
    b_left = display_printf(&dspbuf, ": %s", program_name);

  if(b_left > 0 &&
     tab_log_flag[LF_PROGNAME].lf_val &&
     tab_log_flag[LF_PID].lf_val)
    b_left = display_cat(&dspbuf, "-");

  if(b_left > 0 && tab_log_flag[LF_PID].lf_val)
    b_left = display_printf(&dspbuf, "%d", getpid());

  if(b_left > 0 &&
     (tab_log_flag[LF_PROGNAME].lf_val || tab_log_flag[LF_PID].lf_val) &&
     !tab_log_flag[LF_THREAD_NAME].lf_val)
    b_left = display_cat(&dspbuf, " ");

  b_left = display_start(&tdfbuf);

  if(tab_log_flag[LF_DATE].lf_ext == TD_LOCAL &&
     tab_log_flag[LF_TIME].lf_ext == TD_LOCAL)
    {
      if(b_left > 0)
        b_left = display_cat(&tdfbuf, "%c ");
    }
  else
    {
      switch(tab_log_flag[LF_DATE].lf_ext)
        {
          case TD_GANESHA:
            b_left = display_cat(&tdfbuf, "%d/%m/%Y ");
            break;
          case TD_8601:
            b_left = display_cat(&tdfbuf, "%F ");
            break;
          case TD_LOCAL:
            b_left = display_cat(&tdfbuf, "%x ");
            break;
          case TD_SYSLOG:
            b_left = display_cat(&tdfbuf, "%b %e ");
            break;
          case TD_SYSLOG_USEC:
            if(tab_log_flag[LF_TIME].lf_ext == TD_SYSLOG_USEC)
              b_left = display_cat(&tdfbuf, "%F");
            else
              b_left = display_cat(&tdfbuf, "%F ");
            break;
          case TD_USER:
            b_left = display_printf(&tdfbuf, "%s ", user_date_fmt);
            break;
          case TD_NONE:
          default:
            break;
        }

      switch(tab_log_flag[LF_TIME].lf_ext)
        {
          case TD_GANESHA:
            b_left = display_cat(&tdfbuf, "%H:%M:%S ");
            break;
          case TD_SYSLOG:
          case TD_8601:
          case TD_LOCAL:
            b_left = display_cat(&tdfbuf, "%X ");
            break;
          case TD_SYSLOG_USEC:
            b_left = display_cat(&tdfbuf, "T%H:%M:%S.%%06u%z ");
            break;
          case TD_USER:
            b_left = display_printf(&tdfbuf, "%s ", user_time_fmt);
            break;
          case TD_NONE:
          default:
            break;
        }
      
    }
}

void InitLogging()
{
  int        i;
  log_type_t ltyp;

  /* Finish initialization of and register log facilities. */
  init_glist(&facility_list);
  init_glist(&active_facility_list);

  facilities[STDERRLOG].lf_private = stderr;
  facilities[STDOUTLOG].lf_private = stdout;
  facilities[TESTLOG].lf_private   = stdout;
  facilities[FILELOG].lf_private   = gsh_strdup("/tmp/ganesha.log");

  for(ltyp = SYSLOG; ltyp <= TESTLOG; ltyp++)
    register_log_facility(&facilities[ltyp]);

  /* Activate the default facility */
  activate_log_facility(default_facility);

  /* Initialize const_log_str to defaults. Ganesha can start logging before
   * the LOG config is processed (in fact, LOG config can itself issue log
   * messages to indicate errors.
   */
  set_const_log_str();

  /* Initialisation of tables of families */
  tab_family[ERR_SYS].num_family = 0;
  tab_family[ERR_SYS].tab_err = (family_error_t *) tab_system_err;
  if(strmaxcpy(tab_family[ERR_SYS].name_family,
               "Errors Systeme UNIX",
               sizeof(tab_family[ERR_SYS].name_family)) == 01)
    LogFatal(COMPONENT_LOG,
             "System Family name too long");

  for(i = 1; i < MAX_NUM_FAMILY; i++)
    tab_family[i].num_family = UNUSED_SLOT;
  
}

void ReadLogEnvironment()
{
  char *env_value;
  int newlevel, component, oldlevel;

  for(component = COMPONENT_ALL; component < COMPONENT_COUNT; component++)
    {
      env_value = getenv(LogComponents[component].comp_name);
      if (env_value == NULL)
        continue;
      newlevel = ReturnLevelAscii(env_value);
      if (newlevel == -1) {
        LogCrit(COMPONENT_LOG,
                "Environment variable %s exists, but the value %s is not a"
                " valid log level.",
                LogComponents[component].comp_name, env_value);
        continue;
      }
      oldlevel = LogComponents[component].comp_log_level;
      LogComponents[component].comp_log_level = newlevel;
      LogComponents[component].comp_env_set   = TRUE;
      LogChanges("Using environment variable to switch log level for %s from "
                 "%s to %s",
                 LogComponents[component].comp_name, ReturnLevelInt(oldlevel),
                 ReturnLevelInt(newlevel));
    }

}                               /* InitLogging */

/*
 * Routines for managing error messages
 */

/**
 *
 * @brief Add a family of errors
 *
 * @param[in]  num_family  The family index
 * @param[in]  name_family The name of the family
 * @param[in]  tab_err     The table of errors
 *
 * @return -1 for failure, or num_family
 *
 */
int AddFamilyError(int num_family, char *name_family, family_error_t * tab_err)
{
  int i = 0;

  /* The number of the family is between -1 and MAX_NUM_FAMILY */
  if((num_family < -1) || (num_family >= MAX_NUM_FAMILY))
    return -1;

  /* System errors can't be 0 */
  if(num_family == 0)
    return -1;

  /* Look for a vacant entry. */
  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == UNUSED_SLOT)
      break;

  /* Check if the table is full. */
  if(i == MAX_NUM_FAMILY)
    return -1;

  tab_family[i].num_family = (num_family != -1) ? num_family : i;
  tab_family[i].tab_err = tab_err;
  if(strmaxcpy(tab_family[i].name_family,
               name_family,
               sizeof(tab_family[i].name_family)) == -1)
    LogFatal(COMPONENT_LOG,
             "family name %s too long", name_family);

  return tab_family[i].num_family;
}                               /* AddFamilyError */

char *ReturnNameFamilyError(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == num_family)
      return tab_family[i].name_family;

  return NULL;
}                               /* ReturnFamilyError */

/* Finds a family in the table of family errors. */
static family_error_t *FindTabErr(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    {
      if(tab_family[i].num_family == num_family)
        {
          return tab_family[i].tab_err;
        }
    }

  return NULL;
}                               /* FindTabErr */

static family_error_t FindErr(family_error_t * tab_err, int num)
{
  int i = 0;
  family_error_t returned_err;

  do
    {

      if(tab_err[i].numero == num || tab_err[i].numero == ERR_NULL)
        {
          returned_err = tab_err[i];
          break;
        }

      i += 1;
    }
  while(1);

  return returned_err;
}                               /* FindErr */

static int log_to_syslog(struct log_facility   * facility,
                         log_levels_t            level,
                         struct display_buffer * buffer,
                         char                  * compstr,
                         char                  * message)
{
  if( !syslog_opened )
   {
     openlog("nfs-ganesha", LOG_PID, LOG_USER);
     syslog_opened = 1;
   }

  /* Writing to syslog. */
  syslog(tabLogLevel[level].syslog_level, "%s", compstr);

  if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level
      && level != NIV_NULL)
      print_debug_info_syslog(level);

  return 0;
}

static int log_to_file(struct log_facility   * facility,
                       log_levels_t            level,
                       struct display_buffer * buffer,
                       char                  * compstr,
                       char                  * message)
{
  int    fd, my_status, len, rc = 0;
  char * path = facility->lf_private;

  len = display_buffer_len(buffer);

  /* Add newline to end of buffer */
  buffer->b_start[len]   = '\n';
  buffer->b_start[len+1] = '\0';

  fd = open(path, O_WRONLY | O_SYNC | O_APPEND | O_CREAT, log_mask);

  if(fd != -1)
    {
      rc = write(fd, buffer->b_start, len + 1);

      if(rc < (len + 1))
        {
          if(rc >= 0)
            my_status = ENOSPC;
          else
            my_status = errno;

          goto error;
        }

      if(level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level &&
         level != NIV_NULL)
        print_debug_info_fd(fd);

      rc = close(fd);

      if(rc == 0)
        goto out;
    }

  my_status = errno;

error:

  fprintf(stderr, "Error: couldn't complete write to the log file %s"
          "status=%d (%s) message was:\n%s",
          path, my_status, strerror(my_status), buffer->b_start);

out:

  /* Remove newline from buffer */
  buffer->b_start[len] = '\0';

  return rc;
}

static int log_to_stream(struct log_facility   * facility,
                         log_levels_t            level,
                         struct display_buffer * buffer,
                         char                  * compstr,
                         char                  * message)
{
  FILE * stream = facility->lf_private;
  int    rc;
  char * msg = buffer->b_start;
  int    len;

  len = display_buffer_len(buffer);

  /* Add newline to end of buffer */
  buffer->b_start[len]   = '\n';
  buffer->b_start[len+1] = '\0';

  switch(facility->lf_headers)
    {
      case LH_NONE:
        msg = message;
        break;
      case LH_COMPONENT:
        msg = compstr;
        break;
      case LH_ALL:
        msg = buffer->b_start;
        break;
    }

  rc = fputs(msg, stream);

  if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level &&
      level != NIV_NULL &&
      facility->lf_headers != LH_NONE)
    print_debug_info_file(stream);

  if(rc != EOF)
   rc = fflush(stream);

  /* Remove newline from buffer */
  buffer->b_start[len] = '\0';

  if(rc == EOF)
    return -1;
  else
    return 0;
}

static int display_log_header(ThreadLogContext_t * context)
{
  int b_left = display_start(&context->dspbuf);

  if(b_left <= 0 || max_headers < LH_ALL)
    return b_left;

  /* Print date and/or time if either flag is enabled. */
  if(b_left > 0 &&
     (tab_log_flag[LF_DATE].lf_val || tab_log_flag[LF_TIME].lf_val))
    {
      struct tm      the_date;
      char           tbuf[MAX_TD_FMT_LEN];
      time_t         tm;
      struct timeval tv;

      if(tab_log_flag[LF_TIME].lf_ext == TD_SYSLOG_USEC)
        {
          gettimeofday(&tv, NULL);
          tm = tv.tv_sec;
        }
      else
        {
          tm = time(NULL);
        }

      Localtime_r(&tm, &the_date);

      /* Earlier we build the date/time format string in date_time_fmt, now use
       * that to format the time and/or date. If time format is TD_SYSLOG_USEC,
       * then we need an additional step to add the microseconds (since strftime
       * just takes a struct tm which was filled in from a time_t and thus does
       * not have microseconds.
       */
      if(strftime(tbuf, sizeof(tbuf), date_time_fmt, &the_date) != 0)
        {
          if(tab_log_flag[LF_TIME].lf_ext == TD_SYSLOG_USEC)
            b_left = display_printf(&context->dspbuf, tbuf, tv.tv_usec);
          else
            b_left = display_cat(&context->dspbuf, tbuf);
        }
    }

  if(b_left > 0 && const_log_str[0] != '\0')
    b_left = display_cat(&context->dspbuf, const_log_str);

  /* If thread name will not follow, need a : separator */
  if(b_left > 0 && !tab_log_flag[LF_THREAD_NAME].lf_val)
    b_left = display_cat(&context->dspbuf, ": ");

  /* If we managed to overflow the buffer with the header, just skip it... */
  if(b_left == 0)
    {
      display_reset_buffer(&context->dspbuf);
      b_left = display_start(&context->dspbuf);
    }

  /* The message will now start at context->dspbuf.b_current */
  return b_left;
}

static int display_log_component(ThreadLogContext_t * context,
                                 log_components_t     component,
                                 char               * file,
                                 int                  line,
                                 const char         * function,
                                 int                  level)
{
  int b_left = display_start(&context->dspbuf);

  if(b_left <= 0 || max_headers < LH_COMPONENT)
    return b_left;

  if(b_left > 0 && tab_log_flag[LF_THREAD_NAME].lf_val)
    b_left = display_printf(&context->dspbuf, "[%s] ", context->thread_name);

  if(b_left > 0 && tab_log_flag[LF_FILE_NAME].lf_val)
    {
      if(tab_log_flag[LF_LINE_NUM].lf_val)
        b_left = display_printf(&context->dspbuf, "%s:", file);
      else
        b_left = display_printf(&context->dspbuf, "%s :", file);
    }

  if(b_left > 0 && tab_log_flag[LF_LINE_NUM].lf_val)
    b_left = display_printf(&context->dspbuf, "%d :", line);

  if(b_left > 0 && tab_log_flag[LF_FUNCTION_NAME].lf_val)
    b_left = display_printf(&context->dspbuf, "%s :", function);

  if(b_left > 0 && tab_log_flag[LF_COMPONENT].lf_val)
    b_left = display_printf(&context->dspbuf, "%s :",
                            LogComponents[component].comp_str);

  if(b_left > 0 && tab_log_flag[LF_LEVEL].lf_val)
    b_left = display_printf(&context->dspbuf, "%s :",
                            tabLogLevel[level].short_str);

  /* If we managed to overflow the buffer with the header, just skip it... */
  if(b_left == 0)
    {
      display_reset_buffer(&context->dspbuf);
      b_left = display_start(&context->dspbuf);
    }

  return b_left;
}

void display_log_component_level(log_components_t   component,
                                 char             * file,
                                 int                line,
                                 char             * function,
                                 log_levels_t       level,
                                 char             * format,
                                 va_list            arguments)
{
  ThreadLogContext_t  * context;
  char                * compstr;
  char                * message;
  int                   b_left;
  struct glist_head   * glist;
  struct log_facility * facility;

  context = Log_GetThreadContext(component != COMPONENT_LOG_EMERG);

  if(context != &emergency_context)
    {
      /* Reset and verify the buffer. */
      display_reset_buffer(&context->dspbuf);

      b_left = display_start(&context->dspbuf);

      if(b_left <= 0)
        context = &emergency_context;
    }

  if(context == &emergency_context)
    pthread_mutex_lock(&emergency_mutex);

  /* Build up the messsage and capture the various positions in it. */
  b_left = display_log_header(context);

  if(b_left > 0)
    compstr = context->dspbuf.b_current;
  else
    compstr = context->dspbuf.b_start;

  if(b_left > 0)
    b_left = display_log_component(context,
                                   component,
                                   file,
                                   line,
                                   function,
                                   level);

  if(b_left > 0)
    message = context->dspbuf.b_current;
  else
    message = context->dspbuf.b_start;

  if(b_left > 0)
    b_left = display_vprintf(&context->dspbuf, format, arguments);

  pthread_rwlock_rdlock(&log_rwlock);

  glist_for_each(glist, &active_facility_list)
    {
      facility = glist_entry(glist, struct log_facility, lf_active);

      if(level <= facility->lf_max_level &&
         facility->lf_func != NULL)
        facility->lf_func(facility, level, &context->dspbuf, compstr, message);
    }

  pthread_rwlock_unlock(&log_rwlock);

  if(context == &emergency_context)
    pthread_mutex_unlock(&emergency_mutex);

  if(level == NIV_FATAL)
    Fatal();
}

int display_LogError(struct display_buffer * dspbuf,
                     int                     num_family,
                     int                     num_error,
                     int                     status)
{
  family_error_t *tab_err = NULL;
  family_error_t the_error;

  /* Find the family */
  if((tab_err = FindTabErr(num_family)) == NULL)
    return display_printf(dspbuf, "Could not find famiily %d", num_family);

  /* find the error */
  the_error = FindErr(tab_err, num_error);

  if(status == 0)
    {
      return display_printf(dspbuf,
                            "Error %s : %s : status %d",
                            the_error.label,
                            the_error.msg,
                            status);
    }
  else
    {
      char tempstr[1024];
      char *errstr;
      errstr = strerror_r(status, tempstr, sizeof(tempstr));

      return display_printf(dspbuf,
                            "Error %s : %s : status %d : %s",
                            the_error.label,
                            the_error.msg,
                            status,
                            errstr);
    }
}                               /* display_LogError */

log_component_info __attribute__ ((__unused__)) LogComponents[COMPONENT_COUNT] =
{
  { COMPONENT_ALL,               "COMPONENT_ALL", "",
    NIV_EVENT,
  },
  { COMPONENT_LOG,               "COMPONENT_LOG", "LOG",
    NIV_EVENT,
  },
  { COMPONENT_LOG_EMERG,         "COMPONENT_LOG_EMERG", "LOG",
    NIV_EVENT,
  },
  { COMPONENT_MEMALLOC,          "COMPONENT_MEMALLOC", "MEM ALLOC",
    NIV_EVENT,
  },
  { COMPONENT_MEMLEAKS,          "COMPONENT_MEMLEAKS", "MEM LEAKS",
    NIV_EVENT,
  },
  { COMPONENT_FSAL,              "COMPONENT_FSAL", "FSAL",
    NIV_EVENT,
  },
  { COMPONENT_NFSPROTO,          "COMPONENT_NFSPROTO", "NFS PROTO",
    NIV_EVENT,
  },
  { COMPONENT_NFS_V4,            "COMPONENT_NFS_V4", "NFS V4",
    NIV_EVENT,
  },
  { COMPONENT_NFS_V4_PSEUDO,     "COMPONENT_NFS_V4_PSEUDO", "NFS V4 PSEUDO",
    NIV_EVENT,
  },
  { COMPONENT_FILEHANDLE,        "COMPONENT_FILEHANDLE", "FILE HANDLE",
    NIV_EVENT,
  },
  { COMPONENT_NFS_SHELL,         "COMPONENT_NFS_SHELL", "NFS SHELL",
#ifdef _DEBUG_NFS_SHELL
    NIV_FULL_DEBUG,
#else
    NIV_EVENT,
#endif
  },
  { COMPONENT_DISPATCH,          "COMPONENT_DISPATCH", "DISPATCH",
    NIV_EVENT,
  },
  { COMPONENT_CACHE_CONTENT,     "COMPONENT_CACHE_CONTENT", "CACHE CONTENT",
    NIV_EVENT,
  },
  { COMPONENT_CACHE_INODE,       "COMPONENT_CACHE_INODE", "CACHE INODE",
    NIV_EVENT,
  },
  { COMPONENT_CACHE_INODE_GC,    "COMPONENT_CACHE_INODE_GC", "CACHE INODE GC",
    NIV_EVENT,
  },
  { COMPONENT_CACHE_INODE_LRU,    "COMPONENT_CACHE_INODE_LRU", "CACHE INODE LRU",
    NIV_EVENT,
  },
  { COMPONENT_HASHTABLE,         "COMPONENT_HASHTABLE", "HASH TABLE",
    NIV_EVENT,
  },
  { COMPONENT_HASHTABLE_CACHE,   "COMPONENT_HASHTABLE_CACHE", "HASH TABLE CACHE",
    NIV_EVENT,
  },
  { COMPONENT_LRU,               "COMPONENT_LRU", "LRU",
    NIV_EVENT,
  },
  { COMPONENT_DUPREQ,            "COMPONENT_DUPREQ", "DUPREQ",
    NIV_EVENT,
  },
  { COMPONENT_RPCSEC_GSS,        "COMPONENT_RPCSEC_GSS", "RPCSEC GSS",
    NIV_EVENT,
  },
  { COMPONENT_INIT,              "COMPONENT_INIT", "NFS STARTUP",
    NIV_EVENT,
  },
  { COMPONENT_MAIN,              "COMPONENT_MAIN", "MAIN",
    NIV_EVENT,
  },
  { COMPONENT_IDMAPPER,          "COMPONENT_IDMAPPER", "ID MAPPER",
    NIV_EVENT,
  },
  { COMPONENT_NFS_READDIR,       "COMPONENT_NFS_READDIR", "NFS READDIR",
    NIV_EVENT,
  },

  { COMPONENT_NFS_V4_LOCK,       "COMPONENT_NFS_V4_LOCK", "NFS V4 LOCK",
    NIV_EVENT,
  },
  { COMPONENT_NFS_V4_XATTR,      "COMPONENT_NFS_V4_XATTR", "NFS V4 XATTR",
    NIV_EVENT,
  },
  { COMPONENT_NFS_V4_REFERRAL,   "COMPONENT_NFS_V4_REFERRAL", "NFS V4 REFERRAL",
    NIV_EVENT,
  },
  { COMPONENT_MEMCORRUPT,        "COMPONENT_MEMCORRUPT", "MEM CORRUPT",
    NIV_EVENT,
  },
  { COMPONENT_CONFIG,            "COMPONENT_CONFIG", "CONFIG",
    NIV_EVENT,
  },
  { COMPONENT_CLIENTID,          "COMPONENT_CLIENTID", "CLIENT ID",
    NIV_EVENT,
  },
  { COMPONENT_STDOUT,            "COMPONENT_STDOUT", "STDOUT",
    NIV_EVENT,
  },
  { COMPONENT_SESSIONS,          "COMPONENT_SESSIONS", "SESSIONS",
    NIV_EVENT,
  },
  { COMPONENT_PNFS,              "COMPONENT_PNFS", "PNFS",
    NIV_EVENT,
  },
  { COMPONENT_RPC_CACHE,         "COMPONENT_RPC_CACHE", "RPC CACHE",
    NIV_EVENT,
  },
  { COMPONENT_RW_LOCK,           "COMPONENT_RW_LOCK", "RW LOCK",
    NIV_EVENT,
  },
  { COMPONENT_NLM,               "COMPONENT_NLM", "NLM",
    NIV_EVENT,
  },
  { COMPONENT_RPC,               "COMPONENT_RPC", "RPC",
    NIV_EVENT,
  },
  { COMPONENT_NFS_CB,            "COMPONENT_NFS_CB", "NFS CB",
    NIV_EVENT,
  },
  { COMPONENT_THREAD,            "COMPONENT_THREAD", "THREAD",
    NIV_EVENT,
  },
  { COMPONENT_NFS_V4_ACL,        "COMPONENT_NFS_V4_ACL", "NFS V4 ACL",
    NIV_EVENT,
  },
  { COMPONENT_STATE,             "COMPONENT_STATE", "STATE",
    NIV_EVENT,
  },
  { COMPONENT_9P,                "COMPONENT_9P", "9P",
    NIV_EVENT,
  },
  { COMPONENT_9P_DISPATCH,       "COMPONENT_9P_DISPATCH", "9P DISPATCH",
    NIV_EVENT,
  },
  { COMPONENT_FSAL_UP,             "COMPONENT_FSAL_UP", "FSAL_UP",
    NIV_EVENT,
  },
  { COMPONENT_DBUS,                "COMPONENT_DBUS", "DBUS",
    NIV_EVENT,
  },
  { COMPONENT_FAKE,                "COMPONENT_FAKE", "FAKE",
    NIV_NULL,
  },
  { LOG_MESSAGE_DEBUGINFO,        "LOG_MESSAGE_DEBUGINFO",
                                  "LOG MESSAGE DEBUGINFO",
    NIV_NULL,
  },
  { LOG_MESSAGE_VERBOSITY,        "LOG_MESSAGE_VERBOSITY",
                                  "LOG MESSAGE VERBOSITY",
    NIV_NULL,
  },
};

void DisplayLogComponentLevel(log_components_t   component,
                              char             * file,
                              int                line,
                              char             * function,
                              log_levels_t       level,
                              char             * format, ...)
{
  va_list              arguments;

  va_start(arguments, format);

  display_log_component_level(component,
                              file,
                              line,
                              function,
                              level,
                              format,
                              arguments);

  va_end(arguments);
}

void DisplayErrorComponentLogLine(log_components_t   component,
                                  char             * file,
                                  int                line,
                                  char             * function,
                                  int                num_family,
                                  int                num_error,
                                  int                status)
{
  char                  buffer[LOG_BUFF_LEN];
  struct display_buffer dspbuf = {sizeof(buffer), buffer, buffer};

  (void) display_LogError(&dspbuf, num_family, num_error, status);

  DisplayLogComponentLevel(component, file, line, function, NIV_CRIT, "%s: %s",
                           LogComponents[component].comp_str, buffer);
}                               /* DisplayErrorLogLine */

static int isValidLogPath(char *pathname)
{
  char tempname[MAXPATHLEN];

  char *directory_name;
  int rc;

  if(strmaxcpy(tempname, pathname, sizeof(tempname)) == -1)
      return 0;

  directory_name = dirname(tempname);
  if (directory_name == NULL)
      return 0;

  rc = access(directory_name, W_OK);
  switch(rc)
    {
    case 0:
      break; /* success !! */
    case EACCES:
      LogCrit(COMPONENT_LOG,
              "Either access is denied to the file or denied to one of the "
              "directories in %s",
              directory_name);
      break;
    case ELOOP:
      LogCrit(COMPONENT_LOG,
              "Too many symbolic links were encountered in resolving %s",
              directory_name);
      break;
    case ENAMETOOLONG:
      LogCrit(COMPONENT_LOG,
              "%s is too long of a pathname.",
              directory_name);
      break;
    case ENOENT:
      LogCrit(COMPONENT_LOG,
              "A component of %s does not exist.",
              directory_name);
      break;
    case ENOTDIR:
      LogCrit(COMPONENT_LOG,
              "%s is not a directory.",
              directory_name);
      break;
    case EROFS:
      LogCrit(COMPONENT_LOG,
              "Write permission was requested for a file on a read-only file "
              "system.");
      break;
    case EFAULT:
      LogCrit(COMPONENT_LOG,
              "%s points outside your accessible address space.",
              directory_name);
      break;

    default:
	break ;
    }

  return 1;
}

void SetLogFile(char * name)
{
  struct log_facility * facility;
  char                * tmp;

  pthread_rwlock_wrlock(&log_rwlock);

  facility = &facilities[FILELOG];

  if (!isValidLogPath(name))
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Could not set default logging to %s (invalid path)",
               name);
      return;
    }

  tmp = gsh_strdup(name);

  if(tmp == NULL)
    {
      pthread_rwlock_unlock(&log_rwlock);

      LogMajor(COMPONENT_LOG,
               "Could not set default logging to %s (no memory)",
               name);
      return;
    }

  if(facility->lf_private != NULL)
    gsh_free(facility->lf_private);

  facility->lf_private = tmp;

  pthread_rwlock_unlock(&log_rwlock);

  LogEvent(COMPONENT_LOG, "Changing log file to %s", name);
}

/*
 * Sets the default logging method (whether to a specific filepath or syslog.
 * During initialization this is used and separate layer logging defaults to
 * this destination.
 */
void SetDefaultLogging(char *name)
{
  struct log_facility * facility;

  pthread_rwlock_wrlock(&log_rwlock);

  facility = find_log_facility(name);

  if(facility == NULL)
    {
      /* Try to use FILELOG facility */
      char * tmp;

      facility = &facilities[FILELOG];

      if (!isValidLogPath(name))
        {
          pthread_rwlock_unlock(&log_rwlock);

          LogMajor(COMPONENT_LOG,
                   "Could not set default logging to %s (invalid path)",
                   name);
          return;
        }

      tmp = gsh_strdup(name);

      if(tmp == NULL)
        {
          pthread_rwlock_unlock(&log_rwlock);

          LogMajor(COMPONENT_LOG,
                   "Could not set default logging to %s (no memory)",
                   name);
          return;
        }

      if(facility->lf_private != NULL)
        gsh_free(facility->lf_private);

      facility->lf_private = tmp;
    }

  if(default_facility != facility)
    _deactivate_log_facility(default_facility);

  default_facility = facility;

  _activate_log_facility(facility);

  pthread_rwlock_unlock(&log_rwlock);

  LogEvent(COMPONENT_LOG, "Setting default log destination to name %s", name);
}                               /* SetDefaultLogging */

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void rpc_warnx(/* const */ char *fmt, ...)
{
    va_list              ap;

    if (LogComponents[COMPONENT_RPC].comp_log_level < NIV_DEBUG)
        return;

    va_start(ap, fmt);

    display_log_component_level(COMPONENT_RPC,
                                "<no-file>", 0, "rpc", NIV_DEBUG, fmt, ap);

    va_end(ap);

}

#ifdef _SNMP_ADM_ACTIVE


/* Enable/disable logging for tirpc */
int get_tirpc_debug_bitmask(snmp_adm_type_union *param, void *opt)
{
  unsigned int mask;

  if (!tirpc_control(TIRPC_GET_DEBUG_FLAGS, (void *)&mask))
    LogCrit(COMPONENT_INIT, "Failed to get debug mask for TI-RPC __warnx");
  param->integer = mask;
  return 0;
}

int set_tirpc_debug_bitmask(const snmp_adm_type_union *param, void *opt)
{
  unsigned int mask = param->integer;
  return set_tirpc_debug_mask(mask);
}

int set_tirpc_debug_mask(unsigned int mask)
{
  if (mask > 0 && mask <= 4294967295 &&
      !tirpc_control(TIRPC_SET_DEBUG_FLAGS, &mask))
    LogCrit(COMPONENT_INIT, "Failed setting debug mask for TI-RPC __warnx"
            " with mask %d", mask);
  return 0;
}

int getComponentLogLevel(snmp_adm_type_union * param, void *opt)
{
  long component = (long)opt;

  strncpy(param->string,
          ReturnLevelInt(LogComponents[component].comp_log_level),
          sizeof(param->string));
  param->string[sizeof(param->string) - 1] = '\0';
  return 0;
}

int setComponentLogLevel(const snmp_adm_type_union * param, void *opt)
{
  long component = (long)opt;
  int level_to_set = ReturnLevelAscii(param->string);

  if (level_to_set == -1)
    return -1;

  if (component == COMPONENT_ALL)
    {
      _SetLevelDebug(level_to_set);

      LogChanges("SNMP request changing log level for all components to %s",
                 ReturnLevelInt(level_to_set));
    }
  else
    {
      LogChanges("SNMP request changing log level of %s from %s to %s.",
                 LogComponents[component].comp_name,
                 ReturnLevelInt(LogComponents[component].comp_log_level),
                 ReturnLevelInt(level_to_set));
      LogComponents[component].comp_log_level = level_to_set;
    }

  return 0;
}

#endif /* _SNMP_ADM_ACTIVE */

#define CONF_LABEL_LOG "LOG"

/**
 *
 * read_log_config: Read the configuration ite; for the logging component.
 *
 * Read the configuration ite; for the logging component.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok, -1 if failed, 1 is stanza is not there
 *
 */
int read_log_config(config_file_t in_config)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;
  int component;
  int level;
  struct log_flag * flag;
  int date_spec = FALSE;
  int time_spec = FALSE;
  struct log_facility * facility;

  /* Is the config tree initialized ? */
  if(in_config == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_LOG)) == NULL)
    {
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_LOG);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_CONFIG,
              "Item \"%s\" is expected to be a block",
              CONF_LABEL_LOG);
      return 1;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_LOG);
          return -1;
        }

      if(!strcasecmp(key_name, "Facility"))
        {
          if(create_null_facility(key_value) == NULL)
            LogWarn(COMPONENT_CONFIG,
                    "Can not create %s=\'%s\'",
                    key_name, key_value);
          continue;
        }

      if(!strcasecmp(key_name, "LogFile"))
        {
          SetLogFile(key_value);
          continue;
        }

      flag = StrToFlag(key_name);

      if(!strcasecmp(key_name, "time") ||
         !strcasecmp(key_name, "date"))
        {
          if((flag->lf_idx == LF_DATE && date_spec) ||
             (flag->lf_idx == LF_TIME && time_spec))
            {
              LogWarn(COMPONENT_CONFIG,
                      "Can only specify %s once, ignoring %s=\"%s\"",
                      key_name, key_name, key_value);
              continue;
            }

          if(flag->lf_idx == LF_DATE)
            date_spec = TRUE;

          if(flag->lf_idx == LF_TIME)
            time_spec = TRUE;

          if(!strcasecmp(key_value, "ganesha") ||
             !strcasecmp(key_value, "true"))
            {
              flag->lf_ext = TD_GANESHA;
              flag->lf_val = TRUE;
            }
          else if(!strcasecmp(key_value, "local"))
            {
              flag->lf_ext = TD_LOCAL;
              flag->lf_val = TRUE;
            }
          else if(!strcasecmp(key_value, "8601") ||
                  !strcasecmp(key_value, "ISO-8601") ||
                  !strcasecmp(key_value, "ISO 8601") ||
                  !strcasecmp(key_value, "ISO"))
            {
              flag->lf_ext = TD_8601;
              flag->lf_val = TRUE;
            }
          else if(!strcasecmp(key_value, "syslog"))
            {
              flag->lf_ext = TD_SYSLOG;
              flag->lf_val = TRUE;
            }
          else if(!strcasecmp(key_value, "syslog_usec"))
            {
              flag->lf_ext = TD_SYSLOG_USEC;
              flag->lf_val = TRUE;
            }
          else if(!strcasecmp(key_value, "false") ||
                  !strcasecmp(key_value, "none"))
            {
              flag->lf_val = FALSE;
              flag->lf_ext = TD_NONE;
            }
          else if(flag->lf_idx == LF_DATE)
            {
              if(strmaxcpy(user_date_fmt,
                           key_value,
                           sizeof(user_date_fmt)) == -1)
                LogCrit(COMPONENT_CONFIG,
                        "%s value of \'%s\' too long",
                        key_name, key_value);
              else
                {
                  flag->lf_ext = TD_USER;
                  flag->lf_val = TRUE;
                }
            }
          else if(flag->lf_idx == LF_TIME)
            {
              if(strmaxcpy(user_time_fmt,
                           key_value,
                           sizeof(user_date_fmt)) == -1)
                LogCrit(COMPONENT_CONFIG,
                        "%s value of \'%s\' too long",
                        key_name, key_value);
              else
                {
                  flag->lf_ext = TD_USER;
                  flag->lf_val = TRUE;
                }
            }
          continue;
        }

      if(flag != NULL)
        {
          flag->lf_val = StrToBoolean(key_value);
          continue;
        }

      component = ReturnComponentAscii(key_name);

      if(component != -1)
        {
          level = ReturnLevelAscii(key_value);

          if(level == -1)
            {
              LogWarn(COMPONENT_CONFIG,
                      "Error parsing section \"LOG\" of configuration file, \"%s\""
                      " is not a valid LOG LEVEL for \"%s\"",
                      key_value, key_name);
              continue;
            }

          SetComponentLogLevel(component, level);

          continue;
        }

      pthread_rwlock_wrlock(&log_rwlock);

      facility = find_log_facility(key_name);

      if(facility != NULL)
        {
          level = ReturnLevelAscii(key_value);

          if(level == -1)
            {
              pthread_rwlock_unlock(&log_rwlock);

              LogWarn(COMPONENT_CONFIG,
                      "Error parsing section \"LOG\" of configuration file, \"%s\""
                      " is not a valid LOG LEVEL for \"%s\"",
                      key_value, key_name);

              continue;
            }

          facility->lf_max_level = level;

          if(level != NIV_NULL)
            _activate_log_facility(facility);
          else
            _deactivate_log_facility(facility);

          pthread_rwlock_unlock(&log_rwlock);

          continue;
        }

      pthread_rwlock_unlock(&log_rwlock);

      LogWarn(COMPONENT_CONFIG,
             "Error parsing section \"LOG\" of configuration file, \"%s\""
             " is not a valid LOG configuration variable",
             key_name);
   }

  if(date_spec && !time_spec && tab_log_flag[LF_DATE].lf_ext != TD_NONE)
    tab_log_flag[LF_TIME].lf_ext = tab_log_flag[LF_DATE].lf_ext;

  if(time_spec && !date_spec && tab_log_flag[LF_TIME].lf_ext != TD_NONE)
    tab_log_flag[LF_DATE].lf_ext = tab_log_flag[LF_TIME].lf_ext;

  /* Now that the LOG config has been processed, rebuild const_log_str. */
  set_const_log_str();

  return 0;
}                               /* read_log_config */

void reread_log_config()
{
  int           status = 0;
  int           i;
  config_file_t config_struct;

  /* Clear out the flag indicating component was set from environment. */
  for (i = COMPONENT_ALL; i < COMPONENT_FAKE; i++)
      LogComponents[i].comp_env_set = FALSE;

  /* If no configuration file is given, then the caller must want to reparse the
   * configuration file from startup. */
  if(config_path[0] == '\0')
    {
      LogCrit(COMPONENT_CONFIG,
              "No configuration file was specified for reloading log config.");
      return;
    }

  /* Attempt to parse the new configuration file */
  config_struct = config_ParseFile(config_path);
  if(!config_struct)
    {
      LogCrit(COMPONENT_CONFIG,
              "Error while parsing new configuration file %s: %s",
              config_path, config_GetErrorMsg());
      return;
    }

  /* Create the new exports list */
  status = read_log_config(config_struct);
  if(status < 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "Error while parsing LOG entries");
    }
}
