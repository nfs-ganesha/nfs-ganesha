%{

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#include "config.h"
#include "analyse.h"
#include "abstract_mem.h"
#include "conf_yacc.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <libgen.h>
#include "log.h"

#if HAVE_STRING_H
#   include <string.h>
#endif

/* block/subblock nesting level */
int block_depth;

/* Our versions of parser macros */

#define YY_USER_INIT \
do { \
	BEGIN YY_INIT; \
} while (0);

#define YY_USER_ACTION \
	yylloc->first_line = yylloc->last_line = yylineno; \
	yylloc->first_column = yylloc->last_column = yycolumn + yyleng -1; \
	yycolumn += yyleng; \
	yylloc->filename = current_file;

#ifdef _DEBUG_PARSING
#define DEBUG_LEX   printf
#else
#define DEBUG_LEX(...) (void)0
#endif

struct bufstack {
	struct bufstack *prev;
	YY_BUFFER_STATE bs;
	int lineno;
	char *filename;
	FILE *f;
} *curbs = NULL;

static char *filter_string(char *src, int esc);
static int new_file(char *filename,
	     struct parser_state *st);
static int pop_file(struct parser_state *st);

static char *current_file;

%}

%option nounput
%option yylineno
%option reentrant
%option bison-bridge
%option bison-locations
%option extra-type="struct parser_state *"

SPACE        [ \t\r\f]
NL           [\n]
VAL_CHAR     [^ \t\r\n\f;"'#]
COMMENTEXT	 #.*$
/* lettre posant probleme dans une chaine */
STRING_CHAR       [^\n]
IDENTIFIER [a-zA-Z_.][a-zA-Z0-9_.\-]*

/* INCLUDE state is used for picking the name of the include file */
%START YY_INIT BLOCK IN_BLOCK STMT_OR_SUB_BLOCK VALUE ENDVALUE QUOTE_STR LIT_STRING INCLUDE

%%
%{
	struct parser_state *stp = yyextra;
%}

<YY_INIT>"%include" { /* include file start */
	DEBUG_LEX("INCLUDE\n");
	BEGIN INCLUDE;
	/* not a token, return nothing */
}

<INCLUDE>{VAL_CHAR}+ {
	{
		int c;

		DEBUG_LEX("Calling new_file with unquoted %s\n", yytext);
		c = new_file(yytext, stp);
		if (c != 0) {
			if (c == EINVAL)
				ganesha_yyerror(yylloc, yyscanner,
						"config file already parsed, ignored");
			else
				ganesha_yyerror(yylloc, yyscanner,
						"error opening file");
			yyterminate();
		}
		BEGIN YY_INIT;
		DEBUG_LEX("done new file\n");
	}
}

<INCLUDE>\"{VAL_CHAR}+\" {
	{
		int c;

		DEBUG_LEX("Calling new_file with quoted %s\n", yytext);
		c = new_file(yytext, stp);
		if (c != 0) {
			if (c == EINVAL)
				ganesha_yyerror(yylloc, yyscanner,
						"config file already parsed, ignored");
			else
				ganesha_yyerror(yylloc, yyscanner,
						"error opening file");
			yyterminate();
		}
		BEGIN YY_INIT;
		DEBUG_LEX("done new file\n");
	}
}

<INCLUDE>[.\n]+ {
	DEBUG_LEX("BAD include, line break\n");
	/* error bad include */
	yyterminate();
}

<<EOF>> { /* end of included file */
	DEBUG_LEX("<EOF>\n");
	if (pop_file(stp) == 0)
		yyterminate();
}



<YY_INIT>{IDENTIFIER} { /* first block */
	/* identifier */
	DEBUG_LEX("[block:%s]\n",yytext);
	yylval->token = gsh_strdup(yytext);
	BEGIN BLOCK;
	return IDENTIFIER;
}


<BLOCK>"{"        {/* start of  block */
	DEBUG_LEX("BEGIN_BLOCK\n");
	BEGIN IN_BLOCK;
	block_depth++;
	return BEGIN_BLOCK;
}

<IN_BLOCK>{IDENTIFIER} { /* name of block or keyword */
	/* identifier */
	DEBUG_LEX("[id:%s",yytext);
	yylval->token = gsh_strdup(yytext);
	BEGIN STMT_OR_SUB_BLOCK;
	return IDENTIFIER;
}

<IN_BLOCK>"}"     {   /* end of block */
	DEBUG_LEX("END_BLOCK\n");
	if (block_depth <= 0) {
		/* error */
		ganesha_yyerror(yylloc, yyscanner,
				"closing bracket outside a block");
		return _ERROR_;
	} else
		block_depth --;

	if (block_depth == 0) {
		BEGIN YY_INIT;
		return END_BLOCK;
	} else {
		BEGIN IN_BLOCK;
		return END_SUB_BLOCK;
	}
}

<STMT_OR_SUB_BLOCK>"="  { /* A statement */
	/* equal_op */
	DEBUG_LEX(" , ");
	BEGIN VALUE;
	return EQUAL_OP;
}

<STMT_OR_SUB_BLOCK>"{" {
	/* sub-block */
	DEBUG_LEX("\nBEGIN_SUB_BLOCK\n");
	BEGIN IN_BLOCK;
	block_depth++;
	return BEGIN_SUB_BLOCK;
}

<VALUE>"\""  {  /* start of a double quote string */
	BEGIN QUOTE_STR;
	DEBUG_LEX("quote value:<");
}

<VALUE>"'"   { /* start of a single quote string */
	BEGIN LIT_STRING;
	DEBUG_LEX("lit value:<");
}

<VALUE>({VAL_CHAR})+  { /* start of a number or label/tag */
	DEBUG_LEX("token value:%s",yytext);
	BEGIN ENDVALUE;
	yylval->token = gsh_strdup(yytext);
	return KEYVALUE;
}

<ENDVALUE>";"  { /* end of statement */
	DEBUG_LEX("]\n");
	BEGIN IN_BLOCK;
	return END_STMT;
}

<QUOTE_STR>[^\"]*\" {
	if (yytext[yyleng - 2] == '\\') {
		yyless(yyleng - 1);
		yymore();
	} else {
		yylval->token = filter_string(yytext, 1);
		if (yylval->token == NULL) {
			ganesha_yyerror(yylloc, yyscanner, "Out of memory");
			return _ERROR_;
		} else if (yylval->token == (char *)-1) {
			ganesha_yyerror(yylloc, yyscanner, "Bad literal string");
			return _ERROR_;
		}
		DEBUG_LEX("%s>", yylval->token);
		BEGIN ENDVALUE;
		return KEYVALUE;
	}
}

<LIT_STRING>[^\n]*\'  { /* end of single quote string */
	yylval->token =	filter_string(yytext, 0);
	if (yylval->token == NULL) {
		ganesha_yyerror(yylloc, yyscanner, "Out of memory");
		return _ERROR_;
	} else if (yylval->token == (char *)-1) {
		ganesha_yyerror(yylloc, yyscanner, "Bad literal string");
		return _ERROR_;
	}
	DEBUG_LEX("%s>", yylval->token);
	BEGIN ENDVALUE;
	return KEYVALUE;
}

<LIT_STRING>\n { /* ERROR: eol within single quote string */
	ganesha_yyerror(yylloc, yyscanner, "closing quote missing");
	return _ERROR_;
}

{COMMENTEXT}  ;/* ignore */
{SPACE}        ;/* ignore */
{NL}           ;/* ignore */

. { /* ERROR: out of character character */
	DEBUG_LEX("unexpected stuff %s\n", yytext);
	ganesha_yyerror(yylloc, yyscanner, "Unexpected character");
	return _ERROR_;
}

%%

int ganesha_yywrap(void *yyscanner){
    return 1;
}

int ganesha_yy_init_parser(char *srcfile,
			   struct parser_state *st) {
	FILE *in_file;
	void *yyscanner = st->scanner;
	/* reentrant scanner macro magic requires this... */
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	struct file_list *flist;
	struct config_root *confroot;
	YY_BUFFER_STATE inbuf;
	int rc = ENOMEM;

	confroot = gsh_calloc(1, sizeof(struct config_root));
	if (confroot == NULL)
		goto nospc;
	glist_init(&confroot->root.node);
	confroot->root.type = TYPE_ROOT;
	st->root_node = confroot;
	ganesha_yylex_init_extra(st, &st->scanner);
	return new_file(srcfile, st);

nospc:
	return rc;
}

/**
 * @brief filter a string for escaped chars
 *
 * You can escape anything in quoted strings. Literals
 * can't and must be on one line.
 *
 * @param src yytext from the lexer
 * @param esc do quoted string escapes
 *
 * @return allocated string or NULL for ENOMEM and -1 for esc errors
 */

static char *filter_string(char *src, int esc)
{
	int c;
	char *sp, *dst, *dp;

	dst = gsh_calloc(1, strlen(src) + 1);
	if (dst == NULL)
		return NULL;
	sp = src;
	dp = dst;
	c = *sp++;
	while (c != '\0') {
		if (esc) { /* quoted strings */
			if (c == '\\' && *sp != '\0') {
				*dp++ = *sp++;
			} else if (c == '\"' && *sp == '\0') {
				*dp++ = '\0';
				goto done;
			} else if ((c == '\n' || c == '\"') &&
				   *sp != '\0') {
				gsh_free(dst);
				return (char *)-1;
			} else {
				*dp++ = c;

			}
		} else { /* literal strings */
			if (c == '\'' && *sp == '\0') {
				*dp++ = '\0';
				goto done;
			} else if ((c == '\n' || c == '\'') &&
				   *sp != '\0') {
				gsh_free(dst);
				return (char *)-1;
			} else {
				*dp++ = c;
			}
		}
		c = *sp++;
	}
	*dp = *sp;

done:
	return dst;
}

static int new_file(char *name_tok,
	     struct parser_state *st)
{
	struct bufstack *bs;
	FILE *in_file;
	YY_BUFFER_STATE inbuf;
	struct file_list *flist, *fp;
	void *yyscanner = st->scanner;
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	struct config_root *confroot = st->root_node;
	char *fullpath;
	int rc = ENOMEM;
	char *filename = alloca(strlen(name_tok + 1));

	if (*name_tok == '\"') {
		strcpy(filename, name_tok + 1);
		filename[strlen(filename) - 1] = '\0';
	} else {
		filename = name_tok; /* alloca'd memory freed on exit */
	}
	if (confroot->files == NULL) {
		if (filename[0] == '/') {
			char *path = gsh_strdup(filename);

			if (path == NULL)
				return ENOMEM;
			confroot->conf_dir = gsh_strdup(dirname(path));
			gsh_free(path);
		} else {
			confroot->conf_dir = gsh_strdup(".");
		}
		if (confroot->conf_dir == NULL)
			return ENOMEM;
	}
	if (filename[0] == '/') {
		fullpath = gsh_strdup(filename);
		if (fullpath == NULL)
			return ENOMEM;
	} else {
		fullpath = gsh_calloc(1, (strlen(filename) +
					  strlen(confroot->conf_dir) + 2));
		if (fullpath == NULL)
			return ENOMEM;
		sprintf(fullpath, "%s/%s", confroot->conf_dir, filename);
	}
	/* loop detection */
	for (fp = confroot->files; fp != NULL; fp = fp->next) {
		if (!strcmp(fp->pathname, fullpath)) {
			rc = EINVAL;
			goto errout;
		}
	}
	bs = gsh_calloc(1, sizeof(struct bufstack));
	if (bs == NULL)
		goto errout;
	flist = gsh_calloc(1, sizeof(struct file_list));
	if (flist == NULL) {
		gsh_free(bs);
		goto errout;
	}
	flist->next = confroot->files;
	confroot->files = flist;
	flist->pathname = fullpath;
	in_file = fopen(flist->pathname, "r" );
	if (in_file == NULL) {
		/* error */
		return errno;
	}
	bs->bs = ganesha_yy_create_buffer(in_file,
					 YY_BUF_SIZE,
					 yyscanner);
	if (curbs)
		curbs->lineno = yylineno;
	bs->prev = curbs;
	bs->f = in_file;
	bs->filename = flist->pathname;
	ganesha_yy_switch_to_buffer(bs->bs, yyscanner);
	current_file = flist->pathname;
	curbs = bs;
	return 0;

errout:
	gsh_free(fullpath);
	return rc;
}

static int pop_file(struct parser_state *st)
{
	struct bufstack *bs = curbs;
	struct bufstack *prevbs;
	void *yyscanner = st->scanner;
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;

	if (bs == NULL)
		return 0;
	fclose(bs->f);
	ganesha_yy_delete_buffer(bs->bs, yyscanner);
	prevbs = bs->prev;
	gsh_free(bs);
	if (prevbs == NULL)
		return 0;
	ganesha_yy_switch_to_buffer(prevbs->bs, yyscanner);
	curbs = prevbs;
	yylineno = curbs->lineno;
	current_file = curbs->filename;
	return 1;
}
