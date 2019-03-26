add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalpseudo_LIB_SRCS
   handle.c
   pseudofs_methods.h
   main.c
   export.c
)

add_library(fsalpseudo OBJECT ${fsalpseudo_LIB_SRCS})
add_sanitizers(fsalpseudo)
set_target_properties(fsalpseudo PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
