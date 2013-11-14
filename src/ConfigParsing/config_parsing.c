/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */
#include "config.h"
#include "config_parsing.h"
#include "analyse.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include "abstract_mem.h"
#include "conf_yacc.h"

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
struct parser_state parser_state;

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 */
config_file_t config_ParseFile(char *file_path)
{

	struct parser_state *st = &parser_state;
	int rc;

	memset(st, 0, sizeof(struct parser_state));
	rc = ganesha_yy_init_parser(file_path, st);
	if (rc) {
		return NULL;
	}
	rc = ganesha_yyparse(st);
	ganesha_yylex_destroy(st->scanner);

	/* converts pointer to pointer */
	return rc ? NULL : (config_file_t) st->root_node;
}

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg()
{

	return "Help! Help! We're all gonna die!!!";

}

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE * output, config_file_t config)
{
	config_print_list(output, ((config_struct_t *) config)->syntax_tree);
}

/** 
 * config_Free:
 * Free the memory structure that store the configuration.
 */

void config_Free(config_file_t config)
{
	config_struct_t *config_struct = (config_struct_t *) config;

	config_free_list(config_struct->syntax_tree);

	gsh_free(config_struct);

	return;

}

/**
 * config_GetNbBlocks:
 * Indicates how many blocks are defined into the config file.
 */
int config_GetNbBlocks(config_file_t config)
{
	config_struct_t *config_struct = (config_struct_t *) config;

	/* on regarde si la liste est vide */
	if (!(*config_struct->syntax_tree)) {
		return 0;
	}
	/* on compte le nombre d'elements */
	else {
		/* il y a au moins un element : le premier */
		generic_item *curr_block = (*config_struct->syntax_tree);
		int nb = 1;

		while ((curr_block = curr_block->next) != NULL) {
			nb++;
		}

		return nb;
	}
}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config,
				     unsigned int block_no)
{
	config_struct_t *config_struct = (config_struct_t *) config;
	generic_item *curr_block;
	unsigned int i;

	if (!config_struct->syntax_tree || !(*config_struct->syntax_tree))
		return NULL;

	for (i = 0, curr_block = (*config_struct->syntax_tree);
	     curr_block != NULL; curr_block = curr_block->next, i++) {
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

	assert(curr_block->type == TYPE_BLOCK);

	return curr_block->item.block.block_name;
}

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block)
{
	generic_item *the_block = (generic_item *) block;

	assert(the_block->type == TYPE_BLOCK);

	/* on regarde si la liste est vide */
	if (!(the_block->item.block.block_content)) {
		return 0;
	}
	/* on compte le nombre d'elements */
	else {
		/* il y a au moins un element : le premier */
		generic_item *curr_block = the_block->item.block.block_content;
		int nb = 1;

		while ((curr_block = curr_block->next) != NULL) {
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

	assert(the_block->type == TYPE_BLOCK);

	for (i = 0, curr_item = the_block->item.block.block_content;
	     curr_item != NULL; curr_item = curr_item->next, i++) {
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
	else if (the_item->type == TYPE_STMT)
		return CONFIG_ITEM_VAR;
	else
		return 0;
}

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value)
{
	generic_item *var = (generic_item *) item;

	assert(var->type == TYPE_STMT);

	*var_name = var->item.affect.varname;
	*var_value = var->item.affect.varvalue;

	return 0;
}

/* get an item from a list with the given name */
static generic_item *GetItemFromList(generic_item * list, const char *name)
{
	generic_item *curr;

	for (curr = list; curr != NULL; curr = curr->next) {
		if ((curr->type == TYPE_BLOCK)
		    && !STRNCMP(curr->item.block.block_name, name, MAXSTRLEN))
			return curr;
		if ((curr->type == TYPE_STMT)
		    && !STRNCMP(curr->item.affect.varname, name, MAXSTRLEN))
			return curr;
	}
	/* not found */
	return NULL;

}

/**
 * \retval 0 if the entry is unique in the list
 * \retval != if there are several items with this name in the list
 */
static int CheckDuplicateEntry(generic_item * list, const char *name)
{
	generic_item *curr;
	unsigned int found = 0;

	for (curr = list; curr != NULL; curr = curr->next) {
		if ((curr->type == TYPE_BLOCK)
		    && !STRNCMP(curr->item.block.block_name, name, MAXSTRLEN))
			found++;
		if ((curr->type == TYPE_STMT)
		    && !STRNCMP(curr->item.affect.varname, name, MAXSTRLEN))
			found++;
		if (found > 1)
			break;
	}
	return (found > 1);
}

/* Returns the block with the specified name. This name can be "BLOCK::SUBBLOCK::SUBBLOCK" */
config_item_t internal_FindItemByName(config_file_t config, const char *name,
				      int *unique)
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
	tmp_name[MAXSTRLEN - 1] = '\0';
	current = tmp_name;

	while (current) {
		/* first, split the name into BLOCK/SUBBLOC/SUBBLOC */
		separ = strstr(current, "::");

		/* it is a whole name */
		if (!separ) {
			if (unique) {
				*unique = !CheckDuplicateEntry(list, current);
				sprintf(extern_errormsg,
					"Configuration item '%s' is not unique",
					name);
			}
			return (config_item_t) GetItemFromList(list, current);
		} else {
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

config_item_t config_FindItemByName(config_file_t config, const char *name)
{
	return internal_FindItemByName(config, name, NULL);
}

config_item_t config_FindItemByName_CheckUnique(config_file_t config,
						const char *name, int *unique)
{
	return internal_FindItemByName(config, name, unique);
}

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name)
{
	generic_item *var;

	var = (generic_item *) config_FindItemByName(config, key_name);

	assert(var->type == TYPE_STMT);
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

	assert(curr_block->type == TYPE_BLOCK);

	list = curr_block->item.block.block_content;

	strncpy(tmp_name, name, MAXSTRLEN);
	tmp_name[MAXSTRLEN - 1] = '\0';
	current = tmp_name;

	while (current) {
		/* first, split the name into BLOCK/SUBBLOC/SUBBLOC */
		separ = strstr(current, "::");

		/* it is a whole name */
		if (!separ)
			return (config_item_t) GetItemFromList(list, current);
		else {
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

	assert(var->type == TYPE_STMT);
	return var->item.affect.varvalue;
}
