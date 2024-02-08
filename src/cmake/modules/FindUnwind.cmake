# FindUnwind.cmake

if(UNWIND_PATH_HINT)
  message(STATUS "Using UNWIND_PATH_HINT: ${UNWIND_PATH_HINT}")
else()
  set(UNWIND_PATH_HINT)
endif()

find_path(UNWIND_INCLUDE_DIR
  NAMES libunwind.h
  PATHS ${UNWIND_PATH_HINT}
  PATH_SUFFIXES include
  DOC "The libunwind include directory")

find_library(UNWIND_LIBRARY
  NAMES unwind
  PATHS ${UNWIND_PATH_HINT}
  PATH_SUFFIXES lib lib64
  DOC "The libunwind library")

set(UNWIND_LIBRARIES ${UNWIND_LIBRARY})

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Unwind REQUIRED_VARS UNWIND_LIBRARY UNWIND_INCLUDE_DIR)

mark_as_advanced(UNWIND_INCLUDE_DIR UNWIND_LIBRARY)

