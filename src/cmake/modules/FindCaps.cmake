# Tries to find Capabilities libraries
#
# Usage of this module as follows:
#
#     find_package(Caps)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  CAPS_PREFIX  Set this variable to the root installation of
#                       Caps if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  CAPS_FOUND              System has Caps libs/headers
#  CAPS_LIBRARIES          The Caps libraries (tcmalloc & profiler)
#  CAPS_INCLUDE_DIR        The location of Caps headers

find_library(CAPS NAMES cap PATHS "${CAPS_PREFIX}")
check_library_exists(
	cap
	cap_set_proc
	""
	HAVE_SET_PROC
	)

find_path(CAPS_INCLUDE_DIR NAMES sys/capability.h HINTS ${CAPS_PREFIX}/include)

if (HAVE_SET_PROC)
  set(CAPS_LIBRARIES ${CAPS})
endif (HAVE_SET_PROC)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Caps
  DEFAULT_MSG
  CAPS_LIBRARIES
  CAPS_INCLUDE_DIR)

mark_as_advanced(
  CAPS_PREFIX
  CAPS_LIBRARIES
  CAPS_INCLUDE_DIR)
