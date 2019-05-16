# Tries to find CUnit
#
# Usage of this module as follows:
#
#     find_package(CUnit)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  CUNIT_PREFIX  Set this variable to the root installation of
#                       CUnit if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  CUNIT_FOUND              System has CUnit libs/headers
#  CUNIT_LIBRARIES          The CUnit libraries (tcmalloc & profiler)
#  CUNIT_INCLUDE_DIR        The location of CUnit headers

find_library(CUNIT_LIBRARIES NAMES cunit PATHS "${CUNIT_PREFIX}")

find_path(CUNIT_INCLUDE_DIR NAMES CUnit/Basic.h HINTS ${CUNIT_PREFIX}/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  CUnit
  DEFAULT_MSG
  CUNIT_LIBRARIES
  CUNIT_INCLUDE_DIR)

mark_as_advanced(
  CUNIT_PREFIX
  CUNIT_LIBRARIES
  CUNIT_INCLUDE_DIR)
