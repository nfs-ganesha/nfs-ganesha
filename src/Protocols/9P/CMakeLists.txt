
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
