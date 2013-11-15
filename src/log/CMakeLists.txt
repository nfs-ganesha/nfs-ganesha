add_definitions(
  -D__USE_GNU
  -D _GNU_SOURCE
)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

########### next target ###############

SET(log_STAT_SRCS
   display.c
   log_functions.c
)

add_library(log STATIC ${log_STAT_SRCS})


########### next target ###############

SET(test_liblog_SRCS
   test_display.c
   test_liblog_functions.c
)

add_executable(test_liblog EXCLUDE_FROM_ALL ${test_liblog_SRCS})

target_link_libraries(test_liblog log ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
