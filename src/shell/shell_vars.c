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
 * \file    shell_vars.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:49:33 $
 * \version $Revision: 1.8 $
 * \brief   variables management for the shell.
 *
 * $Log: shell_vars.c,v $
 * Revision 1.8  2006/01/24 13:49:33  leibovic
 * Adding missing includes.
 *
 * Revision 1.6  2005/07/26 12:54:47  leibovic
 * Multi-thread shell with synchronisation routines.
 *
 * Revision 1.5  2005/05/10 14:02:45  leibovic
 * Removing adherence to BuddyMalloc.
 *
 * Revision 1.4  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 * Revision 1.3  2005/05/03 08:06:23  leibovic
 * Adding meminfo command.
 *
 * Revision 1.2  2005/05/03 07:37:58  leibovic
 * Using Mem_Alloc and Mem_Free.
 *
 * Revision 1.1  2004/12/14 09:56:00  leibovic
 * Variables management.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <HashTable.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include "shell_vars.h"
#include "stuff_alloc.h"

/* variable struct */
typedef struct shell_variable__
{
  char var_name[MAX_VAR_LEN];
  char *var_value;              /* mallocated */
  int datalen;
  struct shell_variable__ *next;
  struct shell_variable__ *prev;
} shell_variable_t;

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
 * manages pthread_keys.
 */
static shell_variable_t *GetVarTable()
{

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      printf("Error %d calling pthread_once for thread %p : %s\n",
             errno, (caddr_t) pthread_self(), strerror(errno));
      return NULL;
    }

  return (shell_variable_t *) pthread_getspecific(thread_key);

}                               /* GetVarTable */

void SetVarTable(shell_variable_t * var_table)
{
  /* set the specific value */
  pthread_setspecific(thread_key, var_table);

}

/**
 * print var list.
 */
void print_varlist(FILE * output, int is_dlen)
{
  shell_variable_t *current = GetVarTable();
  while(current)
    {
      if(is_dlen)
        fprintf(output, "\t%s (%d Bytes)\n", current->var_name, current->datalen - 1);
      else
        fprintf(output, "\t%s\n", current->var_name);
      current = current->next;
    }
  return;
}

static shell_variable_t *find_var(char *str)
{
  shell_variable_t *current = GetVarTable();
  while(current)
    {
      if(!strncmp(current->var_name, str, MAX_VAR_LEN))
        return current;
      current = current->next;
    }
  return NULL;
}

static shell_variable_t *create_var(char *str)
{

  shell_variable_t *var_table = GetVarTable();

  /* remembers name */
  shell_variable_t *new_item = (shell_variable_t *) Mem_Alloc(sizeof(shell_variable_t));
  memset( (char *)new_item, 0, sizeof(shell_variable_t ) ) ;

  strncpy(new_item->var_name, str, MAX_VAR_LEN);

  new_item->var_value = NULL;
  new_item->datalen = 0;

  /* inserting */
  if(var_table)
    var_table->prev = new_item;
  new_item->next = var_table;
  new_item->prev = NULL;
  SetVarTable(new_item);

  return new_item;
}

static void set_var(shell_variable_t * var, char *value)
{

  int dlen;

  /* clears old value, if any */
  if(var->var_value)
    {
      Mem_Free(var->var_value);
      var->var_value = NULL;
      var->datalen = 0;
    }

  /* alloc and set new value */
  dlen = strlen(value) + 1;
  var->datalen = dlen;
  var->var_value = (char *)Mem_Alloc(dlen);
  strncpy(var->var_value, value, dlen);

}

static void del_var(shell_variable_t * var)
{

  /* remove from the list */

  if(var->prev)
    {
      var->prev->next = var->next;
    }
  else
    {
      SetVarTable(var->next);
    }

  if(var->next)
    {
      var->next->prev = var->prev;
    }

  /* free */
  if(var->var_value)
    Mem_Free(var->var_value);

  Mem_Free((caddr_t) var);

}

#define IS_LETTER(_c_) (((_c_) >= 'a') && ((_c_) <= 'z'))
#define IS_LETTER_CAP(_c_) (((_c_) >= 'A') && ((_c_) <= 'Z'))
#define IS_NUMERIC(_c_) (((_c_) >= '0') && ((_c_) <= '9'))

/** indicates whether a name is authorized for a variable.
 *  A variable name must be in [a-zA-Z0-9._:]*
 */
int is_authorized_varname(char *str)
{

  int len = 0;

  /* special var $? */
  if(!strcmp(str, "?"))
    return 1;

  while(str[len])
    {
      char c = str[len];
      if(!IS_LETTER(c) &&
         !IS_LETTER_CAP(c) && !IS_NUMERIC(c) && (c != '.') && (c != '_') && (c != ':'))
        {
          return 0;
        }

      len++;
      if(len > MAX_VAR_LEN)
        return 0;
    }

  return 1;

}

/** returns the value for a variable,
 *  NULL if the variable doesn't exist.
 */
char *get_var_value(char *varname)
{
  shell_variable_t *var;
  if((var = find_var(varname)))
    {
      return var->var_value;
    }
  else
    {
      return NULL;
    }
}

/** set the value for a variable,
 *  and create it if necessary.
 */
int set_var_value(char *varname, char *var_value)
{
  shell_variable_t *var;
  /* if the value doesn't exist, create it */
  if(!(var = find_var(varname)))
    {
      var = create_var(varname);
    }
  if(!var)
    return 1;
  set_var(var, var_value);

  return 0;

}

/** free the resources used by a variable.
 */
int free_var(char *varname)
{

  shell_variable_t *var;
  /* if the value doesn't exist, error */
  if(!(var = find_var(varname)))
    {
      return 1;
    }

  del_var(var);
  return 0;

}
