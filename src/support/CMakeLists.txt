if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

# string utilities.  This should eventually go into src/os
# because BSD has them and Linux doesn't

set(string_utils_STAT_SRCS
   strlcpy.c
   strnlen.c
   refstr.c
)

add_library(string_utils OBJECT ${string_utils_STAT_SRCS})
add_sanitizers(string_utils)
set_target_properties(string_utils PROPERTIES COMPILE_FLAGS "-fPIC")

# hash function libraries

set(hash_SRCS
   murmur3.c
   city.c
)

add_library(hash OBJECT ${hash_SRCS})
add_sanitizers(hash)
set_target_properties(hash PROPERTIES COMPILE_FLAGS "-fPIC")

# uid2grp mapper
set( uid2grp_SRCS
     uid2grp.c
     uid2grp_cache.c
)

add_library(uid2grp OBJECT ${uid2grp_SRCS})
add_sanitizers(uid2grp)
set_target_properties(uid2grp PROPERTIES COMPILE_FLAGS "-fPIC")

# netgroup cache
set(netgroup_cache_SRCS
    netgroup_cache.c
)
add_library(netgroup_cache OBJECT ${netgroup_cache_SRCS})
add_sanitizers(netgroup_cache)
set_target_properties(netgroup_cache PROPERTIES COMPILE_FLAGS "-fPIC")

if (USE_RADOS_RECOV)
   add_library(rados_grace OBJECT rados_grace.c)
   include_directories(${RADOS_INCLUDE_DIR})
   set_target_properties(rados_grace PROPERTIES COMPILE_FLAGS "-fPIC")
endif(USE_RADOS_RECOV)

########### next target ###############

SET(support_STAT_SRCS
   nfs4_acls.c
   nfs_creds.c
   nfs_filehandle_mgmt.c
   nfs_read_conf.c
   nfs_convert.c
   nfs_ip_name.c
   ds.c
   exports.c
   fridgethr.c
   delayed_exec.c
   misc.c
   bsd-base64.c
   server_stats.c
   export_mgr.c
   nfs4_fs_locations.c
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

add_library(support OBJECT ${support_STAT_SRCS})
add_sanitizers(support)
set_target_properties(support PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
