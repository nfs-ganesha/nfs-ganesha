# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------

########### next target ###############

SET(handlemapping_STAT_SRCS
   handle_mapping.c
   handle_mapping.h
   handle_mapping_db.c
   handle_mapping_db.h
   handle_mapping_internal.h
)

add_library(handlemapping STATIC ${handlemapping_STAT_SRCS})
add_sanitizers(handlemapping)


########### next target ###############

SET(test_handle_mapping_db_SRCS
   test_handle_mapping_db.c
)

add_executable(test_handle_mapping_db ${test_handle_mapping_db_SRCS})

target_link_libraries(test_handle_mapping_db handlemapping hashtable log common_utils rwlock sqlite3)


########### next target ###############

SET(test_handle_mapping_SRCS
   test_handle_mapping.c
)

add_executable(test_handle_mapping ${test_handle_mapping_SRCS})

target_link_libraries(test_handle_mapping handlemapping hashtable log common_utils rwlock sqlite3)


########### install files ###############




#original Makefile.am contents follow:

#AM_CFLAGS = $(SQLITE_CFLAGS)
#
#noinst_LTLIBRARIES          = libhandlemapping.la
#
#libhandlemapping_la_SOURCES = handle_mapping.c  handle_mapping.h  handle_mapping_db.c  handle_mapping_db.h handle_mapping_internal.h
#
#
#check_PROGRAMS              = test_handle_mapping_db test_handle_mapping
#test_handle_mapping_db_SOURCES      = test_handle_mapping_db.c
#test_handle_mapping_db_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la  $(top_srcdir)/Log/liblog.la \
#					$(top_srcdir)/Common/libcommon_utils.la -lsqlite3 
#
#
#test_handle_mapping_SOURCES      = test_handle_mapping.c
#test_handle_mapping_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la $(top_srcdir)/Log/liblog.la \
#					$(top_srcdir)/Common/libcommon_utils.la -lsqlite3 
#
#new: clean all
#
