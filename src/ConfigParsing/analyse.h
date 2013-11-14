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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 * 
 * ---------------------------------------
 */

#ifndef CONFPARSER_H
#define CONFPARSER_H

#include <stdio.h>
#include "nlm_list.h"

#define MAXSTRLEN   1024

/* A program consists of several blocks,
 * each block consists of variables definitions
 * and subblocks.
 */

/* forward declaration of generic item */
struct _generic_item_;

typedef enum {
	TYPE_BLOCK,
	TYPE_STMT
} type_item;

typedef struct _type_affect_ {

	char varname[MAXSTRLEN];
	char varvalue[MAXSTRLEN];

} type_affect;

typedef struct _type_block_ {

	char block_name[MAXSTRLEN];
	struct _generic_item_ *block_content;

} type_block;

typedef struct _generic_item_ {

	type_item type;
	union {
		type_block block;
		type_affect affect;
	} item;

	/* next item in the list */
	struct _generic_item_ *next;

} generic_item;

/**
 * @brief Configuration parser data structures
 * and linkage betweek the parser, analyse.c and config_parsing.c
 */

/*
 * Parse tree node.
 */

enum  node_type { TYPE_ROOT = 1, TYPE_BLOCK, TYPE_STMT};

struct config_node {
	struct glist_head node;
	char *name;		/* block or parameter name */
	char *filename;		/* pointer to filename in file list */
	int linenumber;
	type_item type;		/* switches union contents */
	union {			/* sub_nodes are always struct config_node */
		char *varvalue;			/* TYPE_STMT */
		struct {				/* TYPE_BLOCK */
			struct config_node *parent;
			struct glist_head sub_nodes;
		} blk;
	}u;
};

typedef generic_item *list_items;

/**
 *  create a list of items
 */
list_items *config_CreateItemsList();

/**
 *  Create a block item with the given content
 */
generic_item *config_CreateBlock(char *blockname, list_items * list);

/*
 * File list
 * Every config_node points to a pathname in this list
 */

struct file_list {
	struct file_list *next;
	char *pathname;
};

/*
 * Parse tree root
 * A parse tree consists of several blocks,
 * each block consists of variables definitions
 * and subblocks.
 * All storage allocated into the parse tree is here
 */

struct config_root {
	struct config_node root;
	char *conf_dir;
	struct file_list *files;
};

/*
 * parser/lexer linkage
 */

struct parser_state {
	struct config_root *root_node;
	void *scanner;
};

/**
 *  Create a key=value peer (assignment)
 */
generic_item *config_CreateAffect(char *varname, char *varval);

/**
 *  Add an item to a list
 */
void config_AddItem(list_items * list, generic_item * item);

/**
 *  Displays the content of a list of blocks.
 */
void config_print_list(FILE * output, list_items * list);

/**
 * config_free_list:
 * Free ressources for a list
 */
void config_free_list(list_items * list);

#endif
