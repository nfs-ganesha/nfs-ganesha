########### next target ###############

set(CMAKE_CXX_STANDARD 17)

SET(gmonitoring_SRCS
  exposer.cc
  monitoring.cc
)

add_library(gmonitoring SHARED ${gmonitoring_SRCS})
add_sanitizers(gmonitoring)
set_target_properties(gmonitoring PROPERTIES COMPILE_FLAGS "-fPIC")
target_include_directories(gmonitoring     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/prometheus-cpp-lite/core/include)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic-errors -Werror -Wall")

install(TARGETS gmonitoring LIBRARY DESTINATION ${LIB_INSTALL_DIR})

########### install files ###############
