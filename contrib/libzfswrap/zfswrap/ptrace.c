
/*-------------------------------------------------------------------------*/
/*
  @file		ptrace.c
  @author	N. Devillard, V. Chudnovsky
  @date		March 2004
  @version	$Revision: 1.1.1.1 $
  @brief	Add tracing capability to any program compiled with gcc.

  This module is only compiled when using gcc and tracing has been
  activated. It allows the compiled program to output messages whenever
  a function is entered or exited.

  To activate this feature, your version of gcc must support
  the -finstrument-functions flag.

  When using ptrace on a dynamic library, you must set the
  PTRACE_REFERENCE_FUNCTION macro to be the name of a function in the
  library. The address of this function when loaded will be the first
  line output to the trace file and will permit the translation of the
  other entry and exit pointers to their symbolic names. You may set
  the macro PTRACE_INCLUDE with any #include directives needed for
  that function to be accesible to this source file.

  The printed messages yield function addresses, not human-readable
  names. To link both, you need to get a list of symbols from the
  program. There are many (unportable) ways of doing that, see the
  'etrace' project on freshmeat for more information about how to dig
  the information.
*/
/*--------------------------------------------------------------------------*/

/*
	$Id: ptrace.c,v 1.1.1.1 2004/03/16 20:00:07 ndevilla Exp $
	$Author: ndevilla $
	$Date: 2004/03/16 20:00:07 $
	$Revision: 1.1.1.1 $
*/

#if (__GNUC__>2) || ((__GNUC__ == 2) && (__GNUC_MINOR__ > 95))

/*---------------------------------------------------------------------------
                                 Includes
 ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/debug.h>

#ifdef _KERNEL
#include <sys/atomic.h>
#else
#include <atomic.h>
#endif

#define PTRACE_FLAG_FILENAME "TRACE"
#define PTRACE_OUTPUT "trace/%u-TRACE.%u.unparsed"

#define START_TRACE        "START"
#define FUNCTION_ENTRY_STR "enter"
#define FUNCTION_EXIT_STR  "exit"
#define END_TRACE          "EXIT"
#define __NON_INSTRUMENT_FUNCTION__ __attribute__((__no_instrument_function__))

typedef enum { FUNCTION_ENTRY, FUNCTION_EXIT } entry_t;
typedef enum { A_UNINITIALIZED, A_DISABLED, A_ACTIVE, A_INACTIVE } active_t;

/* Final trace close */
static void
__NON_INSTRUMENT_FUNCTION__
gnu_ptrace_close(void *arg)
{
	fprintf((FILE *) arg, END_TRACE "\n");
	fclose((FILE *) arg);

	return;
}

static pthread_key_t key;

static void
__NON_INSTRUMENT_FUNCTION__
gnu_ptrace_process_init()
{
	/* Register thread destructor */
	pthread_key_create(&key, gnu_ptrace_close);
}

/* Trace initialization */
FILE *
__NON_INSTRUMENT_FUNCTION__
gnu_ptrace_thread_init()
{
	static pthread_once_t key_once = PTHREAD_ONCE_INIT;
	static volatile uint32_t thread_n = 0;

	struct stat sta;

	/* See if a trace file exists */
	if(stat(PTRACE_FLAG_FILENAME, &sta) != 0) {
		/* No trace file: do not trace at all */
		return NULL;
	}

	char fname[100];
	sprintf(fname, PTRACE_OUTPUT, getpid(), atomic_inc_32_nv(&thread_n));

	unlink(fname);

	FILE *ret = fopen(fname, "a");
	if(ret == NULL)
		return NULL;

	/* Call initialization function, if not called before */
	pthread_once(&key_once, gnu_ptrace_process_init);

	if(pthread_getspecific(key) == NULL)
		pthread_setspecific(key, ret);

	fprintf(ret, START_TRACE "\n");
	fflush(ret);

	return ret;
}

/* Function called by every function event */
void
__NON_INSTRUMENT_FUNCTION__
gnu_ptrace(entry_t e, void *p)
{
	static __thread active_t active = A_UNINITIALIZED;
	static __thread FILE *f = NULL;
	static __thread int indent_level = 0;

	switch(active) {
		case A_INACTIVE:
		case A_DISABLED:
			return;
		case A_ACTIVE:
			active = A_DISABLED;
			break;
		case A_UNINITIALIZED:
			active = A_DISABLED;

			if((f = gnu_ptrace_thread_init()) == NULL)
				return;

			break;
	}

	switch(e) {
		case FUNCTION_ENTRY:
			for(int i = 0; i < indent_level; i++)
				fprintf(f, "    %c  ", i == (indent_level - 1) ? '\\' : '|');

			fprintf(f, "%p\n", p);

			indent_level++;
			break;

		case FUNCTION_EXIT:
			indent_level--;
	}

	fflush(f);

	active = A_ACTIVE;

	return;
}

/* According to gcc documentation: called upon function entry */
void
__NON_INSTRUMENT_FUNCTION__
__cyg_profile_func_enter(void *this_fn, void *call_site)
{
	gnu_ptrace(FUNCTION_ENTRY, this_fn);
	(void) call_site;
}

/* According to gcc documentation: called upon function exit */
void
__NON_INSTRUMENT_FUNCTION__
__cyg_profile_func_exit(void *this_fn, void *call_site)
{
	gnu_ptrace(FUNCTION_EXIT, this_fn);
	(void) call_site;
}

#endif
