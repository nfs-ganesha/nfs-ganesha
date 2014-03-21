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
)


########### install files ###############
