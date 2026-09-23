########### next target ###############

set(CMAKE_CXX_STANDARD 17)

SET(ganesha_monitoring_SRCS
  prometheus_exposer.cc
  dynamic_metrics.cc
)

add_library(ganesha_monitoring SHARED ${ganesha_monitoring_SRCS})

if(LINUX)
    find_library(PROCPS_LIB procps)
    if(PROCPS_LIB)
        target_link_libraries(ganesha_monitoring ${PROCPS_LIB})
        add_definitions(-DHAVE_PROCPS)
    else()
        message(WARNING "procps library not found; please install procps lib for ganesha resource related metrics")
    endif()
else()
    # On non-Linux platforms, skip procps and warning
    message(STATUS "Non-Linux platform detected; skipping procps library check")
endif()


add_sanitizers(ganesha_monitoring)

if(USE_LTTNG)
  add_dependencies(ganesha_monitoring ntirpc_generate_lttng_trace_headers)
  add_dependencies(ganesha_monitoring gsh_generate_lttng_trace_headers)
endif(USE_LTTNG)

set_target_properties(ganesha_monitoring PROPERTIES COMPILE_FLAGS "-fPIC")
target_include_directories(ganesha_monitoring     PRIVATE ${PROJECT_SOURCE_DIR}/libntirpc/src/monitoring/prometheus-cpp-lite/core/include)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic-errors -Werror -Wall -Wextra")

install(TARGETS ganesha_monitoring LIBRARY DESTINATION ${LIB_INSTALL_DIR})

########### install files ###############
