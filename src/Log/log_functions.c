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
 * ensemble des fonctions d'affichage et de gestion des erreurs
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

#include "log_macros.h"
//#include "nfs_core.h"

/* La longueur d'une chaine */
#define STR_LEN_TXT      2048
#define PATH_LEN         1024
#define MAX_STR_LEN      1024
#define MAX_NUM_FAMILY  50
#define UNUSED_SLOT -1

log_level_t tabLogLevel[NB_LOG_LEVEL] =
{
  {NIV_NULL,       "NIV_NULL",       "NULL",       LOG_NOTICE},
  {NIV_FATAL,      "NIV_FATAL",      "FATAL",      LOG_CRIT},
  {NIV_MAJ,        "NIV_MAJ",        "MAJ",        LOG_CRIT},
  {NIV_CRIT,       "NIV_CRIT",       "CRIT",       LOG_ERR},
  {NIV_WARN,       "NIV_WARN",       "WARN",       LOG_WARNING},
  {NIV_EVENT,      "NIV_EVENT",      "EVENT",      LOG_NOTICE},
  {NIV_INFO,       "NIV_INFO",       "INFO",       LOG_INFO},
  {NIV_DEBUG,      "NIV_DEBUG",      "DEBUG",      LOG_DEBUG},
  {NIV_FULL_DEBUG, "NIV_FULL_DEBUG", "FULL_DEBUG", LOG_DEBUG}
};

/* Function names for logging and SNMP stats etc. */

const char *fsal_function_names[] = {
  "FSAL_lookup", "FSAL_access", "FSAL_create", "FSAL_mkdir", "FSAL_truncate",
  "FSAL_getattrs", "FSAL_setattrs", "FSAL_link", "FSAL_opendir", "FSAL_readdir",
  "FSAL_closedir", "FSAL_open", "FSAL_read", "FSAL_write", "FSAL_close",
  "FSAL_readlink", "FSAL_symlink", "FSAL_rename", "FSAL_unlink", "FSAL_mknode",
  "FSAL_unused_20", "FSAL_dynamic_fsinfo", "FSAL_rcp", "FSAL_Init",
  "FSAL_get_stats", "FSAL_unused_25", "FSAL_unused_26", "FSAL_unused_27",
  "FSAL_BuildExportContext", "FSAL_InitClientContext", "FSAL_GetClientContext",
  "FSAL_lookupPath", "FSAL_lookupJunction", "FSAL_test_access",
  "FSAL_rmdir", "FSAL_CleanObjectResources", "FSAL_open_by_name", "FSAL_open_by_fileid",
  "FSAL_ListXAttrs", "FSAL_GetXAttrValue", "FSAL_SetXAttrValue", "FSAL_GetXAttrAttrs",
  "FSAL_close_by_fileid", "FSAL_setattr_access", "FSAL_merge_attrs", "FSAL_rename_access",
  "FSAL_unlink_access", "FSAL_link_access", "FSAL_create_access", "FSAL_unused_49", "FSAL_CleanUpExportContext",
  "FSAL_getextattrs", "FSAL_sync", "FSAL_lock_op", "FSAL_unused_58"
};

/* les code d'error */
errctx_t __attribute__ ((__unused__)) tab_systeme_err[] =
{
  {SUCCES, "SUCCES", "No Error"},
  {ERR_FAILURE, "FAILURE", "Une error est survenue"},
  {ERR_EVNT, "EVNT", "Evennement survenu"},
  {ERR_PTHREAD_KEY_CREATE, "ERR_PTHREAD_KEY_CREATE", "Error in creation of pthread_keys"},
  {ERR_MALLOC, "ERR_MALLOC", "malloc impossible"},
  {ERR_SIGACTION, "ERR_SIGACTION", "sigaction impossible"},
  {ERR_PTHREAD_ONCE, "ERR_PTHREAD_ONCE", "pthread_once impossible"},
  {ERR_FICHIER_LOG, "ERR_FICHIER_LOG", "impossible d'acceder au fichier de log"},
  {ERR_GETHOSTBYNAME, "ERR_GETHOSTBYNAME", "gethostbyname impossible"},
  {ERR_MMAP, "ERR_MMAP", "mmap impossible"},
  {ERR_SOCKET, "ERR_SOCKET", "socket impossible"},
  {ERR_BIND, "ERR_BIND", "bind impossible"},
  {ERR_CONNECT, "ERR_CONNECT", "connect impossible"},
  {ERR_LISTEN, "ERR_LISTEN", "listen impossible"},
  {ERR_ACCEPT, "ERR_ACCEPT", "accept impossible"},
  {ERR_RRESVPORT, "ERR_RRESVPORT", "rresvport impossible"},
  {ERR_GETHOSTNAME, "ERR_GETHOSTNAME", "gethostname impossible"},
  {ERR_GETSOCKNAME, "ERR_GETSOCKNAME", "getsockname impossible"},
  {ERR_IOCTL, "ERR_IOCTL", "ioctl impossible"},
  {ERR_UTIME, "ERR_UTIME", "utime impossible"},
  {ERR_XDR, "ERR_XDR", "Un appel XDR a echoue"},
  {ERR_CHMOD, "ERR_CHMOD", "chmod impossible"},
  {ERR_SEND, "ERR_SEND", "send impossible"},
  {ERR_GETHOSTBYADDR, "ERR_GETHOSTBYADDR", "gethostbyaddr impossible"},
  {ERR_PREAD, "ERR_PREAD", "pread impossible"},
  {ERR_PWRITE, "ERR_PWRITE", "pwrite impossible"},
  {ERR_STAT, "ERR_STAT", "stat impossible"},
  {ERR_GETPEERNAME, "ERR_GETPEERNAME", "getpeername impossible"},
  {ERR_FORK, "ERR_FORK", "fork impossible"},
  {ERR_GETSERVBYNAME, "ERR_GETSERVBYNAME", "getservbyname impossible"},
  {ERR_MUNMAP, "ERR_MUNMAP", "munmap impossible"},
  {ERR_STATVFS, "ERR_STATVFS", "statvfs impossible"},
  {ERR_OPENDIR, "ERR_OPENDIR", "opendir impossible"},
  {ERR_READDIR, "ERR_READDIR", "readdir impossible"},
  {ERR_CLOSEDIR, "ERR_CLOSEDIR", "closedir impossible"},
  {ERR_LSTAT, "ERR_LSTAT", "lstat impossible"},
  {ERR_GETWD, "ERR_GETWD", "getwd impossible"},
  {ERR_CHDIR, "ERR_CHDIR", "chdir impossible"},
  {ERR_CHOWN, "ERR_CHOWN", "chown impossible"},
  {ERR_MKDIR, "ERR_MKDIR", "mkdir impossible"},
  {ERR_OPEN, "ERR_OPEN", "open impossible"},
  {ERR_READ, "ERR_READ", "read impossible"},
  {ERR_WRITE, "ERR_WRITE", "write impossible"},
  {ERR_UTIMES, "ERR_UTIMES", "utimes impossible"},
  {ERR_READLINK, "ERR_READLINK", "readlink impossible"},
  {ERR_SYMLINK, "ERR_SYMLINK", "symlink impossible"},
  {ERR_SYSTEM, "ERR_SYSTEM", "system impossible"},
  {ERR_POPEN, "ERR_POPEN", "popen impossible"},
  {ERR_LSEEK, "ERR_LSEEK", "lseek impossible"},
  {ERR_PTHREAD_CREATE, "ERR_PTHREAD_CREATE", "pthread_create impossible"},
  {ERR_RECV, "ERR_RECV", "recv impossible"},
  {ERR_FOPEN, "ERR_FOPEN", "fopen impossible"},
  {ERR_GETCWD, "ERR_GETCWD", "getcwd impossible"},
  {ERR_SETUID, "ERR_SETUID", "setuid impossible"},
  {ERR_RENAME, "ERR_RENAME", "rename impossible"},
  {ERR_UNLINK, "ERR_UNLINK", "unlink impossible"},
  {ERR_SELECT, "ERR_SELECT", "select impossible"},
  {ERR_WAIT, "ERR_WAIT", "wait impossible"},
  {ERR_SETSID, "ERR_SETSID", "setsid impossible"},
  {ERR_SETGID, "ERR_SETGID", "setgid impossible"},
  {ERR_GETGROUPS, "ERR_GETGROUPS", "getgroups impossible"},
  {ERR_SETGROUPS, "ERR_SETGROUPS", "setgroups impossible"},
  {ERR_UMASK, "ERR_UMASK", "umask impossible"},
  {ERR_CREAT, "ERR_CREAT", "creat impossible"},
  {ERR_SETSOCKOPT, "ERR_SETSOCKOPT", "setsockopt impossible"},
  {ERR_DIRECTIO, "ERR_DIRECTIO", "appel a directio impossible"},
  {ERR_GETRLIMIT, "ERR_GETRLIMIT", "appel a getrlimit impossible"},
  {ERR_SETRLIMIT, "ERR_SETRLIMIT", "appel a setrlimit"},
  {ERR_TRUNCATE, "ERR_TRUNCATE", "appel a truncate impossible"},
  {ERR_PTHREAD_MUTEX_INIT, "ERR_PTHREAD_MUTEX_INIT", "init d'un mutex"},
  {ERR_PTHREAD_COND_INIT, "ERR_PTHREAD_COND_INIT", "init d'une variable de condition"},
  {ERR_FCNTL, "ERR_FCNTL", "call to fcntl is impossible"},
  {ERR_NULL, "ERR_NULL", ""}
};

/* constants */
static int masque_log = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

/* Array of error families */

static family_t tab_family[MAX_NUM_FAMILY];

/* Global variables */

static char nom_programme[1024];
static char nom_host[256];
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

static const char *Log_GetThreadFunction(int ok_errors)
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
 * Fait la conversion nom du niveau de log
 * en ascii vers valeur numerique du niveau
 *
 */

int ReturnLevelAscii(const char *LevelEnAscii)
{
  int i = 0;

  for(i = 0; i < NB_LOG_LEVEL; i++)
    if(!strcmp(tabLogLevel[i].str, LevelEnAscii))
      return tabLogLevel[i].value;

  /* Si rien n'est trouve on retourne -1 */
  return -1;
}                               /* ReturnLevelAscii */

char *ReturnLevelInt(int level)
{
  int i = 0;

  for(i = 0; i < NB_LOG_LEVEL; i++)
    if(tabLogLevel[i].value == level)
      return tabLogLevel[i].str;

  /* Si on n'a rien trouve on retourne NULL */
  return NULL;
}                               /* ReturnLevelInt */

/*
 * Set le nom du programme en cours.
 */
void SetNamePgm(char *nom)
{

  /* Cette fonction n'est pas thread-safe car le nom du programme
   * est commun a tous les threads */
  strcpy(nom_programme, nom);
}                               /* SetNamePgm */

/*
 * Set le nom d'host en cours
 */
void SetNameHost(char *nom)
{
  strcpy(nom_host, nom);
}                               /* SetNameHost */

/*
 *
 * Set le nom de la fonction en cours
 *
 */
void SetNameFunction(char *nom)
{
  ThreadLogContext_t *context = Log_GetThreadContext(0);

  if (context != NULL)
    strcpy(context->nom_fonction, nom);
}                               /* SetNameFunction */

/*
 * Cette fonction permet d'installer un handler de signal
 */

static void ArmeSignal(int signal, void (*action) ())
{
  struct sigaction act;         /* Soyons POSIX et puis signal() c'est pas joli */

  /* Mise en place des champs du struct sigaction */
  act.sa_flags = 0;
  act.sa_handler = action;
  sigemptyset(&act.sa_mask);

  if(sigaction(signal, &act, NULL) == -1)
    LogCrit(COMPONENT_LOG,
            "Impossible to arm signal %d, error %d (%s)",
            signal, errno, strerror(errno));
}                               /* ArmeSignal */

/*
 *
 * Cinq fonctions pour gerer le niveau de debug et le controler
 * a distance au travers de handlers de signaux
 *
 * InitDebug
 * IncrementeLevelDebug
 * DecrementeLevelDebug
 * SetLevelDebug
 * ReturnLevelDebug
 *
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
}                               /* SetLevelDebug */

void SetLevelDebug(int level_to_set)
{
  _SetLevelDebug(level_to_set);

  LogChanges("Setting log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}

static void IncrementeLevelDebug()
{
  _SetLevelDebug(ReturnLevelDebug() + 1);

  LogChanges("SIGUSR1 Increasing log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}                               /* IncrementeLevelDebug */

static void DecrementeLevelDebug()
{
  _SetLevelDebug(ReturnLevelDebug() - 1);

  LogChanges("SIGUSR2 Decreasing log level for all components to %s",
             ReturnLevelInt(LogComponents[COMPONENT_ALL].comp_log_level));
}                               /* DecrementeLevelDebug */

void InitLogging()
{
  int i;
  char *env_value;
  int newlevel, component, oldlevel;

  /* Initialisation du tableau des familys */
  tab_family[0].num_family = 0;
  tab_family[0].tab_err = (family_error_t *) tab_systeme_err;
  strcpy(tab_family[0].name_family, "Errors Systeme UNIX");

  for(i = 1; i < MAX_NUM_FAMILY; i++)
    tab_family[i].num_family = UNUSED_SLOT;

  ArmeSignal(SIGUSR1, IncrementeLevelDebug);
  ArmeSignal(SIGUSR2, DecrementeLevelDebug);

  for(component = COMPONENT_ALL; component < COMPONENT_COUNT; component++)
    {
      env_value = getenv(LogComponents[component].comp_name);
      if (env_value == NULL)
        continue;
      newlevel = ReturnLevelAscii(env_value);
      if (newlevel == -1) {
        LogMajor(COMPONENT_LOG,
                 "Environment variable %s exists, but the value %s is not a valid log level.",
                 LogComponents[component].comp_name, env_value);
        continue;
      }
      oldlevel = LogComponents[component].comp_log_level;
      LogComponents[component].comp_log_level = newlevel;
      LogChanges("Using environment variable to switch log level for %s from %s to %s",
                 LogComponents[component].comp_name, ReturnLevelInt(oldlevel),
                 ReturnLevelInt(newlevel));
    }

}                               /* InitLevelDebug */

/*
 * Une fonction d'affichage tout a fait generique
 */

static void DisplayLogString_valist(char *buff_dest, char * function, log_components_t component, char *format, va_list arguments)
{
  char texte[STR_LEN_TXT];
  struct tm the_date;
  time_t tm;
  const char *threadname = Log_GetThreadFunction(component != COMPONENT_LOG_EMERG);

  tm = time(NULL);
  Localtime_r(&tm, &the_date);

  /* Ecriture sur le fichier choisi */
  log_vsnprintf(texte, STR_LEN_TXT, format, arguments);

  if(LogComponents[component].comp_log_level < LogComponents[LOG_MESSAGE_VERBOSITY].comp_log_level)
    snprintf(buff_dest, STR_LEN_TXT,
             "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld : %s : %s-%d[%s] :%s\n",
             the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
             the_date.tm_hour, the_date.tm_min, the_date.tm_sec, tm, nom_host,
             nom_programme, getpid(), threadname,
             texte);
  else
    snprintf(buff_dest, STR_LEN_TXT,
             "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld : %s : %s-%d[%s] :%s :%s\n",
             the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
             the_date.tm_hour, the_date.tm_min, the_date.tm_sec, tm, nom_host,
             nom_programme, getpid(), threadname, function,
             texte);
}                               /* DisplayLogString_valist */

static int DisplayLogSyslog_valist(log_components_t component, char * function, int level, char * format, va_list arguments)
{
  char texte[STR_LEN_TXT];
  const char *threadname = Log_GetThreadFunction(component != COMPONENT_LOG_EMERG);

  if( !syslog_opened )
   {
     openlog("nfs-ganesha", LOG_PID, LOG_USER);
     syslog_opened = 1;
   }

  /* Ecriture sur le fichier choisi */
  log_vsnprintf(texte, STR_LEN_TXT, format, arguments);

  if(LogComponents[component].comp_log_level < LogComponents[LOG_MESSAGE_VERBOSITY].comp_log_level)
    syslog(tabLogLevel[level].syslog_level, "[%s] :%s", threadname, texte);
  else
    syslog(tabLogLevel[level].syslog_level, "[%s] :%s :%s", threadname, function, texte);

  return 1 ;
} /* DisplayLogSyslog_valist */

static int DisplayLogFlux_valist(FILE * flux, char * function, log_components_t component, char *format, va_list arguments)
{
  char tampon[STR_LEN_TXT];

  DisplayLogString_valist(tampon, function, component, format, arguments);

  fprintf(flux, "%s", tampon);
  return fflush(flux);
}                               /* DisplayLogFlux_valist */

static int DisplayTest_valist(log_components_t component, char *format, va_list arguments)
{
  char text[STR_LEN_TXT];

  log_vsnprintf(text, STR_LEN_TXT, format, arguments);

  fprintf(stdout, "%s\n", text);
  return fflush(stdout);
}

static int DisplayBuffer_valist(char *buffer, log_components_t component, char *format, va_list arguments)
{
  return log_vsnprintf(buffer, STR_LEN_TXT, format, arguments);
}

static int DisplayLogPath_valist(char *path, char * function, log_components_t component, char *format, va_list arguments)
{
  char tampon[STR_LEN_TXT];
  int fd, my_status;

  DisplayLogString_valist(tampon, function, component, format, arguments);

  if(path[0] != '\0')
    {
#ifdef _LOCK_LOG
      if((fd = open(path, O_WRONLY | O_SYNC | O_APPEND | O_CREAT, masque_log)) != -1)
        {
          /* un verrou sur fichier */
          struct flock lock_file;

          /* mise en place de la structure de verrou sur fichier */
          lock_file.l_type = F_WRLCK;
          lock_file.l_whence = SEEK_SET;
          lock_file.l_start = 0;
          lock_file.l_len = 0;

          if(fcntl(fd, F_SETLKW, (char *)&lock_file) != -1)
            {
              /* Si la prise du verrou est OK */
              write(fd, tampon, strlen(tampon));

              /* Relache du verrou sur fichier */
              lock_file.l_type = F_UNLCK;

              fcntl(fd, F_SETLKW, (char *)&lock_file);

              /* fermeture du fichier */
              close(fd);
              return SUCCES;
            }                   /* if fcntl */
          else
            {
              /* Si la prise du verrou a fait un probleme */
              my_status = errno;
              close(fd);
            }
        }

#else
      if((fd = open(path, O_WRONLY | O_NONBLOCK | O_APPEND | O_CREAT, masque_log)) != -1)
        {
          if(write(fd, tampon, strlen(tampon)) < strlen(tampon))
          {
            fprintf(stderr, "Error: couldn't complete write to the log file, ensure disk has not filled up");
            close(fd);
            return ERR_FICHIER_LOG;
          }

          /* fermeture du fichier */
          close(fd);
          return SUCCES;
        }
#endif
      else
        {
          /* Si l'ouverture du fichier s'est mal passee */
          my_status = errno;
        }
      fprintf(stderr, "Error %s : %s : status %d on file %s message was:\n%s\n",
              tab_systeme_err[ERR_FICHIER_LOG].label,
              tab_systeme_err[ERR_FICHIER_LOG].msg, my_status, path, tampon);

      return ERR_FICHIER_LOG;
    }
  /* if path */
  return SUCCES;
}                               /* DisplayLogPath_valist */

/*
 *
 * Les routines de gestions des messages d'erreur
 *
 */

int AddFamilyError(int num_family, char *name_family, family_error_t * tab_err)
{
  int i = 0;

  /* Le numero de la family est entre -1 et MAX_NUM_FAMILY */
  if((num_family < -1) || (num_family >= MAX_NUM_FAMILY))
    return -1;

  /* On n'occupe pas 0 car ce sont les erreurs du systeme */
  if(num_family == 0)
    return -1;

  /* On cherche une entree vacante */
  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == UNUSED_SLOT)
      break;

  /* On verifie que la table n'est pas pleine */
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
      {
        /* A quoi sert cette ligne ??????!!!!!! */
        /* tab_family[i].num_family = UNUSED_SLOT ; */
        return tab_family[i].name_family;
      }

  /* Sinon on retourne NULL */
  return NULL;
}                               /* ReturnFamilyError */

/* Cette fonction trouve une family dans le tabelau des familys d'erreurs */
static family_error_t *TrouveTabErr(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    {
      if(tab_family[i].num_family == num_family)
        {
          return tab_family[i].tab_err;
        }
    }

  /* Sinon on retourne NULL */
  return NULL;
}                               /* TrouveTabErr */

static family_error_t TrouveErr(family_error_t * tab_err, int num)
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
}                               /* TrouveErr */

int MakeLogError(char *buffer, int num_family, int num_error, int status,
                  int ma_ligne)
{
  family_error_t *tab_err = NULL;
  family_error_t the_error;

  /* Find the family */
  if((tab_err = TrouveTabErr(num_family)) == NULL)
    return -1;

  /* find the error */
  the_error = TrouveErr(tab_err, num_error);

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

/* Un sprintf personnalisé */
/* Cette macro est utilisee a chaque fois que l'on avance d'un pas dans le parsing */
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
#ifdef _DEBUG_MEMLEAKS
    NIV_FULL_DEBUG,
#else
    NIV_EVENT,
#endif
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
  { COMPONENT_HASHTABLE,         "COMPONENT_HASHTABLE", "HASH TABLE",
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
  { COMPONENT_CLIENT_ID_COMPUTE, "COMPONENT_CLIENT_ID_COMPUTE", "CLIENT ID COMPUTE",
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
  { COMPONENT_MFSL,              "COMPONENT_MFSL", "MFSL",
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
  { LOG_MESSAGE_VERBOSITY,        "LOG_MESSAGE_VERBOSITY", "LOG MESSAGE VERBOSITY",
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
      rc = DisplayLogPath_valist(LogComponents[component].comp_log_file, function, component, format, arguments);
      break;
    case STDERRLOG:
      rc = DisplayLogFlux_valist(stderr, function, component, format, arguments);
      break;
    case STDOUTLOG:
      rc = DisplayLogFlux_valist(stdout, function, component, format, arguments);
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
              "Either access is denied to the file or denied to one of the directories in %s",
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
              "Write permission was requested for a file on a read-only file system.");
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
 * Pour info : Les tags de printf dont on peut se servir:
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
