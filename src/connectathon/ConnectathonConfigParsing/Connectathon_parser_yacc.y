%start testparam

%token AND

%token TESTPARAM_TOKEN

%token TESTDIR_TOKEN
%token LOGFILE_TOKEN

%token BASICTEST_TOKEN
%token BTEST_TOKEN
%token LEVELS_TOKEN
%token FILES_TOKEN
%token DIRS_TOKEN
%token COUNT_TOKEN
%token SIZE_TOKEN
%token BLOCKSIZE_TOKEN

%token FNAME_TOKEN
%token DNAME_TOKEN
%token NNAME_TOKEN
%token SNAME_TOKEN
%token BIGFILE_TOKEN

%token <num> NUMBER
%token <str> FILENAME

%token QUOTE
%token EQUAL
%token OBRACE
%token EBRACE
%token SEMICOLON

%type <btest_info> btestset btestlist btestdef
%type <str> dirtestset logfileset

%union {
	int			        num;
	char			      *str;
	struct btest		*btest_info;
}

%{

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "Connectathon_config_parsing.h"

//extern char *yylval;
extern int line;

extern struct testparam *param ;
struct btest *btest ;

int yyparse();

int yylex(void);

/*int main(int argc, char **argv) {
    yyparse();
    return 0;
}*/

void yyerror(const char *str) {
    fprintf(stderr,"%s at line %d\n",str,line);
}

int yywrap() {
    return 1;
}

%}

%%

testparam:
	testparamhead OBRACE parameters EBRACE SEMICOLON
	;

testparamhead:
	TESTPARAM_TOKEN
	{
		param = malloc(sizeof(struct testparam));
	}	
	;

parameters:
	dirtestset logfileset btestset
	{
		param->dirtest = $1;
    param->logfile = $2;
		param->btest = $3;
	}
	;

dirtestset:
	TESTDIR_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		$$ = $4;
	}
	;

logfileset:
  LOGFILE_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
  {
    $$ = $4;
  }

btestset:
	BASICTEST_TOKEN OBRACE btestlist EBRACE SEMICOLON
	{
		$$ = $3;
	}
	;

btestlist:
	btestlist btestdef
	{
		$2->nextbtest = $1;
		$$ = $2;
	}
	| btestdef
	{
		$$ = $1;
	}
	;

btestdef:
	btesthead OBRACE btestargs EBRACE SEMICOLON
	{
		$$ = btest;
		btest = NULL;
	}
	;

btesthead:
	BTEST_TOKEN NUMBER
	{
		btest = malloc(sizeof(struct btest));

		btest_init_defaults(btest);
		btest->num = $2;
	}
	| BTEST_TOKEN NUMBER AND NUMBER
	{
		btest = malloc(sizeof(struct btest));

		btest_init_defaults(btest);
		btest->num = $2;
		btest->num2 = $4;
	}
	;

btestargs:
	btestargs btestarg
	| btestarg
	;

btestarg:
	LEVELS_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->levels = $3;
	}
	| FILES_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->files = $3;
	}
	| DIRS_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->dirs = $3;
	}
	| COUNT_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->count = $3;
	}
	| SIZE_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->size = $3;
	}
	| BLOCKSIZE_TOKEN EQUAL NUMBER SEMICOLON
	{
		btest->blocksize = $3;
	}
	| FNAME_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		btest->fname = $4;
	}
	| DNAME_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		btest->dname = $4;
	}
	| NNAME_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		btest->nname = $4;
	}
	| SNAME_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		btest->sname = $4;
	}
	| BIGFILE_TOKEN EQUAL QUOTE FILENAME QUOTE SEMICOLON
	{
		btest->bigfile = $4;
	}
	;
%%
