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

struct config_node *config_stmt(char *varname,
				char *varval,
				char *filename,
				int lineno,
				struct parser_state *st);

#ifdef _DEBUG_PARSING
#define DEBUG_YACK   print_parse_tree
#else
#define DEBUG_YACK(...) (void)0
#endif

}

%token _ERROR_
%token BEGIN_BLOCK
%token END_BLOCK
%token BEGIN_SUB_BLOCK
%token END_SUB_BLOCK
%token EQUAL_OP
%token END_STMT
%token <token> IDENTIFIER
%token <token> KEYVALUE

%type <node> blocklist
%type <node> listitems
%type <node> block
%type <node> definition
%type <node> subblock
%type <node> statement


%%

program:
{ /* empty */
  ganesha_yyerror(&yyloc, st->scanner, "Empty configuration file");
}
| blocklist
{
  DEBUG_YACK(stderr,$1);
  glist_add_tail(&($1)->node, &st->root_node->root.u.blk.sub_nodes);
  link_node(&st->root_node->root);
}
;

blocklist: /* empty */ {
  $$ = NULL;
}
| blocklist block
{
  if ($1 == NULL) {
    $$ = $2;
  } else {
    if ($2 != NULL)
      glist_add_tail(&($1)->node, &($2)->node);
    $$ = $1;
  }
}
;

block:
IDENTIFIER BEGIN_BLOCK listitems END_BLOCK
{
  $$=config_block($1, $3, @$.filename, @$.first_line, st);
}
;

listitems: /* empty */ {
  $$ = NULL;
}
| listitems definition
{
  if ($1 == NULL) {
    $$ = $2;
  } else {
    if ($2 != NULL)
      glist_add_tail(&($1)->node, &($2)->node);
    $$ = $1;
  }
}
;

definition:
statement
| subblock
;


statement:
IDENTIFIER EQUAL_OP KEYVALUE END_STMT
{
  $$=config_stmt($1, $3, @$.filename, @$.first_line, st);
}
;

subblock:
IDENTIFIER BEGIN_SUB_BLOCK listitems END_SUB_BLOCK
{
  $$=config_block($1, $3, @$.filename, @$.first_line, st);
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
 *  Create a block item with the given content
 */
struct config_node *config_block(char *blockname,
				 struct config_node *list,
				 char *filename,
				 int lineno,
				 struct parser_state *st)
{
	struct config_node *node, *cnode;
	int cnt;

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
	node->name = blockname;
	node->filename = filename;
	node->linenumber = lineno;
	node->type = TYPE_BLOCK;
	glist_init(&node->u.blk.sub_nodes);
	glist_add_tail(&list->node, &node->u.blk.sub_nodes);
	link_node(node);
	return node;
}

/**
 * @brief Walk the subnode list and update all the sub-blocks in it
 *
 */

void link_node(struct config_node *node)
{
	struct config_node *subnode;
	struct glist_head *ns;

	assert(node->type == TYPE_BLOCK ||
	       node->type == TYPE_ROOT);
	glist_for_each(ns, &node->u.blk.sub_nodes) {
		subnode = glist_entry(ns, struct config_node, node);
		if (subnode->type == TYPE_BLOCK)
			subnode->u.blk.parent = node;
	}
}

/**
 *  Create a key=value peer (assignment)
 */
struct config_node *config_stmt(char *varname,
				char *varval,
				char *filename,
				int lineno,
				struct parser_state *st)
{
	struct config_node *node;

	node = gsh_calloc(1, sizeof(struct config_node));
	if (node == NULL) {
		st->err_type->resource = true;
		return NULL;
	}
	glist_init(&node->node);
	node->name = varname;
	node->filename = filename;
	node->linenumber = lineno;
	node->type = TYPE_STMT;
	node->u.varvalue = varval;
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
