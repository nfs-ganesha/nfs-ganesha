
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  if(${CMAKE_CXX_COMPILER_ID} MATCHES "MSVC")
    set(MSVC ON)
  endif(${CMAKE_CXX_COMPILER_ID} MATCHES "MSVC")
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

if(UNIX)

  execute_process(
    COMMAND ld -V
    OUTPUT_VARIABLE LINKER_VERS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE LINKER_VERS_RESULT
    )

  if("${LINKER_VERS_RESULT}" MATCHES "^0$")
    if("${LINKER_VERS}" MATCHES "GNU gold")
      set(GOLD_LINKER ON)
    else("${LINKER_VERS}" MATCHES "GNU gold")
    endif("${LINKER_VERS}" MATCHES "GNU gold")
  endif("${LINKER_VERS_RESULT}" MATCHES "^0$")

endif(UNIX)

message(STATUS "toolchain options processed")
