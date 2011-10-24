#ifndef _LOG_MACROS_H
#define _LOG_MACROS_H

#include "log_functions.h"

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

/*
 * Log components used throughout the code.
 *
 * Note: changing the order of these may confuse SNMP users since SNMP OIDs are numeric.
 */
typedef enum log_components
{
  COMPONENT_ALL = 0,               /* Used for changing logging for all components */
  COMPONENT_LOG,                   /* Keep this first, some code depends on it being the first component */
  COMPONENT_LOG_EMERG,             /* Component for logging emergency log messages - avoid infinite recursion */
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
  COMPONENT_HASHTABLE,
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
  COMPONENT_CLIENT_ID_COMPUTE,
  COMPONENT_STDOUT,
  COMPONENT_SESSIONS,
  COMPONENT_PNFS,
  COMPONENT_RPC_CACHE,
  COMPONENT_RW_LOCK,
  COMPONENT_MFSL,
  COMPONENT_NLM,
  COMPONENT_RPC,
  COMPONENT_THREAD,
  COMPONENT_NFS_V4_ACL,
  COMPONENT_STATE,
  COMPONENT_9P,
  COMPONENT_9P_DISPATCH,
  COMPONENT_FSAL_UP,
  LOG_MESSAGE_VERBOSITY,
  COMPONENT_COUNT
} log_components_t;

typedef void (*cleanup_function)(void);
typedef struct cleanup_list_element
{
  struct cleanup_list_element *next;
  cleanup_function             clean;
} cleanup_list_element;

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
    if (LogComponents[component].comp_log_type != TESTLOG || \
        LogComponents[component].comp_log_level <= NIV_FULL_DEBUG) \
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
    if (LogComponents[component].comp_log_level >= NIV_FATAL) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_FATAL, \
                               "%s: FATAL ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogMajor(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_MAJOR) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_MAJ, \
                               "%s: MAJOR ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogCrit(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_CRIT) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_CRIT, \
                               "%s: CRITICAL ERROR: " format, \
                               LogComponents[component].comp_str, ## args ); \
   } while (0)

#define LogWarn(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_WARN) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_WARN, \
                               "%s: WARN: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogEvent(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_EVENT) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_EVENT, \
                               "%s: EVENT: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogInfo(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_INFO) \
      DisplayLogComponentLevel(component, (char *) __FUNCTION__, NIV_INFO, \
                               "%s: INFO: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogDebug(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_DEBUG) \
      DisplayLogComponentLevel(component,  (char *)__FUNCTION__, NIV_DEBUG, \
                               "%s: DEBUG: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogFullDebug(component, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_FULL_DEBUG) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, NIV_FULL_DEBUG, \
                               "%s: FULLDEBUG: " format, \
                               LogComponents[component].comp_str, ## args ); \
  } while (0)

#define LogAtLevel(component, level, format, args...) \
  do { \
    if (LogComponents[component].comp_log_level >= level) \
      DisplayLogComponentLevel(component, (char *)__FUNCTION__, level, \
                               "%s: %s: " format, \
                               LogComponents[component].comp_str, tabLogLevel[level].short_str, ## args ); \
  } while (0)

#define LogError( component, a, b, c ) \
  do { \
    if (LogComponents[component].comp_log_level >= NIV_CRIT) \
      DisplayErrorComponentLogLine( component,(char *)__FUNCTION__, a, b, c, __LINE__ ); \
  } while (0)

#define isLevel(component, level) \
  (LogComponents[component].comp_log_level >= level)

#define isInfo(component) \
  (LogComponents[component].comp_log_level >= NIV_INFO)

#define isDebug(component) \
  (LogComponents[component].comp_log_level >= NIV_DEBUG)

#define isFullDebug(component) \
  (LogComponents[component].comp_log_level >= NIV_FULL_DEBUG)

#endif
