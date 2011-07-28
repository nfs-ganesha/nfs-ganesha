
#
# This macro test for postgresql config program and version 
#
AC_DEFUN([GA_PGSQL_INFO],
[
	AC_CHECK_PROGS(PG_CONFIG, pg_config)

	AC_MSG_CHECKING(for PostgreSQL version)
	PGSQL_VERSION=`$PG_CONFIG --version 2>/dev/null | $AWK '{print [$]2;}'`

	if test -z "$PGSQL_VERSION"; then
		PGSQL_VERSION="none"
	fi

	AC_MSG_RESULT($PGSQL_VERSION)
])

#
# This macro test for MySQL config program and version 
#
AC_DEFUN([GA_MYSQL_INFO],
[
	AC_CHECK_PROGS(MYSQL_CONFIG, mysql_config)

	if test -z "$MYSQL_CONFIG"; then
		AC_MSG_ERROR(MySQL must be installed)
	fi

	AC_MSG_CHECKING(for MySQL version)
	MYSQL_VERSION=`$MYSQL_CONFIG --version 2>/dev/null | $AWK '{print [$]2;}'`

	if test -z "$MYSQL_VERSION"; then
		MYSQL_VERSION="none"
	fi

	AC_MSG_RESULT($MYSQL_VERSION)
])
