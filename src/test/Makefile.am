noinst_LTLIBRARIES            = liboutils_profiling.la 

check_PROGRAMS                = test_mesure_temps test_glist test_access_list_types

liboutils_profiling_la_SOURCES = MesureTemps.c ../include/MesureTemps.h

test_mesure_temps_SOURCES    = test_mesure_temps.c
test_mesure_temps_LDADD      = liboutils_profiling.la
test_glist_SOURCES           = test_glist.c 
test_access_list_types_SOURCES = test_access_list_types.c

