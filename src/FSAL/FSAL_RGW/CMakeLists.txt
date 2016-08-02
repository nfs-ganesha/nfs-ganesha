add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsalrgw_LIB_SRCS
   main.c
   export.c
   handle.c
   internal.c
   internal.h
)

message("RGW_INCLUDE_DIR ${RGW_INCLUDE_DIR}")
include_directories(${RGW_INCLUDE_DIR})

add_library(fsalrgw SHARED ${fsalrgw_LIB_SRCS})

target_link_libraries(fsalrgw ${RGW_LIBRARIES} ${SYSTEM_LIBRARIES})

set_target_properties(fsalrgw PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalrgw COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
