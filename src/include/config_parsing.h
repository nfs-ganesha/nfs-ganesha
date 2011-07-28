/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
#ifndef _CONFIG_PARSING_H
#define _CONFIG_PARSING_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

/* opaque type */
typedef caddr_t config_file_t;
typedef caddr_t config_item_t;

typedef enum
{ CONFIG_ITEM_BLOCK = 1, CONFIG_ITEM_VAR } config_item_type;

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 * \return NULL on error.
 */
config_file_t config_ParseFile(char *file_path);

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg();

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE * output, config_file_t config);

/* Free the memory structure that store the configuration. */
void config_Free(config_file_t config);

/* Indicates how many main blocks are defined into the config file.
 * \return A positive value if no error.
 *         Else return a negative error code.
 */
int config_GetNbBlocks(config_file_t config);

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config, unsigned int block_no);

/* Return the name of a block */
char *config_GetBlockName(config_item_t block);

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block);

/* Retrieves an item from a given block and the subitem index. */
config_item_t config_GetItemByIndex(config_item_t block, unsigned int item_no);

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item);

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value);

/* Returns a block or variable with the specified name. This name can be "BLOCK::SUBBLOCK::SUBBLOCK" */
config_item_t config_FindItemByName(config_file_t config, const char *name);

/* Returns a block or variable with the specified name, and ensure it is unique.
 * The name can be "BLOCK::SUBBLOCK::SUBBLOCK" */
config_item_t config_FindItemByName_CheckUnique(config_file_t config, const char *name, int * is_unique);

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name);

/* Returns a block or variable with the specified name from the given block" */
config_item_t config_GetItemByName(config_item_t block, const char *name);

/* Directly returns the value of the key with the specified name
 * relative to the given block.
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name);

#endif
