%{

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#include "config.h"
#include "analyse.h"
#include "conf_yacc.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <libgen.h>

#if HAVE_STRING_H
#   include <string.h>
#endif

/* Numero de la ligne courante */
int ganesha_yylineno;

/* nombre d'accolades imbriquees */
int accolades;

/* Traitement des messages d'erreur */
void set_error(char * s);

#define ERRLEN 1024
char err_str[ERRLEN]="";

/* Stockage des chaines
*/
char YY_PARSED_STRING[MAXSTRLEN];

void YY_BUFFER_APPEND(char * s){
    strncat( YY_PARSED_STRING,s, MAXSTRLEN - strlen(s));
}

void YY_BUFFER_RESET(void){
    int i;
    for (i=0;i<MAXSTRLEN;i++){
        YY_PARSED_STRING[i]='\0';
    }
}

/* includes management */
#define FILE_LEN 1024
char current_file[FILE_LEN] = "";

#define MAX_INCLUDE_DEPTH  10
YY_BUFFER_STATE include_stack[MAX_INCLUDE_DEPTH];

/* keep track of filenames and line numbers */
unsigned int    lines_stack[MAX_INCLUDE_DEPTH];
char            files_stack[MAX_INCLUDE_DEPTH][FILE_LEN];

int include_stack_index = 0;


/* initialisation du parser */

#define YY_USER_INIT {          \
    unsigned int i;             \
    ganesha_yylineno = 1;               \
    accolades = 0;              \
    include_stack_index = 0;    \
    for ( i=0; i<MAX_INCLUDE_DEPTH; i++) {\
        lines_stack[i]=0;       \
        files_stack[i][0]='\0'; \
    }\
    BEGIN YY_INIT;\
}

#ifdef _DEBUG_PARSING
#define DEBUG_LEX   printf
#else
#define DEBUG_LEX
#endif



%}


SPACE        [ \t\r\f]
NL           [\n]
VAL_CHAR     [^ \t\r\n\f;"'#]
COMMENTAIRE	 #.*$
/* lettre posant probleme dans une chaine */
STRING_CHAR       [^\n]
/* comment est compose un identifiant */
LETTER          [a-zA-Z_.]
IDENTIFIER_CHAR    [a-zA-Z0-9_.\-]

/* INCLUDE state is used for picking the name of the include file */
%START  YY_INIT BLOC INBLOC AFFECT_OR_SUB_BLOCK VALUE ENDVALUE  STRING1 STRING2 ESC1 INCLUDE INCL_STRING INCL_ESC

%%


<YY_INIT>"%include" {/* debut de lecture d'un fichier d'include */
                        DEBUG_LEX("INCLUDE\n");
                        BEGIN INCLUDE;
                        /* not a token, return nothing */
                     }

<INCLUDE>"\""       { /* start include file name */
                      BEGIN INCL_STRING;
                      DEBUG_LEX("file:<");
                      YY_BUFFER_RESET();
                    }

<INCL_STRING>\\     {BEGIN INCL_ESC;}

<INCL_STRING>"\""   { /* include file read */
                        unsigned int i;
                        char new_file_path[FILE_LEN];
                        DEBUG_LEX(">");

                        if ( include_stack_index >= MAX_INCLUDE_DEPTH )
                        {
                           /* error */
                           snprintf(err_str,ERRLEN,"in \"%s\", line %d: includes nested too deeply",current_file, ganesha_yylineno);
                           set_error(err_str);
                           return _ERROR_;
                        }

                        include_stack[include_stack_index] = YY_CURRENT_BUFFER;
                        lines_stack[include_stack_index] = ganesha_yylineno;
                        strncpy( files_stack[include_stack_index], current_file, FILE_LEN );

                        /* relative path management */

                        /* 1) if the new path is absolute, nothing to do
                         * 2) if there was no '/' in previous dir, the new path
                         *  is relative to the current dir.
                         */
                        if ( ( YY_PARSED_STRING[0] == '/' )
                            || ( strchr( current_file, '/') == NULL ) )
                        {
                            strncpy( new_file_path, YY_PARSED_STRING, FILE_LEN );
                        }
                        else
                        {
                            /* in any other case, path is relative to the current config file
                             * directory */

                            char tmp_buf[FILE_LEN];
                            char * path;

                            strncpy( tmp_buf, current_file, FILE_LEN );

                            path = dirname( tmp_buf );

                            snprintf( new_file_path, FILE_LEN, "%s/%s", path, YY_PARSED_STRING );
                        }

                        /* loop detection */

                        for ( i = 0; i <= include_stack_index; i++ )
                        {
                            if (!strncmp(files_stack[i], new_file_path, FILE_LEN))
                            {
                               snprintf(err_str,ERRLEN,"in \"%s\", line %d: include loop detected: \"%s\" already parsed",current_file, ganesha_yylineno, new_file_path );
                               set_error(err_str);
                               return _ERROR_;
                            }
                        }

                        include_stack_index ++;

                        ganesha_yyin = fopen( new_file_path, "r" );

                        if ( ganesha_yyin == NULL )
                        {
                           /* error */
                           snprintf(err_str,ERRLEN,"in \"%s\", line %d: error %d opening file \"%s\": %s",current_file,ganesha_yylineno,
                                    errno, new_file_path, strerror(errno) );
                           set_error(err_str);
                           return _ERROR_;
                        }

                        ganesha_yylineno = 1;
                        strncpy( current_file, new_file_path, FILE_LEN );

                        /* change current buffer */
                        ganesha_yy_switch_to_buffer( ganesha_yy_create_buffer( ganesha_yyin, YY_BUF_SIZE ) );

                        /* start reading file from scratch */
                        BEGIN YY_INIT;

                    }


<INCL_STRING>\n      {
                            snprintf(err_str,ERRLEN,"in \"%s\", line %d: missing closing quote.",current_file, ganesha_yylineno);
                            set_error(err_str);
                            ganesha_yylineno++;
                            return _ERROR_;
                     }

<INCL_STRING>.      {YY_BUFFER_APPEND(ganesha_yytext); DEBUG_LEX("%c",*ganesha_yytext);/* caractere du fichier */}

<INCL_ESC>\n        {BEGIN INCL_STRING; ganesha_yylineno++;}/* ignore un saut de ligne echappe*/
<INCL_ESC>.         {DEBUG_LEX("%c",*ganesha_yytext);YY_BUFFER_APPEND(ganesha_yytext);BEGIN INCL_STRING;/* caractere du fichier */}


<<EOF>> { /* end of included file */
            DEBUG_LEX("<EOF>\n");

            include_stack_index --;

            if ( include_stack_index < 0 )
            {
                /* eof of all streams */
                yyterminate();
            }
            else
            {
                fclose(ganesha_yyin);
                /*go down into stack */
                ganesha_yy_delete_buffer( YY_CURRENT_BUFFER );

                ganesha_yylineno = lines_stack[include_stack_index];
                strncpy( current_file, files_stack[include_stack_index], FILE_LEN );

                ganesha_yy_switch_to_buffer( include_stack[include_stack_index] );
            }
        }



<YY_INIT>{LETTER}({IDENTIFIER_CHAR})* {
                    /* identifier */
                    DEBUG_LEX("[bloc:%s]\n",ganesha_yytext);
                    strncpy(ganesha_yylval.str_val,ganesha_yytext,MAXSTRLEN);
                    BEGIN BLOC;
                    return IDENTIFIER;
                 }


<BLOC>"{"        {/* debut de bloc */
                        DEBUG_LEX("BEGIN_BLOCK\n",ganesha_yytext);
                        BEGIN INBLOC;
                        accolades++;
                        return BEGIN_BLOCK;
                 }

<INBLOC>{LETTER}({IDENTIFIER_CHAR})* {
                    /* identifier */
                    DEBUG_LEX("[id:%s",ganesha_yytext);
                    strncpy(ganesha_yylval.str_val,ganesha_yytext,MAXSTRLEN);
                    BEGIN AFFECT_OR_SUB_BLOCK;
                    return IDENTIFIER;
                }

<INBLOC>"}"     {   /* end of block */
                    DEBUG_LEX("END_BLOCK\n",ganesha_yytext);

                    if ( accolades <= 0 )
                    {
                       /* error */
                       snprintf(err_str,ERRLEN,"in \"%s\", line %d: '%c' closing bracket outside a block",current_file,ganesha_yylineno,*ganesha_yytext);
                       set_error(err_str);
                       return _ERROR_;
                    }
                    else
                        accolades --;

                    if ( accolades == 0 )
                    {
                        BEGIN YY_INIT;
                        return END_BLOCK;
                    }
                    else
                    {
                        BEGIN INBLOC;
                        return END_SUB_BLOCK;
                    }

                }

<AFFECT_OR_SUB_BLOCK>"="  {
                    /* affectation */
                    DEBUG_LEX(" , ",ganesha_yytext);
                    BEGIN VALUE;
                    return AFFECTATION;
                }

<AFFECT_OR_SUB_BLOCK>"{"    {
                                /* sub-block */
                                DEBUG_LEX("\nBEGIN_SUB_BLOCK\n",ganesha_yytext);
                                BEGIN INBLOC;
                                accolades++;
                                return BEGIN_SUB_BLOCK;
                            }

<VALUE>"\""           {BEGIN STRING1;DEBUG_LEX("value:<");YY_BUFFER_RESET();} /* ouverture string 1 */
<VALUE>"'"            {BEGIN STRING2;DEBUG_LEX("value:<");YY_BUFFER_RESET();} /* ouverture string 2 */
<VALUE>({VAL_CHAR})+  {/* valeur */DEBUG_LEX("value:%s",ganesha_yytext);BEGIN ENDVALUE;strncpy(ganesha_yylval.str_val,ganesha_yytext,MAXSTRLEN); return KEYVALUE;}

<ENDVALUE>";"         {DEBUG_LEX("]\n");BEGIN INBLOC;return END_AFFECT;}

<STRING1>\\     {BEGIN ESC1;}
<STRING1>"\""   {DEBUG_LEX(">");strncpy(ganesha_yylval.str_val,YY_PARSED_STRING,MAXSTRLEN);BEGIN ENDVALUE;/* chaine finie */ return KEYVALUE;}
<STRING1>\n      {snprintf(err_str,ERRLEN,"in \"%s\", line %d: missing closing quote.",current_file,ganesha_yylineno); set_error(err_str);ganesha_yylineno++;return _ERROR_;}
<STRING1>.      {YY_BUFFER_APPEND(ganesha_yytext); DEBUG_LEX("%c",*ganesha_yytext);/* caractere de la chaine */}

<ESC1>\n        {BEGIN STRING1;ganesha_yylineno++;}/* ignore un saut de ligne echappe*/
<ESC1>.         {DEBUG_LEX("%c",*ganesha_yytext);YY_BUFFER_APPEND(ganesha_yytext);BEGIN STRING1;/* caractere de la chaine */}

<STRING2>"'"    {DEBUG_LEX(">");strncpy(ganesha_yylval.str_val,YY_PARSED_STRING,MAXSTRLEN);BEGIN ENDVALUE;/* chaine finie */ return KEYVALUE;}
<STRING2>\n     {snprintf(err_str,ERRLEN,"in \"%s\", line %d: closing quote missing.",current_file,ganesha_yylineno); set_error(err_str);ganesha_yylineno++;return _ERROR_;}
<STRING2>.      {YY_BUFFER_APPEND(ganesha_yytext);DEBUG_LEX("%c",*ganesha_yytext);/* caractere de la chaine */}

{COMMENTAIRE}  ;/* ignore */
{SPACE}        ;/* ignore */
{NL}           ganesha_yylineno++;/* ignore */

. { snprintf(err_str,ERRLEN,"in \"%s\", line %d: '%c' unexpected",current_file,ganesha_yylineno,*ganesha_yytext); set_error(err_str);return _ERROR_;}

%%

int ganesha_yywrap(void){
    return 1;
}

void ganesha_yyreset(void){
    YY_FLUSH_BUFFER;
    YY_USER_INIT;
}

void ganesha_yy_set_current_file( char * file )
{
    strncpy( current_file, file, FILE_LEN );
}
