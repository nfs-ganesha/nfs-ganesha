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

typedef enum log_components
{
  COMPONENT_ALL = 0,
  COMPONENT_MEMALLOC,
  COMPONENT_STATES,
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
  COMPONENT_LOG,
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

  COMPONENT_COUNT
} log_components_t;

void SetComponentLogFile(log_components_t comp, char *name);
void SetComponentLogLevel(log_components_t component, int level_to_set);
int DisplayLogComponentLevel(log_components_t component, int level, char *format, ...);
int DisplayErrorComponentLogLine(log_components_t component, int num_family, int num_error, int status, int ma_ligne);

enum log_type
{
  SYSLOG = 0,
  FILELOG
};

typedef struct log_component_info
{
  int   value;
  char *str;
  int   log_level;

  int   log_type;
  char  log_file[MAXPATHLEN];
} log_component_info;
  
log_component_info __attribute__ ((__unused__)) LogComponents[COMPONENT_COUNT];

#define LogMajor(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_MAJOR) \
      DisplayLogComponentLevel(component, NIV_MAJ, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

#define LogCrit(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_CRIT) \
      DisplayLogComponentLevel(component, NIV_CRIT, "%s: " format, LogComponents[component].str, ## args ); \
   } while (0)

#define LogEvent(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_EVENT) \
      DisplayLogComponentLevel(component, NIV_EVENT, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

#define LogDebug(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_DEBUG) \
      DisplayLogComponentLevel(component, NIV_DEBUG, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

#define LogFullDebug(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_FULL_DEBUG) \
      DisplayLogComponentLevel(component, NIV_FULL_DEBUG, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

#define LogError( component, a, b, c ) \
  do { \
    if (LogComponents[component].log_level >= NIV_CRIT) \
      DisplayErrorComponentLogLine( component, a, b, c, __LINE__ ); \
  } while (0)

/* 
 * Temporary define of LogPrintf to handle messages that were
 * displayed to console via printf. This should get renamed
 * something more sensible.
 */
#define LogPrintf(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_FULL_DEBUG) \
      DisplayLogComponentLevel(component, NIV_FULL_DEBUG, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

/*
 * Temporary define of LogMessage for use in replacing
 * DisplayLog calls.
 */
#define LogMessage(component, format, args...) \
  do { \
    if (LogComponents[component].log_level >= NIV_MAJOR) \
      DisplayLogComponentLevel(component, NIV_FULL_DEBUG, "%s: " format, LogComponents[component].str, ## args ); \
  } while (0)

#define isFullDebug(component) \
  (LogComponents[component].log_level >= NIV_FULL_DEBUG)

#endif
