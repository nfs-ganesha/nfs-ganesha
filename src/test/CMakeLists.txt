
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

SET(test_glist_SRCS
   test_glist.c
)

add_executable(test_glist EXCLUDE_FROM_ALL ${test_glist_SRCS})

target_link_libraries(test_glist ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
