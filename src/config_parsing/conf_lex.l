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

/* Alias for the dirty wrap */

#define ganeshun_yyerror ganesha_yyerror

/* Our versions of parser macros */

#define YY_USER_INIT \
do { \
	BEGIN YY_INIT; \
} while (0);

#define YY_USER_ACTION \
	yylloc->first_line = yylloc->last_line = yylineno; \
	yylloc->first_column = yylloc->last_column = yycolumn + yyleng -1; \
	yycolumn += yyleng; \
	yylloc->filename = stp->current_file;

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
};

static char *filter_string(char *src, int esc);
static int new_file(char *filename,
	     struct parser_state *st);
static int pop_file(struct parser_state *st);

%}

%option nounput
%option yylineno
%option reentrant
%option bison-bridge
%option bison-locations
%option extra-type="struct parser_state *"

SPACE        [ \t\r\f]
NL           [\n]
EQUALS       "="
COMMA        ","
SEMI         ";"
LCURLY       "\{"
RCURLY       "\}"
SQUOTE       "'"
DQUOTE       "\""

INCLPATH     \/?([a-zA-Z0-9\-\.\_])+(\/[a-zA-Z0-9\-\.\_]+)*
PATHNAME     (\/[a-zA-Z0-9\-\.\_]+)
LONGPATH     (\/?[a-zA-Z0-9\-\.\_]+(\/[a-zA-Z0-9\-\.\_]+)+)
TOKEN_CHARS   [a-zA-Z0-9\._\-]+
COMMENTEXT	 #.*$
STRING_CHAR       [^\n]
ID_CHARS [a-zA-Z_][a-zA-Z0-9_\-]*

/* INCLUDE state is used for picking the name of the include file */
%START YY_INIT DEFINITION TERM QUOTE_STR LIT_STRING INCLUDE

%%
%{
	struct parser_state *stp = yyextra;
%}

<YY_INIT>"%include" { /* include file start */
	DEBUG_LEX("INCLUDE\n");
	BEGIN INCLUDE;
	/* not a token, return nothing */
}

<INCLUDE>{INCLPATH} {

	{
		int c;

		DEBUG_LEX("Calling new_file with unquoted %s\n", yytext);
		c = new_file(yytext, stp);
		if (c != 0) {
			if (c == EINVAL)
				ganeshun_yyerror(yylloc, yyscanner,
						"config file already parsed, ignored");
			else
				ganeshun_yyerror(yylloc, yyscanner,
						"error opening file");
			yyterminate();
		}
		BEGIN YY_INIT;
		DEBUG_LEX("done new file\n");
	}
}

<INCLUDE>\"{INCLPATH}\" {
	{
		int c;

		DEBUG_LEX("Calling new_file with quoted %s\n", yytext);
		c = new_file(yytext, stp);
		if (c != 0) {
			if (c == EINVAL)
				ganeshun_yyerror(yylloc, yyscanner,
						"config file already parsed, ignored");
			else
				ganeshun_yyerror(yylloc, yyscanner,
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
	stp->err_type->scan = true;
	yyterminate();
}

<<EOF>> { /* end of included file */
	DEBUG_LEX("<EOF>\n");
	if (pop_file(stp) == 0)
		yyterminate();
}

   /* Initial State.  We start with a block identifier */

<YY_INIT>{ID_CHARS} { /* first block */
	/* identifier */
	DEBUG_LEX("[block:%s]\n",yytext);
	yylval->token = gsh_strdup(yytext);
	BEGIN DEFINITION;
	return IDENTIFIER;
}

<DEFINITION>{ID_CHARS} {
	DEBUG_LEX("[id:%s",yytext);
	yylval->token = gsh_strdup(yytext);
	return IDENTIFIER;
}

{EQUALS} {
	DEBUG_LEX(" EQUALS ");
	yylval->token = gsh_strdup(yytext);
	BEGIN TERM;
	return EQUAL_OP;
}

{LCURLY} {
	DEBUG_LEX("BEGIN_BLOCK\n");
	BEGIN DEFINITION;
	stp->block_depth++;
	return LCURLY_OP;
}

{RCURLY}     {   /* end of block */
	DEBUG_LEX("END_BLOCK\n");
	if (stp->block_depth <= 0) {
		/* error */
		ganeshun_yyerror(yylloc, yyscanner,
				"closing bracket outside a block");
		stp->err_type->scan = true;
		return _ERROR_;
	} else {
		stp->block_depth --;
		if (stp->block_depth == 0) {
			BEGIN YY_INIT;
		}
	}
	return RCURLY_OP;
}

{COMMA}  { /* another terminal to follow ',' */
	DEBUG_LEX(" ',' ");
	return COMMA_OP;
}

   /* End of statement */

{SEMI}  { /* end of statement */
	DEBUG_LEX("]\n");
	BEGIN DEFINITION;
	return SEMI_OP;
}


   /* Double Quote, allows char escaping */

<TERM>{DQUOTE}  {  /* start of a double quote string */
	BEGIN QUOTE_STR;
	DEBUG_LEX("quote value:<");
}

   /* Single Quote, single line with no escaping */

<TERM>{SQUOTE}   { /* start of a single quote string */
	BEGIN LIT_STRING;
	DEBUG_LEX("lit value:<");
}

<TERM>{PATHNAME}|{LONGPATH}  { /* a POSIX pathname */
	DEBUG_LEX("pathname:%s", yytext);
	yylval->token = gsh_strdup(yytext);
	return TOK_PATH;
}

   /* Last resort terminal, gobbles anything other than white and ";" */

<TERM>{TOKEN_CHARS}  { /* start of a number or label/tag */
	DEBUG_LEX("token value:%s",yytext);
	yylval->token = gsh_strdup(yytext);
	return TOKEN;
}

   /* Double Quote, gobble chars */

<QUOTE_STR>[^\"]*\" {
	if (yytext[yyleng - 2] == '\\') {
		yyless(yyleng - 1);
		yymore();
	} else {
		yylval->token = filter_string(yytext, 1);
		if (yylval->token == NULL) {
			ganeshun_yyerror(yylloc, yyscanner, "Out of memory");
			stp->err_type->resource = true;
			return _ERROR_;
		} else if (yylval->token == (char *)-1) {
			ganeshun_yyerror(yylloc, yyscanner, "Bad literal string");
			stp->err_type->scan = true;
			return _ERROR_;
		}
		DEBUG_LEX("%s>", yylval->token);
		BEGIN TERM;
		return STRING;
	}
}

   /* Single Quote, gobble chars */

<LIT_STRING>[^\n]*\'  { /* end of single quote string */
	yylval->token =	filter_string(yytext, 0);
	if (yylval->token == NULL) {
		ganeshun_yyerror(yylloc, yyscanner, "Out of memory");
		stp->err_type->resource = true;
		return _ERROR_;
	} else if (yylval->token == (char *)-1) {
		ganeshun_yyerror(yylloc, yyscanner, "Bad literal string");
		stp->err_type->scan = true;
		return _ERROR_;
	}
	DEBUG_LEX("%s>", yylval->token);
	BEGIN TERM;
	return STRING;
}

   /* Single Quote, Throw an error if not a single line */

<LIT_STRING>\n { /* ERROR: eol within single quote string */
	ganeshun_yyerror(yylloc, yyscanner, "closing quote missing");
	stp->err_type->scan = true;
	return _ERROR_;
}

   /* Skip over stuff we don't send upstairs */

{COMMENTEXT}  ;/* ignore */
{SPACE}        ;/* ignore */
{NL}           ;/* ignore */

   /* Unrecognized chars.  Must do better... */

. { /* ERROR: out of character character */
	DEBUG_LEX("unexpected stuff (%s)\n", yytext);
	ganeshun_yyerror(yylloc, yyscanner, "Unexpected character");
	stp->err_type->scan = true;
	return _ERROR_;
}

%%

int ganeshun_yywrap(void *yyscanner){
    return 1;
}

int ganeshun_yy_init_parser(char *srcfile, struct parser_state *st)
{
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
	glist_init(&confroot->root.u.nterm.sub_nodes);
	confroot->root.type = TYPE_ROOT;
	st->root_node = confroot;
	ganeshun_yylex_init_extra(st, &st->scanner);
	return new_file(srcfile, st);

nospc:
	st->err_type->resource = true;
	return rc;
}

void ganeshun_yy_cleanup_parser(struct parser_state *st)
{
	int rc;

	if (st->curbs != NULL) {
		st->err_type->parse = true;
		while(pop_file(st) != 0);
	}
	ganeshun_yylex_destroy(st->scanner);
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
	struct bufstack *bs = NULL;
	FILE *in_file;
	YY_BUFFER_STATE inbuf;
	struct file_list *flist = NULL;
	struct file_list *fp;
	void *yyscanner = st->scanner;
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	struct config_root *confroot = st->root_node;
	char *fullpath = NULL;
	int rc = ENOMEM;
	char *filename = alloca(strlen(name_tok) + 1);

	if (*name_tok == '\"') {
		strcpy(filename, name_tok + 1);
		filename[strlen(filename) - 1] = '\0';
	} else {
		strcpy(filename, name_tok); /* alloca'd memory freed on exit */
	}
	if (confroot->files == NULL) {
		if (filename[0] == '/') {
			char *path = gsh_strdup(filename);

			if (path == NULL)
				goto errout;
			confroot->conf_dir = gsh_strdup(dirname(path));
			gsh_free(path);
		} else {
			confroot->conf_dir = gsh_strdup(".");
		}
		if (confroot->conf_dir == NULL)
			goto errout;
	}
	if (filename[0] == '/') {
		fullpath = gsh_strdup(filename);
		if (fullpath == NULL)
			goto errout;
	} else {
		fullpath = gsh_calloc(1, (strlen(filename) +
					  strlen(confroot->conf_dir) + 2));
		if (fullpath == NULL)
			goto errout;
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
	if (bs == NULL) {
		goto errout;
	}
	flist = gsh_calloc(1, sizeof(struct file_list));
	if (flist == NULL) {
		goto errout;
	}
	flist->next = confroot->files;
	flist->pathname = fullpath;
	confroot->files = flist;
	in_file = fopen(flist->pathname, "r" );
	if (in_file == NULL) {
		rc = errno;
		goto errout;
	}
	bs->bs = ganeshun_yy_create_buffer(in_file,
					 YY_BUF_SIZE,
					 yyscanner);
	if (st->curbs)
		st->curbs->lineno = yylineno;
	bs->prev = st->curbs;
	bs->f = in_file;
	bs->filename = flist->pathname;
	ganeshun_yy_switch_to_buffer(bs->bs, yyscanner);
	st->current_file = flist->pathname;
	st->curbs = bs;
	return 0;

errout:
	if (rc == ENOMEM)
		st->err_type->resource = true;
	else
		st->err_type->invalid = true;
	if (flist != NULL)
		gsh_free(flist);
	if (bs != NULL)
		gsh_free(bs);
	if (fullpath != NULL)
		gsh_free(fullpath);
	return rc;
}

static int pop_file(struct parser_state *st)
{
	struct bufstack *bs = st->curbs;
	struct bufstack *prevbs;
	void *yyscanner = st->scanner;
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;

	if (bs == NULL)
		return 0;
	fclose(bs->f);
	ganeshun_yy_delete_buffer(bs->bs, yyscanner);
	prevbs = bs->prev;
	st->curbs = prevbs;
	gsh_free(bs);
	if (prevbs == NULL)
		return 0;
	ganeshun_yy_switch_to_buffer(prevbs->bs, yyscanner);
	yylineno = st->curbs->lineno;
	st->current_file = st->curbs->filename;
	return 1;
}
