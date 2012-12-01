include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(test_nfs_ip_stats_SRCS
   test_nfs_ip_stats.c
)

add_executable(test_nfs_ip_stats EXCLUDE_FROM_ALL ${test_nfs_ip_stats_SRCS})

target_link_libraries(
  test_nfs_ip_stats
  support hashtable
  log
  ${CMAKE_THREAD_LIBS_INIT}
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
   misc.c
   nfs_cpu_cores.c
   nfs_rlimit.c
)

add_library(string_utils STATIC ${string_utils_STAT_SRCS})

# hash function libraries

set(hash_SRCS
   lookup3.c
   murmur3.c
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
   nfs_stat_mgmt.c
   nfs_ip_name.c
   nfs_ip_stats.c
   exports.c
   fridgethr.c
)

if(ERROR_INJECTION)
  set(support_STAT_SRCS
    ${support_STAT_SRCS}
    err_inject.c
    )
endif(ERROR_INJECTION)

add_library(support STATIC ${support_STAT_SRCS})

 
target_link_libraries(support
  NodeList
  cidr
  idmap
  common_utils
  fsalup
  string_utils
  ${_OPENSSL_LIBRARIES}
)
 

########### install files ###############
