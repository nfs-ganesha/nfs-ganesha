include_directories(
  ${DBUS_INCLUDE_DIRS}
)

########### next target ###############

if(FREEBSD)
   set (FBSD_SUBR "${PROJECT_SOURCE_DIR}/os/freebsd/memstream.c")
endif(FREEBSD)

SET(gshdbus_STAT_SRCS
   dbus_server.c
   properties_handler.c
   signal_handler.c
   ${FBSD_SUBR}
)

add_library(gshdbus STATIC ${gshdbus_STAT_SRCS})


########### install files ###############
