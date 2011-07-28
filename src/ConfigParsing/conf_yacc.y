%{

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "config.h"
#include "analyse.h"

#include <stdio.h>

#if HAVE_STRING_H
#   include <string.h>
#endif


    int ganesha_yylex(void);
    void ganesha_yyerror(char*);

    list_items * program_result=NULL;

	/* stock le message d'erreur donne par le lexer */
    char local_errormsg[1024]="";

    /* stock le message d'erreur complet */
    char extern_errormsg[1024]="";

#ifdef _DEBUG_PARSING
#define DEBUG_YACK   config_print_list
#else
#define DEBUG_YACK
#endif


%}

%union {
    char         str_val[MAXSTRLEN];
    list_items              *  list;
    generic_item            *  item;
};

%token _ERROR_
%token BEGIN_BLOCK
%token END_BLOCK
%token BEGIN_SUB_BLOCK
%token END_SUB_BLOCK
%token AFFECTATION
%token END_AFFECT
%token <str_val> IDENTIFIER
%token <str_val> KEYVALUE

%type <list> listblock
%type <list> listitems
%type <item> block
%type <item> definition
%type <item> subblock
%type <item> affect


%%

program: listblock {DEBUG_YACK(stderr,$1);program_result=$1;}
    ;

listblock:
    block listblock {config_AddItem($2,$1);$$=$2;}
    | {$$=config_CreateItemsList();}
    ;

block:
    IDENTIFIER BEGIN_BLOCK listitems END_BLOCK {$$=config_CreateBlock($1,$3);}
    ;

listitems:
    definition listitems   {config_AddItem($2,$1);$$=$2;}
    |                      {$$=config_CreateItemsList();}
    ;

definition:
    affect
    | subblock
    ;


affect:
    IDENTIFIER AFFECTATION KEYVALUE END_AFFECT {$$=config_CreateAffect($1,$3);}
    ;

subblock:
    IDENTIFIER BEGIN_SUB_BLOCK listitems END_SUB_BLOCK {$$=config_CreateBlock($1,$3);}
    ;


%%

    void ganesha_yyerror(char *s){

		snprintf(extern_errormsg,1024,"%s (%s)",local_errormsg,s);

    }


    void set_error(char * s){
        strncpy(local_errormsg,s,1024);
    }
