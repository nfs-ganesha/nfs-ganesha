/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * All the display functions and error handling.
 *
 */
#include "config.h"

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
#include <sys/resource.h>
#include <execinfo.h>

#include "log.h"
#include "gsh_list.h"
#include "gsh_rpc.h"
#include "common_utils.h"
#include "abstract_mem.h"

#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif

#include "nfs_core.h"
#include "config_parsing.h"

#ifdef USE_LTTNG
#include "gsh_lttng/logger.h"
#endif

/*
 * The usual PTHREAD_RWLOCK_xxx macros log messages for tracing if FULL
 * DEBUG is enabled. If such a macro is called from this logging file as
 * part of logging a message, it generates endless loop of lock tracing
 * messages. The following code redefines these lock macros to avoid the
 * loop.
 */
#ifdef PTHREAD_RWLOCK_wrlock
#undef PTHREAD_RWLOCK_wrlock
#endif
#define PTHREAD_RWLOCK_wrlock(_lock)					\
	do {								\
		if (pthread_rwlock_wrlock(_lock) != 0)			\
			assert(0);					\
	} while (0)

#ifdef PTHREAD_RWLOCK_rdlock
#undef PTHREAD_RWLOCK_rdlock
#endif
#define PTHREAD_RWLOCK_rdlock(_lock)					\
	do {								\
		if (pthread_rwlock_rdlock(_lock) != 0)			\
			assert(0);					\
	} while (0)

#ifdef PTHREAD_RWLOCK_unlock
#undef PTHREAD_RWLOCK_unlock
#endif
#define PTHREAD_RWLOCK_unlock(_lock)					\
	do {								\
		if (pthread_rwlock_unlock(_lock) != 0)			\
			assert(0);					\
	} while (0)

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
enum log_flag_index_t {
	LF_DATE,		/*< Date field. */
	LF_TIME,		/*< Time field. */
	LF_EPOCH,		/*< Server Epoch field (distinguishes server
				    instance. */
	LF_CLIENTIP,            /* <Client IP field. */
	LF_HOSTAME,		/*< Server host name field. */
	LF_PROGNAME,		/*< Ganesha program name field. */
	LF_PID,			/*< Ganesha process identifier. */
	LF_THREAD_NAME,		/*< Name of active thread logging message. */
	LF_FILE_NAME,		/*< Source file name message occured in. */
	LF_LINE_NUM,		/*< Source line number message occurred in. */
	LF_FUNCTION_NAME,	/*< Function name message occurred in. */
	LF_COMPONENT,		/*< Log component. */
	LF_LEVEL,		/*< Log level. */
};

/**
 * @brief Define a set of possible time and date formats.
 *
 * These values will be stored in lf_ext for the LF_DATE and LF_TIME flags.
 *
 */
enum timedate_formats_t {
	TD_NONE,		/*< No time/date. */
	TD_GANESHA,		/*< Legacy Ganesha time and date format. */
	TD_LOCAL,		/*< Use strftime local format for time/date. */
	TD_8601,		/*< Use ISO 8601 time/date format. */
	TD_SYSLOG,		/*< Use a typical syslog time/date format. */
	TD_SYSLOG_USEC,		/*< Use a typical syslog time/date format that
				    also includes microseconds. */
	TD_USER,		/* Use a user defined time/date format. */
};

/**
 * @brief Format control for log messages
 *
 */

struct logfields {
	bool disp_epoch;
	bool disp_clientip;
	bool disp_host;
	bool disp_prog;
	bool disp_pid;
	bool disp_threadname;
	bool disp_filename;
	bool disp_linenum;
	bool disp_funct;
	bool disp_comp;
	bool disp_level;
	enum timedate_formats_t datefmt;
	enum timedate_formats_t timefmt;
	char *user_date_fmt;
	char *user_time_fmt;
};

/**
 * @brief Startup default log message format
 *
 * Baked in here so early startup has something to work with
 */

static struct logfields default_logfields = {
	.disp_epoch = true,
	.disp_host = true,
	.disp_prog = true,
	.disp_pid = true,
	.disp_threadname = true,
	.disp_filename = false,
	.disp_linenum = false,
	.disp_funct = true,
	.disp_comp = true,
	.disp_level = true,
	.datefmt = TD_GANESHA,
	.timefmt = TD_GANESHA
};

static struct logfields *logfields = &default_logfields;

/**
 * @brief Define the structure for a log facility.
 *
 */
struct log_facility {
	struct glist_head lf_list;	/*< List of log facilities */
	struct glist_head lf_active;	/*< This is an active facility */
	char *lf_name;			/*< Name of log facility */
	log_levels_t lf_max_level;	/*< Max log level for this facility */
	log_header_t lf_headers;	/*< If time stamp etc. are part of msg
					 */
	lf_function_t *lf_func;	/*< Function that describes facility   */
	void *lf_private;	/*< Private info for facility          */
};

/* Define the maximum length of a user time/date format. */
#define MAX_TD_USER_LEN 64
/* Define the maximum overall time/date format length, should have room
 * for both user date and user time format plus room for blanks around them.
 */
#define MAX_TD_FMT_LEN (MAX_TD_USER_LEN * 2 + 4)

static int log_to_syslog(log_header_t headers, void *private,
			 log_levels_t level,
			 struct display_buffer *buffer, char *compstr,
			 char *message);

static int log_to_file(log_header_t headers, void *private,
		       log_levels_t level,
		       struct display_buffer *buffer, char *compstr,
		       char *message);

static int log_to_stream(log_header_t headers, void *private,
			 log_levels_t level,
			 struct display_buffer *buffer, char *compstr,
			 char *message);

static struct glist_head facility_list;
static struct glist_head active_facility_list;

static struct log_facility *default_facility;

log_header_t max_headers = LH_COMPONENT;

char const_log_str[LOG_BUFF_LEN] = "\0";
char date_time_fmt[MAX_TD_FMT_LEN] = "\0";

typedef struct loglev {
	char *str;
	char *short_str;
	int syslog_level;
} log_level_t;

static log_level_t tabLogLevel[] = {
	[NIV_NULL] = {"NIV_NULL", "NULL", LOG_NOTICE},
	[NIV_FATAL] = {"NIV_FATAL", "FATAL", LOG_CRIT},
	[NIV_MAJ] = {"NIV_MAJ", "MAJ", LOG_CRIT},
	[NIV_CRIT] = {"NIV_CRIT", "CRIT", LOG_ERR},
	[NIV_WARN] = {"NIV_WARN", "WARN", LOG_WARNING},
	[NIV_EVENT] = {"NIV_EVENT", "EVENT", LOG_NOTICE},
	[NIV_INFO] = {"NIV_INFO", "INFO", LOG_INFO},
	[NIV_DEBUG] = {"NIV_DEBUG", "DEBUG", LOG_DEBUG},
	[NIV_MID_DEBUG] = {"NIV_MID_DEBUG", "M_DBG", LOG_DEBUG},
	[NIV_FULL_DEBUG] = {"NIV_FULL_DEBUG", "F_DBG", LOG_DEBUG}
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

/* constants */
static int log_mask = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

/* Global variables */

static char program_name[1024];
static char hostname[256];
static int syslog_opened;

/*
 * Variables specifiques aux threads.
 */

__thread char thread_name[32];
__thread char log_buffer[LOG_BUFF_LEN + 1];
__thread char *clientip = NULL;

/* threads keys */
#define LogChanges(format, args...) \
	do { \
		if (component_log_level[COMPONENT_LOG] == \
		    NIV_FULL_DEBUG) \
			DisplayLogComponentLevel(COMPONENT_LOG, \
						 __FILE__, \
						 __LINE__, \
						 __func__, \
						 NIV_NULL, \
						 "LOG: " format, \
						 ## args); \
	} while (0)

struct cleanup_list_element *cleanup_list;

void RegisterCleanup(struct cleanup_list_element *clean)
{
	clean->next = cleanup_list;
	cleanup_list = clean;
}

void Cleanup(void)
{
	struct cleanup_list_element *c = cleanup_list;

	while (c != NULL) {
		c->clean();
		c = c->next;
	}
}

void Fatal(void)
{
	Cleanup();
	exit(2);
}

#ifdef _DONT_HAVE_LOCALTIME_R

/* Localtime is not reentrant...
 * So we are obliged to have a mutex for calling it.
 * pffff....
 */
static pthread_mutex_t mutex_localtime = PTHREAD_MUTEX_INITIALIZER;

/* thread-safe and PORTABLE version of localtime */

static struct tm *Localtime_r(const time_t *p_time, struct tm *p_tm)
{
	struct tm *p_tmp_tm;

	if (!p_tm) {
		errno = EFAULT;
		return NULL;
	}

	PTHREAD_MUTEX_lock(&mutex_localtime);

	p_tmp_tm = localtime(p_time);

	/* copy the result */
	(*p_tm) = (*p_tmp_tm);

	PTHREAD_MUTEX_unlock(&mutex_localtime);

	return p_tm;
}
#else
#define Localtime_r localtime_r
#endif

/*
 * Convert a numeral log level in ascii to
 * the numeral value.
 */
int ReturnLevelAscii(const char *LevelInAscii)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(tabLogLevel); i++)
		if (tabLogLevel[i].str != NULL &&
		    (!strcasecmp(tabLogLevel[i].str, LevelInAscii)
		     || !strcasecmp(tabLogLevel[i].str + 4, LevelInAscii)
		     || !strcasecmp(tabLogLevel[i].short_str, LevelInAscii)))
			return i;

	/* If nothing found, return -1 */
	return -1;
}				/* ReturnLevelAscii */

char *ReturnLevelInt(int level)
{
	if (level >= 0 && level < NB_LOG_LEVEL)
		return tabLogLevel[level].str;

	/* If nothing is found, return NULL. */
	return NULL;
}				/* ReturnLevelInt */

/*
 * Set the name of this program.
 */
void SetNamePgm(const char *nom)
{

	/* This function isn't thread-safe because the name of the program
	 * is common among all the threads. */
	if (strmaxcpy(program_name, nom, sizeof(program_name)) == -1)
		LogFatal(COMPONENT_LOG, "Program name %s too long", nom);
}				/* SetNamePgm */

/*
 * Set the hostname.
 */
void SetNameHost(const char *name)
{
	if (strmaxcpy(hostname, name, sizeof(hostname)) == -1)
		LogFatal(COMPONENT_LOG, "Host name %s too long", name);
}				/* SetNameHost */

/* Set the function name in progress. */
void SetNameFunction(const char *nom)
{
	strncpy(thread_name, nom, sizeof(thread_name));
	thread_name[sizeof(thread_name)-1] = '\0';
	if (strlen(nom) >= sizeof(thread_name))
		LogWarn(COMPONENT_LOG,
			"Thread name %s too long truncated to %s",
			nom, thread_name);
	clientip = NULL;
}

/*
 * Sets the IP of the Client for this thread.
 * Make sure ip_str is valid for the duration of the thread
 */
void SetClientIP(char *ip_str)
{
	clientip = ip_str;
}

/* Installs a signal handler */
static void ArmSignal(int signal, void (*action) ())
{
	struct sigaction act;

	/* Placing fields of struct sigaction */
	act.sa_flags = 0;
	act.sa_handler = action;
	sigemptyset(&act.sa_mask);

	if (sigaction(signal, &act, NULL) == -1)
		LogCrit(COMPONENT_LOG, "Failed to arm signal %d, error %d (%s)",
			signal, errno, strerror(errno));
}				/* ArmSignal */

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

static void _SetLevelDebug(int level_to_set)
{
	int i;

	if (level_to_set < NIV_NULL)
		level_to_set = NIV_NULL;

	if (level_to_set >= NB_LOG_LEVEL)
		level_to_set = NB_LOG_LEVEL - 1;

	/* COMPONENT_ALL is a pseudo component, handle it separately */
	component_log_level[COMPONENT_ALL] = level_to_set;
	for (i = COMPONENT_ALL + 1; i < COMPONENT_COUNT; i++) {
		SetComponentLogLevel(i, level_to_set);
	}
}				/* _SetLevelDebug */

static void SetLevelDebug(int level_to_set)
{
	_SetLevelDebug(level_to_set);

	LogChanges("Setting log level for all components to %s",
		   ReturnLevelInt(component_log_level[COMPONENT_ALL]));
}

static void SetNTIRPCLogLevel(int level_to_set)
{
	switch (level_to_set) {
	case NIV_NULL:
	case NIV_FATAL:
		ntirpc_pp.debug_flags = 0; /* disable all flags */
		break;
	case NIV_CRIT:
	case NIV_MAJ:
		ntirpc_pp.debug_flags = TIRPC_DEBUG_FLAG_ERROR;
		break;
	case NIV_WARN:
		ntirpc_pp.debug_flags = TIRPC_DEBUG_FLAG_ERROR |
					TIRPC_DEBUG_FLAG_WARN;
		break;
	case NIV_EVENT:
		ntirpc_pp.debug_flags = TIRPC_DEBUG_FLAG_ERROR |
					TIRPC_DEBUG_FLAG_WARN |
					TIRPC_DEBUG_FLAG_EVENT;
		break;
	case NIV_DEBUG:
	case NIV_MID_DEBUG:
		/* set by log_conf_commit() */
		break;
	case NIV_FULL_DEBUG:
		ntirpc_pp.debug_flags = 0xFFFFFFFF; /* enable all flags */
		break;
	default:
		ntirpc_pp.debug_flags = TIRPC_DEBUG_FLAG_DEFAULT;
		break;
	}

	if (!tirpc_control(TIRPC_SET_DEBUG_FLAGS, &ntirpc_pp.debug_flags))
		LogCrit(COMPONENT_CONFIG, "Setting nTI-RPC debug_flags failed");
}

void SetComponentLogLevel(log_components_t component, int level_to_set)
{

	assert(level_to_set >= NIV_NULL);
	assert(level_to_set < NB_LOG_LEVEL);
	assert(component != COMPONENT_ALL);

	if (LogComponents[component].comp_env_set) {
		LogWarn(COMPONENT_CONFIG,
			"LOG %s level %s from config is ignored because %s was set in environment",
			LogComponents[component].comp_name,
			ReturnLevelInt(level_to_set),
			ReturnLevelInt(component_log_level[component]));
		return;
	}

	if (component_log_level[component] == level_to_set)
		return;

	LogChanges("Changing log level of %s from %s to %s",
		   LogComponents[component].comp_name,
		   ReturnLevelInt(component_log_level[component]),
		   ReturnLevelInt(level_to_set));
	component_log_level[component] = level_to_set;

	if (component == COMPONENT_TIRPC)
		SetNTIRPCLogLevel(level_to_set);
}

static inline int ReturnLevelDebug(void)
{
	return component_log_level[COMPONENT_ALL];
}				/* ReturnLevelDebug */

static void IncrementLevelDebug(void)
{
	_SetLevelDebug(ReturnLevelDebug() + 1);

	LogChanges("SIGUSR1 Increasing log level for all components to %s",
		   ReturnLevelInt(component_log_level[COMPONENT_ALL]));
}				/* IncrementLevelDebug */

static void DecrementLevelDebug(void)
{
	_SetLevelDebug(ReturnLevelDebug() - 1);

	LogChanges("SIGUSR2 Decreasing log level for all components to %s",
		   ReturnLevelInt(component_log_level[COMPONENT_ALL]));
}				/* DecrementLevelDebug */

void set_const_log_str(void)
{
	struct display_buffer dspbuf = { sizeof(const_log_str),
		const_log_str,
		const_log_str
	};
	struct display_buffer tdfbuf = { sizeof(date_time_fmt),
		date_time_fmt,
		date_time_fmt
	};
	int b_left = display_start(&dspbuf);

	const_log_str[0] = '\0';

	if (b_left > 0 && logfields->disp_epoch)
		b_left = display_printf(&dspbuf,
			": epoch %08x ", nfs_ServerEpoch);

	if (b_left > 0 && logfields->disp_host)
		b_left = display_printf(&dspbuf, ": %s ", hostname);

	if (b_left > 0 && logfields->disp_prog)
		b_left = display_printf(&dspbuf, ": %s", program_name);

	if (b_left > 0 && logfields->disp_prog
	    && logfields->disp_pid)
		b_left = display_cat(&dspbuf, "-");

	if (b_left > 0 && logfields->disp_pid)
		b_left = display_printf(&dspbuf, "%d", getpid());

	if (b_left > 0
	    && (logfields->disp_prog || logfields->disp_pid)
	    && !logfields->disp_threadname)
		(void) display_cat(&dspbuf, " ");

	b_left = display_start(&tdfbuf);

	if (b_left <= 0)
		return;

	if (logfields->datefmt == TD_LOCAL
	    && logfields->timefmt == TD_LOCAL) {
		b_left = display_cat(&tdfbuf, "%c ");
	} else {
		switch (logfields->datefmt) {
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
			if (logfields->timefmt == TD_SYSLOG_USEC)
				b_left = display_cat(&tdfbuf, "%F");
			else
				b_left = display_cat(&tdfbuf, "%F ");
			break;
		case TD_USER:
			b_left = display_printf(&tdfbuf, "%s ",
				logfields->user_date_fmt);
			break;
		case TD_NONE:
		default:
			break;
		}

		if (b_left <= 0)
			return;

		switch (logfields->timefmt) {
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
			b_left = display_printf(&tdfbuf, "%s ",
				logfields->user_time_fmt);
			break;
		case TD_NONE:
		default:
			break;
		}

	}

	/* Trim trailing blank from date time format. */
	if (date_time_fmt[0] != '\0' &&
	    date_time_fmt[strlen(date_time_fmt) - 1] == ' ')
		date_time_fmt[strlen(date_time_fmt) - 1] = '\0';
}

static void set_logging_from_env(void)
{
	char *env_value;
	int newlevel, component, oldlevel;

	for (component = COMPONENT_ALL; component < COMPONENT_COUNT;
	     component++) {
		env_value = getenv(LogComponents[component].comp_name);
		if (env_value == NULL)
			continue;
		newlevel = ReturnLevelAscii(env_value);
		if (newlevel == -1) {
			LogCrit(COMPONENT_LOG,
				"Environment variable %s exists, but the value %s is not a valid log level.",
				LogComponents[component].comp_name,
				env_value);
			continue;
		}
		oldlevel = component_log_level[component];
		if (component == COMPONENT_ALL)
			_SetLevelDebug(newlevel);
		else
			SetComponentLogLevel(component, newlevel);
		LogComponents[component].comp_env_set = true;
		LogChanges(
		     "Using environment variable to switch log level for %s from %s to %s",
		     LogComponents[component].comp_name,
		     ReturnLevelInt(oldlevel),
		     ReturnLevelInt(newlevel));
	}

}				/* InitLogging */

/**
 *
 * @brief Finds a log facility by name
 *
 * Must be called under the rwlock
 *
 * @param[in]  name The name of the facility to be found
 *
 * @retval NULL No facility by that name
 * @retval non-NULL Pointer to the facility structure
 *
 */
static struct log_facility *find_log_facility(const char *name)
{
	struct glist_head *glist;
	struct log_facility *facility;

	glist_for_each(glist, &facility_list) {
		facility = glist_entry(glist, struct log_facility, lf_list);
		if (!strcasecmp(name, facility->lf_name))
			return facility;
	}

	return NULL;
}

/**
 * @brief Create a logging facility
 *
 * A logging facility outputs log messages using the helper function
 * log_func.  See below for enabling/disabling.
 *
 * @param name       [IN] the name of the new logger
 * @param log_func   [IN] function pointer to the helper
 * @param max_level  [IN] maximum message level this logger will handle.
 * @param header     [IN] detail level for header part of messages
 * @param private    [IN] logger specific argument.
 *
 * @return 0 on success, -errno for failure
 */

int create_log_facility(const char *name, lf_function_t *log_func,
			log_levels_t max_level, log_header_t header,
			void *private)
{
	struct log_facility *facility;

	if (name == NULL || *name == '\0')
		return -EINVAL;
	if (max_level < NIV_NULL || max_level >= NB_LOG_LEVEL)
		return -EINVAL;
	if (log_func == log_to_file && private != NULL) {
		char *dir;
		int rc;

		if (*(char *)private == '\0' ||
		    strlen(private) >= MAXPATHLEN) {
			LogCrit(COMPONENT_LOG,
				 "New log file path empty or too long");
			return -EINVAL;
		}
		dir = alloca(strlen(private) + 1);
		strcpy(dir, private);
		dir = dirname(dir);
		rc = access(dir, W_OK);
		if (rc != 0) {
			rc = errno;
			LogCrit(COMPONENT_LOG,
				 "Cannot create new log file (%s), because: %s",
				 (char *)private, strerror(rc));
			return -rc;
		}
	}
	PTHREAD_RWLOCK_wrlock(&log_rwlock);

	facility = find_log_facility(name);

	if (facility != NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);

		LogInfo(COMPONENT_LOG, "Facility %s already exists", name);

		return -EEXIST;
	}

	facility = gsh_calloc(1, sizeof(*facility));

	facility->lf_name = gsh_strdup(name);
	facility->lf_func = log_func;
	facility->lf_max_level = max_level;
	facility->lf_headers = header;

	if (log_func == log_to_file && private != NULL)
		facility->lf_private = gsh_strdup(private);
	else
		facility->lf_private = private;

	glist_add_tail(&facility_list, &facility->lf_list);

	PTHREAD_RWLOCK_unlock(&log_rwlock);

	LogInfo(COMPONENT_LOG, "Created log facility %s",
		facility->lf_name);

	return 0;
}

/**
 * @brief Release a logger facility
 *
 * Release the named facility and all its resources.
 * disable it first if it is active.  It will refuse to
 * release the default logger because that could leave the server
 * with no place to send messages.
 *
 * @param name [IN] name of soon to be deceased logger
 *
 * @returns always.  The logger is not disabled or released on errors
 */

void release_log_facility(const char *name)
{
	struct log_facility *facility;

	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "Attempting release of non-existant log facility (%s)",
			 name);
		return;
	}
	if (facility == default_facility) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "Attempting to release default log facility (%s)",
			 name);
		return;
	}
	if (!glist_null(&facility->lf_active))
		glist_del(&facility->lf_active);
	glist_del(&facility->lf_list);
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	if (facility->lf_func == log_to_file &&
	    facility->lf_private != NULL)
		gsh_free(facility->lf_private);
	gsh_free(facility->lf_name);
	gsh_free(facility);
}

/**
 * @brief Enable the named logger
 *
 * Enabling a logger adds it to the list of facilites that will be
 * used to report messages.
 *
 * @param name [IN] the name of the logger to enable
 *
 * @return 0 on success, -errno on errors.
 */

int enable_log_facility(const char *name)
{
	struct log_facility *facility;

	if (name == NULL || *name == '\0')
		return -EINVAL;
	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogInfo(COMPONENT_LOG, "Facility %s does not exist", name);
		return -ENOENT;
	}

	if (glist_null(&facility->lf_active))
		glist_add_tail(&active_facility_list, &facility->lf_active);

	if (facility->lf_headers > max_headers)
		max_headers = facility->lf_headers;
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	return 0;
}

/**
 * @brief Disable the named logger
 *
 * Disabling a logger ends logging output to that facility.
 * Disabling the default logger is not allowed.  Another facility
 * must be set instead.  Loggers can be re-enabled at any time.
 *
 * @param name [IN] the name of the logger to enable
 *
 * @return 0 on success, -errno on errors.
 */

int disable_log_facility(const char *name)
{
	struct log_facility *facility;

	if (name == NULL || *name == '\0')
		return -EINVAL;
	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogInfo(COMPONENT_LOG, "Facility %s does not exist", name);
		return -ENOENT;
	}
	if (glist_null(&facility->lf_active)) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogDebug(COMPONENT_LOG,
			 "Log facility (%s) is already disabled",
			 name);
		return 0;
	}
	if (facility == default_facility) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "Cannot disable the default logger (%s)",
			 default_facility->lf_name);
		return -EPERM;
	}
	glist_del(&facility->lf_active);
	if (facility->lf_headers == max_headers) {
		struct glist_head *glist;
		struct log_facility *found;

		max_headers = LH_NONE;
		glist_for_each(glist, &active_facility_list) {
			found = glist_entry(glist,
					    struct log_facility, lf_active);
			if (found->lf_headers > max_headers)
				max_headers = found->lf_headers;
		}
	}
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	return 0;
}

/**
 * @brief Set the named logger as the default logger
 *
 * The default logger can not be released sp we set another one as
 * the default instead.  The previous default logger is disabled.
 *
 * @param name [IN] the name of the logger to enable
 *
 * @return 0 on success, -errno on errors.
 */

static int set_default_log_facility(const char *name)
{
	struct log_facility *facility;

	if (name == NULL || *name == '\0')
		return -EINVAL;

	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG, "Facility %s does not exist", name);
		return -ENOENT;
	}
	if (facility == default_facility)
		goto out;
	if (glist_null(&facility->lf_active))
		glist_add_tail(&active_facility_list, &facility->lf_active);
	if (default_facility != NULL) {
		assert(!glist_null(&default_facility->lf_active));
		glist_del(&default_facility->lf_active);
		if (facility->lf_headers != max_headers) {
			struct glist_head *glist;
			struct log_facility *found;

			max_headers = LH_NONE;
			glist_for_each(glist, &active_facility_list) {
				found = glist_entry(glist,
						    struct log_facility,
						    lf_active);
				if (found->lf_headers > max_headers)
					max_headers = found->lf_headers;
			}
		}
	} else if (facility->lf_headers > max_headers)
		max_headers = facility->lf_headers;
	default_facility = facility;
out:
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	return 0;
}

/**
 * @brief Set the destination for logger
 *
 * This function only works if the facility outputs to files.
 *
 * @param name [IN] the name of the facility
 * @param dest [IN] "stdout", "stderr", "syslog", or a file path
 *
 * @return 0 on success, -errno on errors
 */

int set_log_destination(const char *name, char *dest)
{
	struct log_facility *facility;
	int rc;

	if (name == NULL || *name == '\0')
		return -EINVAL;
	if (dest == NULL ||
	    *dest == '\0' ||
	    strlen(dest) >= MAXPATHLEN) {
		LogCrit(COMPONENT_LOG,
			 "New log file path empty or too long");
		return -EINVAL;
	}
	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "No such log facility (%s)",
			 name);
		return -ENOENT;
	}
	if (facility->lf_func == log_to_file) {
		char *logfile, *dir;

		dir = alloca(strlen(dest) + 1);
		strcpy(dir, dest);
		dir = dirname(dir);
		rc = access(dir, W_OK);
		if (rc != 0) {
			PTHREAD_RWLOCK_unlock(&log_rwlock);
			LogCrit(COMPONENT_LOG,
				"Cannot create new log file (%s), because: %s",
				dest, strerror(errno));
			return -errno;
		}
		logfile = gsh_strdup(dest);
		gsh_free(facility->lf_private);
		facility->lf_private = logfile;
	} else if (facility->lf_func == log_to_stream) {
		FILE *out;

		if (strcasecmp(dest, "stdout") == 0) {
			out = stdout;
		} else if (strcasecmp(dest, "stderr") == 0) {
			out = stderr;
		} else {
			PTHREAD_RWLOCK_unlock(&log_rwlock);
			LogCrit(COMPONENT_LOG,
				"Expected STDERR or STDOUT, not (%s)",
				dest);
			return -EINVAL;
		}
		facility->lf_private = out;
	} else {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "Log facility %s destination is not changeable",
			facility->lf_name);
		return -EINVAL;
	}
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	return 0;
}

/**
 * @brief Set maximum logging level for a facilty
 *
 * @param name [IN] the name of the facility
 * @param max_level [IN] Maximum level
 *
 *
 * @return 0 on success, -errno on errors
 */

int set_log_level(const char *name, log_levels_t max_level)
{
	struct log_facility *facility;

	if (name == NULL || *name == '\0')
		return -EINVAL;
	if (max_level < NIV_NULL || max_level >= NB_LOG_LEVEL)
		return -EINVAL;
	PTHREAD_RWLOCK_wrlock(&log_rwlock);
	facility = find_log_facility(name);
	if (facility == NULL) {
		PTHREAD_RWLOCK_unlock(&log_rwlock);
		LogCrit(COMPONENT_LOG,
			 "No such log facility (%s)",
			 name);
		return -ENOENT;
	}
	facility->lf_max_level = max_level;
	PTHREAD_RWLOCK_unlock(&log_rwlock);
	return 0;
}

/**
 * @brief Initialize Logging
 *
 * Called very early in server init to make logging available as
 * soon as possible. Create a logger to stderr first and make it
 * the default.  We are forced to fprintf to stderr by hand until
 * this happens.  Once this is up, the logger is working.
 * We then get stdout and syslog loggers init'd.
 * If log_path (passed in via -L on the command line), we get a
 * FILE logger going and make it our default logger.  Otherwise,
 * we use syslog as the default.
 *
 * @param log_path    [IN] optarg from -L, otherwise NULL
 * @param debug_level [IN] global debug level from -N optarg
 */

void init_logging(const char *log_path, const int debug_level)
{
	int rc;

	/* Finish initialization of and register log facilities. */
	glist_init(&facility_list);
	glist_init(&active_facility_list);

	/* Initialize const_log_str to defaults. Ganesha can start logging
	 * before the LOG config is processed (in fact, LOG config can itself
	 * issue log messages to indicate errors.
	 */
	set_const_log_str();

	rc = create_log_facility("STDERR", log_to_stream,
				 NIV_FULL_DEBUG, LH_ALL, stderr);
	if (rc != 0) {
		fprintf(stderr, "Create error (%s) for STDERR log facility!",
			strerror(-rc));
		Fatal();
	}
	rc = set_default_log_facility("STDERR");
	if (rc != 0) {
		fprintf(stderr, "Enable error (%s) for STDERR log facility!",
			strerror(-rc));
		Fatal();
	}
	rc = create_log_facility("STDOUT", log_to_stream,
				 NIV_FULL_DEBUG, LH_ALL, stdout);
	if (rc != 0)
		LogFatal(COMPONENT_LOG,
			 "Create error (%s) for STDOUT log facility!",
			strerror(-rc));
	rc = create_log_facility("SYSLOG", log_to_syslog,
				 NIV_FULL_DEBUG, LH_COMPONENT, NULL);
	if (rc != 0)
		LogFatal(COMPONENT_LOG,
			 "Create error (%s) for SYSLOG log facility!",
			 strerror(-rc));

	if (log_path) {
		if ((strcmp(log_path, "STDERR") == 0) ||
		    (strcmp(log_path, "SYSLOG") == 0) ||
		    (strcmp(log_path, "STDOUT") == 0)) {
			rc = set_default_log_facility(log_path);
			if (rc != 0)
				LogFatal(COMPONENT_LOG,
					 "Enable error (%s) for %s logging!",
					 strerror(-rc), log_path);
		} else {
			rc = create_log_facility("FILE", log_to_file,
						 NIV_FULL_DEBUG, LH_ALL,
						 (void *)log_path);
			if (rc != 0)
				LogFatal(COMPONENT_LOG,
					 "Create error (%s) for FILE (%s) logging!",
					 strerror(-rc), log_path);
			rc = set_default_log_facility("FILE");
			if (rc != 0)
				LogFatal(COMPONENT_LOG,
					 "Enable error (%s) for FILE (%s) logging!",
					 strerror(-rc), log_path);
		}
	} else {
		/* Fall back to SYSLOG as the first default facility */
		rc = set_default_log_facility("SYSLOG");
		if (rc != 0)
			LogFatal(COMPONENT_LOG,
				 "Enable error (%s) for SYSLOG logging!",
				 strerror(-rc));
	}
	if (debug_level >= 0)
		SetLevelDebug(debug_level);

	set_logging_from_env();

	ArmSignal(SIGUSR1, IncrementLevelDebug);
	ArmSignal(SIGUSR2, DecrementLevelDebug);
}

/*
 * Routines for managing error messages
 */
static int log_to_syslog(log_header_t headers, void *private,
			 log_levels_t level,
			 struct display_buffer *buffer, char *compstr,
			 char *message)
{
	if (!syslog_opened) {
		openlog("nfs-ganesha", LOG_PID, LOG_USER);
		syslog_opened = 1;
	}

	/* Writing to syslog. */
	syslog(tabLogLevel[level].syslog_level, "%s", compstr);

	return 0;
}

static int log_to_file(log_header_t headers, void *private,
		       log_levels_t level,
		       struct display_buffer *buffer, char *compstr,
		       char *message)
{
	int fd, my_status, len, rc = 0;
	char *path = private;

	len = display_buffer_len(buffer);

	/* Add newline to end of buffer */
	buffer->b_start[len] = '\n';
	buffer->b_start[len + 1] = '\0';

	fd = open(path, O_WRONLY | O_APPEND | O_CREAT, log_mask);

	if (fd != -1) {
		rc = write(fd, buffer->b_start, len + 1);

		if (rc < (len + 1)) {
			if (rc >= 0)
				my_status = ENOSPC;
			else
				my_status = errno;

			(void)close(fd);

			goto error;
		}

		rc = close(fd);

		if (rc == 0)
			goto out;
	}

	my_status = errno;

 error:

	fprintf(stderr,
		"Error: couldn't complete write to the log file %s status=%d (%s) message was:\n%s",
		path, my_status, strerror(my_status), buffer->b_start);

 out:

	/* Remove newline from buffer */
	buffer->b_start[len] = '\0';

	return rc;
}

static int log_to_stream(log_header_t headers, void *private,
			 log_levels_t level,
			 struct display_buffer *buffer, char *compstr,
			 char *message)
{
	FILE *stream = private;
	int rc;
	char *msg = buffer->b_start;
	int len;

	len = display_buffer_len(buffer);

	/* Add newline to end of buffer */
	buffer->b_start[len] = '\n';
	buffer->b_start[len + 1] = '\0';

	switch (headers) {
	case LH_NONE:
		msg = message;
		break;
	case LH_COMPONENT:
		msg = compstr;
		break;
	case LH_ALL:
		msg = buffer->b_start;
		break;
	default:
		msg = "Somehow header level got messed up!!";
	}

	rc = fputs(msg, stream);

	if (rc != EOF)
		rc = fflush(stream);

	/* Remove newline from buffer */
	buffer->b_start[len] = '\0';

	if (rc == EOF)
		return -1;
	else
		return 0;
}

int display_timeval(struct display_buffer *dspbuf, struct timeval *tv)
{
	char *fmt = date_time_fmt;
	int b_left = display_start(dspbuf);
	struct tm the_date;
	char tbuf[MAX_TD_FMT_LEN];
	time_t tm = tv->tv_sec;

	if (b_left <= 0)
		return b_left;

	if (logfields->datefmt == TD_NONE && logfields->timefmt == TD_NONE)
		fmt = "%c ";

	Localtime_r(&tm, &the_date);

	/* Earlier we build the date/time format string in
	 * date_time_fmt, now use that to format the time and/or date.
	 * If time format is TD_SYSLOG_USEC, then we need an additional
	 * step to add the microseconds (since strftime just takes a
	 * struct tm which was filled in from a time_t and thus does not
	 * have microseconds.
	 */
	if (strftime(tbuf, sizeof(tbuf), fmt, &the_date) != 0) {
		if (logfields->timefmt == TD_SYSLOG_USEC)
			b_left = display_printf(dspbuf, tbuf, tv->tv_usec);
		else
			b_left = display_cat(dspbuf, tbuf);
	}

	return b_left;
}

int display_timespec(struct display_buffer *dspbuf, struct timespec *ts)
{
	char *fmt = date_time_fmt;
	int b_left = display_start(dspbuf);
	struct tm the_date;
	char tbuf[MAX_TD_FMT_LEN];
	time_t tm = ts->tv_sec;

	if (b_left <= 0)
		return b_left;

	if (logfields->datefmt == TD_NONE && logfields->timefmt == TD_NONE)
		fmt = "%c ";

	Localtime_r(&tm, &the_date);

	/* Earlier we build the date/time format string in
	 * date_time_fmt, now use that to format the time and/or date.
	 * If time format is TD_SYSLOG_USEC, then we need an additional
	 * step to add the microseconds (since strftime just takes a
	 * struct tm which was filled in from a time_t and thus does not
	 * have microseconds.
	 */
	if (strftime(tbuf, sizeof(tbuf), fmt, &the_date) != 0) {
		if (logfields->timefmt == TD_SYSLOG_USEC)
			b_left = display_printf(dspbuf, tbuf, ts->tv_nsec);
		else
			b_left = display_cat(dspbuf, tbuf);
	}

	return b_left;
}

static int display_log_header(struct display_buffer *dsp_log)
{
	int b_left = display_start(dsp_log);

	if (b_left <= 0 || max_headers < LH_ALL)
		return b_left;

	/* Print date and/or time if either flag is enabled. */
	if (b_left > 0
	    && (logfields->datefmt != TD_NONE
		|| logfields->timefmt != TD_NONE)) {
		struct timeval tv;

		if (logfields->timefmt == TD_SYSLOG_USEC) {
			gettimeofday(&tv, NULL);
		} else {
			tv.tv_sec = time(NULL);
			tv.tv_usec = 0;
		}

		b_left = display_timeval(dsp_log, &tv);

		if (b_left > 0)
			b_left = display_cat(dsp_log, " ");
	}

	if (b_left > 0 && const_log_str[0] != '\0')
		b_left = display_cat(dsp_log, const_log_str);

	/* If thread name will not follow, need a : separator */
	if (b_left > 0 && !logfields->disp_threadname)
		b_left = display_cat(dsp_log, ": ");

	/* If we overflowed the buffer with the header, just skip it. */
	if (b_left == 0) {
		display_reset_buffer(dsp_log);
		b_left = display_start(dsp_log);
	}

	/* The message will now start at dsp_log.b_current */
	return b_left;
}

static int display_log_component(struct display_buffer *dsp_log,
				 log_components_t component, const char *file,
				 int line, const char *function, int level)
{
	int b_left = display_start(dsp_log);

	if (b_left <= 0 || max_headers < LH_COMPONENT)
		return b_left;

	if (b_left > 0 && logfields->disp_clientip) {
		if (clientip)
			b_left = display_printf(dsp_log, "[%s] ",
						clientip);
		else
			b_left = display_printf(dsp_log, "[none] ");
	}

	if (b_left > 0 && logfields->disp_threadname) {
		if (thread_name[0] != '\0')
			b_left = display_printf(dsp_log, "[%s] ",
						thread_name);
		else
			b_left = display_printf(dsp_log, "[%p] ",
						thread_name);
	}

	if (b_left > 0 && logfields->disp_filename) {
		if (logfields->disp_linenum)
			b_left = display_printf(dsp_log, "%s:", file);
		else
			b_left = display_printf(dsp_log, "%s :", file);
	}

	if (b_left > 0 && logfields->disp_linenum)
		b_left = display_printf(dsp_log, "%d :", line);

	if (b_left > 0 && logfields->disp_funct)
		b_left = display_printf(dsp_log, "%s :", function);

	if (b_left > 0 && logfields->disp_comp)
		b_left =
		    display_printf(dsp_log, "%s :",
				   LogComponents[component].comp_str);

	if (b_left > 0 && logfields->disp_level)
		b_left =
		    display_printf(dsp_log, "%s :",
				   tabLogLevel[level].short_str);

	/* If we overflowed the buffer with the header, just skip it. */
	if (b_left == 0) {
		display_reset_buffer(dsp_log);
		b_left = display_start(dsp_log);
	}

	return b_left;
}

void display_log_component_level(log_components_t component, const char *file,
				int line, const char *function,
				log_levels_t level, const char *format,
				va_list arguments)
{
	char *compstr;
	char *message;
	int b_left;
	struct glist_head *glist;
	struct log_facility *facility;
	struct display_buffer dsp_log = {sizeof(log_buffer),
					 log_buffer, log_buffer};

	/* Build up the messsage and capture the various positions in it. */
	b_left = display_log_header(&dsp_log);

	if (b_left > 0)
		compstr = dsp_log.b_current;
	else
		compstr = dsp_log.b_start;

	if (b_left > 0)
		b_left =
		    display_log_component(&dsp_log, component, file, line,
					  function, level);

	if (b_left > 0)
		message = dsp_log.b_current;
	else
		message = dsp_log.b_start;

	if (b_left > 0)
		b_left = display_vprintf(&dsp_log, format, arguments);

#ifdef USE_LTTNG
	tracepoint(ganesha_logger, log,
		   component, level, file, line, function, message);
#endif

	PTHREAD_RWLOCK_rdlock(&log_rwlock);

	glist_for_each(glist, &active_facility_list) {
		facility = glist_entry(glist, struct log_facility, lf_active);

		if (level <= facility->lf_max_level
		    && facility->lf_func != NULL)
			facility->lf_func(facility->lf_headers,
					  facility->lf_private,
					  level, &dsp_log,
					  compstr, message);
	}

	PTHREAD_RWLOCK_unlock(&log_rwlock);

	if (level == NIV_FATAL)
		Fatal();
}

/**
 * @brief Default logging levels
 *
 * These are for early initialization and whenever we
 * have to fall back to something that will at least work...
 */

static log_levels_t default_log_levels[] = {
	[COMPONENT_ALL] = NIV_NULL,
	[COMPONENT_LOG] = NIV_EVENT,
	[COMPONENT_MEM_ALLOC] = NIV_EVENT,
	[COMPONENT_MEMLEAKS] = NIV_EVENT,
	[COMPONENT_FSAL] = NIV_EVENT,
	[COMPONENT_NFSPROTO] = NIV_EVENT,
	[COMPONENT_NFS_V4] = NIV_EVENT,
	[COMPONENT_EXPORT] = NIV_EVENT,
	[COMPONENT_FILEHANDLE] = NIV_EVENT,
	[COMPONENT_DISPATCH] = NIV_EVENT,
	[COMPONENT_CACHE_INODE] = NIV_EVENT,
	[COMPONENT_CACHE_INODE_LRU] = NIV_EVENT,
	[COMPONENT_HASHTABLE] = NIV_EVENT,
	[COMPONENT_HASHTABLE_CACHE] = NIV_EVENT,
	[COMPONENT_DUPREQ] = NIV_EVENT,
	[COMPONENT_INIT] = NIV_EVENT,
	[COMPONENT_MAIN] = NIV_EVENT,
	[COMPONENT_IDMAPPER] = NIV_EVENT,
	[COMPONENT_NFS_READDIR] = NIV_EVENT,
	[COMPONENT_NFS_V4_LOCK] = NIV_EVENT,
	[COMPONENT_CONFIG] = NIV_EVENT,
	[COMPONENT_CLIENTID] = NIV_EVENT,
	[COMPONENT_SESSIONS] = NIV_EVENT,
	[COMPONENT_PNFS] = NIV_EVENT,
	[COMPONENT_RW_LOCK] = NIV_EVENT,
	[COMPONENT_NLM] = NIV_EVENT,
	[COMPONENT_RPC] = NIV_EVENT,
	[COMPONENT_TIRPC] = NIV_EVENT,
	[COMPONENT_NFS_CB] = NIV_EVENT,
	[COMPONENT_THREAD] = NIV_EVENT,
	[COMPONENT_NFS_V4_ACL] = NIV_EVENT,
	[COMPONENT_STATE] = NIV_EVENT,
	[COMPONENT_9P] = NIV_EVENT,
	[COMPONENT_9P_DISPATCH] = NIV_EVENT,
	[COMPONENT_FSAL_UP] = NIV_EVENT,
	[COMPONENT_DBUS] = NIV_EVENT,
	[COMPONENT_NFS_MSK] = NIV_EVENT
};

log_levels_t *component_log_level = default_log_levels;

struct log_component_info LogComponents[COMPONENT_COUNT] = {
	[COMPONENT_ALL] = {
		.comp_name = "COMPONENT_ALL",
		.comp_str = "",},
	[COMPONENT_LOG] = {
		.comp_name = "COMPONENT_LOG",
		.comp_str = "LOG",},
	[COMPONENT_MEM_ALLOC] = {
		.comp_name = "COMPONENT_MEM_ALLOC",
		.comp_str = "MEM ALLOC",},
	[COMPONENT_MEMLEAKS] = {
		.comp_name = "COMPONENT_MEMLEAKS",
		.comp_str = "LEAKS",},
	[COMPONENT_FSAL] = {
		.comp_name = "COMPONENT_FSAL",
		.comp_str = "FSAL",},
	[COMPONENT_NFSPROTO] = {
		.comp_name = "COMPONENT_NFSPROTO",
		.comp_str = "NFS3",},
	[COMPONENT_NFS_V4] = {
		.comp_name = "COMPONENT_NFS_V4",
		.comp_str = "NFS4",},
	[COMPONENT_EXPORT] = {
		.comp_name = "COMPONENT_EXPORT",
		.comp_str = "EXPORT",},
	[COMPONENT_FILEHANDLE] = {
		.comp_name = "COMPONENT_FILEHANDLE",
		.comp_str = "FH",},
	[COMPONENT_DISPATCH] = {
		.comp_name = "COMPONENT_DISPATCH",
		.comp_str = "DISP",},
	[COMPONENT_CACHE_INODE] = {
		.comp_name = "COMPONENT_CACHE_INODE",
		.comp_str = "INODE",},
	[COMPONENT_CACHE_INODE_LRU] = {
		.comp_name = "COMPONENT_CACHE_INODE_LRU",
		.comp_str = "INODE LRU",},
	[COMPONENT_HASHTABLE] = {
		.comp_name = "COMPONENT_HASHTABLE",
		.comp_str = "HT",},
	[COMPONENT_HASHTABLE_CACHE] = {
		.comp_name = "COMPONENT_HASHTABLE_CACHE",
		.comp_str = "HT CACHE",},
	[COMPONENT_DUPREQ] = {
		.comp_name = "COMPONENT_DUPREQ",
		.comp_str = "DUPREQ",},
	[COMPONENT_INIT] = {
		.comp_name = "COMPONENT_INIT",
		.comp_str = "NFS STARTUP",},
	[COMPONENT_MAIN] = {
		.comp_name = "COMPONENT_MAIN",
		.comp_str = "MAIN",},
	[COMPONENT_IDMAPPER] = {
		.comp_name = "COMPONENT_IDMAPPER",
		.comp_str = "ID MAPPER",},
	[COMPONENT_NFS_READDIR] = {
		.comp_name = "COMPONENT_NFS_READDIR",
		.comp_str = "NFS READDIR",},
	[COMPONENT_NFS_V4_LOCK] = {
		.comp_name = "COMPONENT_NFS_V4_LOCK",
		.comp_str = "NFS4 LOCK",},
	[COMPONENT_CONFIG] = {
		.comp_name = "COMPONENT_CONFIG",
		.comp_str = "CONFIG",},
	[COMPONENT_CLIENTID] = {
		.comp_name = "COMPONENT_CLIENTID",
		.comp_str = "CLIENT ID",},
	[COMPONENT_SESSIONS] = {
		.comp_name = "COMPONENT_SESSIONS",
		.comp_str = "SESSIONS",},
	[COMPONENT_PNFS] = {
		.comp_name = "COMPONENT_PNFS",
		.comp_str = "PNFS",},
	[COMPONENT_RW_LOCK] = {
		.comp_name = "COMPONENT_RW_LOCK",
		.comp_str = "RW LOCK",},
	[COMPONENT_NLM] = {
		.comp_name = "COMPONENT_NLM",
		.comp_str = "NLM",},
	[COMPONENT_RPC] = {
		.comp_name = "COMPONENT_RPC",
		.comp_str = "RPC",},
	[COMPONENT_TIRPC] = {
		.comp_name = "COMPONENT_TIRPC",
		.comp_str = "TIRPC",},
	[COMPONENT_NFS_CB] = {
		.comp_name = "COMPONENT_NFS_CB",
		.comp_str = "NFS CB",},
	[COMPONENT_THREAD] = {
		.comp_name = "COMPONENT_THREAD",
		.comp_str = "THREAD",},
	[COMPONENT_NFS_V4_ACL] = {
		.comp_name = "COMPONENT_NFS_V4_ACL",
		.comp_str = "NFS4 ACL",},
	[COMPONENT_STATE] = {
		.comp_name = "COMPONENT_STATE",
		.comp_str = "STATE",},
	[COMPONENT_9P] = {
		.comp_name = "COMPONENT_9P",
		.comp_str = "9P",},
	[COMPONENT_9P_DISPATCH] = {
		.comp_name = "COMPONENT_9P_DISPATCH",
		.comp_str = "9P DISP",},
	[COMPONENT_FSAL_UP] = {
		.comp_name = "COMPONENT_FSAL_UP",
		.comp_str = "FSAL_UP",},
	[COMPONENT_DBUS] = {
		.comp_name = "COMPONENT_DBUS",
		.comp_str = "DBUS",},
	[COMPONENT_NFS_MSK] = {
		.comp_name = "COMPONENT_NFS_MSK",
		.comp_str = "NFS_MSK",}
};

void DisplayLogComponentLevel(log_components_t component, const char *file,
			int line, const char *function, log_levels_t level,
			const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);

	display_log_component_level(component, file, line, function, level,
				    format, arguments);

	va_end(arguments);
}

void LogMallocFailure(const char *file, int line, const char *function,
		      const char *allocator)
{
	DisplayLogComponentLevel(COMPONENT_MEM_ALLOC, (char *) file, line,
				 (char *)function, NIV_NULL,
				 "Aborting %s due to out of memory",
				 allocator);
}

/*
 *  Re-export component logging to TI-RPC internal logging
 */
void rpc_warnx(char *fmt, ...)
{
	va_list ap;

	if (component_log_level[COMPONENT_TIRPC] <= NIV_FATAL)
		return;

	va_start(ap, fmt);

	display_log_component_level(COMPONENT_TIRPC, "<no-file>", 0, "rpc",
				    component_log_level[COMPONENT_TIRPC],
				    fmt, ap);

	va_end(ap);

}

#ifdef USE_DBUS

static bool dbus_prop_get(log_components_t component, DBusMessageIter *reply)
{
	char *level_code;

	level_code = ReturnLevelInt(component_log_level[component]);
	if (level_code == NULL)
		return false;
	if (!dbus_message_iter_append_basic
	    (reply, DBUS_TYPE_STRING, &level_code))
		return false;
	return true;
}

static bool dbus_prop_set(log_components_t component, DBusMessageIter *arg)
{
	char *level_code;
	int log_level;

	if (dbus_message_iter_get_arg_type(arg) != DBUS_TYPE_STRING)
		return false;
	dbus_message_iter_get_basic(arg, &level_code);
	log_level = ReturnLevelAscii(level_code);
	if (log_level == -1) {
		LogDebug(COMPONENT_DBUS,
			 "Invalid log level: '%s' given for component %s",
			 level_code, LogComponents[component].comp_name);
		return false;
	}

	if (component == COMPONENT_ALL) {
		_SetLevelDebug(log_level);
		LogChanges("Dbus set log level for all components to %s",
			   level_code);
	} else {
		LogChanges("Dbus set log level for %s from %s to %s.",
			   LogComponents[component].comp_name,
			   ReturnLevelInt(component_log_level[component]),
			   ReturnLevelInt(log_level));
		SetComponentLogLevel(component, log_level);
	}
	return true;
}

/* Macros to make mapping properties table to components enum etc. easier
 * expands to table entries and shim functions.
 */

#define HANDLE_PROP(component) \
static bool dbus_prop_get_COMPONENT_##component(DBusMessageIter *reply) \
{ \
	return dbus_prop_get(COMPONENT_##component, reply);\
} \
\
static bool dbus_prop_set_COMPONENT_##component(DBusMessageIter *args) \
{ \
	return dbus_prop_set(COMPONENT_##component, args);\
} \
\
static struct gsh_dbus_prop COMPONENT_##component##_prop = {	\
	.name = "COMPONENT_" #component,			\
	.access = DBUS_PROP_READWRITE,				\
	.type =  "s",						\
	.get = dbus_prop_get_COMPONENT_##component,		\
	.set = dbus_prop_set_COMPONENT_##component		\
}

#define LOG_PROPERTY_ITEM(component) (&COMPONENT_##component##_prop)

/**
 * @brief Log property handlers.
 *
 * Expands to get/set functions that match dbus_prop_get/set protos
 * and call common handler with component enum as arg.
 * There is one line per log_components_t enum.
 * These must also match LOG_PROPERTY_ITEM
 */

HANDLE_PROP(ALL);
HANDLE_PROP(LOG);
HANDLE_PROP(MEM_ALLOC);
HANDLE_PROP(MEMLEAKS);
HANDLE_PROP(FSAL);
HANDLE_PROP(NFSPROTO);
HANDLE_PROP(NFS_V4);
HANDLE_PROP(EXPORT);
HANDLE_PROP(FILEHANDLE);
HANDLE_PROP(DISPATCH);
HANDLE_PROP(CACHE_INODE);
HANDLE_PROP(CACHE_INODE_LRU);
HANDLE_PROP(HASHTABLE);
HANDLE_PROP(HASHTABLE_CACHE);
HANDLE_PROP(DUPREQ);
HANDLE_PROP(INIT);
HANDLE_PROP(MAIN);
HANDLE_PROP(IDMAPPER);
HANDLE_PROP(NFS_READDIR);
HANDLE_PROP(NFS_V4_LOCK);
HANDLE_PROP(CONFIG);
HANDLE_PROP(CLIENTID);
HANDLE_PROP(SESSIONS);
HANDLE_PROP(PNFS);
HANDLE_PROP(RW_LOCK);
HANDLE_PROP(NLM);
HANDLE_PROP(RPC);
HANDLE_PROP(TIRPC);
HANDLE_PROP(NFS_CB);
HANDLE_PROP(THREAD);
HANDLE_PROP(NFS_V4_ACL);
HANDLE_PROP(STATE);
HANDLE_PROP(9P);
HANDLE_PROP(9P_DISPATCH);
HANDLE_PROP(FSAL_UP);
HANDLE_PROP(DBUS);
HANDLE_PROP(NFS_MSK);

static struct gsh_dbus_prop *log_props[] = {
	LOG_PROPERTY_ITEM(ALL),
	LOG_PROPERTY_ITEM(LOG),
	LOG_PROPERTY_ITEM(MEM_ALLOC),
	LOG_PROPERTY_ITEM(MEMLEAKS),
	LOG_PROPERTY_ITEM(FSAL),
	LOG_PROPERTY_ITEM(NFSPROTO),
	LOG_PROPERTY_ITEM(NFS_V4),
	LOG_PROPERTY_ITEM(EXPORT),
	LOG_PROPERTY_ITEM(FILEHANDLE),
	LOG_PROPERTY_ITEM(DISPATCH),
	LOG_PROPERTY_ITEM(CACHE_INODE),
	LOG_PROPERTY_ITEM(CACHE_INODE_LRU),
	LOG_PROPERTY_ITEM(HASHTABLE),
	LOG_PROPERTY_ITEM(HASHTABLE_CACHE),
	LOG_PROPERTY_ITEM(DUPREQ),
	LOG_PROPERTY_ITEM(INIT),
	LOG_PROPERTY_ITEM(MAIN),
	LOG_PROPERTY_ITEM(IDMAPPER),
	LOG_PROPERTY_ITEM(NFS_READDIR),
	LOG_PROPERTY_ITEM(NFS_V4_LOCK),
	LOG_PROPERTY_ITEM(CONFIG),
	LOG_PROPERTY_ITEM(CLIENTID),
	LOG_PROPERTY_ITEM(SESSIONS),
	LOG_PROPERTY_ITEM(PNFS),
	LOG_PROPERTY_ITEM(RW_LOCK),
	LOG_PROPERTY_ITEM(NLM),
	LOG_PROPERTY_ITEM(RPC),
	LOG_PROPERTY_ITEM(TIRPC),
	LOG_PROPERTY_ITEM(NFS_CB),
	LOG_PROPERTY_ITEM(THREAD),
	LOG_PROPERTY_ITEM(NFS_V4_ACL),
	LOG_PROPERTY_ITEM(STATE),
	LOG_PROPERTY_ITEM(9P),
	LOG_PROPERTY_ITEM(9P_DISPATCH),
	LOG_PROPERTY_ITEM(FSAL_UP),
	LOG_PROPERTY_ITEM(DBUS),
	LOG_PROPERTY_ITEM(NFS_MSK),
	NULL
};

struct gsh_dbus_interface log_interface = {
	.name = "org.ganesha.nfsd.log.component",
	.signal_props = false,
	.props = log_props,
	.methods = NULL,
	.signals = NULL
};

#endif				/* USE_DBUS */

enum facility_state {
	FAC_IDLE,
	FAC_ACTIVE,
	FAC_DEFAULT
};

struct facility_config {
	struct glist_head fac_list;
	char *facility_name;
	char *dest;
	enum facility_state state;
	lf_function_t *func;
	log_header_t headers;
	log_levels_t max_level;
	void *lf_private;
};

/**
 * @brief Logger config block parameters
 */

struct logger_config {
	struct glist_head facility_list;
	struct logfields *logfields;
	log_levels_t *comp_log_level;
	log_levels_t default_level;
	uint32_t rpc_debug_flags;
};

/**
 * @brief Enumerated time and date format parameters
 */

static struct config_item_list timeformats[] = {
	CONFIG_LIST_TOK("ganesha", TD_GANESHA),
	CONFIG_LIST_TOK("true", TD_GANESHA),
	CONFIG_LIST_TOK("local", TD_LOCAL),
	CONFIG_LIST_TOK("8601", TD_8601),
	CONFIG_LIST_TOK("ISO-8601", TD_8601),
	CONFIG_LIST_TOK("ISO 8601", TD_8601),
	CONFIG_LIST_TOK("ISO", TD_8601),
	CONFIG_LIST_TOK("syslog", TD_SYSLOG),
	CONFIG_LIST_TOK("syslog_usec", TD_SYSLOG_USEC),
	CONFIG_LIST_TOK("false", TD_NONE),
	CONFIG_LIST_TOK("none", TD_NONE),
	CONFIG_LIST_TOK("user_defined", TD_USER),
	CONFIG_LIST_EOL
};

/**
 * @brief Logging format parameters
 */

static struct config_item format_options[] = {
	CONF_ITEM_TOKEN("date_format", TD_GANESHA, timeformats,
			logfields, datefmt),
	CONF_ITEM_TOKEN("time_format", TD_GANESHA, timeformats,
			logfields, timefmt),
	CONF_ITEM_STR("user_date_format", 1, MAX_TD_FMT_LEN, NULL,
		       logfields, user_date_fmt),
	CONF_ITEM_STR("user_time_format", 1, MAX_TD_FMT_LEN, NULL,
		       logfields, user_time_fmt),
	CONF_ITEM_BOOL("EPOCH", true,
		       logfields, disp_epoch),
	CONF_ITEM_BOOL("CLIENTIP", false,
		       logfields, disp_clientip),
	CONF_ITEM_BOOL("HOSTNAME", true,
		       logfields, disp_host),
	CONF_ITEM_BOOL("PROGNAME", true,
		       logfields, disp_prog),
	CONF_ITEM_BOOL("PID", true,
		       logfields, disp_pid),
	CONF_ITEM_BOOL("THREAD_NAME", true,
		       logfields, disp_threadname),
	CONF_ITEM_BOOL("FILE_NAME", true,
		       logfields, disp_filename),
	CONF_ITEM_BOOL("LINE_NUM", true,
		       logfields, disp_linenum),
	CONF_ITEM_BOOL("FUNCTION_NAME", true,
		       logfields, disp_funct),
	CONF_ITEM_BOOL("COMPONENT", true,
		       logfields, disp_comp),
	CONF_ITEM_BOOL("LEVEL", true,
		       logfields, disp_level),
	CONFIG_EOL
};

/**
 * @brief Initialize the log message format parameters
 */

static void *format_init(void *link_mem, void *self_struct)
{
	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL)
		return NULL;
	if (self_struct == NULL)
		return gsh_calloc(1, sizeof(struct logfields));
	else {
		struct logfields *lf = self_struct;

		if (lf->user_date_fmt != NULL)
			gsh_free(lf->user_date_fmt);
		if (lf->user_time_fmt != NULL)
			gsh_free(lf->user_time_fmt);
		gsh_free(lf);
		return NULL;
	}
}

/**
 * @brief Commit the log format parameters
 *
 * I'd prefer that Date_format and Time_format be enums but they are not.
 * They are enums except when they are not and we do hope that whatever
 * that is can be digested by printf...
 */

static int format_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	struct logfields *log = (struct logfields *)self_struct;
	struct logfields **logp = link_mem;
	struct logger_config *logger;
	int errcnt = 0;

	if (log->datefmt == TD_USER && log->user_date_fmt == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Date is \"user_set\" with empty date format.");
		err_type->validate = true;
		errcnt++;
	}
	if (log->datefmt != TD_USER && log->user_date_fmt != NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Set user date format (%s) but not \"user_set\" format",
			log->user_date_fmt);
		err_type->validate = true;
		errcnt++;
	}
	if (log->timefmt == TD_USER && log->user_time_fmt == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Time is \"user_set\" with empty time format.");
		err_type->validate = true;
		errcnt++;
	}
	if (log->timefmt != TD_USER && log->user_time_fmt != NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Set time format string (%s) but not \"user_set\" format",
			log->user_time_fmt);
		err_type->validate = true;
		errcnt++;
	}
	if (errcnt == 0) {
		logger = container_of(logp, struct logger_config, logfields);
		logger->logfields = log;
	}
	return errcnt;
}

/**
 * @brief Log component levels
 */

static struct config_item_list log_levels[] = {
	CONFIG_LIST_TOK("NULL", NIV_NULL),
	CONFIG_LIST_TOK("FATAL", NIV_FATAL),
	CONFIG_LIST_TOK("MAJ", NIV_MAJ),
	CONFIG_LIST_TOK("CRIT", NIV_CRIT),
	CONFIG_LIST_TOK("WARN", NIV_WARN),
	CONFIG_LIST_TOK("EVENT", NIV_EVENT),
	CONFIG_LIST_TOK("INFO", NIV_INFO),
	CONFIG_LIST_TOK("DEBUG", NIV_DEBUG),
	CONFIG_LIST_TOK("MID_DEBUG", NIV_MID_DEBUG),
	CONFIG_LIST_TOK("M_DBG", NIV_MID_DEBUG),
	CONFIG_LIST_TOK("FULL_DEBUG", NIV_FULL_DEBUG),
	CONFIG_LIST_TOK("F_DBG", NIV_FULL_DEBUG),
	CONFIG_LIST_EOL
};

/**
 * @brief Logging components
 */
static struct config_item component_levels[] = {
	CONF_INDEX_TOKEN("ALL", NB_LOG_LEVEL, log_levels,
			 COMPONENT_ALL, int),
	CONF_INDEX_TOKEN("LOG", NB_LOG_LEVEL, log_levels,
			 COMPONENT_LOG, int),
	CONF_INDEX_TOKEN("MEM_ALLOC", NB_LOG_LEVEL, log_levels,
			 COMPONENT_MEM_ALLOC, int),
	CONF_INDEX_TOKEN("MEMLEAKS", NB_LOG_LEVEL, log_levels,
			 COMPONENT_MEMLEAKS, int),
	CONF_INDEX_TOKEN("LEAKS", NB_LOG_LEVEL, log_levels,
			 COMPONENT_MEMLEAKS, int),
	CONF_INDEX_TOKEN("FSAL", NB_LOG_LEVEL, log_levels,
			 COMPONENT_FSAL, int),
	CONF_INDEX_TOKEN("NFSPROTO", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFSPROTO, int),
	CONF_INDEX_TOKEN("NFS3", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFSPROTO, int),
	CONF_INDEX_TOKEN("NFS_V4", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4, int),
	CONF_INDEX_TOKEN("NFS4", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4, int),
	CONF_INDEX_TOKEN("EXPORT", NB_LOG_LEVEL, log_levels,
			 COMPONENT_EXPORT, int),
	CONF_INDEX_TOKEN("FILEHANDLE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_FILEHANDLE, int),
	CONF_INDEX_TOKEN("FH", NB_LOG_LEVEL, log_levels,
			 COMPONENT_FILEHANDLE, int),
	CONF_INDEX_TOKEN("DISPATCH", NB_LOG_LEVEL, log_levels,
			 COMPONENT_DISPATCH, int),
	CONF_INDEX_TOKEN("DISP", NB_LOG_LEVEL, log_levels,
			 COMPONENT_DISPATCH, int),
	CONF_INDEX_TOKEN("CACHE_INODE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CACHE_INODE, int),
	CONF_INDEX_TOKEN("INODE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CACHE_INODE, int),
	CONF_INDEX_TOKEN("CACHE_INODE_LRU", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CACHE_INODE_LRU, int),
	CONF_INDEX_TOKEN("INODE_LRU", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CACHE_INODE_LRU, int),
	CONF_INDEX_TOKEN("HASHTABLE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_HASHTABLE, int),
	CONF_INDEX_TOKEN("HT", NB_LOG_LEVEL, log_levels,
			 COMPONENT_HASHTABLE, int),
	CONF_INDEX_TOKEN("HASHTABLE_CACHE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_HASHTABLE_CACHE, int),
	CONF_INDEX_TOKEN("HT_CACHE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_HASHTABLE_CACHE, int),
	CONF_INDEX_TOKEN("DUPREQ", NB_LOG_LEVEL, log_levels,
			 COMPONENT_DUPREQ, int),
	CONF_INDEX_TOKEN("INIT", NB_LOG_LEVEL, log_levels,
			 COMPONENT_INIT, int),
	CONF_INDEX_TOKEN("NFS_STARTUP", NB_LOG_LEVEL, log_levels,
			 COMPONENT_INIT, int),
	CONF_INDEX_TOKEN("MAIN", NB_LOG_LEVEL, log_levels,
			 COMPONENT_MAIN, int),
	CONF_INDEX_TOKEN("IDMAPPER", NB_LOG_LEVEL, log_levels,
			 COMPONENT_IDMAPPER, int),
	CONF_INDEX_TOKEN("NFS_READDIR", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_READDIR, int),
	CONF_INDEX_TOKEN("NFS_V4_LOCK", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4_LOCK, int),
	CONF_INDEX_TOKEN("NFS4_LOCK", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4_LOCK, int),
	CONF_INDEX_TOKEN("CONFIG", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CONFIG, int),
	CONF_INDEX_TOKEN("CLIENTID", NB_LOG_LEVEL, log_levels,
			 COMPONENT_CLIENTID, int),
	CONF_INDEX_TOKEN("SESSIONS", NB_LOG_LEVEL, log_levels,
			 COMPONENT_SESSIONS, int),
	CONF_INDEX_TOKEN("PNFS", NB_LOG_LEVEL, log_levels,
			 COMPONENT_PNFS, int),
	CONF_INDEX_TOKEN("RW_LOCK", NB_LOG_LEVEL, log_levels,
			 COMPONENT_RW_LOCK, int),
	CONF_INDEX_TOKEN("NLM", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NLM, int),
	CONF_INDEX_TOKEN("RPC", NB_LOG_LEVEL, log_levels,
			 COMPONENT_RPC, int),
	CONF_INDEX_TOKEN("TIRPC", NB_LOG_LEVEL, log_levels,
			 COMPONENT_TIRPC, int),
	CONF_INDEX_TOKEN("NFS_CB", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_CB, int),
	CONF_INDEX_TOKEN("THREAD", NB_LOG_LEVEL, log_levels,
			 COMPONENT_THREAD, int),
	CONF_INDEX_TOKEN("NFS_V4_ACL", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4_ACL, int),
	CONF_INDEX_TOKEN("NFS4_ACL", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_V4_ACL, int),
	CONF_INDEX_TOKEN("STATE", NB_LOG_LEVEL, log_levels,
			 COMPONENT_STATE, int),
	CONF_INDEX_TOKEN("_9P", NB_LOG_LEVEL, log_levels,
			 COMPONENT_9P, int),
	CONF_INDEX_TOKEN("_9P_DISPATCH", NB_LOG_LEVEL, log_levels,
			 COMPONENT_9P_DISPATCH, int),
	CONF_INDEX_TOKEN("_9P_DISP", NB_LOG_LEVEL, log_levels,
			 COMPONENT_9P_DISPATCH, int),
	CONF_INDEX_TOKEN("FSAL_UP", NB_LOG_LEVEL, log_levels,
			 COMPONENT_FSAL_UP, int),
	CONF_INDEX_TOKEN("DBUS", NB_LOG_LEVEL, log_levels,
			 COMPONENT_DBUS, int),
	CONF_INDEX_TOKEN("NFS_MSK", NB_LOG_LEVEL, log_levels,
			 COMPONENT_NFS_MSK, int),
	CONFIG_EOL
};

/**
 * @brief Initialize the log level array
 *
 * We allocate an array here even for the global case so as to
 * preserve something that works (default_log_levels) during config
 * processing.  If the parse errors out, we just throw it away...
 *
 */

static void *component_init(void *link_mem, void *self_struct)
{
	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL)
		return NULL;
	if (self_struct == NULL)
		return gsh_calloc(COMPONENT_COUNT, sizeof(log_levels_t));
	else {
		gsh_free(self_struct);
		return NULL;
	}
}

/**
 * @brief Commit the component levels
 *
 * COMPONENT_ALL is a magic component.  It gets statically initialized
 * to NIV_NULL (no output) but the initialize pass changes that to
 * NB_LOG_LEVEL which is +1 the last valid level. This is used to detect
 * if COMPONENT_ALL has been set.  If ALL is set, it overrides all
 * components including any that were set in the block.
 *
 * We also set the default for all components to be NB_LOG_LEVELS which
 * gets changed to the LOG { default_log_level ...} or NIV_EVENT if it
 * was not changed by the config.
 */

static int component_commit(void *node, void *link_mem, void *self_struct,
			    struct config_error_type *err_type)
{
	log_levels_t **log_lvls = link_mem;
	struct logger_config *logger;
	log_levels_t *log_level = self_struct;

	if (log_level[COMPONENT_ALL] != NB_LOG_LEVEL) {
		SetLevelDebug(log_level[COMPONENT_ALL]);
	} else {
		int comp;

		logger = container_of(log_lvls,
				      struct logger_config,
				      comp_log_level);
		if (logger->default_level == NB_LOG_LEVEL)
			logger->default_level = NIV_EVENT;
		for (comp = COMPONENT_LOG; comp < COMPONENT_COUNT; comp++)
			if (log_level[comp] == NB_LOG_LEVEL)
				log_level[comp] = logger->default_level;
		log_level[COMPONENT_ALL] = NIV_NULL;
		logger->comp_log_level = log_level;
	}
	return 0;
}

static struct config_item_list header_options[] = {
	CONFIG_LIST_TOK("none", LH_NONE),
	CONFIG_LIST_TOK("component", LH_COMPONENT),
	CONFIG_LIST_TOK("all", LH_ALL),
	CONFIG_LIST_EOL
};

static struct config_item_list enable_options[] = {
	CONFIG_LIST_TOK("idle", FAC_IDLE),
	CONFIG_LIST_TOK("active", FAC_ACTIVE),
	CONFIG_LIST_TOK("default", FAC_DEFAULT),
	CONFIG_LIST_EOL
};

static struct config_item facility_params[] = {
	CONF_ITEM_STR("name", 1, 20, NULL,
		      facility_config, facility_name),
	CONF_MAND_STR("destination", 1, MAXPATHLEN, NULL,
		      facility_config, dest),
	CONF_ITEM_TOKEN("max_level", NB_LOG_LEVEL, log_levels,
			facility_config, max_level),
	CONF_ITEM_TOKEN("headers", NB_LH_TYPES, header_options,
			facility_config, headers),
	CONF_ITEM_TOKEN("enable", FAC_IDLE, enable_options,
			facility_config, state),
	CONFIG_EOL
};

/**
 * @brief Initialize a Facility block.
 *
 * This block is allocated just to capture the fields.  It's members
 * are used to create/modify a facility at which point it gets freed.
 */

static void *facility_init(void *link_mem, void *self_struct)
{
	struct facility_config *facility;

	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL) {
		struct glist_head *facility_list;
		struct logger_config *logger;

		facility_list = self_struct;
		logger = container_of(facility_list,
				      struct logger_config,
				      facility_list);
		glist_init(&logger->facility_list);
		return self_struct;
	} else if (self_struct == NULL) {
		facility = gsh_calloc(1, sizeof(struct facility_config));
		return facility;
	} else {
		facility = self_struct;

		assert(glist_null(&facility->fac_list));

		if (facility->facility_name != NULL)
			gsh_free(facility->facility_name);
		if (facility->dest != NULL)
			gsh_free(facility->dest);
		gsh_free(self_struct);
	}
	return NULL;
}

/**
 * @brief Commit a facility block
 *
 * It can create a stream, syslog, or file facility and modify any
 * existing one.  Special loggers must be created elsewhere.
 * Note that you cannot use a log { facility {... }} to modify one
 * of these special loggers because log block parsing is done first
 * at server initialization.
 */

static int facility_commit(void *node, void *link_mem, void *self_struct,
			   struct config_error_type *err_type)
{
	struct facility_config *conf = self_struct;
	struct glist_head *fac_list;
	int errcnt = 0;

	if (conf->facility_name == NULL) {
		LogCrit(COMPONENT_LOG,
			"No facility name given");
		err_type->missing = true;
		errcnt++;
		return errcnt;
	}
	if (conf->dest != NULL) {
		if (strcasecmp(conf->dest, "stderr") == 0) {
			conf->func = log_to_stream;
			conf->lf_private = stderr;
			if (conf->headers == NB_LH_TYPES)
				conf->headers = LH_ALL;
		} else if (strcasecmp(conf->dest, "stdout") == 0) {
			conf->func = log_to_stream;
			conf->lf_private = stdout;
			if (conf->headers == NB_LH_TYPES)
				conf->headers = LH_ALL;
		} else if (strcasecmp(conf->dest, "syslog") == 0) {
			conf->func = log_to_syslog;
			if (conf->headers == NB_LH_TYPES)
				conf->headers = LH_COMPONENT;
		} else {
			conf->func = log_to_file;
			conf->lf_private = conf->dest;
			if (conf->headers == NB_LH_TYPES)
				conf->headers = LH_ALL;
		}
	} else {
		LogCrit(COMPONENT_LOG,
			"No facility destination given for (%s)",
			conf->facility_name);
		err_type->missing = true;
		errcnt++;
		return errcnt;
	}
	if (conf->func != log_to_syslog && conf->headers < LH_ALL)
		LogWarn(COMPONENT_CONFIG,
			"Headers setting for %s could drop some format fields!",
			conf->facility_name);
	if (conf->max_level == NB_LOG_LEVEL)
		conf->max_level = NIV_FULL_DEBUG;
	fac_list = link_mem;
	glist_add_tail(fac_list, &conf->fac_list);
	return 0;
}

static void *log_conf_init(void *link_mem, void *self_struct)
{
	struct logger_config *logger = self_struct;

	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL)
		return self_struct;
	else if (self_struct == NULL)
		return link_mem;
	else {
		if (logger->comp_log_level) {
			(void)component_init(&logger->comp_log_level,
					     logger->comp_log_level);
			logger->comp_log_level = NULL;
		}
		if (!glist_empty(&logger->facility_list)) {
			struct glist_head *glist, *glistn;

			glist_for_each_safe(glist, glistn,
					    &logger->facility_list) {
				struct facility_config *conf;

				conf = glist_entry(glist,
						   struct facility_config,
						   fac_list);
				glist_del(&conf->fac_list);
				(void)facility_init(&logger->facility_list,
						    conf);
			}
		}
		if (logger->logfields != NULL) {
			(void)format_init(&logger->logfields,
					  logger->logfields);
			logger->logfields = NULL;
		}
	}
	return NULL;
}

static int log_conf_commit(void *node, void *link_mem, void *self_struct,
			   struct config_error_type *err_type)
{
	struct logger_config *logger = self_struct;
	struct glist_head *glist, *glistn;
	int errcnt = 0;
	int rc;

	glist_for_each_safe(glist, glistn, &logger->facility_list) {
		struct facility_config *conf;
		bool facility_exists;

		conf = glist_entry(glist, struct facility_config, fac_list);
		glist_del(&conf->fac_list);
		if (errcnt) {
			LogEvent(COMPONENT_CONFIG,
				 "Skipping facility (%s) due to errors",
				 conf->facility_name);
			goto done;
		}
		rc = create_log_facility(conf->facility_name,
					 conf->func,
					 conf->max_level,
					 conf->headers,
					 conf->lf_private);
		if (rc != 0 && rc != -EEXIST) {
			LogCrit(COMPONENT_CONFIG,
				"Failed to create facility (%s), (%s)",
				conf->facility_name,
				strerror(-rc));
			err_type->resource = true;
			errcnt++;
			goto done;
		}
		facility_exists = (rc == -EEXIST);
		if (facility_exists && conf->dest != NULL) {
			rc = set_log_destination(conf->facility_name,
						 conf->dest);
			if (rc < 0) {
				LogCrit(COMPONENT_LOG,
					"Could not set destination for (%s) because (%s)",
					conf->facility_name,
					strerror(-rc));
				err_type->resource = true;
				errcnt++;
				goto done;
			}
		}
		if (facility_exists && conf->max_level != NB_LOG_LEVEL) {
			rc =  set_log_level(conf->facility_name,
					    conf->max_level);
			if (rc < 0)  {
				LogCrit(COMPONENT_LOG,
					"Could not set severity level for (%s) because (%s)",
					conf->facility_name,
					strerror(-rc));
				err_type->resource = true;
				errcnt++;
				goto done;
			}
		}
		if (conf->state == FAC_ACTIVE) {
			rc = enable_log_facility(conf->facility_name);
			if (rc != 0) {
				LogCrit(COMPONENT_CONFIG,
					"Could not enable (%s) because (%s)",
					conf->facility_name,
					strerror(-rc));
				err_type->resource = true;
				errcnt++;
			}
		} else if (conf->state == FAC_DEFAULT) {
			struct log_facility *old_def
				= default_facility;

			rc = set_default_log_facility(conf->facility_name);
			if (rc != 0) {
				LogCrit(COMPONENT_CONFIG,
					"Could not make (%s) the default because (%s)",
					conf->facility_name,
					strerror(-rc));
				err_type->resource = true;
				errcnt++;
			} else if (old_def != default_facility)
				LogEvent(COMPONENT_CONFIG,
					 "Switched default logger from %s to %s",
					 old_def->lf_name,
					 default_facility->lf_name);
		}
		if (errcnt > 0 && !facility_exists) {
			LogCrit(COMPONENT_CONFIG,
				"Releasing new logger (%s) because of errors",
				conf->facility_name);
			release_log_facility(conf->facility_name);
		}
 done:
		(void)facility_init(&logger->facility_list, conf);
	}
	if (errcnt == 0) {
		if (logger->logfields != NULL) {
			LogEvent(COMPONENT_CONFIG,
				 "Changing definition of log fields");
			if (logfields != &default_logfields) {
				if (logfields->user_date_fmt != NULL)
					gsh_free(logfields->user_date_fmt);
				if (logfields->user_time_fmt != NULL)
					gsh_free(logfields->user_time_fmt);
				gsh_free(logfields);
			}
			logfields = logger->logfields;

			/* rebuild const_log_str with new format params. */
			set_const_log_str();
		}
		if (logger->comp_log_level != NULL) {
			LogEvent(COMPONENT_CONFIG,
				 "Switching to new component log levels");
			if (component_log_level != default_log_levels)
				gsh_free(component_log_level);
			component_log_level = logger->comp_log_level;
		}
		ntirpc_pp.debug_flags = logger->rpc_debug_flags;
		SetNTIRPCLogLevel(component_log_level[COMPONENT_TIRPC]);
	} else {
		if (logger->logfields != NULL) {
			struct logfields *lf = logger->logfields;

			if (lf->user_date_fmt != NULL)
				gsh_free(lf->user_date_fmt);
			if (lf->user_time_fmt != NULL)
				gsh_free(lf->user_time_fmt);
			gsh_free(lf);
		}
		if (logger->comp_log_level != NULL)
			gsh_free(logger->comp_log_level);
	}
	logger->logfields = NULL;
	logger->comp_log_level = NULL;
	return errcnt;
}

static struct config_item logging_params[] = {
	CONF_ITEM_TOKEN("Default_log_level", NB_LOG_LEVEL, log_levels,
			 logger_config, default_level),
	CONF_ITEM_UI32("RPC_Debug_Flags", 0, UINT32_MAX,
		       TIRPC_DEBUG_FLAG_DEFAULT,
		       logger_config, rpc_debug_flags),
	CONF_ITEM_BLOCK("Facility", facility_params,
			facility_init, facility_commit,
			logger_config, facility_list),
	CONF_ITEM_BLOCK("Format", format_options,
			format_init, format_commit,
			logger_config, logfields),
	CONF_ITEM_BLOCK("Components", component_levels,
			component_init, component_commit,
			logger_config, comp_log_level),
	CONFIG_EOL
};

struct config_block logging_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.log",
	.blk_desc.name = "LOG",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE,  /* too risky to have more */
	.blk_desc.u.blk.init = log_conf_init,
	.blk_desc.u.blk.params = logging_params,
	.blk_desc.u.blk.commit = log_conf_commit
};

/**
 *
 * @brief Process the config parse tree for the logging component.
 *
 * Switch from the default component levels only if we found one
 * @param in_config [IN] configuration file handle
 *
 * @return 0 if ok, -1 if failed,
 *
 */
int read_log_config(config_file_t in_config,
		    struct config_error_type *err_type)
{
	struct logger_config logger;

	memset(&logger, 0, sizeof(struct logger_config));
	(void)load_config_from_parse(in_config,
				     &logging_param,
				     &logger,
				     true,
				     err_type);
	if (config_error_is_harmless(err_type))
		return 0;
	else
		return -1;
}				/* read_log_config */

void gsh_backtrace(void)
{
#define MAX_STACK_DEPTH		32	/* enough ? */
	void *buffer[MAX_STACK_DEPTH];
	struct log_facility *facility;
	struct glist_head *glist;
	int fd = -1;
	char **traces;
	int i, nlines;

	nlines = backtrace(buffer, MAX_STACK_DEPTH);

	/* Find an active log facility that is file based to
	 * log the backtrace symbols.
	 */
	PTHREAD_RWLOCK_rdlock(&log_rwlock);
	glist_for_each(glist, &active_facility_list) {
		facility = glist_entry(glist, struct log_facility, lf_active);
		if (facility->lf_func == log_to_file) {
			fd = open((char *)facility->lf_private,
				  O_WRONLY | O_APPEND | O_CREAT, log_mask);
			break;
		}
	}

	if (fd != -1) {
		LogMajor(COMPONENT_INIT, "stack backtrace follows:");
		backtrace_symbols_fd(buffer, nlines, fd);
		close(fd);
	} else {
		/* No file based logging, hope malloc call inside
		 * backtrace_symbols() doesn't hang!
		 */
		traces = backtrace_symbols(buffer, nlines);
		if (traces) {
			for (i = 0; i < nlines; i++) {
				LogMajor(COMPONENT_INIT, "%s", traces[i]);
			}
			free(traces);
		}
	}
	PTHREAD_RWLOCK_unlock(&log_rwlock);
}
