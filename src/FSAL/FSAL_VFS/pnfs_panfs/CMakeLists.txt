# Panfs specific code

set(pnfs_panfs_LIB_SRCS
  mds.c
  panfs_um_pnfs.c
)

add_library(pnfs_panfs STATIC ${pnfs_panfs_LIB_SRCS})

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(pnfs_panfs PROPERTIES COMPILE_FLAGS "-fPIC")
