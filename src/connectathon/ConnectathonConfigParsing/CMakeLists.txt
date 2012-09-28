
########### next target ###############

SET(Connectathon_ConfigParsing_STAT_SRCS
   Connectathon_parser_yacc.y
   Connectathon_parser_yacc.h
   Connectathon_parser_lex.l
   Connectathon_config_parsing.c
)

add_library(Connectathon_ConfigParsing STATIC ${Connectathon_ConfigParsing_STAT_SRCS})


########### next target ###############

SET(Connectathon_test_parse_SRCS
   Connectathon_test_parse.c
)

add_executable(Connectathon_test_parse ${Connectathon_test_parse_SRCS})

target_link_libraries(Connectathon_test_parse Connectathon_ConfigParsing)


########### next target ###############

SET(Connectathon_verif_syntax_SRCS
   Connectathon_verif_syntax.c
)

add_executable(Connectathon_verif_syntax ${Connectathon_verif_syntax_SRCS})

target_link_libraries(Connectathon_verif_syntax Connectathon_ConfigParsing)


########### install files ###############




#original Makefile.am contents follow:

#AM_YFLAGS                   = --defines
#
## yacc relative depedencies
#Connectathon_parser_lex.l: Connectathon_parser_yacc.h
#Connectathon_parser_yacc.h: Connectathon_parser_yacc.c
#
## cleaning FLEX/BISON generated files
#CLEANFILES = Connectathon_parser_yacc.h Connectathon_parser_yacc.c Connectathon_parser_lex.c
#
#noinst_LTLIBRARIES = libConnectathon_ConfigParsing.la
#libConnectathon_ConfigParsing_la_SOURCES = Connectathon_parser_yacc.y Connectathon_parser_yacc.h Connectathon_parser_lex.l Connectathon_config_parsing.c 
#
#check_PROGRAMS = Connectathon_test_parse Connectathon_verif_syntax
#Connectathon_test_parse_SOURCES = Connectathon_test_parse.c 
#Connectathon_test_parse_LDADD = libConnectathon_ConfigParsing.la
#
#
#Connectathon_verif_syntax_SOURCES = Connectathon_verif_syntax.c 
#Connectathon_verif_syntax_LDADD = libConnectathon_ConfigParsing.la
#
#new: clean all
#
