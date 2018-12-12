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

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "gsh_list.h"

/**
 * @brief Configuration parser data structures
 * and linkage betweek the parser, analyse.c and config_parsing.c
 */

/*
 * Parse tree node.
 */

/* Config nodes are either terminals or non-terminals.
 * BLOCK and STMT are non-terminals.
 * ROOT is a special case BLOCK (root of tree...)
 * TERM is a value/token terminal.
 */

enum  node_type { TYPE_ROOT = 1, TYPE_BLOCK, TYPE_STMT, TYPE_TERM};

struct config_node {
	struct glist_head node;
	struct glist_head blocks;
	char *filename;		/* pointer to filename in file list */
	int linenumber;
	bool found;		/* use accounting private in do_block_load */
	enum node_type type;	/* switches union contents */
	union {			/* sub_nodes are always struct config_node */
		struct {		/* TYPE_TERM */
			enum term_type type;
			char *op_code;
			char *varvalue;
		} term;
		struct {		/* TYPE_BLOCK | TYPE_STMT */
			char *name;	/* name */
			struct config_node *parent;
			struct glist_head sub_nodes;
		} nterm;
	}u;
};

/*
 * File list
 * Every config_node points to a pathname in this list
 */

struct file_list {
	struct file_list *next;
	char *pathname;
};

/*
 * Symbol table
 * Every token the scanner keeps goes here.  It is a linear
 * list but we don't need much more than that.  What we do
 * need is an accounting of all that memory so we can free it
 * when the parser barfs and leaves stuff on its FSM stack.
 */

struct token_tab {
	struct token_tab *next;
	char token[];
};

/*
 * Parse tree root
 * A parse tree consists of several blocks,
 * each block consists of variables definitions
 * and subblocks.
 * All storage allocated into the parse tree is here
 */

struct bufstack;  /* defined in conf_lex.l */

struct config_root {
	struct config_node root;
	char *conf_dir;
	struct file_list *files;
	struct token_tab *tokens;
	uint64_t generation;
};

/*
 * parser/lexer linkage
 */

struct parser_state {
	struct config_root *root_node;
	void *scanner;
	struct bufstack *curbs;
	char *current_file;
	int block_depth; /* block/subblock nesting level */
	struct config_error_type *err_type;
};

char *save_token(char *token, bool esc, struct parser_state *st);
int ganesha_yyparse(struct parser_state *st);
int ganeshun_yy_init_parser(char *srcfile,
			   struct parser_state *st);
void ganeshun_yy_cleanup_parser(struct parser_state *st);

/**
 * Error reporting
 */

void config_error(FILE *fp, const char *filename, int linenum,
		  char *format, va_list args);

/**
 *  Displays the content of parse tree.
 */
void print_parse_tree(FILE * output, struct config_root *tree);

/**
 * Free resources of parse tree
 */
void free_parse_tree(struct config_root *tree);

#endif				/* CONFPARSER_H */
