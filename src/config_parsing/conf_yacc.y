/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

%code top {

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "config.h"
#include "config_parsing.h"
#include "analyse.h"
#include "abstract_mem.h"

#include <stdio.h>
#include "log.h"

#if HAVE_STRING_H
#   include <string.h>
#endif

}

/* Options and variants */
%define api.pure
%lex-param {struct parser_state *st}
%parse-param {struct parser_state *st}
%locations

%code requires {
/* alert the parser that we have our own definition */
# define YYLTYPE_IS_DECLARED 1

}

%union {
  char *token;
  struct config_node *node;
}

%code provides {

typedef struct YYLTYPE {
  int first_line;
  int first_column;
  int last_line;
  int last_column;
  char *filename;
} YYLTYPE;

# define YYLLOC_DEFAULT(Current, Rhs, N)			       \
    do								       \
      if (N)							       \
	{							       \
	  (Current).first_line	 = YYRHSLOC (Rhs, 1).first_line;       \
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;     \
	  (Current).last_line	 = YYRHSLOC (Rhs, N).last_line;	       \
	  (Current).last_column	 = YYRHSLOC (Rhs, N).last_column;      \
	  (Current).filename	 = YYRHSLOC (Rhs, 1).filename;	       \
	}							       \
      else							       \
	{ /* empty RHS */					       \
	  (Current).first_line	 = (Current).last_line	 =	       \
	    YYRHSLOC (Rhs, 0).last_line;			       \
	  (Current).first_column = (Current).last_column =	       \
	    YYRHSLOC (Rhs, 0).last_column;			       \
	  (Current).filename  = NULL;			     /* new */ \
	}							       \
    while (0)

int ganeshun_yylex(YYSTYPE *yylval_param,
		   YYLTYPE *yylloc_param,
		   void *scanner);

int ganesha_yylex(YYSTYPE *yylval_param,
		  YYLTYPE *yylloc_param,
		  struct parser_state *st);

void config_parse_error(YYLTYPE *yylloc_param,
			struct parser_state *st,
			char *format, ...);

void ganesha_yyerror(YYLTYPE *yylloc_param,
		     void *yyscanner,
		     char*);

extern struct glist_head all_blocks;

struct config_node *config_block(char *blockname,
				 struct config_node *list,
				 YYLTYPE *yylloc_param,
				 struct parser_state *st);

void link_node(struct config_node *node);

 struct config_node *link_sibling(struct config_node *first,
				  struct config_node *second);

struct config_node *config_stmt(char *varname,
				struct config_node *exprlist,
				 YYLTYPE *yylloc_param,
				struct parser_state *st);

struct config_node *config_term(char *opcode,
				char *varval,
				enum term_type type,
				 YYLTYPE *yylloc_param,
				struct parser_state *st);

}

%token _ERROR_
%token LCURLY_OP
%token RCURLY_OP
%token EQUAL_OP
%token COMMA_OP
%token SEMI_OP
%token <token> IDENTIFIER
%token <token> STRING
%token <token> DQUOTE
%token <token> SQUOTE
%token <token> TOKEN
%token <token> REGEX_TOKEN
%token <token> TOK_PATH
%token <token> TOK_TRUE
%token <token> TOK_FALSE
%token <token> TOK_DECNUM
%token <token> TOK_HEXNUM
%token <token> TOK_OCTNUM
%token <token> TOK_ARITH_OP
%token <token> TOK_V4_ANY
%token <token> TOK_V4ADDR
%token <token> TOK_V4CIDR
%token <token> TOK_V6ADDR
%token <token> TOK_V6CIDR
%token <token> TOK_FSID
%token <token> TOK_NETGROUP

%type <node> deflist
%type <node> definition
%type <node> block
%type <node> statement
%type <node> exprlist
%type <node> expression

%start config

%%

config:
{ /* empty */
  config_parse_error(&yyloc, st, "Empty configuration file");
}
| deflist
{
  if ($1 != NULL)
    glist_add_tail(&($1)->node, &st->root_node->root.u.nterm.sub_nodes);
  link_node(&st->root_node->root);
}
;


deflist:
definition
{
  $$ = $1;
}
| deflist definition
{
  $$ = link_sibling($1, $2);
}
;

/* definition: statement | block ; */

definition:
 IDENTIFIER EQUAL_OP statement
{
  $$ = config_stmt($1, $3, &@$, st);
}
| IDENTIFIER LCURLY_OP block
{
  $$=config_block($1, $3, &@$, st);
}
;

statement:
 exprlist SEMI_OP
{
  $$ = $1;
}
| error SEMI_OP
{
  config_parse_error(&@$, st, "Syntax error in statement");
  yyerrok;
  $$ = NULL;
}
;

block:
RCURLY_OP
{ /* empty */
  $$ = NULL;
}
| deflist RCURLY_OP
{
  $$ = $1;
}
| error RCURLY_OP
{
  config_parse_error(&@$, st, "Syntax error in block");
  yyerrok;
  $$ = NULL;
}
;

/* A statement is a comma separated sequence of option/value tokens
 */

exprlist:
{ /* empty */
  $$ = NULL;
}
| expression
{
  $$ = $1;
}
| exprlist COMMA_OP expression
{
  $$ = link_sibling($1, $3);
}
;

expression:
TOK_PATH
{
  $$ = config_term(NULL, $1, TERM_PATH, &@$, st);
}
| TOKEN
{
  $$ = config_term(NULL, $1, TERM_TOKEN, &@$, st);
}
| REGEX_TOKEN
{
  $$ = config_term(NULL, $1, TERM_REGEX, &@$, st);
}
| STRING
{
  $$ = config_term(NULL, $1, TERM_STRING, &@$, st);
}
| DQUOTE
{
  $$ = config_term(NULL, $1, TERM_DQUOTE, &@$, st);
}
| SQUOTE
{
  $$ = config_term(NULL, $1, TERM_SQUOTE, &@$, st);
}
| TOK_TRUE
{
  $$ = config_term(NULL, $1, TERM_TRUE, &@$, st);
}
| TOK_FALSE
{
  $$ = config_term(NULL, $1, TERM_FALSE, &@$, st);
}
| TOK_OCTNUM
{
  $$ = config_term(NULL, $1, TERM_OCTNUM, &@$, st);
}
| TOK_HEXNUM
{
  $$ = config_term(NULL, $1, TERM_HEXNUM, &@$, st);
}
| TOK_DECNUM
{
  $$ = config_term(NULL, $1, TERM_DECNUM, &@$, st);
}
| TOK_ARITH_OP TOK_OCTNUM
{
  $$ = config_term($1, $2, TERM_OCTNUM, &@$, st);
}
| TOK_ARITH_OP TOK_HEXNUM
{
  $$ = config_term($1, $2, TERM_HEXNUM, &@$, st);
}
| TOK_ARITH_OP TOK_DECNUM
{
  $$ = config_term($1, $2, TERM_DECNUM, &@$, st);
}
| TOK_V4_ANY
{
  $$ = config_term(NULL, $1, TERM_V4_ANY, &@$, st);
}
| TOK_V4ADDR
{
  $$ = config_term(NULL, $1, TERM_V4ADDR, &@$, st);
}
| TOK_V4CIDR
{
  $$ = config_term(NULL, $1, TERM_V4CIDR, &@$, st);
}
| TOK_V6ADDR
{
  $$ = config_term(NULL, $1, TERM_V6ADDR, &@$, st);
}
| TOK_V6CIDR
{
  $$ = config_term(NULL, $1, TERM_V6CIDR, &@$, st);
}
| TOK_FSID
{
  $$ = config_term(NULL, $1, TERM_FSID, &@$, st);
}
| TOK_NETGROUP
{
  $$ = config_term(NULL, $1, TERM_NETGROUP, &@$, st);
}
;

%%

  /**
   * @brief Report an scanner/parser error
   *
   * Replacement for yyerror() to get more info.
   * HACK ALERT: new_file() does not have yylloc initialized yet for
   * first file so create a string and init line number for it.
   */

void config_parse_error(YYLTYPE *yylloc_param,
			struct parser_state *st,
			char *format, ...)
{
	FILE *fp = st->err_type->fp;
	va_list arguments;
	char *filename = "<unknown file>";
	int linenum = 0;;

	if (fp == NULL)
		return;  /* no stream, no message */
	if (yylloc_param != NULL) {
	  filename = yylloc_param->filename;
	  linenum = yylloc_param->first_line;
	}
	va_start(arguments, format);
	config_error(fp, filename, linenum, format, arguments);
	va_end(arguments);
}

/* This is here because bison wants it declared.
 * We do not use it because we can't get around the API.
 * Use config_parse_error() instead.
 */

void ganesha_yyerror(YYLTYPE *yylloc_param,
		     void *yyscanner,
		     char *s){

  LogCrit(COMPONENT_CONFIG,
	  "Config file (%s:%d) error: %s\n",
	  yylloc_param->filename,
	  yylloc_param->first_line,
	  s);
}

/**
 * @brief Notes on parse tree linkage
 *
 * We use glist a little differently so beware.
 * Elsewhere in the server, we only glist_init() the head
 * and leave the 'node' members alone (both next and prev == NULL).
 * However, the parse FSM handles siblings as a list via the LR rules.
 * This means that while sub_nodes is the "head" of the list, it only
 * gets linked in when the rules add the already formed list is fully
 * parsed. Therefore, to make this all work, each node's 'node' member
 * gets a turn as the head which requires it to be glist_init()'d
 * contrary to what the rest of the code does.  The last node to play
 * 'head' is then the 'sub_nodes' member of the parent.
 */

/**
 *  Create a block item with the given content
 */

struct glist_head all_blocks;

void dump_all_blocks(void)
{
	struct glist_head *glh;
	struct config_node *node;

	int ix = 0;
	glist_for_each(glh, &all_blocks) {
		node = glist_entry(glh, struct config_node, blocks);
		printf("%s: ix: %d node blockname: %s\n",
			__func__, ix, node->u.nterm.name);
		++ix;
	}
}

struct config_node *config_block(char *blockname,
				 struct config_node *list,
				 YYLTYPE *yylloc_param,
				 struct parser_state *st)
{
	struct config_node *node;

	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	glist_init(&node->blocks);
	node->u.nterm.name = blockname;
	node->filename = yylloc_param->filename;
	node->linenumber = yylloc_param->first_line;
	node->type = TYPE_BLOCK;
	glist_init(&node->u.nterm.sub_nodes);
	if (list != NULL) {
		glist_add_tail(&list->node, &node->u.nterm.sub_nodes);
		link_node(node);
	}
	glist_add_tail(&all_blocks, &node->blocks);
	return node;
}

/**
 * @brief Walk the subnode list and update all the sub-blocks in it
 * so we can find the root of the parse tree when we need it.
 */

void link_node(struct config_node *node)
{
	struct config_node *subnode;
	struct glist_head *ns;

	assert(node->type == TYPE_BLOCK ||
	       node->type == TYPE_ROOT);
	glist_for_each(ns, &node->u.nterm.sub_nodes) {
		subnode = glist_entry(ns, struct config_node, node);
		if (subnode->type == TYPE_BLOCK)
			subnode->u.nterm.parent = node;
	}
}

/**
 * @brief Link siblings together
 *
 */

struct config_node *link_sibling(struct config_node *first,
				 struct config_node *second)
{
	if (first == NULL) {
		return second;
	} else {
		if (second != NULL)
			glist_add_tail(&first->node, &second->node);
		return first;
	}
}

/**
 *  Create a term (value)
 */

struct config_node *config_term(char *opcode,
				char *varval,
				enum term_type type,
				YYLTYPE *yylloc_param,
				struct parser_state *st)
{
	struct config_node *node;

	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	node->filename = yylloc_param->filename;
	node->linenumber = yylloc_param->first_line;
	node->type = TYPE_TERM;
	node->u.term.type = type;
	node->u.term.op_code = opcode;
	node->u.term.varvalue = varval;
	return node;
}

/**
 *  Create a statement node (key = list of terms)
 */

struct config_node *config_stmt(char *varname,
				struct config_node *exprlist,
				 YYLTYPE *yylloc_param,
				struct parser_state *st)
{
	struct config_node *node;

	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	glist_init(&node->u.nterm.sub_nodes);
	node->filename = yylloc_param->filename;
	node->linenumber = yylloc_param->first_line;
	node->type = TYPE_STMT;
	node->u.nterm.name = varname;
	if (exprlist != NULL)
		glist_add_tail(&exprlist->node, &node->u.nterm.sub_nodes);
	return node;
}


int ganesha_yylex(YYSTYPE *yylval_param,
		  YYLTYPE *yylloc_param,
		  struct parser_state *st)
{
	return ganeshun_yylex(yylval_param,
			      yylloc_param,
			      st->scanner);

}
