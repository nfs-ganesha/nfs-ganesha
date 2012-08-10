#ifndef _LOGS_H
#define _LOGS_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/param.h>
#include <syslog.h>
#include <inttypes.h>

#ifndef LIBLOG_NO_THREAD
#include <errno.h>
#include <pthread.h>
#endif

#ifdef _SNMP_ADM_ACTIVE
#include "snmp_adm.h"
#endif

/* these macros gain a few percent of speed on gcc, especially with so many log entries */
#if (__GNUC__ >= 3)
/* the strange !! is to ensure that __builtin_expect() takes either 0 or 1 as its first argument */
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#else
#ifndef likely
#define likely(x) (x)
#endif
#ifndef unlikely
#define unlikely(x) (x)
#endif
#endif

/*
 * definition des codes d'error
 *
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
 *
 */

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#define STR_LEN 256

/*
 * Log message severity constants
 */
typedef enum log_levels
{
  NIV_NULL,
  NIV_FATAL,
  NIV_MAJ,
  NIV_CRIT,
  NIV_WARN,
  NIV_EVENT,
  NIV_INFO,
  NIV_DEBUG,
  NIV_MID_DEBUG,	// new debug log level
  NIV_FULL_DEBUG,
  NB_LOG_LEVEL
} log_levels_t;

/*
 * Log components used throughout the code.
 *
 * Note: changing the order of these may confuse SNMP users since SNMP OIDs
 * are numeric.
 */
typedef enum log_components
{
  COMPONENT_ALL = 0,               /* Used for changing logging for all
                                    * components */
  COMPONENT_LOG,                   /* Keep this first, some code depends on it
                                    * being the first component */
  COMPONENT_LOG_EMERG,             /* Component for logging emergency log
                                    * messages - avoid infinite recursion */
  COMPONENT_MEMALLOC,
  COMPONENT_MEMLEAKS,
  COMPONENT_FSAL,
  COMPONENT_NFSPROTO,
  COMPONENT_NFS_V4,
  COMPONENT_NFS_V4_PSEUDO,
  COMPONENT_FILEHANDLE,
  COMPONENT_NFS_SHELL,
  COMPONENT_DISPATCH,
  COMPONENT_CACHE_CONTENT,
  COMPONENT_CACHE_INODE,
  COMPONENT_CACHE_INODE_GC,
  COMPONENT_CACHE_INODE_LRU,
  COMPONENT_HASHTABLE,
  COMPONENT_HASHTABLE_CACHE,
  COMPONENT_LRU,
  COMPONENT_DUPREQ,
  COMPONENT_RPCSEC_GSS,
  COMPONENT_INIT,
  COMPONENT_MAIN,
  COMPONENT_IDMAPPER,
  COMPONENT_NFS_READDIR,
  COMPONENT_NFS_V4_LOCK,
  COMPONENT_NFS_V4_XATTR,
  COMPONENT_NFS_V4_REFERRAL,
  COMPONENT_MEMCORRUPT,
  COMPONENT_CONFIG,
  COMPONENT_CLIENTID,
  COMPONENT_STDOUT,
  COMPONENT_SESSIONS,
  COMPONENT_PNFS,
  COMPONENT_RPC_CACHE,
  COMPONENT_RW_LOCK,
  COMPONENT_NLM,
  COMPONENT_RPC,
  COMPONENT_NFS_CB,
  COMPONENT_THREAD,
  COMPONENT_NFS_V4_ACL,
  COMPONENT_STATE,
  COMPONENT_9P,
  COMPONENT_9P_DISPATCH,
  COMPONENT_FSAL_UP,
  COMPONENT_DBUS,
  LOG_MESSAGE_DEBUGINFO,
  LOG_MESSAGE_VERBOSITY,
  COMPONENT_COUNT
} log_components_t;

typedef struct loglev
{
  log_levels_t value;
  char *str;
  char *short_str;
  int syslog_level;
} log_level_t;

extern log_level_t tabLogLevel[NB_LOG_LEVEL];

#define NIV_MAJOR NIV_MAJ

/* Limits on log messages */
#define LOG_MAX_STRLEN 2048
#define LOG_LABEL_LEN 50
#define LOG_MSG_LEN   255

typedef struct
{
  int numero;
  char label[LOG_LABEL_LEN];
  char msg[LOG_MSG_LEN];
} family_error_t;

/* Error family type */
typedef struct
{
  int num_family;
  char name_family[STR_LEN];
  family_error_t *tab_err;
} family_t;

typedef family_error_t status_t;
typedef family_error_t errctx_t;

typedef struct
{
  int err_family;
  int ctx_family;
  errctx_t contexte;
  status_t status;
} log_error_t;

#define ERR_NULL -1

/* Error codes */
#define ERR_SYS 0
#define SUCCES                    0
#define ERR_FAILURE               1
#define EVNT                      2
#define ERR_EVNT                  2
#define ERR_PTHREAD_KEY_CREATE    3
#define ERR_MALLOC                4
#define ERR_SIGACTION             5
#define ERR_PTHREAD_ONCE          6
#define ERR_FILE_LOG              7
#define ERR_GETHOSTBYNAME         8
#define ERR_MMAP                  9
#define ERR_SOCKET               10
#define ERR_BIND                 11
#define ERR_CONNECT              12
#define ERR_LISTEN               13
#define ERR_ACCEPT               14
#define ERR_RRESVPORT            15
#define ERR_GETHOSTNAME          16
#define ERR_GETSOCKNAME          17
#define ERR_IOCTL                18
#define ERR_UTIME                19
#define ERR_XDR                  20
#define ERR_CHMOD                21
#define ERR_SEND                 22
#define ERR_GETHOSTBYADDR        23
#define ERR_PREAD                24
#define ERR_PWRITE               25
#define ERR_STAT                 26
#define ERR_GETPEERNAME          27
#define ERR_FORK                 28
#define ERR_GETSERVBYNAME        29
#define ERR_MUNMAP               30
#define ERR_STATVFS              31
#define ERR_OPENDIR              32
#define ERR_READDIR              33
#define ERR_CLOSEDIR             34
#define ERR_LSTAT                35
#define ERR_GETWD                36
#define ERR_CHDIR                37
#define ERR_CHOWN                38
#define ERR_MKDIR                39
#define ERR_OPEN                 40
#define ERR_READ                 41
#define ERR_WRITE                42
#define ERR_UTIMES               43
#define ERR_READLINK             44
#define ERR_SYMLINK              45
#define ERR_SYSTEM               46
#define ERR_POPEN                47
#define ERR_LSEEK                48
#define ERR_PTHREAD_CREATE       49
#define ERR_RECV                 50
#define ERR_FOPEN                51
#define ERR_GETCWD               52
#define ERR_SETUID               53
#define ERR_RENAME               54
#define ERR_UNLINK		 55
#define ERR_SELECT               56
#define ERR_WAIT                 57
#define ERR_SETSID               58
#define ERR_SETGID		 59
#define ERR_GETGROUPS            60
#define ERR_SETGROUPS            61
#define ERR_UMASK                62
#define ERR_CREAT                63
#define ERR_SETSOCKOPT           64
#define ERR_DIRECTIO             65
#define ERR_GETRLIMIT            66
#define ERR_SETRLIMIT            67
#define ERR_TRUNCATE		 68
#define ERR_PTHREAD_MUTEX_INIT   69
#define ERR_PTHREAD_COND_INIT    70
#define ERR_FCNTL                71

#define ERR_POSIX 1

static status_t __attribute__ ((__unused__)) tab_systeme_status[] =
{
  {
  0, "NO_ERROR", "No errors"},
  {
  EPERM, "EPERM", "Reserved to root"},
  {
  ENOENT, "ENOENT", "No such file or directory"},
  {
  ESRCH, "ESRCH", "No such process"},
  {
  EINTR, "EINTR", "interrupted system call"},
  {
  EIO, "EIO", "I/O error"},
  {
  ENXIO, "ENXIO", "No such device or address"},
  {
  E2BIG, "E2BIG", "Arg list too long"},
  {
  ENOEXEC, "ENOEXEC", "Exec format error"},
  {
  EBADF, "EBADF", "Bad file number"},
  {
  ECHILD, "ECHILD", "No children"},
  {
  EAGAIN, "EAGAIN", "Resource temporarily unavailable"},
  {
  ENOMEM, "ENOMEM", "Not enough core"},
  {
  EACCES, "ENOMEM", "Permission denied"},
  {
  EFAULT, "EFAULT", "Bad address"},
  {
  ENOTBLK, "ENOTBLK", "Block device required"},
  {
  EBUSY, "EBUSY", "Mount device busy"},
  {
  EEXIST, "EEXIST", "File exists"},
  {
  EXDEV, "EXDEV", "Cross-device link"},
  {
  ENODEV, "ENODEV", "No such device"},
  {
  ENOTDIR, "ENOTDIR", "Not a directory"},
  {
  EISDIR, "EISDIR", "Is a directory"},
  {
  EINVAL, "EINVAL", "Invalid argument"},
  {
  ENFILE, "ENFILE", "File table overflow"},
  {
  EMFILE, "EMFILE", "Too many open files"},
  {
  ENOTTY, "ENOTTY", "Inappropriate ioctl for device"},
  {
  ETXTBSY, "ETXTBSY", "Text file busy"},
  {
  EFBIG, "EFBIG", "File too large"},
  {
  ENOSPC, "ENOSPC", "No space left on device"},
  {
  ESPIPE, "ESPIPE", "Illegal seek"},
  {
  EROFS, "EROFS", "Read only file system"},
  {
  EMLINK, "EMLINK", "Too many links"},
  {
  EPIPE, "EPIPE", "Broken pipe"},
  {
  EDOM, "EDOM", "Math arg out of domain of func"},
  {
  ERANGE, "ERANGE", "Math result not representable"},
  {
  ENOMSG, "ENOMSG", "No message of desired type"},
  {
  EIDRM, "EIDRM", "Identifier removed"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

/* other codes families */
#define ERR_LRU           10
#define ERR_HASHTABLE     11
#define ERR_FSAL          13
#define ERR_GHOSTFS       15
#define ERR_CACHE_INODE   16
#define ERR_CACHE_CONTENT 17

/* previously at log_macros.h */
typedef void (*cleanup_function)(void);
typedef struct cleanup_list_element
{
  struct cleanup_list_element *next;
  cleanup_function             clean;
} cleanup_list_element;

/* Allocates buffer containing debug info to be printed.
 * Returned buffer needs to be freed. Returns number of
 * characeters in size if size != NULL.
 */
char *get_debug_info(int *size);

/* Function prototypes */

void SetNamePgm(char *nom);
void SetNameHost(char *nom);
void SetDefaultLogging(char *name);
void SetNameFunction(char *nom); /* thread safe */
void GetNameFunction(char *name, int len);

/* AddFamilyError : not thread safe */
int AddFamilyError(int num_family, char *nom_family, family_error_t * tab_err);

char *ReturnNameFamilyError(int num_family);

void InitLogging();        /* not thread safe */

void SetLevelDebug(int level_to_set);    /* not thread safe */

int ReturnLevelAscii(const char *LevelInAscii);
char *ReturnLevelInt(int level);

int MakeLogError(char *buffer, int num_family, int num_error, int status,
                  int ma_ligne);

int log_vsnprintf(char *out, size_t n, char *format, va_list arguments);
int log_snprintf(char *out, size_t n, char *format, ...);
int log_fprintf(FILE * file, char *format, ...);

#ifdef _SNMP_ADM_ACTIVE
int getComponentLogLevel(snmp_adm_type_union * param, void *opt);
int setComponentLogLevel(const snmp_adm_type_union * param, void *opt);
#endif

/* previously at log_macros.h */
void RegisterCleanup(cleanup_list_element *clean);
void Cleanup(void);
void Fatal(void);
int SetComponentLogFile(log_components_t component, char *name);
void SetComponentLogBuffer(log_components_t component, char *buffer);
void SetComponentLogLevel(log_components_t component, int level_to_set);

#define SetLogLevel(level_to_set) \
  SetComponentLogLevel(COMPONENT_ALL, level_to_set)

int DisplayLogComponentLevel(log_components_t component,
                             char * function,
                             log_levels_t level,
                             char *format, ...)
__attribute__((format(printf, 4, 5))); /* 4=format 5=params */ ;
int DisplayErrorComponentLogLine(log_components_t component,
                                 char * function,
                                 int num_family,
                                 int num_error,
                                 int status,
                                 int ma_ligne);

enum log_type
{
  SYSLOG = 0,
  FILELOG,
  STDERRLOG,
  STDOUTLOG,
  TESTLOG,
  BUFFLOG
};

typedef struct log_component_info
{
  int   comp_value;
  char *comp_name;
  char *comp_str;
  int   comp_log_level;

  int   comp_log_type;
  char  comp_log_file[MAXPATHLEN];
  char *comp_buffer;
} log_component_info;

#define ReturnLevelComponent(component) LogComponents[component].comp_log_level

log_component_info __attribute__ ((__unused__)) LogComponents[COMPONENT_COUNT];

#define LogAlways(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_type != TESTLOG || \
               LogComponents[component].comp_log_level <= NIV_FULL_DEBUG)) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__,  NIV_NULL, \
                               "%s: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogTest(format, args...) \
  do { \
    DisplayLogComponentLevel(COMPONENT_ALL,  (char *)__FUNCTION__, NIV_NULL, \
                             format, ## args ); \
  } while (0)

#define LogFatal(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_level >= NIV_FATAL)) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_FATAL, \
                               "%s: FATAL ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogMajor(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_level >= NIV_MAJOR)) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_MAJ, \
                               "%s: MAJOR ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogCrit(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_level >= NIV_CRIT)) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_CRIT, \
                               "%s: CRITICAL ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
   } while (0)

#define LogWarn(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_level >= NIV_WARN)) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_WARN, \
                               "%s: WARN: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogEvent(component, format, args...) \
  do { \
    if (likely(LogComponents[component].comp_log_level >= NIV_EVENT)) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_EVENT, \
                               "%s: EVENT: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogInfo(component, format, args...) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= NIV_INFO)) \
      DisplayLogComponentLevel(component, (char *) __FUNCTION__, NIV_INFO, \
                               "%s: INFO: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogDebug(component, format, args...) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= NIV_DEBUG)) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_DEBUG, \
                               "%s: DEBUG: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogMidDebug(component, format, args...) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= NIV_MID_DEBUG)) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_MID_DEBUG, \
                               "%s: MID DEBUG: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogFullDebug(component, format, args...) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= NIV_FULL_DEBUG)) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_FULL_DEBUG, \
                               "%s: FULLDEBUG: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogAtLevel(component, level, format, args...) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= level)) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, level, \
                               "%s: %s: " format, \
                               LogComponents[component].comp_str, tabLogLevel[level].short_str, ## args ); \
  } while (0)

#define LogError( component, a, b, c ) \
  do { \
    if (unlikely(LogComponents[component].comp_log_level >= NIV_CRIT)) \
      DisplayErrorComponentLogLine( component,(char *)__FUNCTION__, a, b, c, __LINE__ ); \
  } while (0)

#define isLevel(component, level) \
    (unlikely(LogComponents[component].comp_log_level >= level))

#define isInfo(component) \
    (unlikely(LogComponents[component].comp_log_level >= NIV_INFO))

#define isDebug(component) \
    (unlikely(LogComponents[component].comp_log_level >= NIV_DEBUG))

#define isMidDebug(component) \
    (unlikely(LogComponents[component].comp_log_level >= NIV_MID_DEBUG))

#define isFullDebug(component) \
    (unlikely(LogComponents[component].comp_log_level >= NIV_FULL_DEBUG))

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void rpc_warnx(/* const */ char *fmt, ...);

#endif
