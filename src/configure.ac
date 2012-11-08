#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT( [nfs-ganesha], [1.4.0], [philippe.deniel@cea.fr] )

AC_SUBST(RELEASE, [1])

AC_DEFINE( [NFS_GANESHA], 1, [Ganesha])

dnl AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADER([include/config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_REVISION([Compiles:  i386/x86_64. FSAL: POSIX/PROXY/HPSS/FUSELIKE/LUSTRE/XFS/GPFS/ZFS/VFS/CEPH .xattr ghost dir support. RPCSEC_GSS/KRB5, TI-RPC (prod),  Early pNFS (alpha) ])

# Init Automake
#AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AM_INIT_AUTOMAKE([foreign])

# check for _GNU_SOURCE and set it in config.h
AC_GNU_SOURCE

# Uuse libtool
AM_ENABLE_SHARED(yes)
AC_ENABLE_SHARED(yes)

AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_YACC
AC_PROG_LEX
PKG_PROG_PKG_CONFIG

if test "${ac_cv_prog_YACC+set}" != "set"; then
	AC_MSG_ERROR([Yacc, Bison or an equivalent tool is needed: check you have installed it on your system])
fi

if test "${ac_cv_prog_LEX+set}" != "set"; then
	AC_MSG_ERROR([Lex, Flex or an equivalent tool is needed: check you have installed it on your system])
fi

AC_PROG_CC
AC_PROG_MAKE_SET

AC_DEFINE( [VERSION_COMMENT], ["GANESHA 64 bits compliant. SNMP exported stats. FSAL_PROXY re-exports NFSv3. RPCSEC_GSS support (partial). FUSELIKE added"], [No Comment])


# specific programs used for ganesha
GA_PROG_RPCGEN
GA_PROG_DOXYGEN

# Variables used for internal compilation

# define everything necessary for accessing large files (64bits offset)
AC_SYS_LARGEFILE

CFLAGS="$CFLAGS -D_REENTRANT -Wall -Wimplicit -Wformat -Wmissing-braces -Wno-pointer-sign"

# Version number for shared object
LIBVERSION=`echo $VERSION | sed -e 's/\./:/g'`
AC_SUBST(LIBVERSION)

# set '_EXTENDED_TYPE_NEEDED' in config.h
AC_DEFINE( [_EXTENDED_TYPE_NEEDED], 1, [Using XDR extended types])

# Git latest commit
AC_MSG_CHECKING( [Git HEAD] )
head_commit=`git rev-parse HEAD 2>/dev/null`

if test "x$head_commit" == "x" ; then
  AC_MSG_RESULT( [no git here] ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "not compiled within a git repository", [Lastest HEAD at the time configure was run])
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "not compiled within a git repository", [Result of git-describe --long])
else
  AC_MSG_RESULT( $head_commit ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "$head_commit", [Lastest HEAD at the time configure was run])
  git_describe=`git describe --long`
  AC_MSG_RESULT( $git_describe ) 
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "$git_describe", [Result of git-describe --long])
fi

# plateform relative flags
case $build in
	*-linux*)
		#DBUILDOS="-D_LINUX -DLINUX"
                MYOS=LINUX
		AC_DEFINE( [LINUX], 1, [Build Operating System is Linux] )
		AC_DEFINE( [_LINUX], 1, [Build Operating System is Linux] )
	 	;;
	*-apple*)
		MYOS=MACOS
		AC_DEFINE( [APPLE], 1, [Build Operating System is MacOS] )
                AC_DEFINE( [_APPLE], 1, [Build Operating System is MacOS] )
		EXTRA_LIB=""
		;;
	*-freebsd*)
		MYOS=FREEBSD
		AC_DEFINE( [APPLE], 1, [Build Operating System is FreeBSD] )
                AC_DEFINE( [_APPLE], 1, [Build Operating System is FreeBSD] )
                AC_DEFINE( [FREEBSD], 1, [Build Operating System is FreeBSD] )
                AC_DEFINE( [_FREEBSD], 1, [Build Operating System is FreeBSD] )
		EXTRA_LIB=""
		;;
	*-solaris*)
		MYOS=SOLARIS
		AC_DEFINE( [SOLARIS], 1, [Build Operating System is Solaris] )
                AC_DEFINE( [_SOLARIS], 1, [Build Operating System is Solaris] )
		# set '_XOPEN_SOURCE' in config.h
		AC_DEFINE( [_XOPEN_SOURCE], 1, [Compiling with _XOPEN_SOURCE functions])
		AC_DEFINE([_LARGEFILE64_SOURCE], 1, [Large file support])
		AC_DEFINE([__EXTENSIONS__], 1, [Define missing types and 64bits functions on Solaris])
		EXTRA_LIB="-lnsl -lsocket"
		;;
	*)
		AC_MSG_ERROR([The plateform $build is not supported])
		;;
esac

AM_CONDITIONAL(IS_LINUX, test "$MYOS" = "LINUX")
AM_CONDITIONAL(IS_SOLARIS, test "$MYOS" = "SOLARIS" )
AC_SUBST(EXTRA_LIB)

# checking endianess
AC_C_BIGENDIAN( [AC_DEFINE( [BIGEND],	1,[Big endian system])],
		[AC_DEFINE( [LITTLEEND],1,[Little endian system])],
		[AC_MSG_ERROR( [Endianness could not be found...] ) ] )

# Disabling mount-list feature
AC_DEFINE( [NOMNTLIST], 1, [Mount list is disabled])

AC_CHECK_LIB([krb5], [krb5_principal_compare], [have_krb5='yes'], [have_krb5='no'])
if test "$have_krb5" == "yes"; then
	AC_DEFINE( [HAVE_KRB5], 1, [krb5 library is available])
else
	AC_DEFINE( [NO_KRB5], 1, [krb5 library is not available])
fi

# Checks for libraries.
AC_CHECK_LIB([c], [main])
AC_CHECK_LIB([curses], [scr_init])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([readline], [readline])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC

# ThL: temporary comment this line since we don't use the following "HAVE_..." defines
#AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h limits.h malloc.h netdb.h netinet/in.h stddef.h stdlib.h string.h strings.h sys/file.h sys/mount.h sys/param.h sys/socket.h sys/statvfs.h sys/time.h sys/timeb.h sys/vfs.h unistd.h utime.h])

# ThL: This is actually tested in "MainNFSD/Svc_udp_gssrpc.c"
AC_CHECK_HEADERS([sys/uio.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])
AC_CHECK_SIZEOF([long])

# Ceck daemon() function
AC_CHECK_FUNC([daemon],[has_daemon=yes],[has_daemon=no])
test "$has_daemon" = "yes" && AC_DEFINE(HAVE_DAEMON, 1, [daemon function exist on this system])

# ThL: temporary comment the following lines since we don't use the following "HAVE_..." defines
# Checks for library functions.
#AC_FUNC_CLOSEDIR_VOID
#AC_FUNC_ERROR_AT_LINE
#AC_REPLACE_FNMATCH
#AC_FUNC_FORK
#AC_FUNC_LSTAT
#AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
#AC_FUNC_MALLOC
#AC_FUNC_MEMCMP
#AC_FUNC_MKTIME
#AC_FUNC_MMAP
#AC_FUNC_REALLOC
#AC_FUNC_SELECT_ARGTYPES
#AC_FUNC_STAT
#AC_FUNC_STRFTIME
#AC_FUNC_STRNLEN
#AC_FUNC_STRTOD
#AC_FUNC_UTIME_NULL
#AC_FUNC_VPRINTF
#AC_CHECK_FUNCS([endgrent floor getcwd gethostbyaddr gethostbyname gethostname gettimeofday lchown localtime_r memchr memmove memset mkdir munmap rmdir select setenv socket strcasecmp strchr strdup strerror strncasecmp strndup strrchr strstr strtol strtoul utime])

# snmp config retrieval command
GA_PROG_NETSNMP_CONFIG

# processing compilation options

# Features options

# Cofigure the allocator
AC_ARG_WITH([allocator], AS_HELP_STRING([--with-allocator=jemalloc|tcmalloc|libc],
                                        [specify the memory allocator to use]),
            [ALLOCATOR="$withval"])

if test "x${with_allocator+set}" == "xset"; then
        if test "x$ALLOCATOR" = "xjemalloc"; then
                AC_CHECK_LIB(jemalloc, malloc, ,[AC_MSG_ERROR([libjemalloc cannot be found])])
        elif test "x$ALLOCATOR" = "xtcmalloc"; then
                AC_CHECK_LIB(tcmalloc, malloc, ,[AC_MSG_ERROR([libtcmalloc cannot be found])])
        elif test "x$ALLOCATOR" != "xlibc"; then
                AC_MSG_ERROR([$ALLOCATOR is not a valid option.])
        fi
else
        AC_CHECK_LIB(jemalloc, malloc, [ALLOCATOR="jemalloc"], [ALLOCATOR=""])
        if test "x$ALLOCATOR" = "x"; then
                AC_CHECK_LIB(tcmalloc, malloc, [ALLOCATOR="tcmalloc"], [ALLOCATOR=""])
        fi
        if test "x$ALLOCATOR" = "x"; then
                ALLOCATOR="libc"
        fi
fi

if test "x$ALLOCATOR" = "xjemalloc"; then
        LDFLAGS="$LDFLAGS -ljemalloc"
elif test "x$ALLOCATOR" = "xtcmalloc"; then
        LDFLAGS="$LDFLAGS -ltcmalloc"
fi


# DBUS
GA_ENABLE_AM_CONDITION([dbus],[enable DBUS protocol support],[USE_DBUS])

if test "$enable_dbus" == "yes"; then
        AC_DEFINE(USE_DBUS,1,[enable DBUS protocol support])
	AC_CHECK_LIB([dbus-1], [main], [AC_DEFINE( HAVE_DBUS,1,[enable DBUS support] ) ], [AC_MSG_ERROR( [libdbus-1.x could not be found...] ) ] )
	CFLAGS="$CFLAGS `pkg-config dbus-1 --cflags`"
	LDFLAGS="$LDFLAGS -ldbus-1"
fi
AM_CONDITIONAL(USE_DBUS, test "$enable_dbus" = "yes")

# Callback simulator                                                                                         
GA_ENABLE_AM_CONDITION([cb-simulator],[enable callback simulator thread],[USE_CB_SIMULATOR])

if test "$enable_cb_simulator" == "yes"; then
        AC_DEFINE(_USE_CB_SIMULATOR,1,[enable callback simulator thread])
fi
AM_CONDITIONAL(USE_CB_SIMULATOR, test "$enable_cb_simulator" = "yes")

# FSAL selection
# FSALs which are enabled by default but could be disabled
# during the build
GA_AM_COND_ON_UNLESS([fsal-proxy],[do not build PROXY FSAL shared library],[USE_FSAL_PROXY])
GA_AM_COND_ON_UNLESS([fsal-vfs],[do not build VFS FSAL shared library],[USE_FSAL_VFS])
GA_AM_COND_ON_UNLESS([fsal-posix],[do not build POSIX FSAL shared library],[USE_FSAL_POSIX])
GA_AM_COND_ON_UNLESS([fsal-gpfs],[do not build GPFS FSAL shared library],[USE_FSAL_GPFS])

# We only want to turn Ceph on if we have the right library

# explicit Ceph path
AC_ARG_WITH([ceph-prefix], AS_HELP_STRING([--with-ceph-prefix=<path>], [Path to Ceph installation]),
	    CEPH_PREFIX="$withval", CEPH_PREFIX="/usr")


AC_ARG_ENABLE([fsal-ceph],AS_HELP_STRING([--disable-ceph],[USE_FSAL_CEPH]),
              [enable_fsal_ceph]=$enableval, [enable_fsal_ceph]='yes')

if test "$enable_fsal_ceph" == "yes"; then
	AC_CHECK_LIB([cephfs], [ceph_ll_connectable_x],
		      [AC_DEFINE([_USE_CEPH], 1, [GANESHA exports Ceph Filesystem ])],
		      [enable_fsal_ceph='no'
		       AC_MSG_WARN([Unsable to find Ceph libraries, disabling.])])
	CEPH_CFLAGS="-I${CEPH_PREFIX}/include -D_FILE_OFFSET_BITS=64"
        CEPH_LDFLAGS="-L${CEPH_PREFIX}/lib"
	CEPH_LIB="-lcephfs"
else
        CEPH_CFLAGS=""
        CEPH_LDFLAGS=""
        CEPH_LIB=""
fi

AM_CONDITIONAL([USE_FSAL_CEPH], test "[$enable_fsal_ceph]"  == "yes")

# We only want to turn Lustre on if we have the right library

AC_ARG_ENABLE([fsal-lustre],AS_HELP_STRING([--disable-lustre],[USE_FSAL_LUSTRE]),
              [enable_fsal_lustre]=$enableval, [enable_fsal_lustre]='yes')

if test "$enable_fsal_lustre" == "yes"; then
	AC_CHECK_HEADERS([attr/xattr.h], [], [enable_fsal_lustre='no'
                                              AC_MSG_WARN([Missing xattr files.  Disabling Lustre.])])
	AC_CHECK_LIB([lustreapi], [llapi_fid2path], [have_lustre2_api="yes"])
	if test "x$have_lustre2_api" != "xyes" ; then
	    AC_MSG_WARN([Lustre 2.x and liblustreapi not found.  Disabling Lustre.])
            LUSTRE_CFLAGS=
            LUSTRE_LDFLAGS=
            LUSTRE_LIB=
            enable_fsal_lustre='no'
        else 
	    AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystem])
	    # check if this Lustre version has HSM functions
            AC_CHECK_LIB([lustreapi], [llapi_hsm_state_get], [lustre_hsm="yes"])
            	if test "x$lustre_hsm" == "xyes"; then
		   AC_DEFINE([_LUSTRE_HSM], 1, [Lustre version supports HSM binding])
                fi
                LUSTRE_CFLAGS=
                LUSTRE_LDFLAGS="-llustreapi"
                LUSTRE_LIB="\$(top_builddir)/FSAL/FSAL_LUSTRE/libfsallustre.la"
        fi
fi

AM_CONDITIONAL([USE_FSAL_LUSTRE], test "[$enable_fsal_lustre]"  == "yes")

# SHOOK is a variant of Lustre FSAL

AC_ARG_ENABLE([fsal-shook],AS_HELP_STRING([--disable-shook],[USE_FSAL_SHOOK]),
              [enable_fsal_shook]=$enableval, [enable_fsal_shook]='yes')

if test "$enable_fsal_shook" == "yes"; then
        # Lustre FSAL compiled with special compilation directives
	AC_CHECK_HEADERS([attr/xattr.h], [], [enable_fsal_shook='no';
                                              AC_MSG_WARN(Missing xattr files.  Disabling SHOOK.)])
        AC_CHECK_LIB([shooksvr], [shook_restore],,
        [AC_MSG_WARN([libshooksvr not found.  Disabling SHOOK.])
         enable_fsal_shook='no'])

	AC_CHECK_LIB([lustreapi], [llapi_fid2path], [have_lustre2_api="yes"])
	if test "x$have_lustre2_api" != "xyes" ; then
	    AC_MSG_WARN([Lustre 2.x and liblustreapi not found.  Disabling SHOOK.])
            SHOOK_CFLAGS=
            SHOOK_LDFLAGS=
            SHOOK_LIB=
            enable_fsal_lustre='no'
        else
                AC_DEFINE(_SHOOK, 1, [shook is enabled])
                AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystem])
                SHOOK_CFLAGS=
                SHOOK_LDFLAGS="-lshooksvr -llustreapi"
                SHOOK_LIB="\$(top_builddir)/FSAL/FSAL_LUSTRE/libfsalshook.la"
	fi
fi

AM_CONDITIONAL([USE_FSAL_SHOOK], test "[$enable_fsal_shook]"  == "yes")
GA_AM_COND_ON_UNLESS([fsal-ceph],[do not build VFS FSAL shared library],[USE_FSAL_CEPH])
GA_AM_COND_ON_UNLESS([fsal-lustre],[do not build LUSTRE FSAL shared library],[USE_FSAL_LUSTRE])

# FSALs whicha re disabled by default
GA_ENABLE_AM_CONDITION([fsal-hpss],[build HPSS FSAL],[USE_FSAL_HPSS])
GA_ENABLE_AM_CONDITION([fsal-fuse],[build FUSE FSAL],[USE_FSAL_FUSE])
GA_ENABLE_AM_CONDITION([fsal-xfs],[build XFS FSAL],[USE_FSAL_XFS])
GA_ENABLE_AM_CONDITION([fsal-zfs],[build ZFS FSAL],[USE_FSAL_ZFS])
# SHOOK is a variant of Lustre FSAL
GA_ENABLE_AM_CONDITION([fsal-shook],[build SHOOKFSAL],[USE_FSAL_SHOOK])

# TI-RPC
AC_DEFINE(_USE_TIRPC,1,[enable TIRPC support])
GA_PROG_TIRPC
_SRCDIR="`pwd`"
TIRPCDIR="${_SRCDIR}/libtirpc"
dnl echo "TIRPCDIR: ${TIRPCDIR}"
if test -d ${TIRPCDIR}; then
	cd ${TIRPCDIR}
	sh autogen.sh
	./configure CFLAGS="${CFLAGS}" --enable-gss --prefix="${prefix}"
	cd ${_SRCDIR}
else
	AC_MSG_ERROR([${TIRPCDIR} not present (cf. README.TIRPC).])
fi

# TI-RPC Packaging
libtool_15="`libtool --version|grep 1.5`"
if test "x$libtool_15" != "x" ; then
   LIBTOOL_15="_libtool_15"
   AC_SUBST(_LIBTOOL_15)
fi

# My auto-foo doesn't extend to this
#AC_CONFIG_SUBDIRS([../contrib/libtirpc])
CFLAGS="-I${TIRPCDIR}/tirpc/ $CFLAGS"
TIRPC_LIB="${TIRPCDIR}/src/libntirpc.la"
TIRPC_LIBS="$TIRPC_LIB"

# IPv6
GA_ENABLE_AM_CONDITION([ipv6],[enable IPv6 support (via IPv6)], [USE_TIRPC_IPV6])

if test "$enable_ipv6" == "yes"; then
	AC_DEFINE(_USE_TIRPC_IPV6,1,[enable IPv6 support via TIRPC])
	if test "$RPCAL" != "TIRPC" ; then
		AC_MSG_ERROR([IPv6 support is available only when --with-rpcal=TIRPC is used])
	fi
fi

# libnfsidmap
GA_DISABLE_AM_CONDITION([nfsidmap],[disable the use of libnfsidmap], [USE_NO_NFSIDMAP])

if test "$enable_nfsidmap" == "yes"; then
	AC_CHECK_LIB([nfsidmap], [nfs4_uid_to_name], [have_nfsidmap="yes"])
        if test "x$have_nfsidmap" != "xyes" ; then
		 AC_MSG_ERROR([libnfsidmap is required])
        fi
	AC_DEFINE(_USE_NFSIDMAP, 1, [enable Id Mapping through libnfsidmap])
fi
AM_CONDITIONAL(USE_NFSIDMAP, test "$enable_nfsidmap" == "yes" )

dnl check for the keyutils libraries and headers
AC_KEYUTILS

dnl Check for Kerberos V5
AC_KERBEROS_V5

dnl Invoked after AC_KERBEROS_V5; AC_LIBRPCSECGSS needs to have KRBLIBS set
dnl AC_LIBRPCSECGSS

dnl Need krb5_util.h and friends.  This will be out-of-tree Very Soon,
dnl so don't put pieces into src/include
CFLAGS="$CFLAGS -I\$(top_builddir)/RPCAL/gssd"

# Exports stats via SNMP

GA_ENABLE_AM_CONDITION([snmp-adm], [export GANESHA statistics with SNMP], [ENABLE_SNMP_ADM])

if test "$FSAL" == "FUSE" ; then #it makes no sense to embed snmp stats when generating a lib for FUSE
  enable_snmp_adm="no"
fi

AM_CONDITIONAL(USE_SNMP_ADM,    test "$enable_snmp_adm" = "yes" )

if test "$enable_snmp_adm" == "yes"; then
   AC_DEFINE(_SNMP_ADM_ACTIVE,1,[export GANESHA statistics with SNMP])
   EXT_LIBS=`$NETSNMP_CONFIG --agent-libs`
   EXT_LDADD="\$(top_builddir)/snmp_adm/libsnmp_adm.la $EXT_LIBS"
fi 

AC_SUBST(EXT_LDADD)

GA_ENABLE_AM_CONDITION([error-injection], [enable error injection], [ERROR_INJECTION])

# for using it in Makefiles
AM_CONDITIONAL(ENABLE_ERROR_INJECTION,    test "$enable_error_injection" = "yes" )

if test "$enable_error_injection" == "yes"; then
	if test "$enable_snmp_adm" == "yes"; then
		AC_DEFINE(_ERROR_INJECTION,1,[enable error injection])
	else
		AC_MSG_ERROR([enable-snmp-adm is requirered to enable-error-injection])
	fi
fi

# statistics exporter thread
GA_ENABLE_AM_CONDITION([stat-exporter], [export GANESHA NFS request statistics with a dedicated thread and socket], [USE_STAT_EXPORTER])
if test "$enable_stat_exporter" == "yes"; then
	AC_DEFINE(_USE_STAT_EXPORTER,1,[export GANESHA NFS request statistics with a dedicated thread and socket])
fi


GA_ENABLE_AM_CONDITION([efence],[link with efence memory debug library],[USE_EFENCE])

if test "$enable_efence" == "yes" ; then
	EFENCE=-lefence
else
	EFENCE=
fi

AC_SUBST(EFENCE)

GA_DISABLE_FLAG( [tcp-register], 	 [disable registration of tcp services on portmapper], 		  [-D_NO_TCP_REGISTER] )
GA_DISABLE_FLAG( [portmapper], 	         [disable registration on portmapper], 				  [-D_NO_PORTMAPPER] )
GA_DISABLE_FLAG( [xattr-directory],      [disable ghost xattr directory and files support],               [-D_NO_XATTRD])

GA_ENABLE_FLAG(  [debug-memleaks],       [enable allocator features for tracking memory usage],           [-D_DEBUG_MEMLEAKS] )
GA_ENABLE_FLAG(  [debug-nfsshell],       [enable extended debug traces for ganeshell utility],            [-D_DEBUG_NFS_SHELL] )

GA_ENABLE_FLAG(  [cache-path],		 [Enable entry path caching in POSIX FSAL],	                  [-D_ENABLE_CACHE_PATH])
GA_ENABLE_FLAG(  [handle-mapping],	 [enable NFSv2/3 handle mapping for PROXY FSAL],	          [-D_HANDLE_MAPPING])


# for using it in makefiles
AM_CONDITIONAL(ENABLE_HANDLE_MAPPING,    test "$enable_handle_mapping" = "yes" )

GA_ENABLE_FLAG(  [debug-symbols],	 [include debug symbols to binaries (-g option)],	         [-g])

# enable code profiling
GA_ENABLE_FLAG( [profiling], [turn on code profiling (-g and -pg)], [-g -pg] )

# enable code profiling
GA_ENABLE_FLAG( [strict-compile], [turn on strict compiler flags], [-Wall -Werror -Wimplicit -Wformat -Wmissing-braces -Wreturn-type -Wunused-variable -Wuninitialized -Wno-pointer-sign] )

# NFSv4 ACL needs ssl
AC_CHECK_LIB([ssl],[MD5_Init])

# MSPAC support
GA_ENABLE_FLAG([mspac],[enable mspac support],[-D_MSPAC_SUPPORT -lwbclient])

# makes it possible to add some extra include dir

AC_ARG_WITH( [extra-includedir], AS_HELP_STRING([--with-extra-includedir=<path>], [Alternative path to include files] ),
		EXTRA_INCLUDE_DIR="$withval" )

if test "${with_extra_includedir+set}" == "set"; then
	CFLAGS="$CFLAGS -I$EXTRA_INCLUDE_DIR"
fi

# CUnit
AC_ARG_WITH( [cunit], AS_HELP_STRING([--with-cunit=<path>], [Path to CUnit] ),
		CUNIT="$withval" )

if test "${with_cunit+set}" == "set"; then
	CFLAGS="$CFLAGS -I$CUNIT/include"
	LDFLAGS="-L$CUNIT/lib $LDFLAGS -lcunit"
fi


# RPCAL now fixed as TIRPC
RPCAL="TIRPC"
AM_CONDITIONAL(USE_TIRPC,  true )
AM_CONDITIONAL(HAVE_GSSAPI, true )

# Enable 9P Support
GA_ENABLE_AM_CONDITION([9p],[enable 9P support], [USE_9P])
if test "$enable_9p" == "yes"; then
	AC_DEFINE(_USE_9P, 1, [enable 9P support])
fi

# Enable 9P_RDMA support
GA_ENABLE_AM_CONDITION([9p-rdma],[enable 9P_RDMA support], [USE_9P_RDMA])
if test "$enable_9p_rdma" == "yes"; then
        if test "$enable_9p" != "yes"; then
            AC_MSG_ERROR([You must activate 9P support to activate 9P_RDMA support])
        else
	    AC_DEFINE(_USE_9P_RDMA, 1, [enable 9P_RDMA support])
        fi
fi


# kerberos5 location
AC_ARG_WITH( [krb5], AS_HELP_STRING([--with-krb5=<path>], [Path where kerberos5 is installed] ),
		KRB5_PATH_LOCAL="$withval" )

if test "${with_krb5+set}" == "set"; then
    SEC_CFLAGS="-I$KRB5_PATH_LOCAL/include"
    SEC_LFLAGS="-L$KRB5_PATH_LOCAL/lib"
    SEC_RPATH="-rpath,$KRB5_PATH_LOCAL/lib"
    LDFLAGS="$SEC_LFLAGS $LDFLAGS"
else
    SEC_CFLAGS=""
    SEC_LFLAGS=""
    SEC_RPATH=""
fi

# Compile with "NO_MOUNT_LIST"
AC_DEFINE([_NO_MOUNT_LIST], 1, [Don't use MOUNT PROTOCOL's client list])

# define filesystem relative flags

# HOOK: we escape the '$' sign in FSAL_LIB, so that '$(top_builddir)' is kept uninterpreted until compilation time

# POSIX must be the last of the list (extraneous installed file in posixdb)

if test "$enable_fsal_hpss" = "yes"; then
	# HPSS specific options
	AC_ARG_WITH( [hpss-version], AS_HELP_STRING([--with-hpss-version=5.1|6.2|6.2.2|7.1|7.3 (default=6.2.2)],
		[specify HPSS version] ), HPSS="$withval", HPSS="6.2.2" )
	GA_ENABLE_FLAG(  [strip-cs-uuid], [Strip core server UUID from handle (shorter NFS handle)],
			 [-D_STRIP_CORESERVER_UUID])

	AC_DEFINE([_USE_HPSS], 1, [GANESHA is compiled with HPSS FSAL])
	FSAL_CFLAGS="-I/opt/hpss/include"
	FSAL_LDFLAGS="-L/opt/hpss/lib -Wl,-rpath,/opt/hpss/lib -lhpss -lhpsscs"
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_HPSS/libfsalhpss.la \$(top_builddir)/FSAL/FSAL_HPSS/HPSSclapiExt/libhpssapiext.la"

	case "$HPSS" in
	"5.1")
		FSAL_LDFLAGS="$FSAL_LDFLAGS -L/opt/dcelocal/lib -ldce -ldcepthreads"
		;;
	esac
fi

if test "$enable_fsal_proxy" == "yes"; then
        AC_MSG_WARN([PROXY FSAL is enabled])
	FSAL_CFLAGS=
	FSAL_LDFLAGS=$SEC_RPATH
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_PROXY/libfsalproxy.la"
fi

if test "$enable_fsal_fuse" == "yes"; then
	AC_DEFINE([_USE_FUSE], 1, [GANESHA is compiled as a FUSE-like library])
	FSAL_CFLAGS=
	FSAL_LDFLAGS=""
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_FUSELIKE/libfsalfuse.la"
fi

if test "$enable_fsal_lustre" == "yes"; then
        AC_MSG_WARN([LUSTRE FSAL is enabled])
	AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing xattr header files)])
	AC_CHECK_LIB([lustreapi], [llapi_fid2path], [have_lustre2_api="yes"])
	if test "x$have_lustre2_api" != "xyes" ; then
	    AC_MSG_ERROR([Lustre 2.x and liblustreapi are required])
	fi
	AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystem])

	# check if this Lustre version has HSM functions
	AC_CHECK_LIB([lustreapi], [llapi_hsm_state_get], [lustre_hsm="yes"])
	if test "x$lustre_hsm" == "xyes"; then
		AC_DEFINE([_LUSTRE_HSM], 1, [Lustre version supports HSM binding])
	fi

        LUSTRE_CFLAGS=
        LUSTRE_LIB="-llustreapi"
        LUSTRE_LDFLAGS=
else
        LUSTRE_CFLAGS=
        LUSTRE_LDFLAGS=
        LUSTRE_LIB=
fi

AC_SUBST(LUSTRE_CFLAGS)
AC_SUBST(LUSTRE_LDFLAGS)
AC_SUBST(LUSTRE_LIB)

if test "$enable_fsal_shook" == "yes"; then
        AC_MSG_WARN([SHOOK FSAL is enabled])
        # Lustre FSAL compiled with special compilation directives
        AC_CHECK_LIB([shooksvr], [shook_restore],, AC_MSG_ERROR([libshooksvr not found]))
        AC_DEFINE(_SHOOK, 1, [shook is enabled])

	AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing xattr header files)])
	AC_CHECK_LIB([lustreapi], [llapi_fid2path], [have_lustre2_api="yes"])
	if test "x$have_lustre2_api" != "xyes" ; then
		AC_MSG_ERROR([Lustre 2.x and liblustreapi are required])
	fi
	AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystem])

        SHOOK_CFLAGS=
        SHOOK_LDFLAGS=
        SHOOK_LIB="-lshooksvr -llustreapi"
else
        SHOOK_CFLAGS=
        SHOOK_LDFLAGS=
        SHOOK_LIB=
fi

AC_SUBST(SHOOK_CFLAGS)
AC_SUBST(SHOOK_LDFLAGS)
AC_SUBST(SHOOK_LIB)


if test "$enable_fsal_xfs" == "yes"; then
	AC_DEFINE([_USE_XFS], 1, [GANESHA exports XFS Filesystem])
	AC_CHECK_HEADERS([xfs/xfs.h], [], [AC_MSG_ERROR( missing xfs include files)])
	AC_CHECK_HEADERS([xfs/handle.h], [], [AC_MSG_ERROR(missimg xfs include files)])
	AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing xattr header files)])
	FSAL_CFLAGS=
	FSAL_LDFLAGS="-lhandle"
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_XFS/libfsalxfs.la"
fi

AC_SUBST(CEPH_CFLAGS)
AC_SUBST(CEPH_LDFLAGS)
AC_SUBST(CEPH_LIB)

AC_SUBST(LUSTRE_CFLAGS)
AC_SUBST(LUSTRE_LDFLAGS)
AC_SUBST(LUSTRE_LIB)

AC_SUBST(SHOOK_CFLAGS)
AC_SUBST(SHOOK_LDFLAGS)
AC_SUBST(SHOOK_LIB)

if test "$enable_fsal_gpfs" == "yes"; then
	AC_DEFINE([_USE_GPFS], 1, [GANESHA exports GPFS Filesystem])

	AC_CHECK_LIB([gpfs], [gpfs_ganesha], [], [AC_MSG_ERROR(missing gpfs library)])
	FSAL_CFLAGS=
	FSAL_LDFLAGS=""
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_GPFS/libfsalgpfs.la"
fi

# This function doesn't actually seem to exist in my C library, and
# FSAL_VFS calls into the kernel directly.  -- ACE

dnl if test "$enable_fsal_vfs" == "yes"; then
dnl	AC_CHECK_FUNC([open_by_handle_at],
dnl		      [AC_DEFINE([_USE_VFS], 1, [GANESHA exports VFS Filesystem ])],
dnl		      [USE_FSAL_VFS_TRUE='#'])
dnl	FSAL_CFLAGS=
dnl	FSAL_LDFLAGS=""
dnl	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_VFS/libfsalvfs.la"
dnl fi

if test "$enable_fsal_vfs" == "yes"; then
        AC_DEFINE([_USE_VFS], 1, [GANESHA exports VFS Filesystem ])
	FSAL_CFLAGS=
	FSAL_LDFLAGS=""
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_VFS/libfsalvfs.la"
fi

if test "$enable_fsal_zfs" == "yes"; then
	AC_DEFINE([_USE_ZFS], 1, [GANESHA exports ZFS Filesystem])
	PKG_CHECK_MODULES(ZFSWRAP, libzfswrap,
	[
	  FSAL_CFLAGS="$ZFSWRAP_CFLAGS"
	  FSAL_LDFLAGS="$ZFSWRAP_LIBS"
	],[
	  AC_MSG_ERROR(Impossible to find the libzfswrap library)
	])
	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_ZFS/libfsalzfs.la"
        ZFS_CFLAGS="$ZFSWRAP_CFLAGS"
        ZFS_LDFLAGS="$ZFSWRAP_LIBS"
        ZFS_LIB="-lzfswrap"
fi

AC_SUBST(ZFS_CFLAGS)
AC_SUBST(ZFS_LDFLAGS)
AC_SUBST(ZFS_LIB)

if test "$enable_fsal_posix" == "yes"; then
	AC_DEFINE([_USE_POSIX], 1, [GANESHA is compiled with POSIX FSAL])
	POSIX_CFLAGS=
	POSIX_LDFLAGS=
	POSIX_LIB=
fi

AC_SUBST(POSIX_CFLAGS)
AC_SUBST(POSIX_LDFLAGS)
AC_SUBST(POSIX_LIB)

case $RPCAL in
	"TIRPC")
                KRBLIBS=`krb5-config --libs gssapi`
                if test "$KRBLIBS" == ""; then
                   AC_MSG_ERROR( [krb5 libs not found, is krb5-config available...?] )
                else
                   AC_DEFINE(_HAVE_GSSAPI,1,[enable gss in tirpc])
                   LIBS="$KRBLIBS $LIBS -lgssglue"
                fi
		;;
	*)
		AC_MSG_ERROR([This RPCAL is not supported])

esac

AC_SUBST(CFLAGS)
AC_SUBST(FSAL_CFLAGS)
AC_SUBST(FSAL_LDFLAGS)
AC_SUBST(FSAL_LIB)
AC_SUBST(SEC_CFLAGS)
AC_SUBST(SEC_LFLAGS)
AC_SUBST(DEBIAN_DB_DEP)
AC_SUBST(DEBIAN_DB_VERSION)

# for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([include/Makefile
                 include/FSAL/Makefile
                 include/FSAL/FSAL_HPSS/Makefile
                 include/FSAL/FSAL_POSIX/Makefile
                 include/FSAL/FSAL_FUSELIKE/Makefile
                 include/FSAL/FSAL_LUSTRE/Makefile
                 include/FSAL/FSAL_XFS/Makefile
                 include/FSAL/FSAL_GPFS/Makefile
                 include/FSAL/FSAL_ZFS/Makefile
                 Common/Makefile
                 support/Makefile
                 Log/Makefile
                 NodeList/Makefile
                 ConfigParsing/Makefile
                 IdMapper/Makefile
                 SemN/Makefile
                 cidr/Makefile
                 avl/Makefile
                 HashTable/Makefile
                 Cache_inode/Makefile
                 NodeDB/Makefile
                 SAL/Makefile
		 FSAL_UP/Makefile
		 Protocols/Makefile
                 Protocols/9P/Makefile
		 Protocols/DBUS/Makefile
		 Protocols/NFS/Makefile
		 Protocols/NLM/Makefile
		 Protocols/RQUOTA/Makefile
                 Protocols/XDR/Makefile
		 FSAL/Makefile
		 FSAL/FSAL_HPSS/Makefile
		 FSAL/FSAL_HPSS/HPSSclapiExt/Makefile
		 FSAL/FSAL_POSIX/Makefile
		 FSAL/FSAL_PROXY/Makefile
		 FSAL/FSAL_CEPH/Makefile
		 FSAL/FSAL_FUSELIKE/Makefile
		 FSAL/FSAL_LUSTRE/Makefile
		 FSAL/FSAL_XFS/Makefile
		 FSAL/FSAL_GPFS/Makefile
                 FSAL/FSAL_ZFS/Makefile
                 FSAL/FSAL_VFS/Makefile
		 FSAL/FSAL_VFS/pnfs_panfs/Makefile
                 RPCAL/Makefile
                 RPCAL/gssd/Makefile
                 MainNFSD/Makefile
                 test/Makefile
                 snmp_adm/Makefile
                 example-fuse/Makefile
                 Docs/Makefile
                 tools/Makefile
                 config_samples/Makefile
                 check/Makefile
		 check/layers/Makefile
		 check/layers/maketest.conf
		 debian/Makefile
		 debian/rules.auto
                 rpm/Makefile
                 cppcheck-xml/Makefile
                 testres-xml/Makefile
                 Makefile
                 libganeshaNFS.pc
                 nfs-ganesha.spec])
AC_OUTPUT
