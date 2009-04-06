// File : NFS_remote.i
%module NFS_remote
%{
#include "NFS_remote.h"
%}

%include "NFS_remote.h"
%include "nfs23.h"
%include "mount.h"


%inline %{
extern void print_nfs_attributes(fattr3 * attrs, FILE * output);

cookie3 new_cookie3() {
  return 0;
}

cookieverf3 *new_p_cookieverf3() {
  cookieverf3 *c = (cookieverf3*)malloc(sizeof(cookieverf3));
  return c;
}

nfs_res_t **new_pp_nfs_res_t() {
  nfs_res_t *r = (nfs_res_t*)malloc(sizeof(nfs_res_t));
  return &r;
}
%}


FILE *fopen(char *name, char *mode);
void fclose(FILE *);

typedef union nfs_arg__
{
  fhandle2        arg_getattr2;
  SETATTR2args    arg_setattr2;
  diropargs2      arg_lookup2;
  fhandle2        arg_readlink2;
  READ2args       arg_read2;
  WRITE2args      arg_write2;
  CREATE2args     arg_create2;
  diropargs2      arg_remove2;
  RENAME2args     arg_rename2;
  LINK2args       arg_link2;
  SYMLINK2args    arg_symlink2;
  CREATE2args     arg_mkdir2;
  diropargs2      arg_rmdir2;
  READDIR2args    arg_readdir2;
  fhandle2        arg_statfs2;
  GETATTR3args    arg_getattr3;
  SETATTR3args    arg_setattr3;
  LOOKUP3args     arg_lookup3;
  ACCESS3args     arg_access3;
  READLINK3args   arg_readlink3;
  READ3args       arg_read3;
  WRITE3args      arg_write3;
  CREATE3args     arg_create3;
  MKDIR3args      arg_mkdir3;
  SYMLINK3args    arg_symlink3;
  MKNOD3args      arg_mknod3;
  REMOVE3args     arg_remove3;
  RMDIR3args      arg_rmdir3;
  RENAME3args     arg_rename3;
  LINK3args       arg_link3;
  READDIR3args    arg_readdir3;
  READDIRPLUS3args arg_readdirplus3;
  FSSTAT3args     arg_fsstat3;
  FSINFO3args     arg_fsinfo3;
  PATHCONF3args   arg_pathconf3;
  COMMIT3args     arg_commit3;
  COMPOUND4args   arg_compound4;
  
  /* mnt protocol arguments */
  dirpath         arg_mnt;
  
} nfs_arg_t;


typedef union nfs_res__
{
  ATTR2res        res_attr2;
  DIROP2res       res_dirop2;
  READLINK2res    res_readlink2;
  READ2res        res_read2;
  nfsstat2        res_stat2;
  READDIR2res     res_readdir2;
  STATFS2res      res_statfs2;
  GETATTR3res     res_getattr3;
  SETATTR3res     res_setattr3;
  LOOKUP3res      res_lookup3;
  ACCESS3res      res_access3;
  READLINK3res    res_readlink3;
  READ3res        res_read3;
  WRITE3res       res_write3;
  CREATE3res      res_create3;
  MKDIR3res       res_mkdir3;
  SYMLINK3res     res_symlink3;
  MKNOD3res       res_mknod3;
  REMOVE3res      res_remove3;
  RMDIR3res       res_rmdir3;
  RENAME3res      res_rename3;
  LINK3res        res_link3;
  READDIR3res     res_readdir3;
  READDIRPLUS3res res_readdirplus3;
  FSSTAT3res      res_fsstat3;
  FSINFO3res      res_fsinfo3;
  PATHCONF3res    res_pathconf3;
  COMMIT3res      res_commit3;
  COMPOUND4res    res_compound4 ;
  
  /* mount protocol returned values */
  fhstatus2       res_mnt1 ;
  exports         res_mntexport ;
  mountres3       res_mnt3 ;
  mountlist       res_dump ;
  
  char toto[1024] ;
} nfs_res_t;
