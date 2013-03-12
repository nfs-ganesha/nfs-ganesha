
########### next target ###############

SET(outils_profiling_STAT_SRCS
   MesureTemps.c
)

add_library(outils_profiling STATIC ${outils_profiling_STAT_SRCS})


########### next target ###############

SET(test_avl_SRCS
   test_avl.c
)

add_executable(test_avl EXCLUDE_FROM_ALL ${test_avl_SRCS})

target_link_libraries(test_avl ${CMAKE_THREAD_LIBS_INIT})


########### next target ###############

SET(test_mh_avl_SRCS
   test_mh_avl.c
   ../support/murmur3.c
)

add_executable(test_mh_avl EXCLUDE_FROM_ALL ${test_mh_avl_SRCS})

target_link_libraries(test_mh_avl ${CMAKE_THREAD_LIBS_INIT})


########### next target ###############

SET(test_mesure_temps_SRCS
   test_mesure_temps.c
)

add_executable(test_mesure_temps EXCLUDE_FROM_ALL ${test_mesure_temps_SRCS})

target_link_libraries(test_mesure_temps outils_profiling ${CMAKE_THREAD_LIBS_INIT})


########### next target ###############

SET(test_glist_SRCS
   test_glist.c
)

add_executable(test_glist EXCLUDE_FROM_ALL ${test_glist_SRCS})

target_link_libraries(test_glist ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
