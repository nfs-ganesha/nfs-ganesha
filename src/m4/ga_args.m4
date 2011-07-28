
#
# This macro is for features that are disabled by default
# and we want a CFLAG to be set if it is explicitely enabled
# on "configure" command line (with --enable-...)
#
# GA_ENABLE_FLAG( FEATURE_NAME, HELP_STRING, CFLAGS_IF_ENABLED )
#
# Example:
# GA_ENABLE_FLAG( [debug-memalloc], [enable debug traces for memory allocator], [-D_DEBUG_MEMALLOC] )
#
AC_DEFUN([GA_ENABLE_FLAG],
[
	AC_ARG_ENABLE( [$1], AS_HELP_STRING([--enable-$1],[$2]),
		       [enable_]m4_bpatsubst([$1], -, _)=$enableval, [enable_]m4_bpatsubst([$1], -, _)='no' )

	if test "[$enable_]m4_bpatsubst([$1], -, _)" == yes ; then
		CFLAGS="$CFLAGS $3"
        	echo "$1 feature enabled"
	fi
])


#
# This macro is for features that are enabled by default
# and we want a CFLAG to be set if it is explicitely disabled
# on "configure" command line (with --disable-...)
#
# GA_DISABLE_FLAG( FEATURE_NAME, HELP_STRING, CFLAGS_IF_DISABLED )
#
# Example:
# GA_DISABLE_FLAG( [tcp-register], [disable registration of tcp services on portmapper], [-D_NO_TCP_REGISTER] )
#
AC_DEFUN([GA_DISABLE_FLAG],
[
	AC_ARG_ENABLE( [$1], AS_HELP_STRING([--disable-$1], [$2]),
		       [enable_]m4_bpatsubst([$1], -, _)=$enableval, [enable_]m4_bpatsubst([$1], -, _)='yes' )

	if test "[$enable_]m4_bpatsubst([$1], -, _)" != yes ; then
		CFLAGS="$CFLAGS $3"
        	echo "$1 feature disabled"
	fi
])

#
# This macro defines an AM_CONDITIONAL variable 
# when an --enable argument is specified.
#
# GA_ENABLE_AM_CONDITION( FEATURE_NAME, HELP_STRING, AM_CONDITION_VAR )
#
# Example:
# GA_ENABLE_AM_CONDITION( [gssrpc], [enable gssrpc security management], [USE_GSSRPC] )
#
AC_DEFUN([GA_ENABLE_AM_CONDITION],
[
	AC_ARG_ENABLE( [$1], AS_HELP_STRING([--enable-$1],[$2]),
		       [enable_]m4_bpatsubst([$1],-,_)=$enableval, [enable_]m4_bpatsubst([$1],-,_)='no' )

	AM_CONDITIONAL([$3], test "[$enable_]m4_bpatsubst([$1],-,_)"  == "yes" )
])
#
#
# This macro defines an AM_CONDITIONAL variable 
# when an --disable argument is specified.
#
# GA_DISABLE_AM_CONDITION( FEATURE_NAME, HELP_STRING, AM_CONDITION_VAR )
#
# Example:
# GA_DISABLE_AM_CONDITION( [gssrpc], [disable gssrpc security management], [USE_NO_GSSRPC] )
#
AC_DEFUN([GA_DISABLE_AM_CONDITION],
[
	AC_ARG_ENABLE( [$1], AS_HELP_STRING([--disable-$1],[$2]),
		       [enable_]m4_bpatsubst([$1],-,_)=$enableval, [enable_]m4_bpatsubst([$1],-,_)='yes' )

	AM_CONDITIONAL([$3], test "[$enable_]m4_bpatsubst([$1],-,_)"  == "no" )
])
