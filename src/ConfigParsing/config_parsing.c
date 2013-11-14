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
	rc = ganeshun_yy_init_parser(file_path, st);
	if (rc) {
		return NULL;
	}
	rc = ganesha_yyparse(st);
	ganeshun_yylex_destroy(st->scanner);

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
	print_parse_tree(output, (struct config_root *)config);
}

/** 
 * config_Free:
 * Free the memory structure that store the configuration.
 */

void config_Free(config_file_t config)
{
	free_parse_tree((struct config_root *)config);
	return;

}

/**
 * config_GetNbBlocks:
 * Indicates how many blocks are defined into the config file.
 */
int config_GetNbBlocks(config_file_t config)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->nodes))
		return 0;
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (node->type == TYPE_BLOCK)
			bcnt++;
		else
			scnt++;
	}
	return bcnt + scnt;
}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config,
				     unsigned int block_no)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->nodes))
		return NULL;
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (/* node->type == TYPE_BLOCK && */ block_no == cnt)
			return (config_item_t)node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* Return the name of a block */
char *config_GetBlockName(config_item_t block)
{
	struct config_node *curr_block = (struct config_node *) block;

	assert(curr_block->type == TYPE_BLOCK);

	return curr_block->name;
}

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	assert(node->type == TYPE_BLOCK);
	if (glist_empty(&node->u.sub_nodes))
		return 0;
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (sub_node->type == TYPE_STMT)
			scnt++;
		else
			bcnt++;
	}
	return scnt + bcnt;
}

/* retrieves a given block from the config file, from its index
 */
config_item_t config_GetItemByIndex(config_item_t block,
				    unsigned int item_no)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&node->u.sub_nodes))
		return NULL;
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (/* sub_node->type == TYPE_STMT &&  */item_no == cnt)
			return (config_item_t)sub_node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item)
{
	struct config_node *node = (struct config_node *)item;

	if (node->type == TYPE_BLOCK)
		return CONFIG_ITEM_BLOCK;
	else if (node->type == TYPE_STMT)
		return CONFIG_ITEM_VAR;
	else
		return 0;
}

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value)
{
	struct config_node *node = (struct config_node *)item;

	assert(node->type == TYPE_STMT);

	*var_name = node->name;
	*var_value = node->u.varvalue;

	return 0;
}

static config_item_t find_by_name(struct config_node *node, char *name)
{
	struct glist_head *nsi, *nsn;
	char *separ;
	config_item_t found_item = NULL;

	if (node->type != TYPE_BLOCK || glist_empty(&node->u.sub_nodes))
		return NULL;
	separ = strstr(name, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, name) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	return found_item;
}

config_item_t config_FindItemByName(config_file_t config, const char *name)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	struct glist_head *nsi, *nsn;
	char *separ, *tmpname, *current;
	config_item_t found_item = NULL;

	if (glist_empty(&tree->nodes))
		return NULL;
	tmpname = gsh_strdup(name);
	current = tmpname;
	separ = strstr(current, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, current) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	gsh_free(tmpname);
	return found_item;
}

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name)
{
	struct config_node *node;

	node = (struct config_node *) config_FindItemByName(config, key_name);

	assert(node->type == TYPE_STMT);
	return node->u.varvalue;

}

/* Directly returns the value of the key with the specified name
 * relative to the given block.  this is bad.  will segv on no name...
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name)
{
	struct config_node *node = (struct config_node *)block;
	char *name;

	name = gsh_strdup(key_name);
	if (name == NULL)
		return NULL;
	node = (struct config_node *)find_by_name(node, name);
	gsh_free(name);
	assert(node->type == TYPE_STMT);
	return node->u.varvalue;
}
