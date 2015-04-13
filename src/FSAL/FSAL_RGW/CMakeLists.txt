add_definitions(
  -D_FILE_OFFSET_BITS=64
)

########### next target ###############

# Build source locations and parameters
IF(RGW_PREFIX)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${RGW_PREFIX}/include")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -L${RGW_PREFIX}/lib")
ELSE()
  message(WARNING "no rgw prefix")
ENDIF()

SET(fsalrgw_LIB_SRCS
   main.c
   export.c
   handle.c
   internal.c
   internal.h
)

add_library(fsalrgw SHARED ${fsalrgw_LIB_SRCS})

target_link_libraries(fsalrgw rgw ${SYSTEM_LIBRARIES})

set_target_properties(fsalrgw PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalrgw COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
