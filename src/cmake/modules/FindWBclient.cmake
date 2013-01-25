# Try to find a sufficiently recent wbclient

if(SAMBA4_PREFIX)
  set(SAMBA4_PREFIX ${SAMBA4_PREFIX} CACHE PATH "Path to Samba4 installation")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${SAMBA4_PREFIX}/include")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -L${SAMBA4_PREFIX}/lib")
endif()

# XXX per Lieb, these entry-point checks are unreliable ATM
find_library(WBCLIENT_LIB wbclient)
check_library_exists(
  wbclient
  wbcLookupSids
  "${SAMBA4_PREFIX}/lib"
  WBCLIENT_LIB
  )
check_library_exists(
  wbclient
  wbcLookupSids
  ""
  WBCLIENT_LIB
  )

# the stdint and stdbool includes are required (silly Cmake)
check_include_files("stdint.h;stdbool.h;wbclient.h" WBCLIENT_H)

# XXX this check is doing the heavy lifting
if(WBCLIENT_H)
  check_c_source_compiles("
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

if(WBCLIENT_LIB AND WBCLIENT4_H)
  set(WBCLIENT_FOUND 1)
  set(SYSTEM_LIBRARIES ${WBCLIENT_LIB} ${SYSTEM_LIBRARIES})
  message(STATUS "Found Winbind4 client: ${WBCLIENT_LIB}")
else(WBCLIENT_LIB AND WBCLIENT4_H)
  message(STATUS "Winbind4 client not found ${SAMBA4_PREFIX}/lib")
endif(WBCLIENT_LIB AND WBCLIENT4_H)
