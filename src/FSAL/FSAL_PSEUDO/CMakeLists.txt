add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalpseudo_LIB_SRCS
   handle.c
   file.c
   xattrs.c
   pseudofs_methods.h
   main.c
   export.c
   fsal_convert.c
)

add_library(fsalpseudo SHARED ${fsalpseudo_LIB_SRCS})

target_link_libraries(fsalpseudo
  gos
)

set_target_properties(fsalpseudo PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalpseudo COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############
