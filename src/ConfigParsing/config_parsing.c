/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Copyright CEA/DAM/DIF (2007)
 * Contributor: Thomas LEIBOVICI thomas.leibovici@cea.fr
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
 * therefore means  that it is reserved for developers  and  experienced
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
#include "config.h"
#include "config_parsing.h"
#include "analyse.h"
#include <stdio.h>
#include <errno.h>

#if HAVE_STRING_H
#include <string.h>
#endif

/* case unsensitivity */
#define STRNCMP   strncasecmp

typedef struct config_struct_t {

  /* Syntax tree */

  list_items *syntax_tree;

} config_struct_t;

/***************************************
 * ACCES AUX VARIABLES EXTERNES
 ***************************************/

/* fichier d'entree du lexer */
extern FILE *ganesha_yyin;

/* routine de parsing */
int ganesha_yyparse();

/* routine de reinitialization */
void ganesha_yyreset(void);

/* indique le fichier parse (pour la trace en cas d'erreur) */
void ganesha_yy_set_current_file(char *file);

/* variable renseignee lors du parsing */
extern list_items *program_result;

/* message d'erreur */
extern char extern_errormsg[1024];

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 */
config_file_t config_ParseFile(char *file_path)
{

  FILE *configuration_file;
  config_struct_t *output_struct;

  /* Inits error message */

  extern_errormsg[0] = '\0';

  /* Sanity check */

  if (!file_path || !file_path[0])
    {
      strcpy(extern_errormsg, "Invalid arguments");
      return NULL;
    }

  /* First, opens the file. */

  configuration_file = fopen(file_path, "r");

  if (!configuration_file)
    {
      strcpy(extern_errormsg, strerror(errno));
      return NULL;
    }

  /* Then, parse the file. */
  program_result = NULL;

  ganesha_yyreset();

  ganesha_yy_set_current_file(file_path);
  ganesha_yyin = configuration_file;

  if (ganesha_yyparse())
    {
      fclose(configuration_file);
      return NULL;
    }

  /** @todo : ganesha_yyparse fait exit en cas d'erreur. Remedier au probleme. */

  /* Finally, build the output struct. */

  output_struct = (config_struct_t *) malloc(sizeof(config_struct_t));

  if (!output_struct)
    {
      strcpy(extern_errormsg, strerror(errno));
      fclose(configuration_file);
      return NULL;
    }

  output_struct->syntax_tree = program_result;

  /* converts pointer to pointer */
  fclose(configuration_file);
  return (config_file_t) output_struct;

}

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg()
{

  return extern_errormsg;

}

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE * output, config_file_t config)
{

  /* sanity check */
  if (!config)
    return;

  config_print_list(output, ((config_struct_t *) config)->syntax_tree);

}

/** 
 * config_Free:
 * Free the memory structure that store the configuration.
 */

void config_Free(config_file_t config)
{

  config_struct_t *config_struct = (config_struct_t *) config;

  if (!config_struct)
    return;

  config_free_list(config_struct->syntax_tree);

  free(config_struct);

  return;

}

/**
 * config_GetNbBlocks:
 * Indicates how many blocks are defined into the config file.
 */
int config_GetNbBlocks(config_file_t config)
{

  config_struct_t *config_struct = (config_struct_t *) config;

  if (!config_struct)
    return -EFAULT;

  /* on regarde si la liste est vide */
  if (!(*config_struct->syntax_tree))
    {
      return 0;
    }
  /* on compte le nombre d'elements */
    else
    {
      /* il y a au moins un element : le premier */
      generic_item *curr_block = (*config_struct->syntax_tree);
      int nb = 1;

      while ((curr_block = curr_block->next) != NULL)
	{
	  nb++;
	}

      return nb;
    }
}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config, unsigned int block_no)
{
  config_struct_t *config_struct = (config_struct_t *) config;
  generic_item *curr_block;
  unsigned int i;

  if (!config_struct->syntax_tree || !(*config_struct->syntax_tree))
    return NULL;

  for (i = 0, curr_block = (*config_struct->syntax_tree);
       curr_block != NULL; curr_block = curr_block->next, i++)
    {
      if (i == block_no)
	return (config_item_t) curr_block;
    }

  /* not found */
  return NULL;
}

/* Return the name of a block */
char *config_GetBlockName(config_item_t block)
{
  generic_item *curr_block = (generic_item *) block;

  if (!curr_block || (curr_block->type != TYPE_BLOCK))
    return NULL;

  return curr_block->item.block.block_name;
}

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block)
{
  generic_item *the_block = (generic_item *) block;

  if (!the_block || (the_block->type != TYPE_BLOCK))
    return -1;

  /* on regarde si la liste est vide */
  if (!(the_block->item.block.block_content))
    {
      return 0;
    }
  /* on compte le nombre d'elements */
    else
    {
      /* il y a au moins un element : le premier */
      generic_item *curr_block = the_block->item.block.block_content;
      int nb = 1;

      while ((curr_block = curr_block->next) != NULL)
	{
	  nb++;
	}

      return nb;
    }

}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetItemByIndex(config_item_t block, unsigned int item_no)
{
  generic_item *the_block = (generic_item *) block;
  generic_item *curr_item;
  unsigned int i;

  if (!the_block || (the_block->type != TYPE_BLOCK))
    return NULL;

  for (i = 0, curr_item = the_block->item.block.block_content;
       curr_item != NULL; curr_item = curr_item->next, i++)
    {
      if (i == item_no)
	return (config_item_t) curr_item;
    }

  /* not found */
  return NULL;
}

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item)
{
  generic_item *the_item = (generic_item *) item;

  if (the_item->type == TYPE_BLOCK)
    return CONFIG_ITEM_BLOCK;
  else if (the_item->type == TYPE_AFFECT)
    return CONFIG_ITEM_VAR;
    else
    return 0;
}

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value)
{
  generic_item *var = (generic_item *) item;

  if (!var || (var->type != TYPE_AFFECT))
    return -1;

  *var_name = var->item.affect.varname;
  *var_value = var->item.affect.varvalue;

  return 0;
}

/* get an item from a list with the given name */
static generic_item *GetItemFromList(generic_item * list, const char *name)
{
  generic_item *curr;

  if (!list)
    return NULL;

  for (curr = list; curr != NULL; curr = curr->next)
    {
      if ((curr->type == TYPE_BLOCK)
	  && !STRNCMP(curr->item.block.block_name, name, MAXSTRLEN))
	return curr;
      if ((curr->type == TYPE_AFFECT)
	  && !STRNCMP(curr->item.affect.varname, name, MAXSTRLEN))
	return curr;
    }
  /* not found */
  return NULL;

}

/* Returns the block with the specified name. This name can be "BLOCK::SUBBLOCK::SUBBLOCK" */
config_item_t config_FindItemByName(config_file_t config, const char *name)
{
  config_struct_t *config_struct = (config_struct_t *) config;
  generic_item *block;
  generic_item *list;
  char *separ;
  char *current;
  char tmp_name[MAXSTRLEN];

  /* connot be found if empty */
  if (!config_struct->syntax_tree || !(*config_struct->syntax_tree))
    return NULL;

  list = *config_struct->syntax_tree;

  strncpy(tmp_name, name, MAXSTRLEN);
  current = tmp_name;

  while (current)
    {
      /* first, split the name into BLOCK/SUBBLOC/SUBBLOC */
      separ = strstr(current, "::");

      /* it is a whole name */
      if (!separ)
	return (config_item_t) GetItemFromList(list, current);
	else
	{
	  /* split the name */
	  *separ = '\0';

	  if ((separ - tmp_name) < MAXSTRLEN - 2)
	    separ += 2;
	    else
	    return NULL;	/* overflow */

	  block = GetItemFromList(list, current);

	  /* not found or not a block ? */
	  if (!block || (block->type != TYPE_BLOCK))
	    return NULL;

	  list = block->item.block.block_content;

	  /* "::" was found, must have something after */
	  current = separ;
	}
    }

  /* not found */
  return NULL;

}

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name)
{
  generic_item *var;

  var = (generic_item *) config_FindItemByName(config, key_name);

  if (!var || (var->type != TYPE_AFFECT))
    return NULL;
    else
    return var->item.affect.varvalue;

}

/* Returns a block or variable with the specified name from the given block" */
config_item_t config_GetItemByName(config_item_t block, const char *name)
{
  generic_item *curr_block = (generic_item *) block;
  generic_item *list;
  char *separ;
  char *current;
  char tmp_name[MAXSTRLEN];

  /* cannot be found if empty or non block */
  if (!curr_block || (curr_block->type != TYPE_BLOCK))
    return NULL;

  list = curr_block->item.block.block_content;

  strncpy(tmp_name, name, MAXSTRLEN);
  current = tmp_name;

  while (current)
    {
      /* first, split the name into BLOCK/SUBBLOC/SUBBLOC */
      separ = strstr(current, "::");

      /* it is a whole name */
      if (!separ)
	return (config_item_t) GetItemFromList(list, current);
	else
	{
	  /* split the name */
	  *separ = '\0';

	  if ((separ - tmp_name) < MAXSTRLEN - 2)
	    separ += 2;
	    else
	    return NULL;	/* overflow */

	  curr_block = GetItemFromList(list, current);

	  /* not found or not a block ? */
	  if (!curr_block || (curr_block->type != TYPE_BLOCK))
	    return NULL;

	  list = curr_block->item.block.block_content;

	  /* "::" was found, must have something after */
	  current = separ;
	}
    }

  /* not found */
  return NULL;

}

/* Directly returns the value of the key with the specified name
 * relative to the given block.
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name)
{
  generic_item *var;

  var = (generic_item *) config_GetItemByName(block, key_name);

  if (!var || (var->type != TYPE_AFFECT))
    return NULL;
    else
    return var->item.affect.varvalue;

}
