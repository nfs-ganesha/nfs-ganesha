add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalmdcache_LIB_SRCS
	mdcache_avl.h
	mdcache_ext.h
	mdcache_int.h
	mdcache_hash.h
	mdcache_lru.h
	mdcache_handle.c
	mdcache_file.c
	mdcache_xattrs.c
	mdcache_main.c
	mdcache_export.c
	mdcache_helpers.c
	mdcache_lru.c
	mdcache_hash.c
	mdcache_avl.c
	mdcache_read_conf.c
	mdcache_up.c
	)

add_library(fsalmdcache OBJECT ${fsalmdcache_LIB_SRCS})
add_sanitizers(fsalmdcache)
set_target_properties(fsalmdcache PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
