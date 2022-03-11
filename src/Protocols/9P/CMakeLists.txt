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

SET(9p_STAT_SRCS
   9p_interpreter.c
   9p_proto_tools.c
   9p_read_conf.c
   9p_attach.c
   9p_auth.c
   9p_clunk.c
   9p_flush.c
   9p_flush_hook.c
   9p_getattr.c
   9p_getlock.c
   9p_lcreate.c
   9p_lopen.c
   9p_link.c
   9p_lock.c
   9p_mkdir.c
   9p_mknod.c
   9p_read.c
   9p_readdir.c
   9p_readlink.c
   9p_remove.c
   9p_rename.c
   9p_renameat.c
   9p_fsync.c
   9p_unlinkat.c
   9p_setattr.c
   9p_statfs.c
   9p_symlink.c
   9p_version.c
   9p_walk.c
   9p_xattrcreate.c
   9p_xattrwalk.c
   9p_write.c
   9p_rerror.c
)

add_library(9p OBJECT ${9p_STAT_SRCS})
add_sanitizers(9p)
set_target_properties(9p PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
