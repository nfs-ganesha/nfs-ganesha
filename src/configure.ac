#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT( [nfs-ganesha], [0.99.55], [philippe.deniel@cea.fr,thomas.leibovici@cea.fr] )
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADER([include/config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_REVISION([Compiles:  i386/x86_64. FSAL:  POSIX/SNMP/PROXY/HPSS/FUSELIKE/LUSTRE(all prod) .xattr ghost dir support. RPCSEC_GSS/KRB5 (prod). Locks(rc). TI-RPC (prod), IPv6 (alpha), MFSL_ASYNC(beta) ])

# Init Automake
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# check for _GNU_SOURCE and set it in config.h
AC_GNU_SOURCE

# Uuse libtool
AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_YACC
AC_PROG_LEX

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

CFLAGS="$CFLAGS -D_REENTRANT"


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

# RPSEC_GSS from krb5 distribution
GA_ENABLE_AM_CONDITION([gssrpc],[enable GSSRPC security management (from MIT kerberos implementation)],[USE_GSSRPC])

if test "$enable_gssrpc" == "yes"; then
        AC_DEFINE(_USE_GSSRPC,1,[enable security management with GSSRPC])
	# ==> Add here libraries needed for GSSRPC
        AC_CHECK_LIB([gssapi], [gss_import_name], [AC_DEFINE( _HAVE_GSSAPI,1,[enable GSSAPI support] ) ], [AC_MSG_ERROR( [GSSAPI could not be found...] ) ] )
        AC_CHECK_LIB([gssrpc], [gssrpc_authunix_create_default], [AC_DEFINE( _HAVE_GSSRPC,1,[enable GSSRPC support] ) ], [AC_MSG_ERROR( [GSSRPC_KRB5 could not be found...] ) ] )
fi

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
	

GA_ENABLE_AM_CONDITION([efence],[link with efence memory debug library],[USE_EFENCE])

if test "$enable_efence" == "yes" ; then 
	EFENCE=-lefence 
else
	EFENCE=
fi

AC_SUBST(EFENCE)

# Debugging options
GA_DISABLE_FLAG( [concurrent-memalloc],  [a thread is not allowed to free a block allocated by another thread], [-D_MONOTHREAD_MEMALLOC] )
GA_DISABLE_FLAG( [buddy-malloc],         [disable GANESHA's memory allocator, for debug purpose only],   [-D_NO_BUDDY_SYSTEM] )
GA_DISABLE_FLAG( [block-prealloc],       [disable GANESHA's memory chunks allocator, for debug purpose only],   [-D_NO_BLOCK_PREALLOC] )
GA_DISABLE_FLAG( [tcp-register], 	 [disable registration of tcp services on portmapper], 		 [-D_NO_TCP_REGISTER] )
GA_DISABLE_FLAG( [portmapper], 	         [disable registration on portmapper], 				 [-D_NO_PORTMAPPER] )

GA_ENABLE_FLAG(  [memcorrupt-check],     [enable allocator features for tracking memory corruption],     [-D_DETECT_MEMCORRUPT] )
GA_ENABLE_FLAG(  [debug-memalloc],       [enable debug traces in memory allocator],                      [-D_DEBUG_MEMALLOC] )
GA_ENABLE_FLAG(  [debug-states],         [enable debug traces for nfsv4 states],                         [-D_DEBUG_STATES] )
GA_ENABLE_FLAG(  [debug-memleaks],       [enable allocator features for tracking memory usage],          [-D_DEBUG_MEMLEAKS] )
GA_ENABLE_FLAG(  [debug-fsal],		 [enable debug traces in FileSystem Abstraction Layer],          [-D_DEBUG_FSAL] )
GA_ENABLE_FLAG(  [debug-nfsproto],       [enable extended debug traces for NFS/MOUNT protocols],         [-D_DEBUG_NFSPROTO] )
GA_ENABLE_FLAG(  [debug-nfsv4],		 [enable debug traces related to NFSv4 protocol],                [-D_DEBUG_NFS_V4] )
GA_ENABLE_FLAG(  [debug-nfsv4-pseudo],   [enable debug traces for NFSv4 pseudofs],                       [-D_DEBUG_NFS_V4_PSEUDO] )
GA_ENABLE_FLAG(  [debug-filehandle],     [enable debug traces retated to NFS File handle],               [-D_DEBUG_FILEHANDLE] )
GA_ENABLE_FLAG(  [debug-nfsshell],       [enable extended debug traces for ganeshell utility],           [-D_DEBUG_NFS_SHELL] )  
GA_ENABLE_FLAG(  [debug-dispatch],       [enable extended debug traces about RPC dispatch],              [-D_DEBUG_DISPATCH] ) 
GA_ENABLE_FLAG(  [debug-data-cache],     [enable extended debug traces about internal datacache],        [-D_DEBUG_CACHE_CONTENT] )
GA_ENABLE_FLAG(  [debug-metadata-cache], [enable extended debug traces for internal metadata cache],     [-D_DEBUG_CACHE_INODE] )         
GA_ENABLE_FLAG(  [debug-mdcache-gc],     [enable extened debug traces for md cache garbagge collection], [-D_DEBUG_CACHE_INODE_GC] )
GA_ENABLE_FLAG(  [debug-hashtable],      [enable extended debug traces about the hash tables],           [-D_DEBUG_HASHTABLE] ) 
GA_ENABLE_FLAG(  [debug-lru],            [enable extended debug traces on LRU lists],                    [-D_DEBUG_LRU] )
GA_ENABLE_FLAG(  [debug-dupreq],         [enable extended debug traces on Duplicate Request Cache],      [-D_DEBUG_DUPREQ] ) 
GA_ENABLE_FLAG(  [debug-log],            [enable extended debug traces about the logging module],        [-D_LOG_DEBUG] ) 
GA_ENABLE_FLAG(  [debug-rpcsecgss],      [enable extended debug traces about RPCSEC_GSS],                [-D_DEBUG_RPCSEC_GSS] ) 
GA_ENABLE_FLAG(  [full-debug],           [enable FULL_DEBUG logging level (very verbose)],               [-D_FULL_DEBUG] )
GA_ENABLE_FLAG(  [pl-pgsql],		 [enable PGSQL stored procedures (POSIX FSAL)],		         [-D_WITH_PLPGSQL])
GA_ENABLE_FLAG(  [cache-path],		 [Enable entry path caching in POSIX FSAL],	                 [-D_ENABLE_CACHE_PATH])
GA_ENABLE_FLAG(  [handle-mapping],	 [enable NFSv2/3 handle mapping for PROXY FSAL],	         [-D_HANDLE_MAPPING])
GA_ENABLE_FLAG(  [nfs4-stateid],	 [enable advanced NFSv4 stateid mechanism (experimental) ],      [-D_WITH_STATEID])
GA_DISABLE_FLAG( [xattr-directory],      [disable ghost xattr directory and files support],              [-D_NO_XATTRD])

# for using it in makefiles
AM_CONDITIONAL(ENABLE_HANDLE_MAPPING,    test "$enable_handle_mapping" = "yes" )

GA_ENABLE_FLAG(  [debug-symbols],	 [include debug symbols to binaries (-g option)],	         [-g])

# makes it possible to add some extra include dir

AC_ARG_WITH( [extra-includedir], AS_HELP_STRING([--with-extra-includedir=<path>], [Alternative path to include files] ),
		EXTRA_INCLUDE_DIR="$withval" ) 

if test "${with_extra_includedir+set}" == "set"; then
	CFLAGS="$CFLAGS -I$EXTRA_INCLUDE_DIR"
fi


# FSAL switch argument (default is PROXY)
AC_ARG_WITH( [fsal], AS_HELP_STRING([--with-fsal=HPSS|POSIX|PROXY|SNMP|GHOSTFS|FUSE|LUSTRE (default=PROXY)],[specify the type of filesystem to be exported] ),
	     FSAL="$withval", FSAL="PROXY")

# exporting USE_FSAL_... for conditional compilation
AM_CONDITIONAL(USE_FSAL_HPSS,    test "$FSAL" = "HPSS")
AM_CONDITIONAL(USE_FSAL_GHOSTFS, test "$FSAL" = "GHOSTFS")
AM_CONDITIONAL(USE_FSAL_POSIX,   test "$FSAL" = "POSIX")
AM_CONDITIONAL(USE_FSAL_PROXY,   test "$FSAL" = "PROXY")
AM_CONDITIONAL(USE_FSAL_SNMP,    test "$FSAL" = "SNMP")
AM_CONDITIONAL(USE_FSAL_FUSE,    test "$FSAL" = "FUSE")
AM_CONDITIONAL(USE_FSAL_LUSTRE,   test "$FSAL" = "LUSTRE")
AM_CONDITIONAL(USE_FSAL_TEMPLATE, test "$FSAL" = "TEMPLATE")

# MFSL switch argument (default is NULL )
AC_ARG_WITH( [mfsl], AS_HELP_STRING([--with-mfsl=NONE|NULL|ASYNC|MIRROR|PROXY_RPCSECGSS (default=NULL)],[specify the type of intermediate FSAL module to be used] ),
	     MFSL="$withval", MFSL="NONE" )

AM_CONDITIONAL(USE_MFSL_NONE,            test "$MFSL" = "NONE")
AM_CONDITIONAL(USE_MFSL_NULL,            test "$MFSL" = "NULL")
AM_CONDITIONAL(USE_MFSL_ASYNC,           test "$MFSL" = "ASYNC")
AM_CONDITIONAL(USE_MFSL_PROXY_RPCSECGSS, test "$MFSL" = "PROXY_RPCSECGSS")
AM_CONDITIONAL(USE_MFSL,                 test "$MFSL" != "NONE" ) 

# HPSS version switch 
AC_ARG_WITH( [hpss-version], AS_HELP_STRING([--with-hpss-version=5.1|6.2|6.2.2 (default=6.2.2)],[specify HPSS version] ),
		HPSS="$withval", HPSS="6.2.2" )

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
#FS_ALL_LIST="PROXY SNMP FUSE POSIX LUSTRE"
FS_ALL_LIST="PROXY SNMP FUSE POSIX"

case $FSAL in
	HPSS)
		FSAL_CFLAGS="-I/opt/hpss/include"
		FSAL_LDFLAGS="-L/opt/hpss/lib -Wl,-rpath,/opt/hpss/lib -lhpss"
        	FSAL_LIB="\$(top_builddir)/FSAL/FSAL_HPSS/libfsalhpss.la \$(top_builddir)/FSAL/FSAL_HPSS/HPSSclapiExt/libhpssapiext.la"
               	FS_NAME="hpss"
		FS_ALL_LIST="HPSS PROXY SNMP FUSE POSIX"

		case "$HPSS" in
			"5.1")
				AC_DEFINE([_USE_HPSS_51], 1, [Using HPSS version 5.1])
				FSAL_LDFLAGS="$FSAL_LDFLAGS -L/opt/dcelocal/lib -ldce -ldcepthreads"
				;;
			"6.2")
				AC_DEFINE([_USE_HPSS_62], 1, [Using HPSS version 6.2])
                                ;;
			"6.2.2")
				AC_DEFINE([_USE_HPSS_622], 1, [Using HPSS version 6.2.2])
                                ;;
			*)
				AC_MSG_ERROR([Never heard about this HPSS release ! ($HPSS)])
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
	SNMP)
		AC_DEFINE([_USE_SNMP], 1, [GANESHA is compiled with SNMP FSAL])
                case "$MYOS" in 
			FREEBSD) 
				FSAL_CFLAGS=`$NETSNMP_CONFIG --cflags`
				;;
			*)
				FSAL_CFLAGS=""
				;;
		esac
                FSAL_LDFLAGS=`$NETSNMP_CONFIG --agent-libs`
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_SNMP/libfsalsnmp.la"
		FS_NAME="snmp"
		;;
	FUSE)
		AC_DEFINE([_USE_FUSE], 1, [GANESHA is compiled as a FUSE-like library])
		FSAL_CFLAGS=
                FSAL_LDFLAGS=
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_FUSELIKE/libfsalfuse.la"
		FS_NAME="fuse"
		;;
	LUSTRE)
		AC_DEFINE([_USE_LUSTRE], 1, [GANESHA exports Lustre Filesystems])
		FSAL_CFLAGS=
                FSAL_LDFLAGS="-llustreapi"
		FSAL_LIB="\$(top_builddir)/FSAL/FSAL_LUSTRE/libfsallustre.la"
		FS_NAME="lustre"
		FS_ALL_LIST="LUSTRE PROXY SNMP FUSE POSIX"
		;;

	POSIX)

		case "$DBTYPE" in

		"PGSQL") 
			# define PG_CONFIG program name and PGSQL_VERSION
			GA_PGSQL_INFO

			DBEXT_INC_PATH=`$PG_CONFIG --includedir`
			DBEXT_LIB_PATH=`$PG_CONFIG --libdir`
			DBEXT_FLAGS="-I$DBEXT_INC_PATH"

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
				"8."*)
					AC_DEFINE([_USE_PGSQL], 1, [Using PostgreSQL database])
					AC_DEFINE([_PGSQL8], 1, [Using PostgreSQL version 8.x])
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

			AC_DEFINE([_USE_MYSQL], 1, [Using MySQL database])
			;;
		esac

		AC_DEFINE([_USE_POSIX], 1, [GANESHA is compiled with POSIX FSAL])
                FSAL_CFLAGS="$DBEXT_FLAGS"
                FSAL_LDFLAGS="$DBEXT_LDADD"
                FSAL_LIB="\$(top_builddir)/FSAL/FSAL_POSIX/libfsalposix.la \$(top_builddir)/FSAL/FSAL_POSIX/DBExt/$DBTYPE/libfsaldbext.la"
                FS_NAME="posix"
                ;;
	GHOSTFS)
		AC_DEFINE([_USE_GHOSTFS], 1, [GANESHA is compiled with GHOST FS FSAL])
                FSAL_CFLAGS=
                FSAL_LDFLAGS="\$(top_builddir)/RW_Lock/librwlock.la"
                FSAL_LIB="\$(top_builddir)/FSAL/FSAL_GHOST_FS/libfsalghostfs.la \$(top_builddir)/FSAL/FSAL_GHOST_FS/GHOST_FS/libghostfs.la"
                FS_NAME="ghostfs"
                ;;

	*)
		AC_MSG_ERROR([This FSAL is not supported yet]) 
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
	PROXY_RPCSECGSS)
		AC_DEFINE([_USE_MFSL], 1, [Use MFSL module])
		AC_DEFINE([_USE_MFSL_PROXY_RPCSECGSS], 1, [Use MFSL_PROXY_RPCSECGSS, wraps the FSAL call for RPCSECGSS client side in proxy])
		MFSL_LIB="\$(top_builddir)/MFSL/MFSL_PROXY_RPCSECGSS/libmfslproxy_rpcsecgss.la"
		;;
	*)
		AC_MSG_ERROR([This MFSL is not supported yet])
	
esac

# MFSL_ASYNC should not be used with HPSS
if [[ $FSAL == "HPSS" ]] ; then
  if [[ $MFSL == "ASYNC" ]] ; then 
		AC_MSG_ERROR([FSAL_HPSS and MFSL_ASYNC are incompatible])
  fi
fi

AC_SUBST(FSAL_CFLAGS)
AC_SUBST(FSAL_LDFLAGS)
AC_SUBST(FSAL_LIB)
AC_SUBST(MFSL_LIB)
AC_SUBST(FS_NAME)
AC_SUBST(FS_ALL_LIST)
AC_SUBST(SEC_CFLAGS)
AC_SUBST(SEC_LFLAGS)

# for exporting to spec file
AC_SUBST(ac_configure_args)

# for using fs name in binairies
AC_DEFINE_UNQUOTED([FS_NAME], "$FS_NAME", [Name of the filesystem])

AC_CONFIG_FILES([include/Makefile
                 include/FSAL/Makefile
                 include/FSAL/FSAL_HPSS/Makefile
                 include/FSAL/FSAL_POSIX/Makefile
                 include/FSAL/FSAL_PROXY/Makefile
                 include/FSAL/FSAL_SNMP/Makefile
                 include/FSAL/FSAL_FUSELIKE/Makefile
                 include/FSAL/FSAL_LUSTRE/Makefile
                 include/MFSL/Makefile
                 include/MFSL/MFSL_NULL/Makefile
                 include/MFSL/MFSL_ASYNC/Makefile
                 include/MFSL/MFSL_PROXY_RPCSECGSS/Makefile
                 BuddyMalloc/Makefile
                 StuffAlloc/Makefile
                 Common/Makefile
                 support/Makefile
                 Log/Makefile
                 NodeList/Makefile
                 test/Makefile 
                 ConfigParsing/Makefile
                 IdMapper/Makefile
                 RW_Lock/Makefile
                 SemN/Makefile
                 LRU/Makefile
                 HashTable/Makefile
                 Cache_inode/Makefile
                 File_Content/Makefile
                 File_Content_Policy/Makefile
		 NFS_Protocols/Makefile
                 XDR/Makefile
		 FSAL/Makefile
		 FSAL/FSAL_HPSS/Makefile
		 FSAL/FSAL_HPSS/HPSSclapiExt/Makefile
		 FSAL/FSAL_GHOST_FS/Makefile
		 FSAL/FSAL_GHOST_FS/GHOST_FS/Makefile
		 FSAL/FSAL_POSIX/Makefile
		 FSAL/FSAL_POSIX/DBExt/Makefile
		 FSAL/FSAL_POSIX/DBExt/PGSQL/Makefile
		 FSAL/FSAL_POSIX/DBExt/MYSQL/Makefile
		 FSAL/FSAL_PROXY/Makefile
		 FSAL/FSAL_PROXY/handle_mapping/Makefile
		 FSAL/FSAL_SNMP/Makefile
		 FSAL/FSAL_FUSELIKE/Makefile
		 FSAL/FSAL_LUSTRE/Makefile
		 FSAL/FSAL_TEMPLATE/Makefile
		 MFSL/Makefile
		 MFSL/MFSL_NULL/Makefile                 
		 MFSL/MFSL_ASYNC/Makefile                 
		 MFSL/MFSL_PROXY_RPCSECGSS/Makefile                 
                 MainNFSD/Makefile
                 cmdline_tools/Makefile
                 shell/Makefile
                 snmp_adm/Makefile
                 example-fuse/Makefile
                 Docs/Makefile
                 tools/Makefile
                 config_samples/Makefile
                 connectathon/Makefile
                 connectathon/basic/Makefile
                 connectathon/ConnectathonConfigParsing/Makefile
                 check/Makefile
		 check/layers/Makefile
		 check/layers/maketest.conf
		 debian/Makefile
                 Makefile
                 libganeshaNFS.pc
                 nfs-ganesha.spec])
AC_OUTPUT
