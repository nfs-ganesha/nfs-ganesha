
########### next target ###############

SET(SemN_STAT_SRCS
   SemN.c
)

add_library(SemN STATIC ${SemN_STAT_SRCS})


########### next target ###############

SET(test_sem1_SRCS
   test_sem1.c
)

add_executable(test_sem1 EXCLUDE_FROM_ALL ${test_sem1_SRCS})

target_link_libraries(test_sem1 SemN ${CMAKE_THREAD_LIBS_INIT})


########### next target ###############

SET(test_sem2_SRCS
   test_sem2.c
)

add_executable(test_sem2 EXCLUDE_FROM_ALL ${test_sem2_SRCS})

target_link_libraries(test_sem2 SemN ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
