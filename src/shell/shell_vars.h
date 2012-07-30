/**
 *
 * \file    shell_vars.h
 * \date    $Date: 2005/05/09 12:23:55 $
 * \version $Revision: 1.2 $
 * \brief   variables management for the shell.
 *
 * $Log: shell_vars.h,v $
 * Revision 1.2  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 * Revision 1.1  2004/12/14 09:56:00  leibovic
 * Variables management.
 *
 *
 */

#ifndef _SHELL_VARS_H
#define _SHELL_VARS_H

#include <stdio.h>

#define MAX_VAR_LEN   32

 *  A variable name must be in [a-zA-Z0-9._:]*
 */

/** returns the value for a variable,
 *  NULL if the variable doesn't exist.
 */
char *get_var_value(char *varname);

/** set the value for a variable,
 *  and create it if necessary.
 */
int set_var_value(char *varname, char *var_value);

/** free the resources used by a variable.
 */
int free_var(char *varname);

/**
 * print var list.
 * \param  is_dlen: indicates if it prints the length
 *         of the data they contain.
 */
void print_varlist(FILE * output, int is_dlen);

#endif
