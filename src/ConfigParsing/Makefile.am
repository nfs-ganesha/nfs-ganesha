AM_YFLAGS		    = --defines -pganesha_yy
AM_LFLAGS	            = -Pganesha_yy -olex.yy.c

# yacc relative depedencies
conf_lex.l: conf_yacc.h
conf_yacc.h: conf_yacc.c

# cleaning FLEX/BISON generated files
CLEANFILES = conf_yacc.h conf_yacc.c conf_lex.c

noinst_LTLIBRARIES          = libConfigParsing.la
libConfigParsing_la_SOURCES = conf_yacc.y conf_yacc.h conf_lex.l analyse.c config_parsing.c analyse.h ../include/config_parsing.h


check_PROGRAMS		    = verif_syntax test_parsing

verif_syntax_SOURCES	    = verif_syntax.c 
verif_syntax_LDADD         = libConfigParsing.la ../Log/liblog.la

test_parsing_SOURCES	    = test_parse.c
test_parsing_LDADD         =  libConfigParsing.la ../Log/liblog.la

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
