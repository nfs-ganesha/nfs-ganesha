package NFS_tools;
require Exporter;

our @ISA          = qw(Exporter);
our @EXPORT       = qw( 
                      nfsinit
                      init_sattr3
                      testdir
                      mtestdir
                      dirtree
                      rmdirtree
                      getcwd
                    );
our @EXPORT_OK    = qw(
                      mnt_null3
                      mnt_mount3
                      mnt_dump3
                      mnt_umount3
                      mnt_umountall3
                      mnt_export3
                      nfs_null3
                      nfs_getattr3
                      nfs_setattr3
                      nfs_lookup3
                      nfs_access3
                      nfs_readlink3
                      nfs_read3
                      nfs_write3
                      nfs_create3
                      nfs_mkdir3
                      nfs_symlink3
                      nfs_mknod3
                      nfs_remove3
                      nfs_rmdir3
                      nfs_rename3
                      nfs_link3
                      nfs_readdir3
                      nfs_readdirplus3
                      nfs_fsstat3
                      nfs_fsinfo3
                      nfs_pathconf3
                      nfs_commit3
                    );

use strict;
use warnings;

use NFS;

our $out = NFS::get_output();

######################################################
####################### MOUNT ########################
######################################################

##### mnt_null3 #####
# mnt_null3()
sub mnt_null3 {
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_Null($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();
  
  return $ret;
}

##### mnt_mount3 #####
# mnt_mount3(
#   type dirpath *,
#   ref type nfs_fh3 *
# )
sub mnt_mount3 {
  my ($path, $hdl) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_mnt} = $path;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_Mnt($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_mnt3}->{fhs_status};
  if ($ret != $NFS::MNT3_OK) { 
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_nfs_fh3(NFS::get_nfs_fh3_from_fhandle3($res->{res_mnt3}->{mountres3_u}->{mountinfo}->{fhandle}), $$hdl);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### mnt_dump3 #####
# mnt_dump3 (
# ref type mountbody *
# )
sub mnt_dump3 {
  my ($mountbody) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_Dump($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  if (!NFS::copy_mountbody_from_res($res, $$mountbody)) {
    $$mountbody = 0;
  }
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();
  
  return $ret;
}

##### mnt_umount3 #####
# mnt_umount3 (
#   type dirpath *,
# )
sub mnt_umount3 {
  my ($dirpath) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_mnt} = $dirpath;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_Umnt($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### mnt_umountall3 #####
# mnt_umountall3()
sub mnt_umountall3 {
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_UmntAll($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### mnt_export3 #####
# mnt_export3(
#   ref type exportnode *
# )
sub mnt_export3 {
  my ($exportnode) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::MOUNTPROG);
  $req->{rq_vers} = NFS::get_Vers($NFS::MOUNT_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::mnt_Export($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  if (!NFS::copy_exportnode_from_res($res, $$exportnode)) {
    $$exportnode = 0;
  }
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

######################################################
######################## NFS #########################
######################################################

##### nfs_null3 #####
sub nfs_null3 {
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Null($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();
  
  return $ret;
}

##### nfs_getattr3 #####
# nfs_getattr3(
#   type nfs_fh3 *,
#   ref type fattr3 *
# )
# return int
sub nfs_getattr3 {
  my ($hdl, $fattr) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_getattr3}->{object} = $hdl;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Getattr($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  if ($ret != $NFS::NFS3_OK) { 
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_fattr3_from_res($res, $$fattr);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_setattr3 #####
# nfs_setattr3(
#   type nfs_fh3 *,
#   type stattr3 *
# )
sub nfs_setattr3 {
  my ($hdl, $sattr3) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_setattr3}->{object} = $hdl;
  $arg->{arg_setattr3}->{new_attributes} = $sattr3;
  $arg->{arg_setattr3}->{guard}->{check} = $NFS::FALSE;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Setattr($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_lookup3 #####
# nfs_lookup3(
#   type nfs_fh3 *,
#   type char *,
#   ref type nfs_fh3 *
# )
sub nfs_lookup3 {
  my ($hdl, $name, $hdlret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_lookup3}->{what}->{dir} = $hdl;
  $arg->{arg_lookup3}->{what}->{name} = $name;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Lookup($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_lookup3}->{status};
  if ($ret != $NFS::NFS3_OK) {
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_nfs_fh3_from_lookup3res($res, $$hdlret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_access3 #####
# nfs_access3(
#   type nfs_fh3 *,
#   type access3 *,
#   ref type access3 *
# )
sub nfs_access3 {
  my ($hdl, $access, $accessret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_access3}->{object} = $hdl;
  $arg->{arg_access3}->{access} = $access;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs3_Access($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_access3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_access3_from_res($res, $$accessret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_readlink3 #####
# nfs_readlink3(
#   type nfs_fh3 *,
#   ref type READLINK3resok *
# )
sub nfs_readlink3 {
  my ($hdl, $READLINK3resok) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_readlink3}->{symlink} = $hdl;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Readlink($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_readlink3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_READLINK3resok_from_res($res, $$READLINK3resok);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_read3 #####
# nfs_read3(
#   type nfs_fh3 *,
#   type offset3 *,
#   type count *,
#   ref type nfs_res_t *
# )
sub nfs_read3 {
  my ($hdl, $offset, $count, $res) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_read3}->{file} = $hdl;
  $arg->{arg_read3}->{offset} = $offset;
  $arg->{arg_read3}->{count} = $count;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Read($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $$res);
  $ret = $$res->{res_read3}->{status};
  $arg->NFS::nfs_arg_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_write3 #####
# nfs_write3(
#   ref type nfs_arg_t *,
#   ref type WRITE3resok *
# )
sub nfs_write3 {
  my ($arg, $WRITE3ret) = @_; # to preserv the memory, we construct the nfs_arg_t before ...
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Write($$arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_write3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_WRITE3resok_from_res($res, $$WRITE3ret);
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_create3 #####
# nfs_create3(
#   type nfs_fh3 *,
#   type filename *,
#   type createmode3 *,
#   type sattr3|createverf3 *,
#   ref type nfs_fh3 *
# )
sub nfs_create3 {
  my ($hdl, $name, $mode, $truc, $hdlret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_create3}->{where}->{dir} = $hdl;
  $arg->{arg_create3}->{where}->{name} = $name;
  $arg->{arg_create3}->{how}->{mode} = $mode;
  if ($mode ne $NFS::EXCLUSIVE) {
    $arg->{arg_create3}->{how}->{createhow3_u}->{obj_attributes} = $truc;
  } else {
    $arg->{arg_create3}->{how}->{createhow3_u}->{verf} = $truc;
  }
  
  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Create($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_create3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_nfs_fh3_from_create3res($res, $$hdlret);
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_mkdir3 #####
# nfs_mkdir3(
#   type nfs_fh3 *,
#   type filename *,
#   type sattr3 *,
#   ref type nfs_fh3 *
# )
sub nfs_mkdir3 {
  my ($hdl, $name, $sattr, $hdlret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_mkdir3}->{where}->{dir} = $hdl;
  $arg->{arg_mkdir3}->{where}->{name} = $name;
  $arg->{arg_mkdir3}->{attributes} = $sattr;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Mkdir($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_mkdir3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_nfs_fh3_from_mkdir3res($res, $$hdlret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_symlink3 #####
# nfs_symlink3(
#   type nfs_fh3 *,
#   type filename *,
#   type sattr3 *,
#   type nfspath3 *,
#   ref type nfs_fh3 *
# )
sub nfs_symlink3 {
  my ($hdl, $name, $sattr, $path, $hdlret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_symlink3}->{where}->{dir} = $hdl;
  $arg->{arg_symlink3}->{where}->{name} = $name;
  $arg->{arg_symlink3}->{symlink}->{symlink_attributes} = $sattr;
  $arg->{arg_symlink3}->{symlink}->{symlink_data} = $path;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Symlink($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_symlink3}->{status};
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  if ($ret != $NFS::NFS3_OK) { 
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_nfs_fh3_from_symlink3res($res, $$hdlret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_mknod3 #####
# nfs_mknod3(
# )
sub nfs_mknod3 {
# TODO
}

##### nfs_remove3 #####
# nfs_remove3(
#   type nfs_fh3 *,
#   type filename3 *
# )
sub nfs_remove3 {
  my ($hdl, $name) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_remove3}->{object}->{dir} = $hdl;
  $arg->{arg_remove3}->{object}->{name} = $name;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Remove($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_remove3}->{status};
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_rmdir3 #####
# nfs_rmdir3(
#   type nfs_fh3 *,
#   type filename3 *
# )
sub nfs_rmdir3 {
  my ($hdl, $name) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_rmdir3}->{object}->{dir} = $hdl;
  $arg->{arg_rmdir3}->{object}->{name} = $name;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Rmdir($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_rmdir3}->{status};
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_rename3 #####
# nfs_rename3(
#   type nfs_fh3 *,
#   type filename3 *
#   type nfs_fh3 *,
#   type filename3 *
# )
sub nfs_rename3 {
  my ($hdlfrom, $namefrom, $hdlto, $nameto) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_rename3}->{from}->{dir} = $hdlfrom;
  $arg->{arg_rename3}->{from}->{name} = $namefrom;
  $arg->{arg_rename3}->{to}->{dir} = $hdlto;
  $arg->{arg_rename3}->{to}->{name} = $nameto;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Rename($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_rename3}->{status};
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_link3 #####
# nfs_link3(
#   type nfs_fh3 *,
#   type nfs_fs3 *,
#   type filename3 *
# )
sub nfs_link3 {
  my ($hdl2link, $hdlto, $nameto) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_link3}->{file} = $hdl2link;
  $arg->{arg_link3}->{link}->{dir} = $hdlto;
  $arg->{arg_link3}->{link}->{name} = $nameto;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Link($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_rename3}->{status};
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_readdir3 #####
# nfs_readdir3(
#   type nfs_fh3 *,
#   type cookie3 *,
#   type cookieverf3 *,
#   ref type cookieverf3 *, 
#   ref type dirlist3 * 
# )
sub nfs_readdir3 {
  my ($hdl, $cookie, $cookieverf, $cookieverfret, $dirlist) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_readdir3}->{dir} = $hdl;
  $arg->{arg_readdir3}->{cookie} = $cookie;
  $arg->{arg_readdir3}->{cookieverf} = $cookieverf;
  $arg->{arg_readdir3}->{count} = 4096;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Readdir($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_readdir3}->{status};
  if ($ret != $NFS::NFS3_OK) { 
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_cookieverf_dirlist_from_res($res, $$cookieverfret, $$dirlist);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();
  
  return $ret;
}

##### nfs_readdirplus3 #####
# nfs_readdirplus3(
#   type nfs_fh3 *,
#   type cookie3 *,
#   type cookieverf3 *,
#   ref type cookieverf3 *, 
#   ref type dirlistplus3 * 
# )
sub nfs_readdirplus3 {
  my ($hdl, $cookie, $cookieverf, $cookieverfret, $dirlist) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_readdirplus3}->{dir} = $hdl;
  $arg->{arg_readdirplus3}->{cookie} = $cookie;
  $arg->{arg_readdirplus3}->{cookieverf} = $cookieverf;
  $arg->{arg_readdirplus3}->{dircount} = 1024;
  $arg->{arg_readdirplus3}->{maxcount} = 4096;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs3_Readdirplus($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_readdirplus3}->{status};
  if ($ret != $NFS::NFS3_OK) {
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_cookieverf_dirlistplus_from_res($res, $$cookieverfret, $$dirlist);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();
  
  return $ret;
}

##### nfs_fsstat3 #####
# nfs_fsstat3(
#   type nfs_fh3 *,
#   ref type FSSTAT3resok *
# )
sub nfs_fsstat3 {
  my ($hdl, $FSSTAT3ret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_fsstat3}->{fsroot} = $hdl;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs_Fsstat($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_fsstat3}->{status};
  if ($ret != $NFS::NFS3_OK) {
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_FSSTAT3resok_from_res($res, $$FSSTAT3ret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_fsinfo3 #####
# nfs_fsinfo3(
#   type nfs_fh3 *,
#   ref type FSINFO3resok *
# )
sub nfs_fsinfo3 {
  my ($hdl, $FSINFO3ret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_fsinfo3}->{fsroot} = $hdl;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs3_Fsinfo($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_fsinfo3}->{status};
  if ($ret != $NFS::NFS3_OK) {
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_FSINFO3resok_from_res($res, $$FSINFO3ret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_pathconf3 #####
# nfs_pathconf3(
#   type nfs_fh3 *,
#   ref type PATHCONF3resok *
# )
sub nfs_pathconf3 {
  my ($hdl, $PATHCONF3ret) = @_;
  my $arg = NFS::nfs_arg_t::new();
  my $res = NFS::nfs_res_t::new();
  my $req = NFS::Svc_req::new();
  my $ret;

  $arg->{arg_pathconf3}->{object} = $hdl;

  my $pthr = NFS::GetNFSClient();
  if ($pthr->{is_thread_init} != $NFS::TRUE) {
    $ret = NFS::InitNFSClient($pthr);
    if ($ret != 0 ) { die "Error $ret during thread initialization\n"; }
  }
  $req->{rq_prog} = NFS::get_Prog($NFS::NFS_PROGRAM);
  $req->{rq_vers} = NFS::get_Vers($NFS::NFS_V3);
  $req->{rq_clntcred} = NFS::get_ClntCred($pthr);
  
  $ret = NFS::nfs3_Pathconf($arg, $NFS::pexportlist, NFS::get_Context($pthr), NFS::get_Client($pthr), $NFS::ht, $req, $res);
  $ret = $res->{res_pathconf3}->{status};
  if ($ret != $NFS::NFS3_OK) {
    $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
    $arg->NFS::nfs_arg_t::DESTROY();
    $res->NFS::nfs_res_t::DESTROY();
    $req->NFS::Svc_req::DESTROY();
    return $ret; 
  }
  NFS::copy_PATHCONF3resok_from_res($res, $$PATHCONF3ret);
  $pthr->NFS::cmdnfs_thr_info_t::DESTROY();
  $arg->NFS::nfs_arg_t::DESTROY();
  $res->NFS::nfs_res_t::DESTROY();
  $req->NFS::Svc_req::DESTROY();

  return $ret;
}

##### nfs_commit3 #####
# nfs_commit3(
# )
sub nfs_commit3 {
# TODO
}

######################################################
####################### TOOLS ########################
######################################################

##### nfsinit #####
# nfsinit(
#   CONFIG_FILE   => '/path/to/file/conf',
#   FLAG_VERBOSE  => '1' / '0'
# )
sub nfsinit {
  my %options = @_;
  my $ret;

  $ret = NFS::BuddyInit(undef);
  if ($ret != $NFS::BUDDY_SUCCESS) {return $ret; }
  NFS::aux_init();
  $ret = NFS::fsal_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  if ($ret != 0) { return $ret; }
  $ret = NFS::cacheinode_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  if ($ret != 0) { return $ret; }
  $ret = NFS::nfs_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  return $ret;
}

##### init_sattr3 #####
# init_sattr3(
#   ref type sattr3 *,
# )
sub init_sattr3 {
  my ($sattr) = @_;
  $$sattr->{mode}->{set_it} = $NFS::FALSE;
  $$sattr->{uid}->{set_it} = $NFS::FALSE;
  $$sattr->{gid}->{set_it} = $NFS::FALSE;
  $$sattr->{size}->{set_it} = $NFS::FALSE;
  $$sattr->{atime}->{set_it} = $NFS::DONT_CHANGE;
  $$sattr->{mtime}->{set_it} = $NFS::DONT_CHANGE;
}

##### rm_r #####
# rm_r(
#   type nfs_fh3 *,
#   type nfs_fh3 *,
#   type char *
# )
sub rm_r {
  my ($dirhdl, $hdl, $name) = @_;
  my $ret;

  my $eof = 0;
  my $begin_cookie = NFS::get_new_cookie3();
  my $cookieverf = "00000000";
  my $cookieverfret = "00000000";

  my $dirlist = NFS::dirlistplus3::new();
  while (!$eof) {
    my $entry;
    $ret = nfs_readdirplus3($hdl, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != $NFS::NFS3_OK) { 
      NFS::free_cookie3($begin_cookie);
      return $ret; 
    }
    $entry = $dirlist->{entries};
    $eof = NFS::is_eofplus($dirlist);
    while ($entry) {
      if ($entry->{name} ne "." && $entry->{name} ne "..") {
        if ($entry->{name_attributes}->{post_op_attr_u}->{attributes}->{type} == $NFS::NF3DIR) {
          $ret = rm_r($hdl, $entry->{name_handle}->{post_op_fh3_u}->{handle}, $entry->{name});
          if ($ret != $NFS::NFS3_OK) { 
            NFS::free_cookie3($begin_cookie);
            return $ret; 
          }
        } else {
          $ret = nfs_remove3($hdl, $entry->{name});
          if ($ret != $NFS::NFS3_OK) { 
            NFS::free_cookie3($begin_cookie);
            return $ret; 
          }
        }
      }
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    $cookieverf = $cookieverfret;
  }
  NFS::free_cookie3($begin_cookie);
  $ret = nfs_rmdir3($dirhdl, $name);
  
  return $ret;
}

##### solvepath #####
# solvepath(
#   type nfs_fh3 *,
#   type char *,
#   type nfs_fh3 *,
#   type char *,
#   ref type nfs_fh3 *,
#   ref type char *,
# )
sub solvepath {
  my ($roothdl, $globalpath, $curhdl, $specpath, $rethdl, $retpath) = @_;
  my $lookuphdl = NFS::nfs_fh3::new();
  my $newpath = ".";
  my $last = 0;
  my $ret;
  
  if ($specpath =~ /^(\/)/) {
    # absolute path
    $specpath = $';
    NFS::copy_nfs_fh3($roothdl, $lookuphdl);
    if ($specpath =~ /^\/$/) { # end, the path to solve is "/"
      $$retpath .= "/";
      NFS::copy_nfs_fh3($roothdl, $$rethdl);
      return $NFS::NFS3_OK;
    }
  } else {
    # relative path, start to $globalpath and $curhdl
    NFS::copy_nfs_fh3($curhdl, $lookuphdl);
    $newpath = $globalpath;
  }

  do {
    if ($specpath =~ /^(([^\/]+)\/?)/) { # path not null ("toto/..." or "toto")
      $ret = nfs_lookup3($lookuphdl, $2, \$lookuphdl);
      if ($ret != 0) { # element not found : $rethdl will be the last handle found.
        $last = 1;
      } # element found : continue.
      $specpath = $';
      unless ($last) {
        if ($2 eq "..") {
          if ($newpath =~ /\/([^\/]+)$/) {
            $newpath = $`;
          }
        } elsif ($2 ne ".") {
          $newpath .= "/".$2;
        }
#        NFS::print_friendly_nfs_fh3($newpath, $lookuphdl);
      }
    } else { # no more element to find
      $ret = 0;
      $last = 1;
    }
  } until ($last);
  # @return the last handle found and the path corresponding
  NFS::copy_nfs_fh3($lookuphdl, $$rethdl);
  $$retpath = $newpath;
  
  return $ret;
}

##### testdir #####
# testdir (
#   type nfs_fh3 *,
#   ref type char *,
#   ref type nfs_fh3 *,
#   type char *,
# )
sub testdir {
  my ($roothdl, $curpath, $curhdl, $testpath) = @_;
  my $tmphdl = NFS::nfs_fh3::new();
  my $tmppath = "";
  my $ret;

  # test if exists
  $ret = solvepath($roothdl, $$curpath, $$curhdl, $testpath, \$tmphdl, \$tmppath);
  # $tmphdl is the last handle found, $tmppath is the path corresponding
  if ($ret == $NFS::NFS3ERR_NOENT) {
    $testpath =~ /([^\/]+)\/?$/; # to be sure that the missing directory is the only and the last
    if (($tmppath eq ".") or ("./".$` eq $tmppath) or ("./".$` eq $tmppath."/")) {
      my $sattr = NFS::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_RWX;
      $ret = nfs_mkdir3($tmphdl, $1, $sattr, $curhdl);
      if ($ret != $NFS::NFS3_OK) {
        print "Error during creating test directory\n";
        return $ret;
      }
      $$curpath = $tmppath."/".$1;
    } else {
      print "Enable to create test directory : previous directory does not exist !\n";
      return -1;
    }
  } elsif ($ret != $NFS::NFS3_OK) { # something is wrong
    return $ret;
  } else { # ok
    if (NFS::are_nfs_fh3_equal($roothdl, $tmphdl)) { die "Impossible to work in the root directory !\n"; }
    # $tmppath = "toto/truc/blabla/parent/last"
    # delete old directory
    $ret = nfs_lookup3($tmphdl, "..", $curhdl); # find the filehandle of ".." (parent) and put it into $$curhdl
    # (yet we are in the directory "parent")
    $tmppath =~ /\/([^\/]+)$/; # name of the directory (last) to remove ?
    $ret = rm_r($$curhdl, $tmphdl, $1); # rm -r "last" ($1) in the directory "parent"
    if ($ret != $NFS::NFS3_OK) {
      print "Error during rm_r\n";
      return $ret;
    }
    # create a new one
    my $sattr = NFS::sattr3::new();
    init_sattr3(\$sattr);
    $sattr->{mode}->{set_it} = $NFS::TRUE;
    $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_RWX;
    $ret = nfs_mkdir3($$curhdl, $1, $sattr, $curhdl); # $$curhdl become the handle of the directory "last"
    if ($ret != $NFS::NFS3_OK) {
      print "Error during creating test directory\n";
      return $ret;
    }
    $$curpath = $tmppath; # the path to return is the testpath
  }
#  print "Current path : \n";
#  NFS::print_friendly_nfs_fh3($$curpath, $$curhdl);

  return $NFS::NFS3_OK;
}

##### mtestdir ##### (Just chdir the test_directory)
# mtestdir(
#   type nfs_fh3 *,
#   ref type char *,
#   ref type nfs_fh3 *,
#   type char *,
# )
sub mtestdir {
  my ($roothdl, $curpath, $curhdl, $testpath) = @_;

  return solvepath($roothdl, $$curpath, $$curhdl, $testpath, $curhdl, $curpath);
}

##### dirtree #####
# dirtree(
#   type nfs_fh3 *,
#   int,
#   int,
#   int,
#   char *,
#   char *,
#   ref int,
#   ref int
# )
sub dirtree {
  my ($dirfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $ffh = NFS::nfs_fh3::new();
  my $dfh = NFS::nfs_fh3::new();
  my $ret;

  if ($levels-- == 0) { return $NFS::NFS3_OK; }
  
  my $sattr = NFS::sattr3::new();
  init_sattr3(\$sattr);
  $sattr->{mode}->{set_it} = $NFS::TRUE;
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_RW;
  
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = nfs_create3($dirfh, $fname.$f, $NFS::GUARDED, $sattr, \$ffh);
    if ($ret != $NFS::NFS3_OK) { return $ret; }
    $$totfiles++; 
  }
  
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_RWX;

  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = nfs_mkdir3($dirfh, $dname.$d, $sattr, \$dfh);
    if ($ret != $NFS::NFS3_OK) { return $ret; }
    $$totdirs++;
    $ret = dirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != $NFS::NFS3_OK) { return $ret; }
  }

  return $NFS::NFS3_OK;
}

##### rmdirtree #####
# rmdirtree(
#   type nfs_fh3 *,
#   int,
#   int,
#   int,
#   char *,
#   char *,
#   ref int,
#   ref int
# )
sub rmdirtree {
  my ($dirfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $ffh = NFS::nfs_fh3::new();
  my $dfh = NFS::nfs_fh3::new();
  my $ret;

  if ($levels-- == 0) { return $NFS::NFS3_OK; }
  
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = nfs_remove3($dirfh, $fname.$f);
    if ($ret != $NFS::NFS3_OK) { print "remove $fname$f failed\n"; return $ret; }
    $$totfiles++; 
  }
  
  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = nfs_lookup3($dirfh, $dname.$d, \$dfh); # existe-t-il un moyen de ne pas faire ça ?
    if ($ret != $NFS::NFS3_OK) { print "lookup $dname$d failed\n"; return $ret; }
    $ret = rmdirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != $NFS::NFS3_OK) { return $ret; }
    $$totdirs++;
    $ret = nfs_rmdir3($dirfh, $dname.$d);
    if ($ret != $NFS::NFS3_OK) { print "rmdir $dname$d failed\n"; return $ret; }
  }

  return $NFS::NFS3_OK;
}

##### getcwd #####
# getcwd(
#   type nfs_fh3 *,
#   type char *,
#   ref type char *
# )
sub getcwd {
  my ($hdl, $path, $pathret) = @_;
  my $ret;
  my $last = 0;
  my $eof = 0;
  my $begin_cookie = NFS::get_new_cookie3();
  my $cookieverf = "00000000";
  my $cookieverfret = "00000000";
  # lookup '..'
  my $parenthdl = NFS::nfs_fh3::new();
  $ret = nfs_lookup3($hdl, "..", \$parenthdl);
  if ($ret != $NFS::NFS3_OK) { 
    NFS::free_cookie3($begin_cookie);
    print "lookup '..' failed";
    return $ret; 
  }
  # test if parenthdl == hdl
  if (NFS::are_nfs_fh3_equal($hdl, $parenthdl)) {
    NFS::free_cookie3($begin_cookie);
    $$pathret = ".".$path;
    return $NFS::NFS3_OK;
  }
  # readdirplus parent
  my $dirlist = NFS::dirlistplus3::new();
  while (!$last and !$eof) {
    my $entry;
    # call to readdirplus remote
    $ret = nfs_readdirplus3($parenthdl, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != $NFS::NFS3_OK) { 
      NFS::free_cookie3($begin_cookie);
      print "readdirplus failed : $ret\n"; 
      return $ret; 
    }
    # get first entry
    $entry = $dirlist->{entries};
    $eof = NFS::is_eofplus($dirlist);
    while ($entry) { # foreach entry
      # who is $hdl ?
      if (NFS::are_nfs_fh3_equal($hdl, $entry->{name_handle}->{post_op_fh3_u}->{handle})) { # here it is !
        $path = "/".$entry->{name}.$path;
        $last = 1; # do not readdirplus the end
        last; # do not check other entries
      }
      # next entry ...
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    # next dirlist ...
    $cookieverf = $cookieverfret;
  }
  NFS::free_cookie3($begin_cookie);
  # loop
  $ret = getcwd($parenthdl, $path, $pathret);

  return $ret;
}

1;
