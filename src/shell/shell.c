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
 */

/**
 * \file    shell.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/24 08:33:44 $
 * \version $Revision: 1.20 $
 * \brief   Internal routines for the shell.
 *
 *
 * $Log: shell.c,v $
 * Revision 1.20  2006/02/24 08:33:44  leibovic
 * shellid is read only.
 *
 * Revision 1.19  2006/02/23 07:42:53  leibovic
 * Adding -n option to shell.
 *
 * Revision 1.18  2006/02/08 12:50:00  leibovic
 * changing NIV_EVNMT to NIV_EVENT.
 *
 * Revision 1.17  2006/01/17 14:56:22  leibovic
 * Adaptation de HPSS 6.2.
 *
 * Revision 1.15  2005/09/27 09:30:16  leibovic
 * Removing non-thread safe trace buffer.
 *
 * Revision 1.14  2005/09/27 08:15:13  leibovic
 * Adding traces and changhing readexport prototype.
 *
 * Revision 1.13  2005/08/12 12:15:33  leibovic
 * Erreur d'init.
 *
 * Revision 1.12  2005/08/12 11:21:27  leibovic
 * Now, set cat concatenate strings.
 *
 * Revision 1.11  2005/08/05 07:59:21  leibovic
 * Better help printing.
 *
 * Revision 1.10  2005/07/29 13:34:28  leibovic
 * Changing _FULL_DEBUG to _DEBUG_SHELL
 *
 * Revision 1.9  2005/07/26 12:54:47  leibovic
 * Multi-thread shell with synchronisation routines.
 *
 * Revision 1.8  2005/07/25 12:50:45  leibovic
 * Adding thr_create and thr_join commands.
 *
 * Revision 1.7  2005/05/11 15:53:37  leibovic
 * Adding time function.
 *
 * Revision 1.6  2005/05/11 07:25:58  leibovic
 * Escaped char support.
 *
 * Revision 1.5  2005/05/10 14:02:27  leibovic
 * Fixed bug in log management.
 *
 * Revision 1.4  2005/05/10 11:38:07  leibovic
 * Adding log initialization.
 *
 * Revision 1.3  2005/05/10 11:07:21  leibovic
 * Adapting to ganeshell v2.
 *
 * Revision 1.2  2005/05/09 14:54:59  leibovic
 * Adding if.
 *
 * Revision 1.1  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "shell.h"
#include "shell_utils.h"
#include "shell_vars.h"
#include "log_functions.h"
#include "commands.h"
#include "cmd_tools.h"

#ifndef _NO_BUDDY_SYSTEM
#include "BuddyMalloc.h"
#endif

#include "stuff_alloc.h"
#include <unistd.h>
#include <string.h>

#include <stdio.h>
#include <pthread.h>

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <sys/time.h>

#define MAX_OUTPUT_LEN  (1024*1024)     /* 1MB */

#define TRACEBUFFSIZE 1024

#define PROMPTSIZE 64

layer_def_t layer_list[] = {
  {"FSAL", commands_FSAL, "File system abstraction layer", fsal_layer_SetLogLevel}
  ,
  {"Cache_inode", commands_Cache_inode, "Cache inode layer", Cache_inode_layer_SetLogLevel}
  ,
  {"NFS", commands_NFS,
   "NFSv2, NFSv3, MNTv1, MNTv3 protocols (direct calls, not through RPCs)",
   nfs_layer_SetLogLevel}
  ,
  {"NFS_remote", commands_NFS_remote,
   "NFSv2, NFSv3, MNTv1, MNTv3 protocols (calls through RPCs)",
   nfs_remote_layer_SetLogLevel}
  ,
#ifdef _USE_MFSL
  {"MFSL", commands_MFSL, "MFSL intermediate layer", nfs_remote_layer_SetLogLevel}
  ,
#endif
  {NULL, NULL, NULL, NULL}      /* End of layer list */
};

char *shell_special_vars[] = {
  "INPUT",                      /* a filename or <stdin> */
  "INTERACTIVE",                /* Indicates if we are in interactive mode */
  "LAYER",                      /* The current layer */
  "STATUS",                     /* Last command status */
  "?",                          /* idem */
  "VERBOSE",                    /* shel verbose mode */
  "DEBUG_LEVEL",                /* layer debug level */
  "DBG_LVL",                    /* idem */
  "PROMPT",                     /* shell prompt string */
  "LINE",                       /* line number */

  /* end of special vars list */
  NULL
};

command_def_t shell_utils[] = {
  {"chomp", util_chomp, "removes final newline character"},
  {"cmp", util_cmp, "compares two expressions"},
  {"diff", util_diff, "lists differences between two expressions"},
  {"eq", util_cmp, "test if two expressions are equal"},
  {"meminfo", util_meminfo, "prints information about memory use"},
  {"ne", util_cmp, "test if two expressions are different"},
  {"shell", util_shell, "executes a real shell command"},
  {"sleep", util_sleep, "suspends script execution for some time"},
  {"timer", util_timer, "timer management command"},
  {"wc", util_wc, "counts the number of char/words/lines in a string"},

  {NULL, NULL, NULL}            /* End of command list */
};

/* ------------------------------------------*
 *        Barrier management.
 * ------------------------------------------*/

#define P_shell( _mutex_ ) pthread_mutex_lock( &_mutex_ )
#define V_shell( _mutex_ ) pthread_mutex_unlock( &_mutex_ )

/* variables for managing barriers */

/* barrier protection */
static pthread_mutex_t barrier_mutex = PTHREAD_MUTEX_INITIALIZER;

/* condition for crossing barrier */
static pthread_cond_t barrier_cond = PTHREAD_COND_INITIALIZER;

/* total number of threads to wait for */
static int total_nb_threads = -1;       /* -1 = not initialized */

/* number of threads that reached the barrier */
static int nb_waiting_threads = 0;

/**
 *  Initialize the barrier for shell synchronization routines.
 *  The number of threads to wait for is given as parameter.
 */
int shell_BarrierInit(int nb_threads)
{

  P_shell(barrier_mutex);

  if(total_nb_threads == -1)
    {
      total_nb_threads = nb_threads;
      V_shell(barrier_mutex);
      return SHELL_SUCCESS;
    }
  else
    {
      V_shell(barrier_mutex);
      printf("ganeshell: Error: Barrier already initialized\n");
      return SHELL_ERROR;
    }
}

static int shell_BarrierWait()
{

  P_shell(barrier_mutex);

  /* not used in a single thread environment */

  if(total_nb_threads == -1)
    {
      V_shell(barrier_mutex);
      return SHELL_ERROR;
    }

  /* increase number of waiting threads */

  nb_waiting_threads++;

  /* test for condition */

  if(nb_waiting_threads == total_nb_threads)
    {
      /* reset the number of waiting threads */
      nb_waiting_threads = 0;

      /* wake up all threads */
      pthread_cond_broadcast(&barrier_cond);

    }
  else
    pthread_cond_wait(&barrier_cond, &barrier_mutex);

  /* leaves the critical section */

  V_shell(barrier_mutex);

  return SHELL_SUCCESS;

}

/* ------------------------------------------*
 *        Thread safety management.
 * ------------------------------------------*/

/* threads keys */
static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init pthtread_key for current thread */

static void init_keys(void)
{
  if(pthread_key_create(&thread_key, NULL) == -1)
    printf("Error %d creating pthread key for thread %p : %s\n",
           errno, (caddr_t) pthread_self(), strerror(errno));

  return;
}                               /* init_keys */

/**
 * GetShellContext :
 * manages pthread_keys.
 */
static shell_state_t *GetShellContext()
{

  shell_state_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      printf("Error %d calling pthread_once for thread %p : %s\n",
             errno, (caddr_t) pthread_self(), strerror(errno));
      return NULL;
    }

  p_current_thread_vars = (shell_state_t *) pthread_getspecific(thread_key);

  /* we allocate the thread context if this is the first time */
  if(p_current_thread_vars == NULL)
    {

      /* allocates thread structure */
      p_current_thread_vars = (shell_state_t *) Mem_Alloc(sizeof(shell_state_t));

      /* panic !!! */
      if(p_current_thread_vars == NULL)
        {
          printf("%p:ganeshell: Not enough memory\n", (caddr_t) pthread_self());
          return NULL;
        }

      /* Clean thread context */

      memset(p_current_thread_vars, 0, sizeof(shell_state_t));

      /* setting default values */

      p_current_thread_vars->input_stream = stdin;
      p_current_thread_vars->interactive = TRUE;
      p_current_thread_vars->layer = NULL;
      p_current_thread_vars->status = 0;
      p_current_thread_vars->verbose = 0;
      p_current_thread_vars->debug_level = NIV_EVENT;
      p_current_thread_vars->line = 0;

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);

    }

  return p_current_thread_vars;

}                               /* GetShellContext */

/*------------------------------------------------------------------
 *                    Main shell routines.
 *-----------------------------------------------------------------*/

/**
 *  Initialize the shell.
 *  The command line for the shell is given as parameter.
 *  \param input_file the file to read from (NULL if stdin).
 */
int shell_Init(int verbose, char *input_file, char *prompt, int shell_index)
{

  int rc;
  char localmachine[256];
  shell_state_t *context;

  /* First init Buddy Malloc System */

#ifndef _NO_BUDDY_SYSTEM

  /* Init Buddy allocator */

  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* can't use shell tracing functions there */
      fprintf(stderr, "Error %d initializing Buddy allocator.\n", rc);
      return rc;
    }
#endif

  /* Init logging */

  SetNamePgm("ganeshell");
  /*if (verbose) */
  SetDefaultLogging("STDERR");
  /*else
     SetDefaultLogging( "/dev/null" ) ; */

  SetNameFunction("shell");

  /* getting the hostname */
  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      fprintf(stderr, "Error %d calling gethostname.\n", errno);
      return errno;
    }
  else
    SetNameHost(localmachine);

  InitLogging();

  /* retrieve/initialize shell context */

  context = GetShellContext();

  /* Initializes verbose mode. */

  if((rc = shell_SetVerbose(context, (verbose ? "1" : "0"))))
    return rc;

  if((rc = shell_SetDbgLvl(context, "NIV_EVENT")))
    return rc;

  /* Then, initializes input file. */

  if((rc = shell_SetInput(context, input_file)))
    return rc;

  /* Initialize prompt */

  if((rc = shell_SetPrompt(context, prompt)))
    return rc;

  /* Initialize Shell id */

  if((rc = shell_SetShellId(context, shell_index)))
    return rc;

  return SHELL_SUCCESS;

}

/* reads a line from input, and prints a prompt in interactive mode. */


#ifdef HAVE_LIBREADLINE
/* the same as previous, except it doesnt not trunc line at # sign */

static char *skipblanks2(char *str)
{

  char *curr = str;

  while(1)
    {

      switch (*curr)
        {
          /* end of lines */
        case '\0':
          return NULL;

        case ' ':
        case '\t':
        case '\r':
        case '\n':
          curr++;
          break;

        default:
          return curr;

        }                       /* switch */

    }                           /* while */

}                               /* skipblanks2 */


#endif

static char *shell_readline(shell_state_t * context, char *s, int n, FILE * stream,
                            int interactive)
{

  char *retval = shell_GetPrompt(context);

#ifdef HAVE_LIBREADLINE

  char *l;

  if(interactive)
    {
      /* use readline */
      l = readline((retval ? retval : ""));
      if(l)
        {
          strncpy(s, l, n);

          /* add line to history, if it is not empty */
          l = skipblanks2(l);

          if(l != NULL)
            add_history(l);

          return s;
        }
      else
        return NULL;
    }
  else
    return fgets(s, n, stream);

#else
  if(interactive)
    printf("%s", (retval ? retval : ""));

  return fgets(s, n, stream);
#endif

}

/**
 *  Run the interpreter.
 */
int shell_Launch()
{

  char cmdline[MAX_LINE_LEN + 1];
  char *arglist[MAX_ARGS];
  int alloctab[MAX_ARGS];
  int argcount;
  int rc = 0;

  shell_state_t *context = GetShellContext();

  while(shell_readline(context, cmdline, MAX_LINE_LEN,
                       context->input_stream, context->interactive) != NULL)
    {

      /* Increments line number */

      shell_SetLine(context, shell_GetLine(context) + 1);

      /* Parse command line */

      if(shell_ParseLine(cmdline, arglist, &argcount))
        continue;

      /* nothing to do if the line is empty. */
      if(argcount == 0)
        continue;

      /* Evaluates arguments */

      if(shell_SolveArgs(argcount, arglist, alloctab))
        continue;

      /* Execute command */
      rc = shell_Execute(argcount, arglist, stdout);

      /* clean allocated strings */
      shell_CleanArgs(argcount, arglist, alloctab);

      /* set command status */
      shell_SetStatus(context, rc);

    }
  return rc;
}

/*------------------------------------------------------------------
 *                Parsing and execution routines.
 *-----------------------------------------------------------------*/

/* address of the first non blank char if any, null else.*/

static char *skipblanks(char *str)
{

  char *curr = str;

  while(1)
    {

      switch (*curr)
        {
          /* end of lines */
        case '\0':
        case '#':
          return NULL;

        case ' ':
        case '\t':
        case '\r':
        case '\n':
          curr++;
          break;

        default:
          return curr;

        }                       /* switch */

    }                           /* while */

}                               /* skipblanks */

/* adress of the first blank char
 * outside a string.
 */
static char *nextblank(char *str)
{

  int dquote_string = 0;
  int squote_string = 0;
  int bquote_string = 0;

  int escaped = 0;

  char *curr = str;

  while(1)
    {

      switch (*curr)
        {

          /* end of lines */
        case ' ':
        case '\t':
          if(!dquote_string && !squote_string && !bquote_string)
            return curr;
          else
            curr++;
          break;

        case '\0':
        case '\n':
          return curr;
          break;

        case '\\':
          /* escape sequence */
          escaped = 1;
          curr++;
          break;

        case '"':
          /* start or end of double quoted string */
          if(dquote_string)
            dquote_string = 0;
          else
            (dquote_string) = 1;
          curr++;
          break;

        case '\'':
          /* start or end of single quoted string */
          if(squote_string)
            squote_string = 0;
          else
            (squote_string) = 1;
          curr++;
          break;

        case '`':
          /* start or end of back-quoted string */
          if(bquote_string)
            bquote_string = 0;
          else
            (bquote_string) = 1;
          curr++;
          break;

        default:
          curr++;

        }                       /* switch */

      /* escape ? */

      if(escaped && (*curr != '\0'))
        {
          escaped = 0;
          curr++;
        }

    }                           /* while */

}                               /* nextblank */

/**
 *  shell_ParseLine:
 *  Extract an arglist from a command line.
 *
 *  \param  in_out_line The command line (modified).
 *  \param  out_arglist The list of command line tokens.
 *  \param  p_argcount  The number of command line tokens.
 *
 *  \return 0 if no errors.
 */

int shell_ParseLine(char *in_out_line, char **out_arglist, int *p_argcount)
{

  char *curr_pos = in_out_line;
  (*p_argcount) = 0;

  /* While there is something after the Oblivion... */

  while((curr_pos = skipblanks(curr_pos)))
    {
      out_arglist[(*p_argcount)] = curr_pos;
      (*p_argcount)++;
      curr_pos = nextblank(curr_pos);

      if(*curr_pos == '\0')
        break;
      else
        *curr_pos = '\0';

      curr_pos++;
    }

  return SHELL_SUCCESS;

}                               /* shell_ParseLine */

/**
 *  remove escape sequence
 *  \return 0 if OK,
 *          Error code else (missing closing quote...)
 */
static int unescape(char *str)
{

  char *src = str;
  char *tgt = str;

  while(*src != '\0')
    {
      if(*src == '\\')
        {
          src++;

          /* escaped null char */
          if(*src == '\0')
            {
#ifdef _DEBUG_SHELL
              printf("UNESCAPE ERROR >>>>>>>>>> [%s][%c][%c]\n", str, *src, *tgt);
#endif
              return SHELL_ERROR;
            }

        }

      if(tgt != src)
        *tgt = *src;

      src++;
      tgt++;
    }

  /* final zero */
  if(tgt != src)
    *tgt = *src;

  return SHELL_SUCCESS;

}

/**
 *  remove string quotes
 *  \return 0 if OK,
 *          Error code else (missing closing quote...)
 */
static int remove_quotes(char quote, char **pstr)
{

  size_t len = strlen(*pstr);

  if(len <= 1)
    return SHELL_ERROR;
  if((*pstr)[len - 1] != quote)
    return SHELL_ERROR;

  (*pstr)[len - 1] = '\0';
  (*pstr)[0] = '\0';

  (*pstr)++;

  return SHELL_SUCCESS;

}

/**
 *  shell_SolveArgs:
 *  Interprets arguments if they are vars or commands.
 *
 *  \param  argc          The number of command line tokens.
 *  \param  in_out_argv   The list of command line tokens (modified).
 *  \param  out_allocated   Indicates which tokens must be freed.
 *
 *  \return 0 if no errors.
 */

int shell_SolveArgs(int argc, char **in_out_argv, int *out_allocated)
{

  int i;
  int error = 0;
  char tracebuff[TRACEBUFFSIZE];

  shell_state_t *context = GetShellContext();

#ifdef _DEBUG_SHELL
  printf("SOLVE:");
  for(i = 0; i < argc; i++)
    printf("[%s]", in_out_argv[i]);
  printf("\n");
#endif

  for(i = 0; i < argc; i++)
    {

      out_allocated[i] = FALSE;

      /* double quotes */

      if(in_out_argv[i][0] == '"')
        {

          if(remove_quotes('"', &(in_out_argv[i])))
            {
              shell_PrintError(context, "Syntax error: Missing closing quotes");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

          if(unescape(in_out_argv[i]))
            {
              shell_PrintError(context, "Syntax error: Invalid escape sequence");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

        }

      /* single quotes */

      else if(in_out_argv[i][0] == '\'')
        {

          if(remove_quotes('\'', &(in_out_argv[i])))
            {
              shell_PrintError(context, "Syntax error: Missing closing quote");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

          if(unescape(in_out_argv[i]))
            {
              shell_PrintError(context, "Syntax error: Invalid escape sequence");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

        }

      /* var name */

      else if(in_out_argv[i][0] == '$')
        {

          char *value = get_var_value(&(in_out_argv[i][1]));

          if(value)
            in_out_argv[i] = value;
          else
            {
              snprintf(tracebuff, TRACEBUFFSIZE,
                       "Undefined variable \"%s\"", &(in_out_argv[i][1]));
              shell_PrintError(context, tracebuff);
              error = SHELL_NOT_FOUND;
              break;
            }

        }

      /* command */

      else if(in_out_argv[i][0] == '`')
        {

          char *arglist[MAX_ARGS];
          int argcount;
          int rc, status;

          /* remove quotes */

          if(remove_quotes('`', &(in_out_argv[i])))
            {
              shell_PrintError(context, "Syntax error: Missing closing backquote");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

          if(unescape(in_out_argv[i]))
            {
              shell_PrintError(context, "Syntax error: Invalid escape sequence");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

          /* Parse command line */

          if(shell_ParseLine(in_out_argv[i], arglist, &argcount))
            {
              error = SHELL_SYNTAX_ERROR;
              break;
            }

          /* nothing to do if the command is empty. */

          if(argcount == 0)
            {

              /* empty output */
              in_out_argv[i][0] = '\0';

              /* command status */
              shell_SetStatus(context, 0);

            }
          else
            {

              int fd[2];
              FILE *output_stream;
              int alloctab[MAX_ARGS];

              char output_string[MAX_OUTPUT_LEN];

              /* Evaluates arguments */

              if(shell_SolveArgs(argcount, arglist, alloctab))
                {
                  error = SHELL_SYNTAX_ERROR;
                  break;
                }

              /* create pipe for command output */

              if(pipe(fd))
                {

                  snprintf(tracebuff, TRACEBUFFSIZE, "Can't create pipe: %s (%d)",
                           strerror(errno), errno);
                  shell_PrintError(context, tracebuff);

                  /* clean allocated strings */
                  shell_CleanArgs(argcount, arglist, alloctab);

                  error = errno;
                  break;

                }

              /* opening output stream */

              output_stream = fdopen(fd[1], "a");

              if(output_stream == NULL)
                {

                  snprintf(tracebuff, TRACEBUFFSIZE, "Can't open pipe stream: %s (%d)",
                           strerror(errno), errno);
                  shell_PrintError(context, tracebuff);

                  /* clean allocated strings */
                  shell_CleanArgs(argcount, arglist, alloctab);

                  /* close pipe */
                  close(fd[1]);
                  close(fd[0]);

                  error = errno;
                  break;

                }

              /* @todo : thread for pipe reading */

              /* Execute command */

              status = shell_Execute(argcount, arglist, output_stream);

              /* closing ouput stream. */

              fclose(output_stream);
              close(fd[1]);

              /* clean allocated strings */
              shell_CleanArgs(argcount, arglist, alloctab);

              /* read the output from pipe */

              rc = read(fd[0], output_string, MAX_OUTPUT_LEN);

              /* close pipe */
              close(fd[0]);

              if(rc == -1)
                {
                  snprintf(tracebuff, TRACEBUFFSIZE, "Cannot read from pipe: %s (%d)",
                           strerror(errno), errno);
                  shell_PrintError(context, tracebuff);

                  error = errno;
                  break;
                }

              /* allocate and fill output buffer */

              in_out_argv[i] = Mem_Alloc(rc + 1);

              if(in_out_argv[i] == NULL)
                {
                  shell_PrintError(context, "Malloc error");
                  error = -1;
                  break;
                }

              memcpy(in_out_argv[i], output_string, rc);

              out_allocated[i] = TRUE;

              in_out_argv[i][rc] = '\0';

              /* set command status */

              shell_SetStatus(context, status);

            }

        }
      /*  normal arg  */
      else
        {
          if(unescape(in_out_argv[i]))
            {
              shell_PrintError(context, "Syntax error: Invalid escape sequence");
              error = SHELL_SYNTAX_ERROR;
              break;
            }

        }                       /* in_out_argv[i][0] */

    }                           /* for */

  /* the case when we exited the for because of an error. */

  if(error)
    {
      /* free allocated strings */
      shell_CleanArgs(i + 1, in_out_argv, out_allocated);
      return error;
    }

  return SHELL_SUCCESS;

}                               /* shell_SolveArgs */

/**
 *  shell_CleanArgs:
 *  Free allocated arguments.
 *
 *  \param  argc           The number of command line tokens.
 *  \param  in_out_argv    The list of command line tokens (modified).
 *  \param  in_allocated   Indicates which tokens must be freed.
 *
 */

void shell_CleanArgs(int argc, char **in_out_argv, int *in_allocated)
{

  int i;

  for(i = 0; i < argc; i++)
    {

      if(in_allocated[i])
        {
          Mem_Free(in_out_argv[i]);
          in_out_argv[i] = NULL;
          in_allocated[i] = FALSE;
        }

    }

  return;

}

/**
 *  shell_Execute:
 *  Commands dispatcher.
 *
 *  \param  argc The number of arguments of this command.
 *  \param  argv The arguments for this command.
 *  \param  output The output stream of this command.
 *
 *  \return The returned status of this command.
 */
int shell_Execute(int argc, char **argv, FILE * output)
{

  /* pointer to the command to be launched */
  int (*command_func) (int, char **, FILE *) = NULL;

  int i;
  int rc;
  char tracebuff[TRACEBUFFSIZE];

  shell_state_t *context = GetShellContext();

  /* First, look at shell internal commands */

  for(i = 0; shell_commands[i].command_name; i++)
    {
      if(!strcmp(argv[0], shell_commands[i].command_name))
        {
          command_func = shell_commands[i].command_func;
          break;
        }
    }

  /* If not found, look at shell utils commands */

  if(!command_func)
    {

      for(i = 0; shell_utils[i].command_name; i++)
        {
          if(!strcmp(argv[0], shell_utils[i].command_name))
            {
              command_func = shell_utils[i].command_func;
              break;
            }
        }

    }

  /* If not found, look at layer commands */

  if(!command_func)
    {
      layer_def_t *current_layer = shell_GetLayer(context);

      if(current_layer)
        {

          for(i = 0; current_layer->command_list[i].command_name; i++)
            {
              if(!strcmp(argv[0], current_layer->command_list[i].command_name))
                {
                  command_func = current_layer->command_list[i].command_func;

                  /* set layer's debug level */
                  current_layer->setlog_func(shell_GetDbgLvl(context));

                  break;
                }
            }                   /* for */

        }
      /* if current_layer */
    }

  /* if command_func */
  /* command not found */
  if(!command_func)
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "%s: command not found", argv[0]);
      shell_PrintError(context, tracebuff);
      return SHELL_NOT_FOUND;
    }

  /* verbose trace */

  if(shell_GetVerbose(context))
    {
      tracebuff[0] = '\0';
      for(i = 0; i < argc; i++)
        {
          /* + 1 = size of the additional char ( + or space) */
          size_t len1 = strlen(tracebuff) + 1;
          size_t len2 = strlen(argv[i]);

          if(len1 > TRACEBUFFSIZE - 1)
            break;

          if(i != 0)
            strcat(tracebuff, " ");
          else
            strcat(tracebuff, "+");

          if(len1 + len2 > TRACEBUFFSIZE - 1)
            {
              if(TRACEBUFFSIZE - 6 - len1 > 0)
                strncat(tracebuff, argv[i], TRACEBUFFSIZE - 6 - len1);

              strcat(tracebuff, "[...]");
              break;
            }
          else
            {
              strcat(tracebuff, argv[i]);
            }

        }
    }
  shell_PrintTrace(context, tracebuff);

  /* execute the command */

  rc = command_func(argc, argv, output);

  /* verbose trace */

  snprintf(tracebuff, TRACEBUFFSIZE, "%s returned %d", argv[0], rc);
  shell_PrintTrace(context, tracebuff);

  return rc;

}                               /* shell_Execute */

/*------------------------------------------------------------------
 *                 Shell ouput routines.
 *-----------------------------------------------------------------*/

/**
 *  shell_PrintError:
 *  Prints an error.
 */
void shell_PrintError(shell_state_t * context, char *error_msg)
{

  char *input_name = get_var_value("INPUT");

  fprintf(stderr, "******* ERROR in %s line %d: %s\n",
          (input_name ? input_name : "?"), shell_GetLine(context), error_msg);

}

/**
 *  shell_PrintTrace:
 *  Prints a verbose trace.
 */
void shell_PrintTrace(shell_state_t * context, char *msg)
{

  char *input_name;

  if(shell_GetVerbose(context))
    {
      input_name = get_var_value("INPUT");

      fprintf(stderr, "%s l.%d: %s\n",
              (input_name ? input_name : "?"), shell_GetLine(context), msg);
    }

}

/*------------------------------------------------------------------
 *                 Shell state management routines.
 *-----------------------------------------------------------------*/

/**
 * shell_SetLayer:
 * Set the current active layer.
 * \return 0 if OK.
 *         else, an error code.
 */
int shell_SetLayer(shell_state_t * context, char *layer_name)
{

  layer_def_t *layer = NULL;
  int i, rc;
  char tracebuff[TRACEBUFFSIZE];

  /* search for layer */

  for(i = 0; layer_list[i].layer_name; i++)
    {
      if(!strcasecmp(layer_name, layer_list[i].layer_name))
        {
          layer = &layer_list[i];
          break;
        }
    }

  /* saves current layer */

  if(layer)
    {
      /* saves layer pointer */

      context->layer = layer;

      /* stores layer name into vars  */

      rc = set_var_value("LAYER", layer->layer_name);

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting LAYER value to %s", rc, layer->layer_name);
          shell_PrintError(context, tracebuff);
        }

      snprintf(tracebuff, TRACEBUFFSIZE, "Current layer is now %s", layer->layer_name);
      shell_PrintTrace(context, tracebuff);

      return SHELL_SUCCESS;

    }
  else
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "Layer not found: %s", layer_name);
      shell_PrintError(context, tracebuff);
      return SHELL_NOT_FOUND;
    }

}                               /* shell_SetLayer */

/**
 * shell_GetLayer:
 * Retrieves the current active layer (internal use).
 */
layer_def_t *shell_GetLayer(shell_state_t * context)
{

  return context->layer;

}

/**
 * shell_SetStatus
 * Set the special variables $? and $STATUS.
 */
int shell_SetStatus(shell_state_t * context, int returned_status)
{

  int rc;
  char str_int[64];
  char tracebuff[TRACEBUFFSIZE];

  context->status = returned_status;

  snprintf(str_int, 64, "%d", returned_status);

  rc = set_var_value("STATUS", str_int);

  if(rc != 0)
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "Error %d setting STATUS value to %s", rc, str_int);
      shell_PrintError(context, tracebuff);
    }

  rc = set_var_value("?", str_int);

  if(rc != 0)
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "Error %d setting ? value to %s", rc, str_int);
      shell_PrintError(context, tracebuff);
    }

  return SHELL_SUCCESS;

}

/**
 * shell_GetStatus
 * Get the special variables $? or $STATUS (internal use).
 */
int shell_GetStatus(shell_state_t * context)
{
  return context->status;
}

/**
 * shell_SetVerbose
 * Set the special variable $VERBOSE.
 */
int shell_SetVerbose(shell_state_t * context, char *str_verbose)
{

  int rc;
  char tracebuff[TRACEBUFFSIZE];

  if(!strcasecmp(str_verbose, "ON") ||
     !strcasecmp(str_verbose, "TRUE") ||
     !strcasecmp(str_verbose, "YES") || !strcmp(str_verbose, "1"))
    {
      context->verbose = TRUE;

      rc = set_var_value("VERBOSE", "1");

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting VERBOSE value to %s", rc, "1");
          shell_PrintError(context, tracebuff);
        }

      return SHELL_SUCCESS;

    }
  else if(!strcasecmp(str_verbose, "OFF") ||
          !strcasecmp(str_verbose, "FALSE") ||
          !strcasecmp(str_verbose, "NO") || !strcmp(str_verbose, "0"))
    {
      context->verbose = FALSE;

      rc = set_var_value("VERBOSE", "0");

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting VERBOSE value to %s", rc, "0");
          shell_PrintError(context, tracebuff);
        }

      return SHELL_SUCCESS;

    }
  else
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "Unexpected value for VERBOSE: %s", str_verbose);
      shell_PrintError(context, tracebuff);

      return SHELL_SYNTAX_ERROR;
    }

}

/**
 * shell_GetVerbose
 * Get the special variable $VERBOSE (internal use).
 */
int shell_GetVerbose(shell_state_t * context)
{
  return context->verbose;
}

/**
 * shell_SetDbgLvl
 * Set the special variables $DEBUG_LEVEL and $DBG_LVL
 */
int shell_SetDbgLvl(shell_state_t * context, char *str_debug_level)
{
  int level_debug;
  int rc;
  char tracebuff[TRACEBUFFSIZE];

  level_debug = ReturnLevelAscii(str_debug_level);

  if(level_debug != -1)
    {
      /* set shell state */
      context->debug_level = level_debug;

      /* call to logfunctions */
      SetLevelDebug(level_debug);

      /* set shell vars */

      rc = set_var_value("DEBUG_LEVEL", str_debug_level);

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting DEBUG_LEVEL value to %s", rc, str_debug_level);
          shell_PrintError(context, tracebuff);
        }

      rc = set_var_value("DBG_LVL", str_debug_level);

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting DBG_LVL value to %s", rc, str_debug_level);
          shell_PrintError(context, tracebuff);
        }

      return SHELL_SUCCESS;

    }
  else
    {

      snprintf(tracebuff, TRACEBUFFSIZE,
               "Unexpected value for DEBUG_LEVEL: %s", str_debug_level);
      shell_PrintError(context, tracebuff);

      return SHELL_SYNTAX_ERROR;
    }

}                               /* shell_SetDbgLvl */

/**
 * shell_GetDbgLvl
 * Get the special variable $DEBUG_LEVEL and $DBG_LVL (internal use).
 */
int shell_GetDbgLvl(shell_state_t * context)
{
  return context->debug_level;
}

/**
 * shell_SetInput
 * Set the input for reading commands
 * and set the value of $INPUT and $INTERACTIVE.
 *
 * \param file_name:
 *        a script file or NULL for reading from stdin.
 */
int shell_SetInput(shell_state_t * context, char *file_name)
{

  FILE *stream;
  int rc;
  char tracebuff[TRACEBUFFSIZE];

  if(file_name)
    {
      if((stream = fopen(file_name, "r")) == NULL)
        {
          snprintf(tracebuff, TRACEBUFFSIZE, "Can't open \"%s\": %s (%d)",
                   file_name, strerror(errno), errno);
          shell_PrintError(context, tracebuff);
          return errno;
        }

      /* close previous filestream and reset line number */
      if(context->input_stream != NULL)
        {
          /* don't close stdin */
          if(context->input_stream != stdin)
            fclose(context->input_stream);

          shell_SetLine(context, 0);
        }

      /* set filestream */
      context->input_stream = stream;

      rc = set_var_value("INPUT", file_name);

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting INPUT value to \"%s\"", rc, file_name);
          shell_PrintError(context, tracebuff);
        }

      /* set interative mode to FALSE */

      context->interactive = FALSE;

      rc = set_var_value("INTERACTIVE", "0");

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting INTERACTIVE value to %s", rc, "0");
          shell_PrintError(context, tracebuff);
        }

      snprintf(tracebuff, TRACEBUFFSIZE, "Using script file \"%s\"", file_name);
      shell_PrintTrace(context, tracebuff);

      return SHELL_SUCCESS;

    }
  else
    {
      stream = stdin;

      /* close previous filestream and reset line number */
      if(context->input_stream != NULL)
        {
          /* don't close stdin */
          if(context->input_stream != stdin)
            fclose(context->input_stream);
          shell_SetLine(context, 0);
        }

      /* set filestream */
      context->input_stream = stream;

      rc = set_var_value("INPUT", "<stdin>");

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting INPUT value to %s", rc, "<stdin>");
          shell_PrintError(context, tracebuff);
        }

      /* set interative mode to TRUE */

      context->interactive = TRUE;

      rc = set_var_value("INTERACTIVE", "1");

      if(rc != 0)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "Error %d setting INTERACTIVE value to %s", rc, "1");
          shell_PrintError(context, tracebuff);
        }

      snprintf(tracebuff, TRACEBUFFSIZE, "Using standard input");
      shell_PrintTrace(context, tracebuff);

      return SHELL_SUCCESS;

    }

}                               /* shell_SetInput */

/**
 * shell_GetInputStream
 * Get the input stream for reading commands (internal use).
 */
FILE *shell_GetInputStream(shell_state_t * context)
{
  if(context->input_stream)
    return context->input_stream;
  else
    return stdin;
}

/**
 * shell_SetPrompt
 * Set the special variable $PROMPT
 */
int shell_SetPrompt(shell_state_t * context, char *str_prompt)
{
  int rc = set_var_value("PROMPT", str_prompt);
  char tracebuff[TRACEBUFFSIZE];

  if(rc != 0)
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "Error %d setting PROMPT value to \"%s\"", rc, str_prompt);
      shell_PrintError(context, tracebuff);
    }

  return rc;
}

/**
 * shell_GetPrompt
 * Get the special variable $PROMPT
 */
char *shell_GetPrompt(shell_state_t * context)
{
  return get_var_value("PROMPT");
}

/**
 * shell_SetShellId
 * Set the special variable $SHELLID
 */
int shell_SetShellId(shell_state_t * context, int shell_index)
{
  int rc;
  char str[64];
  char tracebuff[TRACEBUFFSIZE];

  snprintf(str, 64, "%d", shell_index);

  rc = set_var_value("SHELLID", str);

  if(rc != 0)
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "Error %d setting SHELLID value to \"%s\"", rc, str);
      shell_PrintError(context, tracebuff);
    }

  return SHELL_SUCCESS;
}

/**
 * shell_SetLine
 * Set the special variable $LINE
 */
int shell_SetLine(shell_state_t * context, int lineno)
{
  int rc;
  char str_line[64];
  char tracebuff[TRACEBUFFSIZE];

  context->line = lineno;

  snprintf(str_line, 64, "%d", lineno);

  rc = set_var_value("LINE", str_line);

  if(rc != 0)
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "Error %d setting LINE value to \"%s\"", rc, str_line);
      shell_PrintError(context, tracebuff);
    }

  return SHELL_SUCCESS;

}

/**
 * shell_GetLine
 * Get the special variable $LINE
 */
int shell_GetLine(shell_state_t * context)
{
  return context->line;
}

/*------------------------------------------------------------------
 *                      Shell commands.
 *-----------------------------------------------------------------*/

int shellcmd_help(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  int i;
  char tracebuff[TRACEBUFFSIZE];
  layer_def_t *current_layer = shell_GetLayer(GetShellContext());

  /* check args */

  if(argc > 1)
    {
      for(i = 1; i < argc; i++)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Unexpected argument \"%s\"", argv[0], argv[i]);
          shell_PrintError(GetShellContext(), tracebuff);
        }
    }

  /* List shell build-in commands */

  fprintf(output, "Shell built-in commands:\n");

  for(i = 0; shell_commands[i].command_name; i++)
    {
      fprintf(output, "   %15s: %s\n", shell_commands[i].command_name,
              shell_commands[i].command_help);
    }

  /* List shell tools commands */

  fprintf(output, "\nShell tools commands:\n");

  for(i = 0; shell_utils[i].command_name; i++)
    {
      fprintf(output, "   %15s: %s\n", shell_utils[i].command_name,
              shell_utils[i].command_help);
    }

  /* Layer list */

  fprintf(output, "\nLayers list:\n");

  for(i = 0; layer_list[i].layer_name; i++)
    {
      fprintf(output, "   %15s: %s\n", layer_list[i].layer_name,
              layer_list[i].layer_description);
    }

  /* Layer commands */

  if(current_layer)
    {

      fprintf(output, "\n%s layer commands:\n", current_layer->layer_name);

      for(i = 0; current_layer->command_list[i].command_name; i++)
        {
          fprintf(output, "   %15s: %s\n", current_layer->command_list[i].command_name,
                  current_layer->command_list[i].command_help);
        }

    }

  return SHELL_SUCCESS;

}                               /* shellcmd_help */

int shellcmd_if(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    )
{

  int i, rc;
  int index_test = -1;
  int longueur_test = -1;
  int index_cmd1 = -1;
  int longueur_cmd1 = -1;
  int index_cmd2 = -1;
  int longueur_cmd2 = -1;

  const char *help_if =
      "Usage: if command0 ? command1 [: command2]\n"
      "   Execute command1 if command0 returns a null status.\n"
      "   Else, execute command2 (if any).\n"
      "Ex: if eq -n $STATUS 0 ? print \"status=0\" : print \"status<>0\" \n";

  /* first, check that there is a test */
  if(argc > 1)
    {

      index_test = 1;

      i = index_test + 1;

      /* look for command 1 */

      while((i < argc) && strcmp(argv[i], "?"))
        i++;

      if(i + 1 < argc)
        {
          longueur_test = i - index_test;
          index_cmd1 = i + 1;

          i = index_cmd1 + 1;

          /* look for command 2 */

          while((i < argc) && strcmp(argv[i], ":"))
            i++;

          if(i + 1 < argc)
            {
              longueur_cmd1 = i - index_cmd1;
              index_cmd2 = i + 1;
              longueur_cmd2 = argc - index_cmd2;
            }
          else
            {
              longueur_cmd1 = argc - index_cmd1;
            }

        }
      else
        {
          longueur_test = argc - index_test;
        }

    }

  /* test or cmd1 missing */

  if((longueur_test <= 0) || (longueur_cmd1 <= 0))
    {
      fprintf(output, help_if, NULL);
      return SHELL_SYNTAX_ERROR;
    }

  /* executes test */

  rc = shell_Execute(longueur_test, &(argv[index_test]), output);

  /* if rc is not null, executes command 1 */
  if(rc)
    {
      return shell_Execute(longueur_cmd1, &(argv[index_cmd1]), output);
    }
  else if(longueur_cmd2 > 0)
    {
      return shell_Execute(longueur_cmd2, &(argv[index_cmd2]), output);
    }

  return 0;

}                               /* shellcmd_if */

int shellcmd_interactive(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    )
{
  int i;
  char tracebuff[TRACEBUFFSIZE];

  /* check args */

  if(argc > 1)
    {
      for(i = 1; i < argc; i++)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Unexpected argument \"%s\"", argv[0], argv[i]);
          shell_PrintError(GetShellContext(), tracebuff);
        }
    }

  /* set input as stdin */

  return shell_SetInput(GetShellContext(), NULL);

}                               /* shellcmd_interactive */

int shellcmd_set(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{

  int i;
  char *varname;
  char tracebuff[TRACEBUFFSIZE];
  char varvalue[MAX_OUTPUT_LEN];

  /* check args */

  if(argc < 3)
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "%s: Usage: %s <var_name> <expr1> [<expr2> ...<exprN>]", argv[0], argv[0]);
      shell_PrintError(GetShellContext(), tracebuff);

      return SHELL_SYNTAX_ERROR;
    }

  varname = argv[1];

  varvalue[0] = '\0';

  /* concatenation of strings */

  for(i = 2; i < argc; i++)
    if(concat(varvalue, argv[i], MAX_OUTPUT_LEN) == NULL)
      {
        shell_PrintError(GetShellContext(), "Output too large.");
        return SHELL_ERROR;
      }

  /* special variables */

  if(!strcmp(varname, "INPUT"))
    {
      return shell_SetInput(GetShellContext(), varvalue);
    }
  else if(!strcmp(varname, "INTERACTIVE"))
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "%s: cannot set \"%s\": set the value of \"INPUT\" or use the \"interactive\" command instead.",
               argv[0], varname);
      shell_PrintError(GetShellContext(), tracebuff);

      return SHELL_ERROR;

    }
  else if(!strcmp(varname, "LAYER"))
    {
      return shell_SetLayer(GetShellContext(), varvalue);
    }
  else if(!strcmp(varname, "STATUS") || !strcmp(varname, "?"))
    {
      return shell_SetStatus(GetShellContext(), my_atoi(varvalue));
    }
  else if(!strcmp(varname, "VERBOSE"))
    {
      return shell_SetVerbose(GetShellContext(), varvalue);
    }
  else if(!strcmp(varname, "DEBUG_LEVEL") || !strcmp(varname, "DBG_LVL"))
    {
      return shell_SetDbgLvl(GetShellContext(), varvalue);
    }
  else if(!strcmp(varname, "PROMPT"))
    {
      return shell_SetPrompt(GetShellContext(), varvalue);
    }
  else if(!strcmp(varname, "LINE"))
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "%s: cannot set \"%s\".", argv[0], varname);
      shell_PrintError(GetShellContext(), tracebuff);
      return SHELL_ERROR;
    }
  else
    {

      /* other variables */

      if(!is_authorized_varname(varname))
        {
          snprintf(tracebuff, TRACEBUFFSIZE, "%s: Invalid variable name \"%s\".", argv[0],
                   varname);
          shell_PrintError(GetShellContext(), tracebuff);
          return SHELL_ERROR;
        }

      if(set_var_value(varname, varvalue))
        {
          snprintf(tracebuff, TRACEBUFFSIZE, "%s: Error setting the value of \"%s\".",
                   argv[0], varname);
          shell_PrintError(GetShellContext(), tracebuff);
          return SHELL_ERROR;
        }

      return SHELL_SUCCESS;

    }

  /* should never happen */
  return SHELL_ERROR;

}                               /* shellcmd_set */

int shellcmd_unset(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  int i, arg_idx;
  char tracebuff[TRACEBUFFSIZE];
  int error = SHELL_SUCCESS;

  if(argc <= 1)
    {
      snprintf(tracebuff, TRACEBUFFSIZE, "%s: Missing argument: <var name>", argv[0]);
      shell_PrintError(GetShellContext(), tracebuff);

      return SHELL_SYNTAX_ERROR;
    }

  for(arg_idx = 1; arg_idx < argc; arg_idx++)
    {

      /* check if it is not a special var */

      for(i = 0; shell_special_vars[i] != NULL; i++)
        {

          if(!strcmp(shell_special_vars[i], argv[arg_idx]))
            {

              snprintf(tracebuff, TRACEBUFFSIZE,
                       "%s: This special variable cannot be deleted: \"%s\"", argv[0],
                       argv[arg_idx]);
              shell_PrintError(GetShellContext(), tracebuff);

              return SHELL_ERROR;

            }

        }

      /* unset the variable */

      if(free_var(argv[arg_idx]))
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Variable not found: \"%s\"", argv[0], argv[arg_idx]);
          shell_PrintError(GetShellContext(), tracebuff);

          error = SHELL_NOT_FOUND;
          /* however, continue */
        }

    }

  return error;

}                               /* shellcmd_unset */

int shellcmd_print(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  int i;

  /* print args */
  for(i = 1; i < argc; i++)
    fprintf(output, "%s", argv[i]);

  fprintf(output, "\n");

  return 0;

}                               /* shellcmd_print */

int shellcmd_varlist(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    )
{
  int i;
  char tracebuff[TRACEBUFFSIZE];

  /* check args */

  if(argc > 1)
    {
      for(i = 1; i < argc; i++)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Unexpected argument \"%s\"", argv[0], argv[i]);
          shell_PrintError(GetShellContext(), tracebuff);
        }
    }

  print_varlist(output, shell_GetVerbose(GetShellContext()));

  return 0;

}                               /* shellcmd_varlist */

int shellcmd_time(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  const char help_time[] =
      "Usage: time command [args ...]\n"
      "   Measure the time for executing a command.\n" "Ex: time shell ls\n";

  struct timeval timer_start;
  struct timeval timer_stop;
  struct timeval timer_tmp;

  int rc;

  /* first, check that there is a test */
  if(argc < 2)
    {
      fprintf(output, help_time);
      return SHELL_SYNTAX_ERROR;
    }

  if(gettimeofday(&timer_start, NULL) == -1)
    {
      fprintf(output, "Error retrieving system time.\n");
      return SHELL_ERROR;
    }

  rc = shell_Execute(argc - 1, &(argv[1]), output);

  if(gettimeofday(&timer_stop, NULL) == -1)
    {
      fprintf(output, "Error retrieving system time.\n");
      return SHELL_ERROR;
    }

  timer_tmp = time_diff(timer_start, timer_stop);
  fprintf(output, "\nExecution time for command \"%s\": ", argv[1]);
  print_timeval(output, timer_tmp);

  return rc;

}                               /* shellcmd_time */

int shellcmd_quit(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{
  int i;
  char tracebuff[TRACEBUFFSIZE];

  /* check args */

  if(argc > 1)
    {
      for(i = 1; i < argc; i++)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Unexpected argument \"%s\"", argv[0], argv[i]);
          shell_PrintError(GetShellContext(), tracebuff);
        }
    }

  exit(0);
  return 0;

}                               /* shellcmd_quit */

int shellcmd_barrier(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    )
{
  int i;
  char tracebuff[TRACEBUFFSIZE];

  /* check args */

  if(argc > 1)
    {
      for(i = 1; i < argc; i++)
        {
          snprintf(tracebuff, TRACEBUFFSIZE,
                   "%s: Unexpected argument \"%s\"", argv[0], argv[i]);
          shell_PrintError(GetShellContext(), tracebuff);
        }
    }

  /* call shell_BarrierWait */

  if(shell_BarrierWait())
    {
      snprintf(tracebuff, TRACEBUFFSIZE,
               "%s: barrier cannot be used in a single thread/script environment.",
               argv[0]);
      shell_PrintError(GetShellContext(), tracebuff);
      return SHELL_ERROR;
    }

  return SHELL_SUCCESS;

}                               /* shellcmd_quit */
