prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${exec_prefix}/lib@LIB_SUFFIX@
includedir=${prefix}/include

Name:  libganeshaNFS
Description: NFS-GANESHA's "fuselike" library
Requires:
Version: @GANESHA_MAJOR_VERSION@.@GANESHA_MINOR_VERSION@.@GANESHA_PATCH_LEVEL@
Libs: -L${exec_prefix}/lib@LIB_SUFFIX@ -lganeshaNFS
Cflags: -I@INCLUDE_INSTALL_DIR@
