# Try to find a sufficiently recent wbclient

if(SAMBA4_PREFIX)
  set(SAMBA4_INCLUDE_DIRS ${SAMBA4_PREFIX}/include)
  set(SAMBA4_LIBRARIES ${SAMBA4_PREFIX}/lib${LIB_SUFFIX})
endif()

if(NOT WIN32)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(_WBCLIENT_PC QUIET wbclient)
  endif(PKG_CONFIG_FOUND)
endif(NOT WIN32)

find_path(WBCLIENT_INCLUDE_DIR wbclient.h
  ${_WBCLIENT_PC_INCLUDE_DIRS}
  ${SAMBA4_INCLUDE_DIRS}
  /usr/include
  /usr/local/include
  )

find_library(WBCLIENT_LIBRARIES NAMES wbclient
  PATHS
  ${_WBCLIENT_PC_LIBDIR}
  )

check_library_exists(
  wbclient
  wbcLookupSids
  ${WBCLIENT_LIBRARIES}
  WBCLIENT_LIB_OK
  )

# the stdint and stdbool includes are required (silly Cmake)
if(WBCLIENT_LIB_OK)
  LIST(APPEND CMAKE_REQUIRED_INCLUDES ${WBCLIENT_INCLUDE_DIR})
  check_include_files("stdint.h;stdbool.h;wbclient.h" WBCLIENT_H)
endif(WBCLIENT_LIB_OK)

# now see if this is a winbind 4 header
if(WBCLIENT_H)
  check_c_source_compiles("
/* do the enum */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <wbclient.h>

int main(void)
{
  enum wbcAuthUserLevel level = WBC_AUTH_USER_LEVEL_PAC;
  return (0);
}" WBCLIENT4_H)
endif(WBCLIENT_H)

if(WBCLIENT4_H)
  set(WBCLIENT_FOUND 1)
  message(STATUS "Found Winbind4 client: ${WBCLIENT_LIB}")
else(WBCLIENT4_H)
  if(WBclient_FIND_REQUIRED)
    message(FATAL_ERROR "Winbind4 client not found ${SAMBA4_PREFIX}/lib")
  else(WBclient_FIND_REQUIRED)
    message(STATUS "Winbind4 client not found ${SAMBA4_PREFIX}/lib")
  endif(WBclient_FIND_REQUIRED)
endif(WBCLIENT4_H)
