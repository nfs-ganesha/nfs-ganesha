AC_PREREQ([2.59])
AC_INIT(libzfswrap, 1.0.2, remi.duraffort.ocre@cea.fr)

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall])

dnl Check for C and C++ compilers
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_C_INLINE
AC_PROG_CXX
AM_PROG_AS
AC_PROG_LIBTOOL

AC_MSG_CHECKING([whether pthread_mutex_t is larger than 24 bytes])
AC_TRY_RUN(
  [
#include <pthread.h>
int main(void){return (sizeof(pthread_mutex_t) > 24);}
  ],
  [AC_MSG_RESULT(yes)],
  [
    AC_MSG_RESULT(no)
    AC_DEFINE(UMEM_PTHREAD_MUTEX_TOO_BIG, [1], [need bigger cache])
    AC_MSG_WARN([*** increasing umem cpu cache size to compensate.])
  ]
)


AC_CHECK_HEADERS([sys/mman.h sys/sysmacros.h sys/time.h malloc.h])
AC_CHECK_FUNCS([issetugid mallinfo malloc_stats])

AC_CONFIG_HEADERS([config.h])

DEBUG_CFLAGS="-pipe -Wall -std=c99 -Wno-switch -Wno-unused -Wno-missing-braces -Wno-parentheses -Wno-uninitialized -fno-strict-aliasing -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -DTEXT_DOMAIN='\"libzfswrap\"' -DLINUX_AIO -DDEBUG -fPIC"
topdir=`pwd`
LIB_DIR="${topdir}/lib"

ARCH=""
case "${host}" in
  i[[3-6]]86*)
    ARCH="i386"
  ;;
  x86_64*)
    ARCH="amd64"
  ;;
  *)
    AC_MSG_ERROR([Unknown architecture])
  ;;
esac

dnl
dnl Check for the required libraries:
dnl libaio, openssl and zlib
dnl
AC_CHECK_HEADERS([libaio.h], [], [AC_MSG_ERROR(Missing libaio library)])
AC_CHECK_HEADERS([openssl/sha.h], [], [AC_MSG_ERROR(Missing libssl library)])
AC_CHECK_HEADERS([zlib.h], [], [AC_MSG_ERROR(Missing zlib library)])


AC_SUBST(DEBUG_CFLAGS)
AC_SUBST(LIB_DIR)
AC_SUBST(ARCH)


AC_CONFIG_FILES([libzfswrap.pc
                 libzfswrap.spec
                 Makefile
                 cmd/Makefile
                 lib/Makefile
                 lib/libatomic/Makefile
                 lib/libavl/Makefile
                 lib/libnvpair/Makefile
                 lib/libsolcompat/Makefile
                 lib/libsolkerncompat/Makefile
                 lib/libumem/Makefile
                 lib/libuutil/Makefile
                 lib/libzfs/Makefile
                 lib/libzfscommon/Makefile
                 lib/libzpool/Makefile
                 zfswrap/Doxyfile
                 zfswrap/Makefile])
AC_OUTPUT
