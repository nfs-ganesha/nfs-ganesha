include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

if(USE_DBUS_STATS)
  include_directories(
    ${DBUS_INCLUDE_DIR}
    ${DBUS_ARCH_INCLUDE_DIR}
    )
endif(USE_DBUS_STATS)

########### next target ###############

SET(test_timespec_SRCS
   test_timespec.c
)

add_executable(test_timespec EXCLUDE_FROM_ALL ${test_timespec_SRCS})

target_link_libraries(
  test_timespec
  log
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBRT}
)


########### next target ###############

SET(test_nfs_ip_name_SRCS
   test_nfs_ip_name.c
)

add_executable(test_nfs_ip_name EXCLUDE_FROM_ALL ${test_nfs_ip_name_SRCS})

target_link_libraries(test_nfs_ip_name
  log
  support
  ${CMAKE_THREAD_LIBS_INIT}
)

# string utilities.  This should eventually go into src/os
# because BSD has them and Linux doesn't

set(string_utils_STAT_SRCS
   strlcat.c
   strlcpy.c
   strnlen.c
)

add_library(string_utils STATIC ${string_utils_STAT_SRCS})

# hash function libraries

set(hash_SRCS
   lookup3.c
   murmur3.c
   city.c
)

add_library(hash STATIC ${hash_SRCS})

set(weakrefSRCS
   generic_weakref.c
)

add_library(weakref STATIC ${weakrefSRCS})

########### next target ###############

SET(support_STAT_SRCS
   nfs4_acls.c
   nfs_export_list.c
   nfs_filehandle_mgmt.c
   nfs_mnt_list.c
   nfs_read_conf.c
   nfs_convert.c
   nfs_ip_name.c
   exports.c
   fridgethr.c
   delayed_exec.c
   misc.c
   bsd-base64.c
   server_stats.c
   export_mgr.c
)

if(ERROR_INJECTION)
  set(support_STAT_SRCS
    ${support_STAT_SRCS}
    err_inject.c
    )
endif(ERROR_INJECTION)

if(APPLE)
  set(support_STAT_SRCS
    ${support_STAT_SRCS}
    misc.c
    )
endif(APPLE)

add_library(support STATIC ${support_STAT_SRCS})

########### install files ###############
