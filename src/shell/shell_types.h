/**
 *
 * \file    shell_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/26 12:54:47 $
 * \version $Revision: 1.2 $
 * \brief   Internal routines for the shell.
 *
 *
 * $Log: shell_types.h,v $
 * Revision 1.2  2005/07/26 12:54:47  leibovic
 * Multi-thread shell with synchronisation routines.
 *
 * Revision 1.1  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */

#ifndef _SHELL_TYPES_H
#define _SHELL_TYPES_H

#include <stdio.h>

/** command definition */

typedef struct command_def__
{

  /* name of a command */
  char *command_name;

  /* function for processing the command : */
  int (*command_func) (int, char **, FILE *);

  /* short help message */
  char *command_help;

} command_def_t;

/** layer definition */

typedef struct layer_def__
{

  char *layer_name;
  command_def_t *command_list;
  char *layer_description;
  void (*setlog_func) (int);

} layer_def_t;

/* shell state structure */

typedef struct shell_state__
{
  FILE *input_stream;
  int interactive;
  layer_def_t *layer;
  int status;
  int verbose;
  int debug_level;
  int line;
} shell_state_t;

#endif
