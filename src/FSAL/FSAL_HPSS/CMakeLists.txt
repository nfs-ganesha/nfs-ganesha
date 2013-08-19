add_subdirectory(HPSSclapiExt)

########### next target ###############

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

# Already set the rpath after build, don't wait for make install
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "${HPSS_PREFIX}/lib")

include_directories("${HPSS_PREFIX}/include")

SET(fsalhpss_LIB_SRCS
   fsal_convert.c
   fsal_internal.c
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
)

add_library(fsalhpss SHARED ${fsalhpss_LIB_SRCS})

target_link_libraries(fsalhpss
  ${HPSS_LIBRARIES}
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalhpss PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalhpss COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
