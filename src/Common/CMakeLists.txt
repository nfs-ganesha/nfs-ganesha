
########### next target ###############

SET(common_utils_STAT_SRCS
   common_utils.c
)

add_library(common_utils STATIC ${common_utils_STAT_SRCS})

target_link_libraries(common_utils
  ${CMAKE_THREAD_LIBS_INIT}
)

########### install files ###############
