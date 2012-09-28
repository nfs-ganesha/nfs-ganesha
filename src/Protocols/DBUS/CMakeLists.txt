include_directories(
  ${DBUS_INCLUDE_DIR}
  ${DBUS_ARCH_INCLUDE_DIR}
)

########### next target ###############

SET(gshdbus_STAT_SRCS
   dbus_server.c
)

add_library(gshdbus STATIC ${gshdbus_STAT_SRCS})


########### install files ###############
