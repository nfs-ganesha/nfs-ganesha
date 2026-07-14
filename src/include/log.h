/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
#include <sys/socket.h>

#ifndef LIBLOG_NO_THREAD
#include <errno.h>
#include <pthread.h>
#endif

#include "gsh_intrinsic.h"
#include "config_parsing.h"
#include "display.h"
#include "log_common.h"
#include "gsh_list.h"
#include "ip_utils.h"
#include "mem_components.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Conditional Logging related global structure */
typedef struct export_id_list {
	struct glist_head export_id_glist;
	uint16_t export_id;
	mem_components_t mem_comp;
} export_id_list_t;

/* The maximum size of a log buffer */
#define LOG_BUFF_LEN 2048

/* previously at log_macros.h */
typedef void (*cleanup_function)(void);
struct cleanup_list_element {
	struct cleanup_list_element *next;
	cleanup_function clean;
};

/* Allocates buffer containing debug info to be printed.
 * Returned buffer needs to be freed. Returns number of
 * characters in size if size != NULL.
 */
char *get_debug_info(int *size);

/* Function prototypes */

void SetNamePgm(const char *nom);
void SetNameHost(const char *nom);
void SetNameFunction(const char *nom); /* thread safe */
void SetClientIP(char *ip_str);

void init_logging(const char *log_path, const int debug_level);

int ReturnLevelAscii(const char *LevelInAscii);
char *ReturnLevelInt(int level);

/* previously at log_macros.h */
void RegisterCleanup(struct cleanup_list_element *clean);
void Cleanup(void);
void Fatal(void);

/* This function is primarily for setting log level from config, it will
 * not override log level set from environment.
 */
void SetComponentLogLevel(log_components_t component, int level_to_set);

void display_log_component_level(log_components_t component, const char *file,
				 int line, const char *function,
				 log_levels_t level, const char *format,
				 va_list arguments);

void DisplayLogComponentLevel(log_components_t component, const char *file,
			      int line, const char *function,
			      log_levels_t level, const char *format, ...)
	__attribute__((format(printf, 6, 7)));
/* 6=format 7=params */

void LogMallocFailure(const char *file, int line, const char *function,
		      const char *allocator);

int read_log_config(config_file_t in_config,
		    struct config_error_type *err_type);

#ifdef USE_UNWIND
void gsh_libunwind(void);
#endif

void gsh_backtrace(void);

void gsh_log_backtrace(void);

/* These functions display a timeval or timespec into the display buffer
 * in the same format used for logging timestamp.
 */
int display_timeval(struct display_buffer *dspbuf, struct timeval *tv);
int display_timespec(struct display_buffer *dspbuf, struct timespec *ts);

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

typedef int(lf_function_t)(log_header_t headers, void *priv, log_levels_t level,
			   struct display_buffer *buffer, char *compstr,
			   char *message);

int create_log_facility(const char *name, lf_function_t *log_func,
			log_levels_t max_level, log_header_t header,
			void *private_data);
void release_log_facility(const char *name);
int enable_log_facility(const char *name);
int disable_log_facility(const char *name);
int set_log_destination(const char *name, char *dest);
int set_log_level(const char *name, log_levels_t max_level);
void set_const_log_str(void);

struct log_component_info {
	const char *comp_name; /* component name */
	const char *comp_str; /* shorter, more useful name */
};

extern log_levels_t *component_log_level;
extern log_levels_t original_log_level;
extern log_levels_t default_log_level;
extern log_levels_t *conditional_component_log_level;
extern cond_log_match_policies_t cond_log_match_policy;
extern bool conditional_logging_configured;
extern struct log_component_info LogComponents[];

extern bool is_op_context_conditional_flag_set(void);

static inline bool isLevel(log_components_t comp, log_levels_t lvl)
{
	if (lvl == NIV_NULL)
		return true;

	if (unlikely(conditional_logging_configured) &&
	    is_op_context_conditional_flag_set()) {
		log_levels_t *cond_comp_log = conditional_component_log_level;

		return (lvl == NIV_FULL_DEBUG)
			       ? likely(cond_comp_log[comp] >= lvl)
			       : unlikely(cond_comp_log[comp] >= lvl);
	}

	return (lvl > NIV_EVENT) ? unlikely(component_log_level[comp] >= lvl)
				 : likely(component_log_level[comp] >= lvl);
}

/* clang-format off */

#define LogTest(format, ...)                                                  \
	DisplayLogComponentLevel(COMPONENT_ALL, __FILE__, __LINE__, __func__, \
				 NIV_NULL, format, ##__VA_ARGS__)

#define LogFatal(component, format, ...)                                       \
	do {                                                                   \
		DisplayLogComponentLevel(component, __FILE__, __LINE__,        \
					 __func__, NIV_FATAL, format,          \
					 ##__VA_ARGS__);                       \
		abort();                                                       \
	} while (0)

#define LogMajor(component, format, ...)                                      \
	do {                                                                  \
		if (isLevel(component, NIV_MAJ))                              \
			DisplayLogComponentLevel(component, __FILE__,         \
						 __LINE__, __func__, NIV_MAJ, \
						 format, ##__VA_ARGS__);      \
	} while (0)

#define LogCrit(component, format, ...)                                        \
	do {                                                                   \
		if (isLevel(component, NIV_CRIT))                              \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__, NIV_CRIT, \
						 format, ##__VA_ARGS__);       \
	} while (0)

#define LogWarn(component, format, ...)                                        \
	do {                                                                   \
		if (isLevel(component, NIV_WARN))                              \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__, NIV_WARN, \
						 format, ##__VA_ARGS__);       \
	} while (0)

#define LogWarnOnce(component, format, ...)                                    \
	do {                                                                   \
		static bool warned;                                            \
		if (unlikely(!warned) && isLevel(component, NIV_WARN)) {       \
			warned = true;                                         \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__, NIV_WARN, \
						 format, ##__VA_ARGS__);       \
		}                                                              \
	} while (0)

#define LogEvent(component, format, ...)                                     \
	do {                                                                 \
		if (isLevel(component, NIV_EVENT))                           \
			DisplayLogComponentLevel(component, __FILE__,        \
						 __LINE__, __func__,         \
						 NIV_EVENT, format,	     \
						 ##__VA_ARGS__);             \
	} while (0)

#define LogInfo(component, format, ...)                                        \
	do {                                                                   \
		if (isLevel(component, NIV_INFO))                              \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__, NIV_INFO, \
						 format, ##__VA_ARGS__);       \
	} while (0)

#define LogDebug(component, format, ...)                                     \
	do {                                                                 \
		if (isLevel(component, NIV_DEBUG))                           \
			DisplayLogComponentLevel(component, __FILE__,        \
						 __LINE__, __func__,         \
						 NIV_DEBUG, format,          \
						 ##__VA_ARGS__);             \
	} while (0)

#define LogMidDebug(component, format, ...)                                    \
	do {                                                                   \
		if (isLevel(component, NIV_MID_DEBUG))                         \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__,           \
						 NIV_MID_DEBUG, format,        \
						 ##__VA_ARGS__);               \
	} while (0)

#define LogFullDebug(component, format, ...)                             \
	do {                                                             \
		if (isLevel(component, NIV_FULL_DEBUG))                  \
			DisplayLogComponentLevel(component, __FILE__,    \
						 __LINE__, __func__,     \
						 NIV_FULL_DEBUG, format, \
						 ##__VA_ARGS__);         \
	} while (0)

#define LogFullDebugOpaque(component, format, buf_size, value, length,         \
			   ...)                                                \
	do {                                                                   \
		if (isLevel(component, NIV_FULL_DEBUG)) {                      \
			char buf[buf_size];                                    \
			struct display_buffer dspbuf = { buf_size, buf, buf }; \
									       \
			(void)display_opaque_value(&dspbuf, value, length);    \
									       \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__,           \
						 NIV_FULL_DEBUG, format, buf,  \
						 ##__VA_ARGS__);               \
		}                                                              \
	} while (0)

#define LogFullDebugBytes(component, format, buf_size, value, length, ...)     \
	do {                                                                   \
		if (isLevel(component, NIV_FULL_DEBUG)) {                      \
			char buf[buf_size];                                    \
			struct display_buffer dspbuf = { buf_size, buf, buf }; \
									       \
			(void)display_opaque_bytes(&dspbuf, value, length);    \
									       \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__,           \
						 NIV_FULL_DEBUG, format, buf,  \
						 ##__VA_ARGS__);               \
		}                                                              \
	} while (0)

#define LogAtLevel(component, level, format, ...)                           \
	do {                                                                \
		if (isLevel(component, level))                              \
			DisplayLogComponentLevel(component, __FILE__,       \
						 __LINE__, __func__, level, \
						 format, ##__VA_ARGS__);    \
	} while (0)

#define isInfo(component) (isLevel(component, NIV_INFO))

#define isDebug(component) (isLevel(component, NIV_DEBUG))

#define isMidDebug(component) (isLevel(component, NIV_MID_DEBUG))

#define isFullDebug(component) (isLevel(component, NIV_FULL_DEBUG))

/* Use either the first component, or if it is not at least at level,
 * use the second component.
 */
#define LogEventAlt(comp1, comp2, format, ...)                                \
	do {                                                                  \
		if (isLevel(comp1, NIV_EVENT) || isLevel(comp2, NIV_EVENT)) { \
			log_components_t component =                          \
				component_log_level[comp1] >= NIV_EVENT       \
					? comp1                               \
					: comp2;                              \
									      \
			DisplayLogComponentLevel(component, __FILE__,         \
						 __LINE__, __func__,          \
						 NIV_EVENT, format,           \
						 ##__VA_ARGS__);              \
		}                                                             \
	} while (0)

#define LogInfoAlt(comp1, comp2, format, ...)                                  \
	do {                                                                   \
		if (isLevel(comp1, NIV_INFO) || isLevel(comp2, NIV_INFO)) {    \
			log_components_t component =                           \
				component_log_level[comp1] >= NIV_INFO         \
					? comp1                                \
					: comp2;                               \
									       \
			DisplayLogComponentLevel(component, __FILE__,          \
						 __LINE__, __func__, NIV_INFO, \
						 format, ##__VA_ARGS__);       \
		}                                                              \
	} while (0)

#define LogDebugAlt(comp1, comp2, format, ...)                                \
	do {                                                                  \
		if (isLevel(comp1, NIV_DEBUG) || isLevel(comp2, NIV_DEBUG)) { \
			log_components_t component =                          \
				component_log_level[comp1] >= NIV_DEBUG       \
					? comp1                               \
					: comp2;                              \
									     \
			DisplayLogComponentLevel(component, __FILE__,        \
						 __LINE__, __func__,         \
						 NIV_DEBUG, format,          \
						 ##__VA_ARGS__);             \
		}                                                            \
	} while (0)

#define LogMidDebugAlt(comp1, comp2, format, ...)                            \
	do {                                                                 \
		if (isLevel(comp1, NIV_MID_DEBUG) ||                         \
		    isLevel(comp2, NIV_MID_DEBUG)) {                         \
			log_components_t component =                         \
				component_log_level[comp1] >= NIV_MID_DEBUG  \
					? comp1                              \
					: comp2;                             \
									     \
			DisplayLogComponentLevel(component, __FILE__,        \
						 __LINE__, __func__,         \
						 NIV_MID_DEBUG, format,      \
						 ##__VA_ARGS__);             \
		}                                                            \
	} while (0)

#define LogFullDebugAlt(comp1, comp2, format, ...)                            \
	do {                                                                  \
		if (isLevel(comp1, NIV_FULL_DEBUG) ||                         \
		    isLevel(comp2, NIV_FULL_DEBUG)) {                         \
			log_components_t component =                          \
				component_log_level[comp1] >= NIV_FULL_DEBUG  \
					? comp1                               \
					: comp2;                              \
									      \
			DisplayLogComponentLevel(component, __FILE__,         \
						 __LINE__, __func__,          \
						 NIV_FULL_DEBUG, format,      \
						 ##__VA_ARGS__);              \
		}                                                             \
	} while (0)

#define LogDebugAlt3(comp1, comp2, comp3, format, ...)                        \
	do {                                                                  \
		if (isLevel(comp1, NIV_DEBUG) || isLevel(comp2, NIV_DEBUG) || \
		    isLevel(comp3, NIV_DEBUG)) {                              \
			log_components_t component =                          \
				component_log_level[comp1] >= NIV_DEBUG       \
					? comp1                               \
					: comp2;                              \
									     \
			DisplayLogComponentLevel(component, __FILE__,        \
						 __LINE__, __func__,         \
						 NIV_DEBUG, format,          \
						 ##__VA_ARGS__);             \
		}                                                            \
	} while (0)

#define LogFullDebugAlt3(comp1, comp2, comp3, format, ...)                    \
	do {                                                                  \
		if (isLevel(comp1, NIV_FULL_DEBUG) ||                         \
		    isLevel(comp2, NIV_FULL_DEBUG) ||                         \
		    isLevel(comp3, NIV_FULL_DEBUG)) {                         \
			log_components_t component =                          \
				component_log_level[comp1] >= NIV_FULL_DEBUG  \
					? comp1                               \
					: comp2;                              \
									      \
			DisplayLogComponentLevel(component, __FILE__,         \
						 __LINE__, __func__,          \
						 NIV_FULL_DEBUG, format,      \
						 ##__VA_ARGS__);              \
		}                                                             \
	} while (0)

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void rpc_warnx(/* const */ char *fmt, ...);

#ifdef USE_DBUS
extern struct gsh_dbus_interface log_interface;
extern struct gsh_dbus_interface log_conditional_interface;
#endif

/* Rate limited logging */
struct ratelimit_state {
	pthread_mutex_t mutex;
	int interval;
	int burst;
	int printed;
	int missed;
	time_t begin;
};
bool _ratelimit(struct ratelimit_state *rs, int *missed);

#define RATELIMIT_STATE_INIT(interval_init, burst_init)                        \
	{                                                                      \
		.mutex = PTHREAD_MUTEX_INITIALIZER, .interval = interval_init, \
		.burst = burst_init,                                           \
	}

#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init) \
	struct ratelimit_state name =                           \
		RATELIMIT_STATE_INIT(interval_init, burst_init)

#define DEFAULT_RATELIMIT_INTERVAL 30 /* 30 seconds */
#define DEFAULT_RATELIMIT_BURST 2

#define LogEventLimited(comp, fmt, ...)                                        \
	({                                                                     \
		int missed;                                                    \
									       \
		static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL, \
					      DEFAULT_RATELIMIT_BURST);        \
		if (_ratelimit(&(_rs), &missed)) {                             \
			if (missed)                                            \
				LogEvent(comp, "message missed %d times",      \
					 missed);                              \
			LogEvent(comp, fmt, ##__VA_ARGS__);                    \
		}                                                              \
	})

#define LogWarnLimited(comp, fmt, ...)                                         \
	({                                                                     \
		int missed;                                                    \
									       \
		static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL, \
					      DEFAULT_RATELIMIT_BURST);        \
		if (_ratelimit(&(_rs), &missed)) {                             \
			if (missed)                                            \
				LogWarn(comp, "message missed %d times",       \
					missed);                               \
			LogWarn(comp, fmt, ##__VA_ARGS__);                     \
		}                                                              \
	})

#ifdef __cplusplus
}
#endif /* extern "C" */

/* clang-format on */

#endif
