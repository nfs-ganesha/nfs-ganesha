/**
 *
 * \file    shell.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 07:42:53 $
 * \version $Revision: 1.7 $
 * \brief   Internal routines for the shell.
 *
 *
 * $Log: shell.h,v $
 * Revision 1.7  2006/02/23 07:42:53  leibovic
 * Adding -n option to shell.
 *
 * Revision 1.6  2005/07/26 12:54:47  leibovic
 * Multi-thread shell with synchronisation routines.
 *
 * Revision 1.5  2005/07/25 12:50:46  leibovic
 * Adding thr_create and thr_join commands.
 *
 * Revision 1.4  2005/05/27 12:01:48  leibovic
 * Adding write command.
 *
 * Revision 1.3  2005/05/11 15:53:37  leibovic
 * Adding time function.
 *
 * Revision 1.2  2005/05/09 14:54:59  leibovic
 * Adding if.
 *
 * Revision 1.1  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */

#ifndef _SHELL_H
#define _SHELL_H

#include "shell_types.h"

#define MAX_LINE_LEN      1024
#define MAX_ARGS           256

/*------------------------------------------------------------------
 *                    Internal error codes.
 *-----------------------------------------------------------------*/

#define SHELL_SUCCESS 0
#define SHELL_ERROR  -1
#define SHELL_NOT_FOUND -2
#define SHELL_SYNTAX_ERROR -22

/*------------------------------------------------------------------
 *                    Main shell routines.
 *-----------------------------------------------------------------*/

/**
 *  Initialize the shell.
 *  The command line for the shell is given as parameter.
 *  \param input_file the file to read from (NULL if stdin).
 */
int shell_Init(int verbose, char *input_file, char *prompt, int shell_index);

/**
 *  Launch the interpreter.
 */
int shell_Launch();

/**
 *  Initialize the barrier for shell synchronization routines.
 *  The number of threads to wait for is given as parameter.
 */
int shell_BarrierInit(int nb_threads);

/*------------------------------------------------------------------
 *                Parsing and execution routines.
 *-----------------------------------------------------------------*/

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

int shell_ParseLine(char *in_out_line, char **out_arglist, int *p_argcount);

/**
 *  shell_CleanArgs:
 *  Free allocated arguments.
 *
 *  \param  argc           The number of command line tokens.
 *  \param  in_out_argv    The list of command line tokens (modified).
 *  \param  in_allocated   Indicates which tokens must be freed.
 *
 *  \return 0 if no errors.
 */

void shell_CleanArgs(int argc, char **in_out_argv, int *in_allocated);

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

int shell_SolveArgs(int argc, char **in_out_argv, int *out_allocated);

/**
 *  shell_Execute:
 *  Execute a command.
 *
 *  \param  argc The number of arguments of this command.
 *  \param  argv The arguments for this command.
 *  \param  output The output stream of this command.
 *
 *  \return The returned status of this command.
 */
int shell_Execute(int argc, char **argv, FILE * output);

/*------------------------------------------------------------------
 *                 Shell ouput routines.
 *-----------------------------------------------------------------*/

/**
 *  shell_PrintError:
 *  Prints an error.
 */
void shell_PrintError(shell_state_t * context, char *error_msg);

/**
 *  shell_PrintTrace:
 *  Prints a verbose trace.
 */
void shell_PrintTrace(shell_state_t * context, char *msg);

/*------------------------------------------------------------------
 *                 Shell state management routines.
 *-----------------------------------------------------------------*/

/** List of the shell special variables */

extern char *shell_special_vars[];

/**
 * shell_SetLayer:
 * Set the current active layer.
 * \return 0 if OK.
 *         else, an error code.
 */
int shell_SetLayer(shell_state_t * context, char *layer_name);

/**
 * shell_GetLayer:
 * Retrieves the current active layer (internal use).
 */
layer_def_t *shell_GetLayer(shell_state_t * context);

/**
 * shell_SetStatus
 * Set the special variables $? and $STATUS.
 */
int shell_SetStatus(shell_state_t * context, int returned_status);

/**
 * shell_GetStatus
 * Get the special variables $? or $STATUS (internal use).
 */
int shell_GetStatus(shell_state_t * context);

/**
 * shell_SetVerbose
 * Set the special variable $VERBOSE.
 */
int shell_SetVerbose(shell_state_t * context, char *str_verbose);

/**
 * shell_GetVerbose
 * Get the special variable $VERBOSE (internal use).
 */
int shell_GetVerbose(shell_state_t * context);

/**
 * shell_SetDbgLvl
 * Set the special variables $DEBUG_LEVEL and $DBG_LVL
 */
int shell_SetDbgLvl(shell_state_t * context, char *str_debug_level);

/**
 * shell_GetDbgLvl
 * Get the special variable $DEBUG_LEVEL and $DBG_LVL (internal use).
 */
int shell_GetDbgLvl();

/**
 * shell_GetInputStream
 * Get the input stream for reading commands (internal use).
 */
FILE *shell_GetInputStream(shell_state_t * context);

/**
 * shell_SetInput
 * Set the input for reading commands
 * and set the value of $INPUT and $INTERACTIVE.
 *
 * \param file_name:
 *        a script file or NULL for reading from stdin.
 */
int shell_SetInput(shell_state_t * context, char *file_name);

/**
 * shell_SetPrompt
 * Set the special variable $PROMPT
 */
int shell_SetPrompt(shell_state_t * context, char *str_prompt);

/**
 * shell_GetPrompt
 * Get the special variable $PROMPT
 */
char *shell_GetPrompt(shell_state_t * context);

/**
 * shell_SetShellId
 * Set the special variable $SHELLID
 */
int shell_SetShellId(shell_state_t * context, int shell_index);

/**
 * shell_SetLine
 * Set the special variable $LINE
 */
int shell_SetLine(shell_state_t * context, int lineno);

/**
 * shell_GetLine
 * Get the special variable $LINE
 */
int shell_GetLine(shell_state_t * context);

/*------------------------------------------------------------------
 *                      Shell commands.
 *-----------------------------------------------------------------*/

int shellcmd_help(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int shellcmd_if(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

int shellcmd_interactive(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    );

int shellcmd_set(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

int shellcmd_unset(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int shellcmd_print(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int shellcmd_varlist(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

int shellcmd_time(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int shellcmd_quit(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int shellcmd_barrier(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** List of the shell commands */

static command_def_t __attribute__ ((__unused__)) shell_commands[] =
{

  {
  "barrier", shellcmd_barrier, "synchronization in a multi-thread shell"},
  {
  "echo", shellcmd_print, "print one or more arguments"},
  {
  "exit", shellcmd_quit, "exit this shell"},
  {
  "help", shellcmd_help, "print this help"},
  {
  "if", shellcmd_if, "conditionnal execution"},
  {
  "interactive", shellcmd_interactive, "close script file and start interactive mode"},
  {
  "print", shellcmd_print, "print one or more arguments"},
  {
  "quit", shellcmd_quit, "exit this shell"},
  {
  "set", shellcmd_set, "set the value of a shell variable"},
  {
  "time", shellcmd_time, "measures the time for executing a command"},
  {
  "unset", shellcmd_unset, "free a shell variable"},
  {
  "varlist", shellcmd_varlist, "print the list of shell variables"},
  {
  NULL, NULL, NULL}             /* End of command list */
};

/* @todo: if, source */

#endif
