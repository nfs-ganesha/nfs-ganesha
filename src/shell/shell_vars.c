/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
 * Revision 1.7  2005/11/28 17:03:01  deniel
 * Added CeCILL headers
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
  if (pthread_key_create(&thread_key, NULL) == -1)
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
  if (pthread_once(&once_key, init_keys) != 0)
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
  while (current)
    {
      if (is_dlen)
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
  while (current)
    {
      if (!strncmp(current->var_name, str, MAX_VAR_LEN))
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
  strncpy(new_item->var_name, str, MAX_VAR_LEN);

  new_item->var_value = NULL;
  new_item->datalen = 0;

  /* inserting */
  if (var_table)
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
  if (var->var_value)
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

  if (var->prev)
    {
      var->prev->next = var->next;
    }
  else
    {
      SetVarTable(var->next);
    }

  if (var->next)
    {
      var->next->prev = var->prev;
    }

  /* free */
  if (var->var_value)
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
  if (!strcmp(str, "?"))
    return 1;

  while (str[len])
    {
      char c = str[len];
      if (!IS_LETTER(c) &&
          !IS_LETTER_CAP(c) && !IS_NUMERIC(c) && (c != '.') && (c != '_') && (c != ':'))
        {
          return 0;
        }

      len++;
      if (len > MAX_VAR_LEN)
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
  if (var = find_var(varname))
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
  if (!(var = find_var(varname)))
    {
      var = create_var(varname);
    }
  if (!var)
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
  if (!(var = find_var(varname)))
    {
      return 1;
    }

  del_var(var);
  return 0;

}
