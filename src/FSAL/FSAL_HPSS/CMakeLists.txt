add_subdirectory(HPSSclapiExt)

########### next target ###############

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

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
  hpssapiext
  ${HPSS_LIBRARY}
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalhpss PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalhpss COMPONENT fsal DESTINATION lib)
