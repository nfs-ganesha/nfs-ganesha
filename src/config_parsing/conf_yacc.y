%code top {

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "config.h"
#include "analyse.h"
#include "abstract_mem.h"

#include <stdio.h>
#include "log.h"

#if HAVE_STRING_H
#   include <string.h>
#endif

}

/* Options and variants */
%pure-parser
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

void ganesha_yyerror(YYLTYPE *yylloc_param,
		     void *yyscanner,
		     char*);

struct config_node *config_block(char *blockname,
				 struct config_node *list,
				 char *filename,
				 int lineno,
				 struct parser_state *st);

void link_node(struct config_node *node);

 struct config_node *link_sibling(struct config_node *first,
				  struct config_node *second);

struct config_node *config_stmt(char *varname,
				struct config_node *exprlist,
				char *filename,
				int lineno,
				struct parser_state *st);

struct config_node *config_term(char *varval,
				enum term_type type,
				char *filename,
				int lineno,
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
%token <token> TOK_PATH

%type <node> deflist
%type <node> definition
%type <node> exprlist
%type <node> expression

%start config

%%

config:
{ /* empty */
  ganesha_yyerror(&yyloc, st->scanner, "Empty configuration file");
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

definition: { /* empty */
  $$ = NULL;
}
| IDENTIFIER EQUAL_OP exprlist SEMI_OP
{
  $$=config_stmt($1, $3, @$.filename, @$.first_line, st);
}
| IDENTIFIER LCURLY_OP deflist RCURLY_OP
{
  $$=config_block($1, $3, @$.filename, @$.first_line, st);
}
;

/* A statement is a comma separated sequence of option/value tokens
 */

exprlist:
expression
{
  $$ = $1;
}
| exprlist COMMA_OP expression
{
  $$ = link_sibling($1, $3);
}
;

expression: /* empty */ {
  printf("empty expr\n");
  $$ = NULL;
}
| TOK_PATH
{
  $$ = config_term($1, TERM_PATH, @$.filename, @$.first_line, st);
}
| TOKEN
{
  $$ = config_term($1, TERM_TOKEN, @$.filename, @$.first_line, st);
}
| STRING
{
  $$ = config_term($1, TERM_STRING, @$.filename, @$.first_line, st);
}
| DQUOTE
{
  $$ = config_term($1, TERM_DQUOTE, @$.filename, @$.first_line, st);
}
| SQUOTE
{
  $$ = config_term($1, TERM_SQUOTE, @$.filename, @$.first_line, st);
}
;

%%

void ganesha_yyerror(YYLTYPE *yylloc_param,
		     void *yyscanner,
		     char *s){

  LogCrit(COMPONENT_CONFIG,
	  "Config file (%s:%d) error: %s",
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
struct config_node *config_block(char *blockname,
				 struct config_node *list,
				 char *filename,
				 int lineno,
				 struct parser_state *st)
{
	struct config_node *node, *cnode;

	if (blockname == NULL) {
		LogWarn(COMPONENT_CONFIG,
			"Config file (%s:%d) no memory for block ID token.",
			filename, lineno);
		st->err_type->empty = true;
		return NULL;
	}
	if (list == NULL) {
		LogWarn(COMPONENT_CONFIG,
			"Config file (%s:%d) Block %s is empty",
			filename, lineno,  blockname);
		st->err_type->empty = true;
		return NULL;
	}
	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	node->u.nterm.name = blockname;
	node->filename = filename;
	node->linenumber = lineno;
	node->type = TYPE_BLOCK;
	glist_init(&node->u.nterm.sub_nodes);
	glist_add_tail(&list->node, &node->u.nterm.sub_nodes);
	link_node(node);
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

struct config_node *config_term(char *varval,
				enum term_type type,
				char *filename,
				int lineno,
				struct parser_state *st)
{
	struct config_node *node;

	if (varval == NULL) {
		LogWarn(COMPONENT_CONFIG,
			"Config file (%s:%d) no memory for option value token.",
			filename, lineno);
		st->err_type->empty = true;
		return NULL;
	}
	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	node->filename = filename;
	node->linenumber = lineno;
	node->type = TYPE_TERM;
	node->u.term.type = type;
	node->u.term.varvalue = varval;
	return node;
}

/**
 *  Create a statement node (key = list of terms)
 */

struct config_node *config_stmt(char *varname,
				struct config_node *exprlist,
				char *filename,
				int lineno,
				struct parser_state *st)
{
	struct config_node *node;

	if (varname == NULL) {
		LogWarn(COMPONENT_CONFIG,
			"Config file (%s:%d) no memory for option ID token.",
			filename, lineno);
		st->err_type->empty = true;
		return NULL;
	}
	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	glist_init(&node->u.nterm.sub_nodes);
	glist_add_tail(&exprlist->node, &node->u.nterm.sub_nodes);
	node->filename = filename;
	node->linenumber = lineno;
	node->type = TYPE_STMT;
	node->u.nterm.name = varname;
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
