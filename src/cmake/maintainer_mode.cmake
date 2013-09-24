SET( CMAKE_CXX_FLAGS_MAINTAINER "-Wall -Wabi" CACHE STRING
    "Flags used by the C++ compiler during maintainer builds."
    FORCE )
SET( CMAKE_C_FLAGS_MAINTAINER "-Werror -Wall -Wimplicit -Wformat -Wmissing-braces -Wreturn-type -Wunused-variable -Wuninitialized -Wno-pointer-sign"
  CACHE STRING
    "Flags used by the C compiler during maintainer builds."
    FORCE )

# These ultimately must be conditional, and of course are also toolchain
# specific (*sigh*).  Maybe at some point we should build up a token list and
# then expand it into the linker args (and likewise other fixed strings here)
IF (GOLD_LINKER)
  SET(WARN_ONCE "")
ELSE(GOLD_LINKER)
  SET(WARN_ONCE ",--warn-once")
ENDIF(GOLD_LINKER)

SET( CMAKE_EXE_LINKER_FLAGS_MAINTAINER
    "-Wl,--warn-unresolved-symbols" CACHE STRING
    "Flags used for linking binaries during maintainer builds."
    FORCE )
SET( CMAKE_SHARED_LINKER_FLAGS_MAINTAINER
    "-Wl,--warn-unresolved-symbols${WARN_ONCE}" CACHE STRING
    "Flags used by the shared libraries linker during maintainer builds."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_MAINTAINER
    CMAKE_C_FLAGS_MAINTAINER
    CMAKE_EXE_LINKER_FLAGS_MAINTAINER
    CMAKE_SHARED_LINKER_FLAGS_MAINTAINER )

SET(ALLOWED_BUILD_TYPES None Debug Release RelWithDebInfo MinSizeRel Maintainer)
STRING(REGEX REPLACE ";" " " ALLOWED_BUILD_TYPES_PRETTY "${ALLOWED_BUILD_TYPES}")

# Update the documentation string of CMAKE_BUILD_TYPE for GUIs
SET( CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
    "Choose the type of build, options are: ${ALLOWED_BUILD_TYPES_PRETTY}."
    FORCE )

if( CMAKE_BUILD_TYPE STREQUAL "" )
	message( WARNING "CMAKE_BUILD_TYPE is not set, defaulting to Debug" )
	set( CMAKE_BUILD_TYPE "Debug" )
endif( CMAKE_BUILD_TYPE STREQUAL "" )

list(FIND ALLOWED_BUILD_TYPES ${CMAKE_BUILD_TYPE} BUILD_TYPE_INDEX)

if (BUILD_TYPE_INDEX EQUAL -1)
	message(SEND_ERROR "${CMAKE_BUILD_TYPE} is not a valid build type.")
endif()
