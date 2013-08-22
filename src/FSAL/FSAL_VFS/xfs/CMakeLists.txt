add_definitions( -D__USE_GNU -D_GNU_SOURCE)

SET(xfsfdhdl_LIB_SRCS handle.c)

add_library(xfsfdhdl SHARED ${xfsfdhdl_LIB_SRCS})
target_link_libraries(xfsfdhdl handle)
install(TARGETS xfsfdhdl COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

