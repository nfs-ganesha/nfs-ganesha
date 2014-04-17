# cmake rule for GCC thread-sanitizer (tsan).
#
# Make sure gcc 4.8 (or up) is installed and configured.
#
# To use a GCC different from the default version, set the env vars.
# For example:
#
#	  $ export CC=/opt/gcc-4.8.2/bin/gcc
#	  $ export CXX=/opt/gcc-4.8.2/bin/g++
#

if (USE_TSAN)
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE
                  GCC_VERSION)
  if (GCC_VERSION VERSION_LESS 4.8)
    message(FATAL_ERROR "thread-sanitizer is not supported by GCC ${GCC_VERSION}")
  endif()

  message(STATUS "thread-sanitizer powered by GCC ${GCC_VERSION}")

  set(TSAN_C_FLAGS "-fsanitize=thread -fPIE")
  set(TSAN_CXX_FLAGS "-fsanitize=thread -fPIE")

  set(TSAN_EXE_LINKER_FLAGS "-fsanitize=thread -pie")
  set(TSAN_SHARED_LINKER_FLAGS "-fsanitize=thread -pie")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TSAN_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_CXX_FLAGS}")

  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_EXE_LINKER_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS
      "${CMAKE_SHARED_LINKER_FLAGS} ${TSAN_SHARED_LINKER_FLAGS}")
endif()

# vim:expandtab:shiftwidth=2:tabstop=2:
