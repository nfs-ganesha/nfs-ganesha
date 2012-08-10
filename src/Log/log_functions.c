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
//#include "nfs_core.h"

/* La longueur d'une chaine */
#define STR_LEN_TXT      2048
#define PATH_LEN         1024
#define MAX_STR_LEN      1024
#define MAX_NUM_FAMILY  50
#define UNUSED_SLOT -1

log_level_t tabLogLevel[] =
{
  {NIV_NULL,       "NIV_NULL",       "NULL",       LOG_NOTICE},
  {NIV_FATAL,      "NIV_FATAL",      "FATAL",      LOG_CRIT},
  {NIV_MAJ,        "NIV_MAJ",        "MAJ",        LOG_CRIT},
  {NIV_CRIT,       "NIV_CRIT",       "CRIT",       LOG_ERR},
  {NIV_WARN,       "NIV_WARN",       "WARN",       LOG_WARNING},
  {NIV_EVENT,      "NIV_EVENT",      "EVENT",      LOG_NOTICE},
  {NIV_INFO,       "NIV_INFO",       "INFO",       LOG_INFO},
  {NIV_DEBUG,      "NIV_DEBUG",      "DEBUG",      LOG_DEBUG},
  {NIV_MID_DEBUG,  "NIV_MID_DEBUG",  "MID_DEBUG",  LOG_DEBUG},
  {NIV_FULL_DEBUG, "NIV_FULL_DEBUG", "FULL_DEBUG", LOG_DEBUG}
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

  char nom_fonction[STR_LEN];

} ThreadLogContext_t;

/* threads keys */
static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

#define LogChanges(format, args...) \
  do { \
    if (LogComponents[COMPONENT_LOG].comp_log_type != TESTLOG || \
        LogComponents[COMPONENT_LOG].comp_log_level == NIV_FULL_DEBUG) \
      DisplayLogComponentLevel(COMPONENT_LOG, (char *)__FUNCTION__, \
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
  exit(1);
}

/* Feel free to add what you want here. This is a collection
 * of debug info that will be printed when a log message is
 * printed that matches or exceeds the severity level of
 * component LOG_MESSAGE_DEBUGINFO. */
extern uint32_t open_fd_count;
char *get_debug_info(int *size) {
  int rc, i, bt_str_size, offset, BT_MAX = 256;
  long bt_data[BT_MAX];
  char **bt_str, *debug_str, *final_bt_str;
  int ret;

  struct rlimit rlim = {
    .rlim_cur = RLIM_INFINITY,
    .rlim_max = RLIM_INFINITY
  };

  rc = backtrace((void **)&bt_data, BT_MAX);
  if (rc > 0)
    bt_str = backtrace_symbols((void **)&bt_data, rc);
  else
    return NULL;
  if (bt_str == NULL || *bt_str == NULL)
    return NULL;

  // Form a single printable string from array of backtrace symbols
  bt_str_size = 0;
  for(i=0; i < rc; i++)
    bt_str_size += strlen(bt_str[i]);
  final_bt_str = malloc(sizeof(char)*(bt_str_size+rc+20));
  offset = 0;
  for(i=0; i < rc; i++)
    {
      // note: strlen excludes '\0', strcpy includes '\0'
      strncpy(final_bt_str+offset, bt_str[i], strlen(bt_str[i]));
      offset += strlen(bt_str[i]);
      final_bt_str[offset++] = '\n';
    }
  final_bt_str[offset-1] = '\0';
  free(bt_str);

  getrlimit(RLIMIT_NOFILE, &rlim);

  debug_str = malloc(sizeof(char) * strlen(final_bt_str)
                     + sizeof(char) * 512);
  if (debug_str == NULL) {
    free(bt_str);
    return NULL;
  }

  ret = sprintf(debug_str,
                "\nDEBUG INFO -->\n"
                "backtrace:\n%s\n\n"
                "open_fd_count        = %-6d\n"
                "rlimit_cur           = %-6ld\n"
                "rlimit_max           = %-6ld\n"
                "<--DEBUG INFO\n\n",
                final_bt_str,
                open_fd_count,
                rlim.rlim_cur,
                rlim.rlim_max);
  if (size != NULL)
    *size = ret;

  return debug_str;
}

void print_debug_info_fd(int fd)
{
  char *str = get_debug_info(NULL);
  int rc;

  if (str != NULL)
    {
      rc = write(fd, str, strlen(str));
      free(str);
    }
}

void print_debug_info_file(FILE *flux)
{
  char *str = get_debug_info(NULL);

  if (str != NULL)
    {
      fprintf(flux, "%s", str);
      free(str);
    }
}

void print_debug_info_syslog(int level)
{
  int size;
  char *debug_str = get_debug_info(&size);
  char *end_c=debug_str, *first_c=debug_str;

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

  free(debug_str);
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


const char *emergency = "* log emergency *";

/**
 * GetThreadContext :
 * manages pthread_keys.
 */
static ThreadLogContext_t *Log_GetThreadContext(int ok_errors)
{
  ThreadLogContext_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      if (ok_errors)
        LogCrit(COMPONENT_LOG_EMERG,
                "Log_GetThreadContext - pthread_once returned %d (%s)",
                errno, strerror(errno));
      return NULL;
    }

  p_current_thread_vars = (ThreadLogContext_t *) pthread_getspecific(thread_key);

  /* we allocate the thread key if this is the first time */
  if(p_current_thread_vars == NULL)
    {
      /* allocates thread structure */
      p_current_thread_vars = (ThreadLogContext_t *) malloc(sizeof(ThreadLogContext_t));

      if(p_current_thread_vars == NULL)
        {
          if (ok_errors)
            LogCrit(COMPONENT_LOG_EMERG,
                    "Log_GetThreadContext - malloc returned %d (%s)",
                    errno, strerror(errno));
          return NULL;
        }

      /* inits thread structures */
      p_current_thread_vars->nom_fonction[0] = '\0';

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);

      if (ok_errors)
        LogFullDebug(COMPONENT_LOG_EMERG, "malloc => %p",
                     p_current_thread_vars);
    }

  return p_current_thread_vars;

}                               /* Log_GetThreadContext */

static inline const char *Log_GetThreadFunction(int ok_errors)
{
  ThreadLogContext_t *context = Log_GetThreadContext(ok_errors);

  if (context == NULL)
    return emergency;
  else
    return context->nom_fonction;
}

void GetNameFunction(char *name, int len)
{
  const char *s = Log_GetThreadFunction(0);

  if (s != emergency && s != NULL)
    strncpy(name, s, len);
  else
    snprintf(name, len, "Thread %p", (caddr_t)pthread_self());
}

/*
 * Convert a numeral log level in ascii to
 * the numeral value. 
 */
int ReturnLevelAscii(const char *LevelInAscii)
{
  int i = 0;

  for(i = 0; i < ARRAY_SIZE(tabLogLevel); i++)
    if(!strcmp(tabLogLevel[i].str, LevelInAscii))
      return tabLogLevel[i].value;

  /* If nothing found, return -1 */
  return -1;
}                               /* ReturnLevelAscii */

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
  strcpy(program_name, nom);
}                               /* SetNamePgm */

/*
 * Set the hostname.
 */
void SetNameHost(char *name)
{
  strcpy(hostname, name);
}                               /* SetNameHost */

/* Set the function name in progress. */
void SetNameFunction(char *nom)
{
  ThreadLogContext_t *context = Log_GetThreadContext(0);

  if (context != NULL)
    strcpy(context->nom_fonction, nom);
}                               /* SetNameFunction */

/* Installs a signal handler */
static void ArmSignal(int signal, void (*action) ())
{
  struct sigaction act;

  /* Placing fields of struct sigaction */
  act.sa_flags = 0;
  act.sa_handler = action;
  sigemptyset(&act.sa_mask);

  if(sigaction(signal, &act, NULL) == -1)
    LogCrit(COMPONENT_LOG,
            "Failed to arm signal %d, error %d (%s)",
            signal, errno, strerror(errno));
}                               /* ArmSignal */

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

  for (i = COMPONENT_ALL; i < COMPONENT_COUNT; i++)
      LogComponents[i].comp_log_level = level_to_set;
}                               /* _SetLevelDebug */

void SetLevelDebug(int level_to_set)
{
  _SetLevelDebug(level_to_set);

  LogChanges("Setting log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}

static void IncrementLevelDebug()
{
  _SetLevelDebug(ReturnLevelDebug() + 1);

  LogChanges("SIGUSR1 Increasing log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}                               /* IncrementLevelDebug */

static void DecrementLevelDebug()
{
  _SetLevelDebug(ReturnLevelDebug() - 1);

  LogChanges("SIGUSR2 Decreasing log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}                               /* DecrementLevelDebug */

void InitLogging()
{
  int i;
  char *env_value;
  int newlevel, component, oldlevel;

  /* Initialisation du tableau des familys */
  tab_family[0].num_family = 0;
  tab_family[0].tab_err = (family_error_t *) tab_system_err;
  strcpy(tab_family[0].name_family, "Errors Systeme UNIX");

  for(i = 1; i < MAX_NUM_FAMILY; i++)
    tab_family[i].num_family = UNUSED_SLOT;

  ArmSignal(SIGUSR1, IncrementLevelDebug);
  ArmSignal(SIGUSR2, DecrementLevelDebug);

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
      LogChanges("Using environment variable to switch log level for %s from "
                 "%s to %s",
                 LogComponents[component].comp_name, ReturnLevelInt(oldlevel),
                 ReturnLevelInt(newlevel));
    }

}                               /* InitLogging */

/*
 * A generic display function
 */

static void DisplayLogString_valist(char *buff_dest, char * function, log_components_t component, char *format, va_list arguments)
{
  char texte[STR_LEN_TXT];
  struct tm the_date;
  time_t tm;
  const char *threadname = Log_GetThreadFunction(component != COMPONENT_LOG_EMERG);

  tm = time(NULL);
  Localtime_r(&tm, &the_date);

  /* Writing to the chosen file. */
  log_vsnprintf(texte, STR_LEN_TXT, format, arguments);

  if(LogComponents[component].comp_log_level
     < LogComponents[LOG_MESSAGE_VERBOSITY].comp_log_level)
    snprintf(buff_dest, STR_LEN_TXT,
             "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld : %s : %s-%d[%s] :%s\n",
             the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
             the_date.tm_hour, the_date.tm_min, the_date.tm_sec, tm, hostname,
             program_name, getpid(), threadname,
             texte);
  else
    snprintf(buff_dest, STR_LEN_TXT,
             "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld : %s : %s-%d[%s] :%s :%s\n",
             the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
             the_date.tm_hour, the_date.tm_min, the_date.tm_sec, tm, hostname,
             program_name, getpid(), threadname, function,
             texte);
}                               /* DisplayLogString_valist */

static int DisplayLogSyslog_valist(log_components_t component, char * function,
                                   int level, char * format, va_list arguments)
{
  char texte[STR_LEN_TXT];
  const char *threadname = Log_GetThreadFunction(component != COMPONENT_LOG_EMERG);

  if( !syslog_opened )
   {
     openlog("nfs-ganesha", LOG_PID, LOG_USER);
     syslog_opened = 1;
   }

  /* Writing to the chosen file. */
  log_vsnprintf(texte, STR_LEN_TXT, format, arguments);

  if(LogComponents[component].comp_log_level < LogComponents[LOG_MESSAGE_VERBOSITY].comp_log_level)
    syslog(tabLogLevel[level].syslog_level, "[%s] :%s", threadname, texte);
  else
    syslog(tabLogLevel[level].syslog_level, "[%s] :%s :%s", threadname, function, texte);

  if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level
      && level != NIV_NULL)
      print_debug_info_syslog(level);

  return 1 ;
} /* DisplayLogSyslog_valist */

static int DisplayLogFlux_valist(FILE * flux, char * function, log_components_t component,
                                 int level, char *format, va_list arguments)
{
  char buffer[STR_LEN_TXT];

  DisplayLogString_valist(buffer, function, component, format, arguments);

  fprintf(flux, "%s", buffer);
  if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level
      && level != NIV_NULL)
    print_debug_info_file(flux);
  return fflush(flux);
}                               /* DisplayLogFlux_valist */

static int DisplayTest_valist(log_components_t component, char *format,
                              va_list arguments)
{
  char text[STR_LEN_TXT];

  log_vsnprintf(text, STR_LEN_TXT, format, arguments);

  fprintf(stdout, "%s\n", text);
  return fflush(stdout);
}

static int DisplayBuffer_valist(char *buffer, log_components_t component,
                                char *format, va_list arguments)
{
  return log_vsnprintf(buffer, STR_LEN_TXT, format, arguments);
}

static int DisplayLogPath_valist(char *path, char * function,
                                 log_components_t component, int level,
                                 char *format, va_list arguments)
{
  char buffer[STR_LEN_TXT];
  int fd, my_status;

  DisplayLogString_valist(buffer, function, component, format, arguments);

  if(path[0] != '\0')
    {
#ifdef _LOCK_LOG
      if((fd = open(path, O_WRONLY | O_SYNC | O_APPEND | O_CREAT, log_mask)) != -1)
        {
          /* A lock on file */
          struct flock lock_file;
          int rc;

          lock_file.l_type = F_WRLCK;
          lock_file.l_whence = SEEK_SET;
          lock_file.l_start = 0;
          lock_file.l_len = 0;

          if(fcntl(fd, F_SETLKW, (char *)&lock_file) != -1)
            {
              rc = write(fd, buffer, strlen(buffer));
              if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level
                  && level != NIV_NULL)
                print_debug_info_fd(fd);

              /* Release the lock */
              lock_file.l_type = F_UNLCK;

              fcntl(fd, F_SETLKW, (char *)&lock_file);

              close(fd);

              return SUCCES;
            }                   /* if fcntl */
          else
            {
              my_status = errno;
              close(fd);
            }
        }

#else
      if((fd = open(path, O_WRONLY | O_NONBLOCK | O_APPEND | O_CREAT, log_mask)) != -1)
        {
          if(write(fd, buffer, strlen(buffer)) < strlen(buffer))
          {
            fprintf(stderr, "Error: couldn't complete write to the log file, "
                    "ensure disk has not filled up");
            close(fd);

            return ERR_FILE_LOG;
          }
          if (level <= LogComponents[LOG_MESSAGE_DEBUGINFO].comp_log_level
              && level != NIV_NULL)
            print_debug_info_fd(fd);

          close(fd);

          return SUCCES;
        }
#endif
      else
        {
          my_status = errno;
        }
      fprintf(stderr, "Error %s : %s : status %d on file %s message was:\n%s\n",
              tab_system_err[ERR_FILE_LOG].label,
              tab_system_err[ERR_FILE_LOG].msg, my_status, path, buffer);

      return ERR_FILE_LOG;
    }
  /* if path */
  return SUCCES;
}                               /* DisplayLogPath_valist */

/*
 * Routines for managing error messages
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
  strcpy(tab_family[i].name_family, name_family);

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

int MakeLogError(char *buffer, int num_family, int num_error, int status,
                  int ma_ligne)
{
  family_error_t *tab_err = NULL;
  family_error_t the_error;

  /* Find the family */
  if((tab_err = FindTabErr(num_family)) == NULL)
    return -1;

  /* find the error */
  the_error = FindErr(tab_err, num_error);

  if(status == 0)
    {
      return sprintf(buffer, "Error %s : %s : status %d : Line %d",
                     the_error.label, the_error.msg, status, ma_ligne);
    }
  else
    {
      char tempstr[1024];
      char *errstr;
      errstr = strerror_r(status, tempstr, 1024);

      return sprintf(buffer, "Error %s : %s : status %d : %s : Line %d",
                     the_error.label, the_error.msg, status, errstr, ma_ligne);
    }
}                               /* MakeLogError */

/* A sprintf personnal is é */
/* This macro is used each time the parsing advances */
#define ONE_STEP  do { iterformat +=1 ; len += 1; } while(0)

#define NO_TYPE       0
#define INT_TYPE      1
#define LONG_TYPE     2
#define CHAR_TYPE     3
#define STRING_TYPE   4
#define FLOAT_TYPE    5
#define DOUBLE_TYPE   6
#define POINTEUR_TYPE 7

/* Type specifiques a la log */
#define EXTENDED_TYPE 8

#define STATUS_SHORT       1
#define STATUS_LONG        2
#define CONTEXTE_SHORT     3
#define CONTEXTE_LONG      4
#define ERREUR_SHORT       5
#define ERREUR_LONG        6
#define ERRNUM_SHORT       7
#define ERRNUM_LONG        8
#define ERRCTX_SHORT       9
#define ERRCTX_LONG        10
#define CHANGE_ERR_FAMILY  11
#define CHANGE_CTX_FAMILY  12
#define ERRNO_SHORT        13
#define ERRNO_LONG         14

#define NO_LONG 0
#define SHORT_LG 1
#define LONG_LG 2
#define LONG_LONG_LG 3

#define MAX_STR_TOK LOG_MAX_STRLEN

int log_vsnprintf(char *out, size_t taille, char *format, va_list arguments)
{
  /* TODO: eventually remove this entirely, but this makes the code
   * work for now */
  return vsnprintf(out, taille, format, arguments);
}

int log_snprintf(char *out, size_t n, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = log_vsnprintf(out, n, format, arguments);
  va_end(arguments);

  return rc;
}

int log_fprintf(FILE * file, char *format, ...)
{
  va_list arguments;
  char tmpstr[LOG_MAX_STRLEN];
  int rc;

  va_start(arguments, format);
  memset(tmpstr, 0, LOG_MAX_STRLEN);
  rc = log_vsnprintf(tmpstr, LOG_MAX_STRLEN, format, arguments);
  va_end(arguments);
  fputs(tmpstr, file);
  return rc;
}

log_component_info __attribute__ ((__unused__)) LogComponents[COMPONENT_COUNT] =
{
  { COMPONENT_ALL,               "COMPONENT_ALL", "",
    NIV_EVENT,
  },
  { COMPONENT_LOG,               "COMPONENT_LOG", "LOG",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_LOG_EMERG,         "COMPONENT_LOG_EMERG", "LOG",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_MEMALLOC,          "COMPONENT_MEMALLOC", "MEM ALLOC",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_MEMLEAKS,          "COMPONENT_MEMLEAKS", "MEM LEAKS",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_FSAL,              "COMPONENT_FSAL", "FSAL",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFSPROTO,          "COMPONENT_NFSPROTO", "NFS PROTO",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_V4,            "COMPONENT_NFS_V4", "NFS V4",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_V4_PSEUDO,     "COMPONENT_NFS_V4_PSEUDO", "NFS V4 PSEUDO",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_FILEHANDLE,        "COMPONENT_FILEHANDLE", "FILE HANDLE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_SHELL,         "COMPONENT_NFS_SHELL", "NFS SHELL",
#ifdef _DEBUG_NFS_SHELL
    NIV_FULL_DEBUG,
#else
    NIV_EVENT,
#endif
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_DISPATCH,          "COMPONENT_DISPATCH", "DISPATCH",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CACHE_CONTENT,     "COMPONENT_CACHE_CONTENT", "CACHE CONTENT",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CACHE_INODE,       "COMPONENT_CACHE_INODE", "CACHE INODE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CACHE_INODE_GC,    "COMPONENT_CACHE_INODE_GC", "CACHE INODE GC",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CACHE_INODE_LRU,    "COMPONENT_CACHE_INODE_LRU", "CACHE INODE LRU",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_HASHTABLE,         "COMPONENT_HASHTABLE", "HASH TABLE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_HASHTABLE_CACHE,   "COMPONENT_HASHTABLE_CACHE", "HASH TABLE CACHE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_LRU,               "COMPONENT_LRU", "LRU",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_DUPREQ,            "COMPONENT_DUPREQ", "DUPREQ",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_RPCSEC_GSS,        "COMPONENT_RPCSEC_GSS", "RPCSEC GSS",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_INIT,              "COMPONENT_INIT", "NFS STARTUP",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_MAIN,              "COMPONENT_MAIN", "MAIN",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_IDMAPPER,          "COMPONENT_IDMAPPER", "ID MAPPER",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_READDIR,       "COMPONENT_NFS_READDIR", "NFS READDIR",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },

  { COMPONENT_NFS_V4_LOCK,       "COMPONENT_NFS_V4_LOCK", "NFS V4 LOCK",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_V4_XATTR,      "COMPONENT_NFS_V4_XATTR", "NFS V4 XATTR",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_V4_REFERRAL,   "COMPONENT_NFS_V4_REFERRAL", "NFS V4 REFERRAL",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_MEMCORRUPT,        "COMPONENT_MEMCORRUPT", "MEM CORRUPT",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CONFIG,            "COMPONENT_CONFIG", "CONFIG",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_CLIENTID,          "COMPONENT_CLIENTID", "CLIENT ID",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_STDOUT,            "COMPONENT_STDOUT", "STDOUT",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_SESSIONS,          "COMPONENT_SESSIONS", "SESSIONS",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_PNFS,              "COMPONENT_PNFS", "PNFS",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_RPC_CACHE,         "COMPONENT_RPC_CACHE", "RPC CACHE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_RW_LOCK,           "COMPONENT_RW_LOCK", "RW LOCK",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NLM,               "COMPONENT_NLM", "NLM",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_RPC,               "COMPONENT_RPC", "RPC",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_CB,            "COMPONENT_NFS_CB", "NFS CB",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_THREAD,            "COMPONENT_THREAD", "THREAD",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_NFS_V4_ACL,        "COMPONENT_NFS_V4_ACL", "NFS V4 ACL",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_STATE,             "COMPONENT_STATE", "STATE",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_9P,                "COMPONENT_9P", "9P",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_9P_DISPATCH,       "COMPONENT_9P_DISPATCH", "9P DISPATCH",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_FSAL_UP,             "COMPONENT_FSAL_UP", "FSAL_UP",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { COMPONENT_DBUS,                "COMPONENT_DBUS", "DBUS",
    NIV_EVENT,
    SYSLOG,
    "SYSLOG"
  },
  { LOG_MESSAGE_DEBUGINFO,        "LOG_MESSAGE_DEBUGINFO",
                                  "LOG MESSAGE DEBUGINFO",
    NIV_NULL,
    SYSLOG,
    "SYSLOG"
  },
  { LOG_MESSAGE_VERBOSITY,        "LOG_MESSAGE_VERBOSITY",
                                  "LOG MESSAGE VERBOSITY",
    NIV_NULL,
    SYSLOG,
    "SYSLOG"
  },
};

int DisplayLogComponentLevel(log_components_t component,
                             char * function,
                             log_levels_t level,
                             char *format, ...)
{
  va_list arguments;
  int rc;
  va_start(arguments, format);

  switch(LogComponents[component].comp_log_type)
    {
    case SYSLOG:
      rc = DisplayLogSyslog_valist(component, function, level, format, arguments);
      break;
    case FILELOG:
      rc = DisplayLogPath_valist(LogComponents[component].comp_log_file, function, component, level, format, arguments);
      break;
    case STDERRLOG:
      rc = DisplayLogFlux_valist(stderr, function, component, level, format, arguments);
      break;
    case STDOUTLOG:
      rc = DisplayLogFlux_valist(stdout, function, component, level, format, arguments);
      break;
    case TESTLOG:
      rc = DisplayTest_valist(component, format, arguments);
      break;
    case BUFFLOG:
      rc = DisplayBuffer_valist(LogComponents[component].comp_buffer, component, format, arguments);
      break;
    default:
      rc = ERR_FAILURE;
    }

  va_end(arguments);

  if(level == NIV_FATAL)
    Fatal();

  return rc;
}

int DisplayErrorComponentLogLine(log_components_t component,
                                 char * function,
                                 int num_family,
                                 int num_error,
                                 int status,
                                 int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(MakeLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;
  return DisplayLogComponentLevel(component, function, NIV_CRIT, "%s: %s",
                                  LogComponents[component].comp_str, buffer);
}                               /* DisplayErrorLogLine */

static int isValidLogPath(char *pathname)
{
  char tempname[MAXPATHLEN];

  char *directory_name;
  int rc;

  strncpy(tempname, pathname, MAXPATHLEN);

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

/*
 * Sets the default logging method (whether to a specific filepath or syslog.
 * During initialization this is used and separate layer logging defaults to
 * this destination.
 */
void SetDefaultLogging(char *name)
{
  int component;

  SetComponentLogFile(COMPONENT_LOG, name);

  LogChanges("Setting log destination for ALL components to %s", name);
  for(component = COMPONENT_ALL; component < COMPONENT_COUNT; component++)
    {
      if (component == COMPONENT_STDOUT)
        continue;
      SetComponentLogFile(component, name);
    }
}                               /* SetDefaultLogging */

int SetComponentLogFile(log_components_t component, char *name)
{
  int newtype, changed;

  if (strcmp(name, "SYSLOG") == 0)
    newtype = SYSLOG;
  else if (strcmp(name, "STDERR") == 0)
    newtype = STDERRLOG;
  else if (strcmp(name, "STDOUT") == 0)
    newtype = STDOUTLOG;
  else if (strcmp(name, "TEST") == 0)
    newtype = TESTLOG;
  else
    newtype = FILELOG;

  if (newtype == FILELOG)
    {
      if (!isValidLogPath(name))
        {
          LogMajor(COMPONENT_LOG, "Could not set default logging to %s", name);
          errno = EINVAL;
          return -1;
        }
      }

  changed = (newtype != LogComponents[component].comp_log_type) ||
            (newtype == FILELOG && strcmp(name, LogComponents[component].comp_log_file) != 0);

  if (component != COMPONENT_LOG && changed)
    LogChanges("Changing log destination for %s from %s to %s",
               LogComponents[component].comp_name,
               LogComponents[component].comp_log_file,
               name);

  LogComponents[component].comp_log_type = newtype;
  strncpy(LogComponents[component].comp_log_file, name, MAXPATHLEN);

  if (component == COMPONENT_LOG && changed)
    LogChanges("Changing log destination for %s from %s to %s",
               LogComponents[component].comp_name,
               LogComponents[component].comp_log_file,
               name);

  return 0;
}                               /* SetComponentLogFile */

void SetComponentLogBuffer(log_components_t component, char *buffer)
{
  LogComponents[component].comp_log_type = BUFFLOG;
  LogComponents[component].comp_buffer   = buffer;
}

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void
rpc_warnx(/* const */ char *fmt, ...)
{
    va_list ap;
    log_components_t comp = COMPONENT_RPC;
    int level;

    level = LogComponents[comp].comp_log_level;
    if (level < NIV_DEBUG)
        goto out;

    va_start(ap, fmt);

    switch(LogComponents[comp].comp_log_type) {
    case SYSLOG:
      DisplayLogSyslog_valist(comp, "rpc", level, fmt, ap);
      break;
    case FILELOG:
      DisplayLogPath_valist(LogComponents[comp].comp_log_file, "rpc",
                            comp, level, fmt, ap);
      break;
    case STDERRLOG:
      DisplayLogFlux_valist(stderr, "rpc", comp, level, fmt, ap);
      break;
    case STDOUTLOG:
      DisplayLogFlux_valist(stdout, "rpc", comp, level, fmt, ap);
      break;
    case TESTLOG:
      DisplayTest_valist(comp, fmt, ap);
      break;
    case BUFFLOG:
      DisplayBuffer_valist(LogComponents[comp].comp_buffer, comp, fmt, ap);
    } /* switch */

    va_end(ap);

out:
    return;

} /* rpc_warnx */

/*
 * For info : printf tags that can be used:
 * w DMNOPQTUWX
 */

#ifdef _SNMP_ADM_ACTIVE

int getComponentLogLevel(snmp_adm_type_union * param, void *opt)
{
  long component = (long)opt;

  strcpy(param->string, ReturnLevelInt(LogComponents[component].comp_log_level));
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
