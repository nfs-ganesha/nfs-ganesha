# Tries to find GTest.
#
# Usage of this module as follows:
#
#     find_package(GTest)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  GTEST_PREFIX  Set this variable to the root installation of
#                       GTest if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  GTEST_FOUND              System has GTest libs/headers
#  GTEST_LIBRARIES          The GTest libraries (tcmalloc & profiler)
#  GTEST_INCLUDE_DIR        The location of GTest headers

find_library(GTEST NAMES gtest PATHS "${GTEST_PREFIX}")
find_library(GTEST_MAIN NAMES gtest_main PATHS "${GTEST_PREFIX}")

find_path(GTEST_INCLUDE_DIR NAMES gtest/gtest.h HINTS ${GTEST_PREFIX}/include)

set(GTEST_LIBRARIES ${GTEST} ${GTEST_MAIN})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  GTest
  DEFAULT_MSG
  GTEST_LIBRARIES
  GTEST_INCLUDE_DIR)

mark_as_advanced(
  GTEST_PREFIX
  GTEST_LIBRARIES
  GTEST_INCLUDE_DIR)
