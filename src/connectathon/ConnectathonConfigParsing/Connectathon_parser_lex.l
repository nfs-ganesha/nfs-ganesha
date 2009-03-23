%{

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "Connectathon_parser_yacc.h"

int line = 1;

%}

DIGIT [0-9]
COMMENT #
BACKSPACE _

%%

{COMMENT}.*		  {}

and			        { return AND; }

test_params		  { return TESTPARAM_TOKEN; }

test_directory  { return TESTDIR_TOKEN; }
log_file        { return LOGFILE_TOKEN; }

basic_test		  { return BASICTEST_TOKEN; }
btest			      { return BTEST_TOKEN; }

levels			    { return LEVELS_TOKEN; }
files			      { return FILES_TOKEN; }
dirs			      { return DIRS_TOKEN; }
count			      { return COUNT_TOKEN; }
size			      { return SIZE_TOKEN; }
blocksize	      { return BLOCKSIZE_TOKEN; }

fname			      { return FNAME_TOKEN; }
dname			      { return DNAME_TOKEN; }
nname			      { return NNAME_TOKEN; }
sname			      { return SNAME_TOKEN; }
big_file		    { return BIGFILE_TOKEN; }

{DIGIT}+		    { yylval.num = atoi(strdup(yytext)); return NUMBER; }
([a-zA-Z0-9\/\.-]|{BACKSPACE})+  	{ yylval.str = strdup(yytext); return FILENAME; }

\"	    		    { return QUOTE; }
\=	    		    { return EQUAL; }
\{	    		    { return OBRACE; }
\}	    		    { return EBRACE; }
\;	    		    { return SEMICOLON; }
\n	    		    { line++; }
[ \t]+	    		{}
%%
