#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT( [nfs-ganesha], [1.3.0], [philippe.deniel@cea.fr,thomas.leibovici@cea.fr] )

AC_SUBST(RELEASE, [1])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADER([include/config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_REVISION([Compiles:  i386/x86_64. FSAL: POSIX/PROXY/HPSS/FUSELIKE/LUSTRE/XFS/GPFS/ZFS/VFS .xattr ghost dir support. RPCSEC_GSS/KRB5, TI-RPC (prod),  Early pNFS (alpha) ])

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

# plateform relative flags
case $build in
	*-linux*)
		#DBUILDOS="-D_LINUX -DLINUX"
                MYOS=LINUX
		AC_DEFINE( [LINUX], 1, [Build Operating System is Linux] )
		AC_DEFINE( [_LINUX], 1, [Build Operating System is Linux] )
		EXTRA_LIB=""
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


# Build FSAL's dynamic lib
GA_ENABLE_AM_CONDITION([buildsharedfsal],[when enable, build the FSAL as both static and dynamic library],[BUILD_SHARED_FSAL])

if test "$enable_buildsharedfsal" == "yes"; then
        AC_DEFINE(_BUILD_SHARED_FSAL,1,[Build shared FSAL])
	SHARED_FSAL=1
	SHARED_FSAL_PKG=yes
else
	SHARED_FSAL=0
	SHARED_FSAL_PKG=no
fi
AM_CONDITIONAL(BUILD_SHARED_FSAL, test "$enable_buildsharedfsal" = "yes")


# NLMv4
GA_ENABLE_AM_CONDITION([nlm],[enable NLMv4 support (Lock management for NFSv3)],[USE_NLM])

if test "$enable_nlm" == "yes"; then
	AC_DEFINE(_USE_NLM,1,[enable NLMv4 support])
fi
AM_CONDITIONAL(USE_NLM, test "$enable_nlm" = "yes")


# QUOTA
GA_ENABLE_AM_CONDITION([quota],[enable quota support],[USE_QUOTA])

if test "$enable_quota" == "yes"; then
        AC_DEFINE(_USE_QUOTA,1,[enable quota support])
fi
AM_CONDITIONAL(USE_QUOTA, test "$enable_quota" = "yes")

# Invalidation simulator (to be removed when FSAL_UP will be completed)
GA_ENABLE_AM_CONDITION([upcall-simulator],[enable FSAL upcalls simulator thread],[USE_UPCALL_SIMULATOR])

if test "$enable_upcall_simulator" == "yes"; then
        AC_DEFINE(_USE_UPCALL_SIMULATOR,1,[enable Upcall simulator thread])
fi
AM_CONDITIONAL(USE_UPCALL_SIMULATOR, test "$enable_upcall_simulator" = "yes")


# FSAL_UP
GA_ENABLE_AM_CONDITION([fsal-up],[enable FSAL Upcall Interface support],[USE_FSAL_UP])

if test "$enable_fsal_up" == "yes"; then
        AC_DEFINE(_USE_FSAL_UP,1,[enable FSAL Upcall Interface support])
fi
AM_CONDITIONAL(USE_FSAL_UP, test "$enable_fsal_up" = "yes")

# PNFS/DS
GA_ENABLE_AM_CONDITION([ds],[enable pNFS DS support],[USE_DS])

if test "$enable_ds" == "yes"; then
        AC_DEFINE(_USE_DS,1,[enable pNFS DS support])
fi
AM_CONDITIONAL(USE_DS, test "$enable_ds" = "yes")

# TI-RPC
GA_ENABLE_AM_CONDITION([tirpc],[enable TIRPC support],[USE_TIRPC])

if test "$enable_tirpc" == "yes"; then
	AC_DEFINE(_USE_TIRPC,1,[enable TIRPC support])
	AC_CHECK_LIB([tirpc], [svc_tli_create], [AC_DEFINE( _HAVE_TIRPC,1,[enable TIRPC support] ) ], [AC_MSG_ERROR( [TI-RPC could not be found...] ) ] )
fi

# IPv6
GA_ENABLE_AM_CONDITION([ipv6],[enable IPv6 support (via IPv6)], [USE_TIRPC_IPV6])

if test "$enable_ipv6" == "yes"; then
	AC_DEFINE(_USE_TIRPC_IPV6,1,[enable IPv6 support via TIRPC])
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


CACHE_INODE_DIR="Cache_inode"

AC_SUBST(CACHE_INODE_DIR)

GA_ENABLE_AM_CONDITION([snmp-adm], [export GANESHA statistics with SNMP], [USE_SNMP_ADM])

# for using it in Makefiles
AM_CONDITIONAL(ENABLE_SNMP_ADM,    test "$enable_snmp_adm" = "yes" )

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

# Debugging options
#GA_DISABLE_FLAG( [concurrent-memalloc],  [a thread is not allowed to free a block allocated by another thread], [-D_MONOTHREAD_MEMALLOC] )
GA_DISABLE_FLAG( [prealloc],             [disable memory preallocator, for debug purpose only], [-D_NO_BLOCK_PREALLOC] )
GA_DISABLE_FLAG( [tcp-register], 	 [disable registration of tcp services on portmapper], 		  [-D_NO_TCP_REGISTER] )
GA_DISABLE_FLAG( [portmapper], 	         [disable registration on portmapper], 				  [-D_NO_PORTMAPPER] )
GA_DISABLE_FLAG( [xattr-directory],      [disable ghost xattr directory and files support],               [-D_NO_XATTRD])

GA_ENABLE_FLAG(  [debug-memleaks],       [enable allocator features for tracking memory usage],           [-D_DEBUG_MEMLEAKS] )
GA_ENABLE_FLAG(  [debug-nfsshell],       [enable extended debug traces for ganeshell utility],            [-D_DEBUG_NFS_SHELL] )

GA_ENABLE_FLAG(  [pl-pgsql],		 [enable PGSQL stored procedures (POSIX FSAL)],		          [-D_WITH_PLPGSQL])
GA_ENABLE_FLAG(  [cache-path],		 [Enable entry path caching in POSIX FSAL],	                  [-D_ENABLE_CACHE_PATH])
GA_ENABLE_FLAG(  [handle-mapping],	 [enable NFSv2/3 handle mapping for PROXY FSAL],	          [-D_HANDLE_MAPPING])
GA_ENABLE_FLAG(  [nfs4-locks],	         [enable NFSv4 locks],                                            [-D_WITH_NFSV4_LOCKS])
GA_DISABLE_FLAG( [nfs41-locks],          [disable NFSv4 locks],                                           [-D_WITH_NO_NFSV41_LOCKS])


# for using it in makefiles
AM_CONDITIONAL(ENABLE_HANDLE_MAPPING,    test "$enable_handle_mapping" = "yes" )

GA_ENABLE_FLAG(  [debug-symbols],	 [include debug symbols to binaries (-g option)],	         [-g])

# enable code profiling
GA_ENABLE_FLAG( [profiling], [turn on code profiling (-g and -pg)], [-g -pg] )

# enable code profiling
GA_ENABLE_FLAG( [strict-compile], [turn on strict compiler flags], [-Wall -Werror -Wimplicit -Wformat -Wmissing-braces -Wreturn-type -Wunused-variable -Wuninitialized -Wno-pointer-sign] )

# NFSv4 ACL
GA_ENABLE_AM_CONDITION([nfs4-acl],[disable NFSv4 ACL support],[USE_NO_NFS4_ACL])

if test "$enable_nfs4_acl" == "yes"; then
        AC_CHECK_LIB([ssl],[MD5_Init])
        AC_DEFINE(_USE_NFS4_ACL,1,[enable NFSv4 ACL support])
fi
AM_CONDITIONAL(USE_NFS4_ACL, test "$enable_nfs4_acl" == "yes" )

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

# FSAL switch argument (default is PROXY)
AC_ARG_WITH( [fsal], AS_HELP_STRING([--with-fsal=HPSS|POSIX|PROXY|SNMP|FUSE|LUSTRE|XFS|GPFS|ZFS|VFS|DYNFSAL|SHOOK (default=PROXY)],
             [specify the type of filesystem to be exported] ), FSAL="$withval", FSAL="PROXY")

# exporting USE_FSAL_... for conditional compilation
AM_CONDITIONAL(USE_FSAL_HPSS,     test "$FSAL" = "HPSS")
AM_CONDITIONAL(USE_FSAL_POSIX,    test "$FSAL" = "POSIX")
AM_CONDITIONAL(USE_FSAL_PROXY,    test "$FSAL" = "PROXY")
AM_CONDITIONAL(USE_FSAL_FUSE,     test "$FSAL" = "FUSE")
AM_CONDITIONAL(USE_FSAL_LUSTRE,   test "$FSAL" = "LUSTRE")
AM_CONDITIONAL(USE_FSAL_SHOOK,    test "$FSAL" = "SHOOK") # variant of Lustre FSAL
AM_CONDITIONAL(USE_FSAL_XFS,      test "$FSAL" = "XFS")
AM_CONDITIONAL(USE_FSAL_GPFS,     test "$FSAL" = "GPFS")
AM_CONDITIONAL(USE_FSAL_TEMPLATE, test "$FSAL" = "TEMPLATE")
AM_CONDITIONAL(SHARED_FSAL,       test "$FSAL" = "DYNFSAL")
AM_CONDITIONAL(USE_FSAL_ZFS,      test "$FSAL" = "ZFS")
AM_CONDITIONAL(USE_FSAL_VFS,      test "$FSAL" = "VFS")


AM_CONDITIONAL(RESULT_IS_LIB,      test "$FSAL" = "FUSE")
AM_CONDITIONAL(RESULT_IS_DAEMON,   test "$FSAL" != "FUSE")

# NFSv4.0 NFSv4.1 switch
AC_ARG_WITH( [nfs4-minorversion], AS_HELP_STRING([--with-nfs4-minorversion=0|1  (default=1)], [specify if server support NFSv4 or NFSv4.1] ),
             NFS4_MINOR="$withval", NFS4_MINOR="1" )

AM_CONDITIONAL(USE_NFS4_1, test "$NFS4_MINOR" = "1")
AM_CONDITIONAL(USE_NFS4_0, test "$NFS4_MINOR" = "0")

# RPCAL switch argument (default is ONCRPC)
AC_ARG_WITH( [rpcal], AS_HELP_STRING([--with-rpcal=ONCRPC|GSSRPC|TIRPC (default=ONCRPC)], [specify the type of RPCAL to be used] ),
              RPCAL="$withval", RPCAL="ONCRPC")
AM_CONDITIONAL(USE_GSSRPC, test "$RPCAL" = "GSSRPC" )
AM_CONDITIONAL(HAVE_GSSAPI, test "$RPCAL" = "GSSRPC" )
AM_CONDITIONAL(USE_TIRPC,  test "$RPCAL" = "TIRPC" )
AM_CONDITIONAL(HAVE_GSSAPI, test "$RPCAL" = "TIRPC" )
AM_CONDITIONAL(USE_ONCRPC, test "$RPCAL" = "ONCRPC" )

# Enable 9P Support
GA_ENABLE_AM_CONDITION([9p],[enable 9P support], [USE_9P])
if test "$enable_9p" == "yes"; then
	AC_DEFINE(_USE_9P, 1, [enable 9P support])
fi


# IPv6
GA_ENABLE_AM_CONDITION([ipv6],[enable IPv6 support (via IPv6)], [USE_TIRPC_IPV6])
if test "$enable_ipv6" == "yes"; then
	AC_DEFINE(_USE_TIRPC_IPV6,1,[enable IPv6 support via TIRPC])
	if test "$RPCAL" != "TIRPC" ; then
		AC_MSG_ERROR([IPv6 support is available only when --with-rpcal=TIRPC is used])
	fi
fi

# BuddyMalloc
GA_DISABLE_AM_CONDITION([BuddyMalloc], [disable GANESHA's memory allocator, for debug purpose only], [_NO_BUDDY_SYSTEM])

echo "enable_BuddyMalloc=$enable_BuddyMalloc"

if test "$enable_BuddyMalloc" == "yes"; then
        AC_DEFINE(_BUDDY_SYSTEM, 1, [enable GANESHA's memory allocator])
else
	AC_DEFINE(_NO_BUDDY_SYSTEM, 1, [disable GANESHA's memory allocator, for debug purpose only])
fi
AM_CONDITIONAL(USE_BUDDY_SYSTEM, test "$enable_BuddyMalloc" == "yes")

# PNFS switch argument (default is nothing)
AC_ARG_WITH( [pnfs], AS_HELP_STRING([--with-pnfs=PARALLEL_FS|NONE (default=NONE)], [specify the type of pNFS layout to be used] ),
	PNFS="$withval", PNFS="NONE" )

AM_CONDITIONAL(USE_PNFS_NONE, 		test "$PNFS" = "NONE" )
AM_CONDITIONAL(USE_PNFS_PARALLEL_FS,	test "$PNFS" = "PARALLEL_FS" )
AM_CONDITIONAL(USE_PNFS_OSD, 	        test "$PNFS" = "OSD" )
AM_CONDITIONAL(USE_PNFS_BLOCK, 	        test "$PNFS" = "BLOCK" )

AM_CONDITIONAL(USE_PNFS,                 test "$PNFS" != "NONE" )


# MFSL switch argument (default is NULL )
AC_ARG_WITH( [mfsl], AS_HELP_STRING([--with-mfsl=NONE|NULL|ASYNC|AIO|MIRROR|PROXY_RPCSECGSS (default=NULL)],[specify the type of intermediate FSAL module to be used] ),
	     MFSL="$withval", MFSL="NONE" )

AM_CONDITIONAL(USE_MFSL_NONE,            test "$MFSL" = "NONE")
AM_CONDITIONAL(USE_MFSL_AIO,             test "$MFSL" = "AIO")
AM_CONDITIONAL(USE_MFSL_NULL,            test "$MFSL" = "NULL")
AM_CONDITIONAL(USE_MFSL_ASYNC,           test "$MFSL" = "ASYNC")
AM_CONDITIONAL(USE_MFSL_PROXY_RPCSECGSS, test "$MFSL" = "PROXY_RPCSECGSS")

AM_CONDITIONAL(USE_MFSL,                 test "$MFSL" != "NONE" )

# Database switch (for POSIX FSAL)
AC_ARG_WITH( [db], AS_HELP_STRING([--with-db=MYSQL|PGSQL (default=MYSQL)],[specify the database engine for POSIX FSAL] ),
	     DBTYPE="$withval", DBTYPE="MYSQL" )

AM_CONDITIONAL(USE_PGSQL, test "$DBTYPE" = "PGSQL")
AM_CONDITIONAL(USE_MYSQL, test "$DBTYPE" = "MYSQL")


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

case $FSAL in
	HPSS)
		# HPSS specific options
		AC_ARG_WITH( [hpss-version], AS_HELP_STRING([--with-hpss-version=5.1|6.2|6.2.2|7.1|7.3 (default=6.2.2)],
			[specify HPSS version] ), HPSS="$withval", HPSS="6.2.2" )
		GA_ENABLE_FLAG(  [strip-cs-uuid], [Strip core server UUID from handle (shorter NFS handle)],
				 [-D_STRIP_CORESERVER_UUID])

		AC_DEFINE([_USE_HPSS], 1, [GANESHA is compiled with HPSS FSAL])
		FSAL_CFLAGS="-I/opt/hpss/include"
		FSAL_LDFLAGS="-L/opt/hpss/lib -Wl,-rpath,/opt/hpss/lib -lhpss"
        	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_HPSS/libfsalhpss.la \$(top_builddir)/FSAL/FSAL_HPSS/HPSSclapiExt/libhpssapiext.la"
               	FS_NAME="hpss"

		case "$HPSS" in
			"5.1")
				FSAL_LDFLAGS="$FSAL_LDFLAGS -L/opt/dcelocal/lib -ldce -ldcepthreads"
				;;
		esac
		;;
	PROXY)
		AC_DEFINE([_USE_PROXY], 1, [GANESHA is compiled with NFS PROXY FSAL])
                FSAL_CFLAGS=
                FSAL_LDFLAGS=$SEC_RPATH
		if test "$enable_handle_mapping" == "yes"; then
			FSAL_LIB="\$(top_builddir)/FSAL/FSAL_PROXY/libfsalproxy.la \$(top_builddir)/FSAL/FSAL_PROXY/handle_mapping/libhandlemapping.la"
			FSAL_LDFLAGS="$FSAL_LDFLAGS -lsqlite3"
		else
                	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_PROXY/libfsalproxy.la"
		fi
                FS_NAME="proxy"
                ;;
	FUSE)
		AC_DEFINE([_USE_FUSE], 1, [GANESHA is compiled as a FUSE-like library])
		FSAL_CFLAGS=
                FSAL_LDFLAGS=""
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_FUSELIKE/libfsalfuse.la"
		FS_NAME="fuse"
		;;
	LUSTRE)
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

        FSAL_CFLAGS=
        FSAL_LDFLAGS="-llustreapi"
        FSAL_LIB="\$(top_builddir)/FSAL/FSAL_LUSTRE/libfsallustre.la"
		FS_NAME="lustre"
		;;

    SHOOK)
        # Lustre FSAL compiled with special compilation directives
        AC_CHECK_LIB([shooksvr], [shook_restore],, AC_MSG_ERROR([libshooksvr not found]))
        AC_DEFINE(_SHOOK, 1, [shook is enabled])

		AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing xattr header files)])
		AC_CHECK_LIB([lustreapi], [llapi_fid2path], [have_lustre2_api="yes"])
		if test "x$have_lustre2_api" != "xyes" ; then
	            AC_MSG_ERROR([Lustre 2.x and liblustreapi are required])
		fi
		AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystem])
        FSAL_CFLAGS=
        FSAL_LDFLAGS="-lshooksvr -llustreapi"
        FSAL_LIB="\$(top_builddir)/FSAL/FSAL_LUSTRE/libfsalshook.la"
		FS_NAME="shook"
        ;;

	XFS)
		AC_DEFINE([_USE_XFS], 1, [GANESHA exports XFS Filesystem])
 		AC_CHECK_HEADERS([xfs/xfs.h], [], [AC_MSG_ERROR( missing xfs include files)])
                AC_CHECK_HEADERS([xfs/handle.h], [], [AC_MSG_ERROR(missimg xfs include files)])
		AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing xattr header files)])
		FSAL_CFLAGS=
                FSAL_LDFLAGS="-lhandle"
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_XFS/libfsalxfs.la"
		FS_NAME="xfs"
		;;
        DYNFSAL)
               	AC_DEFINE( _USE_SHARED_FSAL, 1, [enable shared FSAL] )
		AM_CONDITIONAL( SHARED_FSAL, test "$FSAL" = "DYNFSAL" )
		FSAL_CFLAGS=-rdynamic
                FSAL_LDFLAGS="-ldl"
		FSAL_LIB=
		FS_NAME="dynfsal"
                ;;
	GPFS)
                # The following are needed in many GPFS environments
                CFLAGS="$CFLAGS -I/usr/lpp/mmfs/include"
                LDFLAGS="$LDFLAGS -L/usr/lpp/mmfs/lib"

		AC_DEFINE([_USE_GPFS], 1, [GANESHA exports GPFS Filesystem])


		AC_CHECK_HEADERS([gpfs_nfs.h], [], [AC_MSG_ERROR(missing gpfs include files)], [
                #include <fcntl.h>
                ])

                AC_CHECK_MEMBER([struct file_handle.f_handle], [
                                        AC_DEFINE([_GPFS_DECLARES_HANDLE], 1, [GPFS declares the handle structure])
                                      ],
                                      [],
                                      [
                                        #include <fcntl.h>
                                        #include <gpfs_nfs.h>
                                      ])

		AC_CHECK_DECL([gpfs_ganesha], [], [AC_MSG_ERROR(missing gpfs_ganesha function)], [
                  #include <fcntl.h>
                  #include <gpfs_nfs.h>
                ])

		AC_CHECK_DECLS([OPENHANDLE_NAME_TO_HANDLE,
                                OPENHANDLE_OPEN_BY_HANDLE,
                                OPENHANDLE_LINK_BY_FD,
		                OPENHANDLE_READLINK_BY_FD,
                                OPENHANDLE_STAT_BY_HANDLE,
                                XATTR_STAT,
                                XATTR_MODE,
                                XATTR_UID,
                                XATTR_GID,
                                XATTR_ATIME_SET,
                                XATTR_MTIME_SET,
                                XATTR_ACL
                                ],
		               [],
		               [AC_MSG_ERROR(GPFS < 3.5.0 found, you must upgrade GPFS to use this feature)],
		               [
                                 #include <fcntl.h>
                                 #include <gpfs_nfs.h>
                               ])

		if [[  $PNFS != "NONE" ]] ; then
			AC_CHECK_DECLS([OPENHANDLE_LAYOUT_TYPE, OPENHANDLE_GET_DEVICEINFO, OPENHANDLE_GET_DEVICEITER,
			                OPENHANDLE_LAYOUT_GET, OPENHANDLE_LAYOUT_RETURN],
			               [],
			               [AC_MSG_ERROR(missing Layout function)],
			               [
                                         #include <fcntl.h>
                                         #include <gpfs_nfs.h>
                                       ])
		fi
		AC_CHECK_LIB([gpfs], [gpfs_ganesha], [], [AC_MSG_ERROR(missing gpfs library)])
		FSAL_CFLAGS=
		FSAL_LDFLAGS=""
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_GPFS/libfsalgpfs.la"
		FS_NAME="gpfs"
		;;
	VFS)
		AC_DEFINE([_USE_VFS], 1, [GANESHA exports VFS Filesystem (kernel is >= 2.6.39])
		FSAL_CFLAGS=
                FSAL_LDFLAGS=""
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_VFS/libfsalvfs.la"
		FS_NAME="vfs"
		;;
        ZFS)
                AC_DEFINE([_USE_ZFS], 1, [GANESHA exports ZFS Filesystem])
                PKG_CHECK_MODULES(ZFSWRAP, libzfswrap,
                [
                  FSAL_CFLAGS="$ZFSWRAP_CFLAGS"
                  FSAL_LDFLAGS="$ZFSWRAP_LIBS"
                ],[
                  AC_MSG_ERROR(Impossible to find the libzfswrap library)
                ])
                FSAL_LIB="\$(top_builddir)/FSAL/FSAL_ZFS/libfsalzfs.la"
                FS_NAME="zfs"
                ;;
	POSIX)

		case "$DBTYPE" in

		"PGSQL")
			# define PG_CONFIG program name and PGSQL_VERSION
			GA_PGSQL_INFO

			DBEXT_INC_PATH=`$PG_CONFIG --includedir`
			DBEXT_LIB_PATH=`$PG_CONFIG --libdir`
			DBEXT_FLAGS="-I$DBEXT_INC_PATH"

			DEBIAN_DB_DEP="postgresql"
			DEBIAN_DB_VERSION="7.1"

			case "$MYOS" in
				"LINUX")
					DBEXT_LDADD="-L$DBEXT_LIB_PATH -lcrypt -lpq"
					;;
				"MACOS")
					DBEXT_LDADD="-L$DBEXT_LIB_PATH -lpq"
					;;
				"FREEBSD")
					DBEXT_LDADD="-L$DBEXT_LIB_PATH -lpq"
					;;
				"SOLARIS")
					DBEXT_LDADD="-L$DBEXT_LIB_PATH -lpq"
					;;
			esac

			case "$PGSQL_VERSION" in
				"7."*)
					AC_DEFINE([_USE_PGSQL], 1, [Using PostgreSQL database])
					;;
				"8."*|"9."*)
					AC_DEFINE([_USE_PGSQL], 1, [Using PostgreSQL database])
					AC_DEFINE([_PGSQL8], 1, [Using PostgreSQL version >= 8.x])
					;;
				*)
					AC_MSG_ERROR("Unsupported PostgreSQL version: $PGSQL_VERSION")
			esac
			;;

		"MYSQL")

			# define MYSQL_CONFIG program name and MYSQL_VERSION
			GA_MYSQL_INFO

			DBEXT_LDADD=`$MYSQL_CONFIG --libs_r`
			DBEXT_FLAGS=`$MYSQL_CONFIG --include`

			DEBIAN_DB_DEP="mysql"
			DEBIAN_DB_VERSION="5.0"

			AC_DEFINE([_USE_MYSQL], 1, [Using MySQL database])
			;;
		esac

		AC_DEFINE([_USE_POSIX], 1, [GANESHA is compiled with POSIX FSAL])
                FSAL_CFLAGS="$DBEXT_FLAGS"
                FSAL_LDFLAGS="$DBEXT_LDADD"
                FSAL_LIB="\$(top_builddir)/FSAL/FSAL_POSIX/libfsalposix.la \$(top_builddir)/FSAL/FSAL_POSIX/DBExt/$DBTYPE/libfsaldbext.la"
                FS_NAME="posix"
                ;;
	*)
		AC_MSG_ERROR([This FSAL is not supported])
esac

case $MFSL in
        NONE)
		MFS_LIB=""
		;;
	NULL)
		AC_DEFINE([_USE_MFSL], 1, [Use MFSL module])
		AC_DEFINE([_USE_MFSL_NULL], 1, [Use MFSL_NULL module which calls directly the FSAL functions])
		MFSL_LIB="\$(top_builddir)/MFSL/MFSL_NULL/libmfslnull.la"
		;;
	ASYNC)
		AC_DEFINE([_USE_MFSL], 1, [Use MFSL module])
		AC_DEFINE([_USE_MFSL_ASYNC], 1, [Use MFSL_ASYNC module which calls the FSAL functions asynchronously])
		MFSL_LIB="\$(top_builddir)/MFSL/MFSL_ASYNC/libmfslasync.la"
		;;
	AIO)
		AC_DEFINE([_USE_MFSL], 1, [Use MFSL module])
		AC_DEFINE([_USE_MFSL_AIO], 1, [Use MFSL_AIO module with asynchronous FSAL read and write ])
		MFSL_LIB="\$(top_builddir)/MFSL/MFSL_AIO/libmfslaio.la"
		;;
	PROXY_RPCSECGSS)
		AC_DEFINE([_USE_MFSL], 1, [Use MFSL module])
		AC_DEFINE([_USE_MFSL_PROXY_RPCSECGSS], 1, [Use MFSL_PROXY_RPCSECGSS, wraps the FSAL call for RPCSECGSS client side in proxy])
		MFSL_LIB="\$(top_builddir)/MFSL/MFSL_PROXY_RPCSECGSS/libmfslproxy_rpcsecgss.la"
		;;
	*)
		AC_MSG_ERROR([This MFSL is not supported yet])

esac

case $RPCAL in
	"ONCRPC")
		;;
	"GSSRPC")
                AC_DEFINE(_USE_GSSRPC,1,[enable security management with GSSRPC])
	        # ==> Add here libraries needed for GSSRPC
                AC_CHECK_LIB([gssapi_krb5], [gss_import_name], [AC_DEFINE( _HAVE_GSSAPI,1,[enable GSSAPI support] ) ],
			     [AC_MSG_ERROR( [GSSAPI could not be found...] ) ] )
                AC_CHECK_LIB([gssrpc], [gssrpc_authunix_create_default], [AC_DEFINE( _HAVE_GSSRPC,1,[enable GSSRPC support] ) ],
			      [AC_MSG_ERROR( [GSSRPC_KRB5 could not be found...] ) ] )
		;;
	"TIRPC")
                PKG_CHECK_MODULES(TIRPC, libtirpc,
                  [
                        AC_DEFINE(_USE_TIRPC,1,[enable TIRPC support])
                        AC_DEFINE( _HAVE_TIRPC,1,[enable TIRPC support])
                        CFLAGS="$CFLAGS $TIRPC_CFLAGS"
                        LIBS="$TIRPC_LIBS $LIBS"
                  ],
                  [
                        AC_MSG_ERROR( [TI-RPC could not be found...] )
                  ]
                )

                KRBLIBS=`krb5-config --libs gssapi`
                if test "$KRBLIBS" == ""; then
                   AC_MSG_ERROR( [krb5 libs not found, is krb5-config available...?] )
                else
                   AC_DEFINE(_HAVE_GSSAPI,1,[enable gss in tirpc])
                   LIBS="$KRBLIBS $LIBS"
                fi
		;;
	*)
		AC_MSG_ERROR([This RPCAL is not supported])

esac

case $PNFS in
	"NONE")
		;;
	"PARALLEL_FS")
		AC_DEFINE(_USE_PNFS,1,[enable NFSv4.1/pNFS support])
		AC_DEFINE(_USE_PNFS_PARALLEL_FS,1,[enable NFSv4.1/pNFS support for parallel file system ])
		PNFS_LIB="\$(top_builddir)/Protocols/PNFS/libpnfs.la"
		;;
	"OSD")
		AC_DEFINE(_USE_PNFS,1,[enable NFSv4.1/pNFS support])
		AC_DEFINE(_USE_PNFS_OSD,1,[enable OSD based NFSv4.1/pNFS support])
		PNFS_LIB="\$(top_builddir)/Protocols/PNFS/libpnfs.la"
		;;
	"BLOCK")
		AC_DEFINE(_USE_PNFS,1,[enable NFSv4.1/pNFS support])
		AC_DEFINE(_USE_PNFS_BLOCK,1,[enable Block Device based NFSv4.1/pNFS support])
		PNFS_LIB="\$(top_builddir)/Protocols/PNFS/libpnfs.la"
		;;
	*)
		AC_MSG_ERROR([This pNFS layout is not supported])
esac

case $NFS4_MINOR in
	"0")
		AC_DEFINE([_USE_NFS4_0],1, [Use NFSv4.0] )
		;;
	"1")
		AC_DEFINE([_USE_NFS4_1],1, [Use NFSv4.1] )
		;;
	*)
		AC_MSG_ERROR([This NFSv4 minor version is not supported])
esac

# Sanity check : no pnfs if NFS4_MINOR is not NFSv4.1
if [[  $PNFS != "NONE" ]] ; then
  if [[ $NFS4_MINOR == "0" ]] ; then
     AC_MSG_ERROR("You must activate NFSv4.1 support to enable pnfs support" )
  fi
fi

# Sanity check : no ds if NFS4_MINOR is not NFSv4.1
if [[  $enable_ds == "yes" ]] ; then
  if [[ $NFS4_MINOR == "0" ]] ; then
     AC_MSG_ERROR("You must activate NFSv4.1 support to enable pnfs DS support" )
  fi
fi


# MFSL_ASYNC should not be used with HPSS
if [[ $FSAL == "HPSS" ]] ; then
  if [[ $MFSL == "ASYNC" ]] ; then
		AC_MSG_ERROR([FSAL_HPSS and MFSL_ASYNC are incompatible])
  fi
fi

AC_SUBST(CFLAGS)
AC_SUBST(FSAL_CFLAGS)
AC_SUBST(FSAL_LDFLAGS)
AC_SUBST(FSAL_LIB)
AC_SUBST(MFSL_LIB)
AC_SUBST(PNFS_LIB)
AC_SUBST(FS_NAME)
AC_SUBST(SHARED_FSAL)
AC_SUBST(SHARED_FSAL_PKG)
AC_SUBST(SEC_CFLAGS)
AC_SUBST(SEC_LFLAGS)
AC_SUBST(DEBIAN_DB_DEP)
AC_SUBST(DEBIAN_DB_VERSION)

# for exporting to spec file
AC_SUBST(ac_configure_args)

# for using fs name in binairies
AC_DEFINE_UNQUOTED([FS_NAME], "$FS_NAME", [Name of the filesystem])

AC_CONFIG_FILES([include/Makefile
                 include/FSAL/Makefile
                 include/FSAL/FSAL_HPSS/Makefile
                 include/FSAL/FSAL_POSIX/Makefile
                 include/FSAL/FSAL_PROXY/Makefile
                 include/FSAL/FSAL_FUSELIKE/Makefile
                 include/FSAL/FSAL_LUSTRE/Makefile
                 include/FSAL/FSAL_XFS/Makefile
                 include/FSAL/FSAL_GPFS/Makefile
                 include/FSAL/FSAL_ZFS/Makefile
                 include/FSAL/FSAL_VFS/Makefile
                 include/FSAL/FSAL_TEMPLATE/Makefile
                 include/MFSL/Makefile
                 include/MFSL/MFSL_NULL/Makefile
                 include/MFSL/MFSL_AIO/Makefile
                 include/MFSL/MFSL_ASYNC/Makefile
                 include/MFSL/MFSL_PROXY_RPCSECGSS/Makefile
                 include/PNFS/Makefile
		 include/PNFS/PARALLEL_FS/Makefile
		 include/PNFS/OSD/Makefile
		 include/PNFS/BLOCK/Makefile
                 BuddyMalloc/Makefile
                 Common/Makefile
                 support/Makefile
                 Log/Makefile
                 NodeList/Makefile
                 ConfigParsing/Makefile
                 IdMapper/Makefile
                 RW_Lock/Makefile
                 SemN/Makefile
                 cidr/Makefile
                 LRU/Makefile
                 avl/Makefile
                 HashTable/Makefile
                 Cache_inode/Makefile
                 SAL/Makefile
		 FSAL_UP/Makefile
		 FSAL_UP/DUMB/Makefile
                 File_Content/Makefile
                 File_Content_Policy/Makefile
		 Protocols/Makefile
                 Protocols/9P/Makefile
		 Protocols/NFS/Makefile
		 Protocols/NLM/Makefile
		 Protocols/PNFS/Makefile
		 Protocols/PNFS/PARALLEL_FS/Makefile
		 Protocols/PNFS/OSD/Makefile
		 Protocols/PNFS/BLOCK/Makefile
                 Protocols/XDR/Makefile
		 FSAL/Makefile
		 FSAL/FSAL_HPSS/Makefile
		 FSAL/FSAL_HPSS/HPSSclapiExt/Makefile
		 FSAL/FSAL_POSIX/Makefile
		 FSAL/FSAL_POSIX/DBExt/Makefile
		 FSAL/FSAL_POSIX/DBExt/PGSQL/Makefile
		 FSAL/FSAL_POSIX/DBExt/MYSQL/Makefile
		 FSAL/FSAL_PROXY/Makefile
		 FSAL/FSAL_PROXY/handle_mapping/Makefile
		 FSAL/FSAL_FUSELIKE/Makefile
		 FSAL/FSAL_LUSTRE/Makefile
		 FSAL/FSAL_LUSTRE/PNFS/Makefile
		 FSAL/FSAL_XFS/Makefile
		 FSAL/FSAL_GPFS/Makefile
                 FSAL/FSAL_ZFS/Makefile
                 FSAL/FSAL_VFS/Makefile
		 FSAL/FSAL_TEMPLATE/Makefile
		 MFSL/Makefile
		 MFSL/MFSL_NULL/Makefile
		 MFSL/MFSL_ASYNC/Makefile
		 MFSL/MFSL_AIO/Makefile
		 MFSL/MFSL_PROXY_RPCSECGSS/Makefile
                 RPCAL/Makefile
                 RPCAL/ONCRPC/Makefile
                 RPCAL/GSSRPC/Makefile
                 RPCAL/TIRPC/Makefile
                 MainNFSD/Makefile
                 test/Makefile
                 cmdline_tools/Makefile
                 shell/Makefile
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
