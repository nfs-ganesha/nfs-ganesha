AM_YFLAGS                   = --defines

# yacc relative depedencies
Connectathon_parser_lex.l: Connectathon_parser_yacc.h
Connectathon_parser_yacc.h: Connectathon_parser_yacc.c

# cleaning FLEX/BISON generated files
CLEANFILES = Connectathon_parser_yacc.h Connectathon_parser_yacc.c Connectathon_parser_lex.c

noinst_LTLIBRARIES = libConnectathon_ConfigParsing.la
libConnectathon_ConfigParsing_la_SOURCES = Connectathon_parser_yacc.y Connectathon_parser_yacc.h Connectathon_parser_lex.l Connectathon_config_parsing.c 

check_PROGRAMS = Connectathon_test_parse Connectathon_verif_syntax
Connectathon_test_parse_SOURCES = Connectathon_test_parse.c 
Connectathon_test_parse_LDADD = libConnectathon_ConfigParsing.la


Connectathon_verif_syntax_SOURCES = Connectathon_verif_syntax.c 
Connectathon_verif_syntax_LDADD = libConnectathon_ConfigParsing.la

new: clean all

