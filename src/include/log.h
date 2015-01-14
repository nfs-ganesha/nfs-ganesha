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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 *
 *
 */

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

#include "gsh_intrinsic.h"
#include "config_parsing.h"
#include "display.h"

/* The maximum size of a log buffer */
#define LOG_BUFF_LEN 2048

/*
 * Log message severity constants
 */
typedef enum log_levels {
	NIV_NULL,
	NIV_FATAL,
	NIV_MAJ,
	NIV_CRIT,
	NIV_WARN,
	NIV_EVENT,
	NIV_INFO,
	NIV_DEBUG,
	NIV_MID_DEBUG,
	NIV_FULL_DEBUG,
	NB_LOG_LEVEL
} log_levels_t;

/*
 * Log components used throughout the code.
 */
typedef enum log_components {
	COMPONENT_ALL = 0,	/* Used for changing logging for all
				 * components */
	COMPONENT_LOG,		/* Keep this first, some code depends on it
				 * being the first component */
	COMPONENT_LOG_EMERG,	/* Component for logging emergency log
				 * messages - avoid infinite recursion */
	COMPONENT_MEMLEAKS,
	COMPONENT_FSAL,
	COMPONENT_NFSPROTO,
	COMPONENT_NFS_V4,
	COMPONENT_EXPORT,
	COMPONENT_FILEHANDLE,
	COMPONENT_DISPATCH,
	COMPONENT_CACHE_INODE,
	COMPONENT_CACHE_INODE_LRU,
	COMPONENT_HASHTABLE,
	COMPONENT_HASHTABLE_CACHE,
	COMPONENT_DUPREQ,
	COMPONENT_INIT,
	COMPONENT_MAIN,
	COMPONENT_IDMAPPER,
	COMPONENT_NFS_READDIR,
	COMPONENT_NFS_V4_LOCK,
	COMPONENT_CONFIG,
	COMPONENT_CLIENTID,
	COMPONENT_SESSIONS,
	COMPONENT_PNFS,
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
	COMPONENT_NFS_MSK,
	COMPONENT_COUNT
} log_components_t;

/* previously at log_macros.h */
typedef void (*cleanup_function) (void);
typedef struct cleanup_list_element {
	struct cleanup_list_element *next;
	cleanup_function clean;
} cleanup_list_element;

/* Allocates buffer containing debug info to be printed.
 * Returned buffer needs to be freed. Returns number of
 * characters in size if size != NULL.
 */
char *get_debug_info(int *size);

/* Function prototypes */

void SetNamePgm(const char *nom);
void SetNameHost(const char *nom);
void SetNameFunction(const char *nom);	/* thread safe */
void SetClientIP(char *ip_str);

void init_logging(const char *log_path, const int debug_level);

int ReturnLevelAscii(const char *LevelInAscii);
char *ReturnLevelInt(int level);

/* previously at log_macros.h */
void RegisterCleanup(cleanup_list_element *clean);
void Cleanup(void);
void Fatal(void);

/* This function is primarily for setting log level from config, it will
 * not override log level set from environment.
 */
void SetComponentLogLevel(log_components_t component, int level_to_set);

void DisplayLogComponentLevel(log_components_t component, char *file, int line,
			      char *function, log_levels_t level, char *format,
			      ...)
			      __attribute__ ((format(printf, 6, 7)));
			      /* 6=format 7=params */

int read_log_config(config_file_t in_config,
		    struct config_error_type *err_type);
void reread_log_config();

typedef enum log_type {
	SYSLOG = 0,
	FILELOG,
	STDERRLOG,
	STDOUTLOG,
	TESTLOG
} log_type_t;

typedef enum log_header_t {
	LH_NONE,
	LH_COMPONENT,
	LH_ALL,
	NB_LH_TYPES
} log_header_t;

/**
 * @brief Prototype for special log facility logging functions
 */

typedef int (lf_function_t) (log_header_t headers, void *private,
			     log_levels_t level,
			     struct display_buffer *buffer, char *compstr,
			     char *message);

int create_log_facility(char *name,
			lf_function_t *log_func,
			log_levels_t max_level,
			log_header_t header,
			void *private);
void release_log_facility(char *name);
int enable_log_facility(char *name);
int disable_log_facility(char *name);
int set_log_destination(char *name, char *dest);
int set_log_level(char *name, log_levels_t max_level);
void set_const_log_str();

struct log_component_info {
	const char *comp_name;	/* component name */
	const char *comp_str;	/* shorter, more useful name */
	bool comp_env_set;	/* level was set by env(), now RO */
};

extern log_levels_t *component_log_level;

extern struct log_component_info LogComponents[COMPONENT_COUNT];

#define LogAlways(component, format, args...) \
	do { \
		if (likely(component_log_level[component] \
		    <= NIV_FULL_DEBUG)) \
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *) __func__, \
						 NIV_NULL, format, ## args); \
	} while (0)

#define LogTest(format, args...) \
	DisplayLogComponentLevel(COMPONENT_ALL, (char *) __FILE__, \
				 __LINE__,  (char *) __func__, \
				 NIV_NULL, format, ## args)

#define LogFatal(component, format, args...) \
	DisplayLogComponentLevel(component, (char *) __FILE__, \
				 __LINE__, \
				 (char *) __func__, \
				 NIV_FATAL, format, ## args)

#define LogMajor(component, format, args...) \
	do { \
		if (likely(component_log_level[component] \
		    >= NIV_MAJ)) \
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *) __func__, \
						 NIV_MAJ, format, ## args); \
	} while (0)

#define LogCrit(component, format, args...) \
	do { \
		if (likely(component_log_level[component] \
		    >= NIV_CRIT)) \
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *) __func__, \
						 NIV_CRIT, format, ## args); \
	} while (0)

#define LogWarn(component, format, args...) \
	do { \
		if (likely(component_log_level[component] \
		    >= NIV_WARN)) \
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *) __func__, \
						 NIV_WARN, format, ## args); \
	} while (0)

#define LogEvent(component, format, args...) \
	do { \
		if (likely(component_log_level[component] \
		    >= NIV_EVENT)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_EVENT, format, ## args); \
	} while (0)

#define LogInfo(component, format, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_INFO)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_INFO, format, ## args); \
	} while (0)

#define LogDebug(component, format, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_DEBUG)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_DEBUG, format, ## args); \
	} while (0)

#define LogMidDebug(component, format, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_MID_DEBUG)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_MID_DEBUG, \
						 format, ## args); \
	} while (0)

#define LogFullDebug(component, format, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_FULL_DEBUG)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_FULL_DEBUG, \
						 format, ## args); \
	} while (0)

#define \
LogFullDebugOpaque(component, format, buf_size, value, length, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_FULL_DEBUG)) { \
			char buf[buf_size]; \
			struct display_buffer dspbuf = {buf_size, buf, buf}; \
			\
			(void) display_opaque_value(&dspbuf, value, length); \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 NIV_FULL_DEBUG, \
						 format, buf, ## args); \
		} \
	} while (0)

#define LogFullDebugBytes(component, format, buf_size, value, length, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= NIV_FULL_DEBUG)) { \
			char buf[buf_size]; \
			struct display_buffer dspbuf = {buf_size, buf, buf}; \
			\
			(void) display_opaque_bytes(&dspbuf, value, length); \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *) __func__, \
						 NIV_FULL_DEBUG, \
						 format, buf, ## args); \
		} \
	} while (0)

#define LogAtLevel(component, level, format, args...) \
	do { \
		if (unlikely(component_log_level[component] \
		    >= level)) \
			DisplayLogComponentLevel(component, (char *) __FILE__,\
						 __LINE__, \
						 (char *) __func__, \
						 level, format, ## args); \
	} while (0)

#define isLevel(component, level) \
	(unlikely(component_log_level[component] >= level))

#define isInfo(component) \
	(unlikely(component_log_level[component] >= NIV_INFO))

#define isDebug(component) \
	(unlikely(component_log_level[component] >= NIV_DEBUG))

#define isMidDebug(component) \
	(unlikely(component_log_level[component] >= NIV_MID_DEBUG))

#define isFullDebug(component) \
	(unlikely(component_log_level[component] >= NIV_FULL_DEBUG))

/* Use either the first component, or if it is not at least at level,
 * use the second component.
 */
#define LogInfoAlt(comp1, comp2, format, args...) \
	do { \
		if (unlikely(component_log_level[comp1] \
		    >= NIV_INFO) || \
		    unlikely(component_log_level[comp2] \
		    >= NIV_INFO)) { \
			log_components_t component = \
			    component_log_level[comp1] \
				>= NIV_INFO ? comp1 : comp2; \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *)__func__, \
						 NIV_INFO, \
						 "%s: INFO: " format, \
						 LogComponents[component] \
						     .comp_str, ## args); \
		} \
	} while (0)

#define LogDebugAlt(comp1, comp2, format, args...) \
	do { \
		if (unlikely(component_log_level[comp1] \
		    >= NIV_DEBUG) || \
		    unlikely(component_log_level[comp2] \
		    >= NIV_DEBUG)) { \
			log_components_t component = \
			    component_log_level[comp1] \
				>= NIV_DEBUG ? comp1 : comp2; \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *)__func__, \
						 NIV_DEBUG, \
						 "%s: DEBUG: " format, \
						 LogComponents[component] \
						     .comp_str, ## args); \
		} \
	} while (0)

#define LogMidDebugAlt(comp1, comp2, format, args...) \
	do { \
		if (unlikely(component_log_level[comp1] \
		    >= NIV_MID_DEBUG) || \
		    unlikely(component_log_level[comp2] \
		    >= NIV_MID_DEBUG)) { \
			log_components_t component = \
			    component_log_level[comp1] \
				>= NIV_MID_DEBUG ? comp1 : comp2; \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *)__func__, \
						 NIV_MID_DEBUG, \
						 "%s: MID DEBUG: " format, \
						 LogComponents[component] \
						     .comp_str, ## args); \
		} \
	} while (0)

#define LogFullDebugAlt(comp1, comp2, format, args...) \
	do { \
		if (unlikely(component_log_level[comp1] \
		    >= NIV_FULL_DEBUG) || \
		    unlikely(component_log_level[comp2] \
		    >= NIV_FULL_DEBUG)) { \
			log_components_t component = \
			    component_log_level[comp1] \
				>= NIV_FULL_DEBUG ? comp1 : comp2; \
			\
			DisplayLogComponentLevel(component, (char *) __FILE__, \
						 __LINE__, \
						 (char *)__func__, \
						 NIV_FULL_DEBUG, \
						 "%s: FULLDEBUG: " format, \
						 LogComponents[component] \
						     .comp_str, ## args); \
		} \
	} while (0)

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void rpc_warnx(/* const */ char *fmt, ...);

#endif
