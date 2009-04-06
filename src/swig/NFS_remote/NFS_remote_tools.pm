package NFS_remote_tools;
require Exporter;

our @ISA          = qw(Exporter);
our @EXPORT       = qw( 
                      rpcinit
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

use NFS_remote;

our $out = NFS_remote::get_output();

######################################################
##################### MOUNT v3 #######################
######################################################

##### mnt_null3 #####
# mnt_null3()
sub mnt_null3 {
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_Null($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_Null", "mnt_null3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return 0;
}

##### mnt_mount3 #####
# mnt_mount3(
#   type dirpath *,
#   ref type nfs_fh3 *
# )
sub mnt_mount3 {
  my ($path, $hdl) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_mnt} = $path;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_Mnt($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_Mnt", "mnt_mount3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_mnt3}->{fhs_status};
  if ($ret != $NFS_remote::MNT3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_nfs_fh3(NFS_remote::get_nfs_fh3_from_fhandle3($res->{res_mnt3}->{mountres3_u}->{mountinfo}->{fhandle}), $$hdl);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return 0;
}

##### mnt_dump3 #####
# mnt_dump3 (
# ref type mountbody *
# )
sub mnt_dump3 {
  my ($mountbody) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_Dump($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_Dump", "mnt_dump3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  if (!NFS_remote::copy_mountbody_from_res($res, $$mountbody)) {
    $$mountbody = 0;
  }
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return 0;
}

##### mnt_umount3 #####
# mnt_umount3 (
#   type dirpath *,
# )
sub mnt_umount3 {
  my ($dirpath) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
  
  $arg->{arg_mnt} = $dirpath;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_Umnt($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_Umnt", "mnt_umount3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return 0;
}

##### mnt_umountall3 #####
# mnt_umountall3()
sub mnt_umountall3 {
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_UmntAll($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_UmntAll", "mnt_umountall3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return 0;
}

##### mnt_export3 #####
# mnt_export3(
#   ref type exportnode *
# )
sub mnt_export3 {
  my ($exportnode) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
  
  do {
    my $clnt = NFS_remote::getClient("mount3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::mnt3_remote_Export($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "mount3", "mnt3_remote_Export", "mnt_export3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  if (!NFS_remote::copy_exportnode_from_res($res, $$exportnode)) {
    $$exportnode = 0;
  }
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return 0;
}

######################################################
###################### NFS v3 ########################
######################################################

##### nfs_null3 #####
sub nfs_null3 {
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Null($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Null", "nfs_null3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return 0;
}

##### nfs_getattr3 #####
# nfs_getattr3(
#   type nfs_fh3 *,
#   ref type fattr3 *
# )
# return int
sub nfs_getattr3 {
  my ($hdl, $fattr) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_getattr3}->{object} = $hdl;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Getattr($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Getattr", "nfs_getattr3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_getattr3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_fattr3_from_res($res, $$fattr);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return $ret;
}

##### nfs_setattr3 #####
# nfs_setattr3(
#   type nfs_fh3 *,
#   type stattr3 *
# )
sub nfs_setattr3 {
  my ($hdl, $sattr3) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;
    
  $arg->{arg_setattr3}->{object} = $hdl;
  $arg->{arg_setattr3}->{new_attributes} = $sattr3;
  $arg->{arg_setattr3}->{guard}->{check} = $NFS_remote::FALSE;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Setattr($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Setattr", "nfs_setattr3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_setattr3}->{status};
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_lookup3}->{what}->{dir} = $hdl;
  $arg->{arg_lookup3}->{what}->{name} = $name;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Lookup($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Lookup", "nfs_lookup3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_lookup3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) {
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_nfs_fh3_from_lookup3res($res, $$hdlret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_access3}->{object} = $hdl;
  $arg->{arg_access3}->{access} = $access;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Access($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Access", "nfs_access3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_access3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_access3_from_res($res, $$accessret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return $ret;
}

##### nfs_readlink3 #####
# nfs_readlink3(
#   type nfs_fh3 *,
#   ref type READLINK3resok *
# )
sub nfs_readlink3 {
  my ($hdl, $READLINK3resok) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_readlink3}->{symlink} = $hdl;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Readlink($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Readlink", "nfs_readlink3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_readlink3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_READLINK3resok_from_res($res, $$READLINK3resok);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_read3}->{file} = $hdl;
  $arg->{arg_read3}->{offset} = $offset;
  $arg->{arg_read3}->{count} = $count;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Read($clnt, $arg, $$res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Read", "nfs_read3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $$res->{res_read3}->{status};
  $arg->NFS_remote::nfs_arg_t::DESTROY();

  return $ret;
}

##### nfs_write3 #####
# nfs_write3(
#   ref type nfs_arg_t *,
#   ref type WRITE3resok *
# )
sub nfs_write3 {
  my ($arg, $WRITE3ret) = @_; # to preserv the memory, we construct the nfs_arg_t before ...
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Write($clnt, $$arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Write", "nfs_write3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_write3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_WRITE3resok_from_res($res, $$WRITE3ret);
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_create3}->{where}->{dir} = $hdl;
  $arg->{arg_create3}->{where}->{name} = $name;
  $arg->{arg_create3}->{how}->{mode} = $mode;
  if ($mode ne $NFS_remote::EXCLUSIVE) {
    $arg->{arg_create3}->{how}->{createhow3_u}->{obj_attributes} = $truc;
  } else {
    $arg->{arg_create3}->{how}->{createhow3_u}->{verf} = $truc;
  }
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Create($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Create", "nfs_create3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_create3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_nfs_fh3_from_create3res($res, $$hdlret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_mkdir3}->{where}->{dir} = $hdl;
  $arg->{arg_mkdir3}->{where}->{name} = $name;
  $arg->{arg_mkdir3}->{attributes} = $sattr;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Mkdir($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Mkdir", "nfs_mkdir3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_mkdir3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_nfs_fh3_from_mkdir3res($res, $$hdlret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_symlink3}->{where}->{dir} = $hdl;
  $arg->{arg_symlink3}->{where}->{name} = $name;
  $arg->{arg_symlink3}->{symlink}->{symlink_attributes} = $sattr;
  $arg->{arg_symlink3}->{symlink}->{symlink_data} = $path;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Symlink($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Symlink", "nfs_symlink3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_symlink3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_nfs_fh3_from_symlink3res($res, $$hdlret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_remove3}->{object}->{dir} = $hdl;
  $arg->{arg_remove3}->{object}->{name} = $name;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Remove($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Remove", "nfs_remove3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_remove3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return $ret;
}

##### nfs_rmdir3 #####
# nfs_rmdir3(
#   type nfs_fh3 *,
#   type filename3 *
# )
sub nfs_rmdir3 {
  my ($hdl, $name) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_rmdir3}->{object}->{dir} = $hdl;
  $arg->{arg_rmdir3}->{object}->{name} = $name;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Rmdir($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Rmdir", "nfs_rmdir3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_rmdir3}->{status};
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_rename3}->{from}->{dir} = $hdlfrom;
  $arg->{arg_rename3}->{from}->{name} = $namefrom;
  $arg->{arg_rename3}->{to}->{dir} = $hdlto;
  $arg->{arg_rename3}->{to}->{name} = $nameto;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Rename($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Rename", "nfs_rename3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_rename3}->{status};
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_link3}->{file} = $hdl2link;
  $arg->{arg_link3}->{link}->{dir} = $hdlto;
  $arg->{arg_link3}->{link}->{name} = $nameto;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Link($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Link", "nfs_link3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_link3}->{status};
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_readdir3}->{dir} = $hdl;
  $arg->{arg_readdir3}->{cookie} = $cookie;
  $arg->{arg_readdir3}->{cookieverf} = $cookieverf;
  $arg->{arg_readdir3}->{count} = 1024;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Readdir($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Readdir", "nfs_readdir3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_readdir3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) { 
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_cookieverf_dirlist_from_res($res, $$cookieverfret, $$dirlist);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
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
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_readdirplus3}->{dir} = $hdl;
  $arg->{arg_readdirplus3}->{cookie} = $cookie;
  $arg->{arg_readdirplus3}->{cookieverf} = $cookieverf;
  $arg->{arg_readdirplus3}->{dircount} = 1024;
  $arg->{arg_readdirplus3}->{maxcount} = 4096;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Readdirplus($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Readdirplus", "nfs_readdirplus3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_readdirplus3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) {
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_cookieverf_dirlistplus_from_res($res, $$cookieverfret, $$dirlist);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();
  
  return $ret;
}

##### nfs_fsstat3 #####
# nfs_fsstat3(
#   type nfs_fh3 *,
#   ref type FSSTAT3resok *
# )
sub nfs_fsstat3 {
  my ($hdl, $FSSTAT3ret) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_fsstat3}->{fsroot} = $hdl;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Fsstat($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Fsstat", "nfs_fsstat3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_fsstat3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) {
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_FSSTAT3resok_from_res($res, $$FSSTAT3ret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return $ret;
}

##### nfs_fsinfo3 #####
# nfs_fsinfo3(
#   type nfs_fh3 *,
#   ref type FSINFO3resok *
# )
sub nfs_fsinfo3 {
  my ($hdl, $FSINFO3ret) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_fsinfo3}->{fsroot} = $hdl;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Fsinfo($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Fsinfo", "nfs_fsinfo3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_fsinfo3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) {
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_FSINFO3resok_from_res($res, $$FSINFO3ret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

  return $ret;
}

##### nfs_pathconf3 #####
# nfs_pathconf3(
#   type nfs_fh3 *,
#   ref type PATHCONF3resok *
# )
sub nfs_pathconf3 {
  my ($hdl, $PATHCONF3ret) = @_;
  my $arg = NFS_remote::nfs_arg_t::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $ret;
  my $i = 0;

  $arg->{arg_pathconf3}->{object} = $hdl;
  do {
    my $clnt = NFS_remote::getClient("nfs3");
    if (!$clnt) {
      die "CLIENT not initialized\n";
    }
    $ret = NFS_remote::nfs3_remote_Pathconf($clnt, $arg, $res);
    $ret = NFS_remote::switch_result($ret, $i, "nfs3", "nfs3_remote_Pathconf", "nfs_pathconf3", $out);
    if ($ret > 0) { die; }
    $i++;
  } until $ret != -1;
  $ret = $res->{res_pathconf3}->{status};
  if ($ret != $NFS_remote::NFS3_OK) {
    $arg->NFS_remote::nfs_arg_t::DESTROY();
    $res->NFS_remote::nfs_res_t::DESTROY();
    return $ret; 
  }
  NFS_remote::copy_PATHCONF3resok_from_res($res, $$PATHCONF3ret);
  $arg->NFS_remote::nfs_arg_t::DESTROY();
  $res->NFS_remote::nfs_res_t::DESTROY();

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

##### rpcinit #####
# rpcinit(
#   SERVER      => 'localhost' ...,
#   MNT_VERSION => 'mount1' or 'mount3',
#   MNT_PROTO   => 'udp' or 'tcp',
#   MNT_PORT    => xxxxx
#   NFS_VERSION => 'nfs2' or 'nfs3',
#   NFS_PROTO   => 'udp' or 'tcp'
#   NFS_PORT    => 2048
# )
# return type CLIENT * (mnt), type CLIENT * (nfs)
sub rpcinit {
  my %options = @_;
  
  NFS_remote::Swig_BuddyInit();
  NFS_remote::rpc_init($options{SERVER}, $options{MNT_VERSION}, $options{MNT_PROTO}, $options{MNT_PORT}, $out);
  NFS_remote::rpc_init($options{SERVER}, $options{NFS_VERSION}, $options{NFS_PROTO}, $options{NFS_PORT}, $out);
}

##### init_sattr3 #####
# init_sattr3(
#   ref type sattr3 *,
# )
sub init_sattr3 {
  my ($sattr) = @_;
  $$sattr->{mode}->{set_it} = $NFS_remote::FALSE;
  $$sattr->{uid}->{set_it} = $NFS_remote::FALSE;
  $$sattr->{gid}->{set_it} = $NFS_remote::FALSE;
  $$sattr->{size}->{set_it} = $NFS_remote::FALSE;
  $$sattr->{atime}->{set_it} = $NFS_remote::DONT_CHANGE;
  $$sattr->{mtime}->{set_it} = $NFS_remote::DONT_CHANGE;
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
  my $begin_cookie = NFS_remote::get_new_cookie3();
  my $cookieverf = "00000000";
  my $cookieverfret = "00000000";

  my $dirlist = NFS_remote::dirlistplus3::new();
  while (!$eof) {
    my $entry;
    $ret = nfs_readdirplus3($hdl, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != $NFS_remote::NFS3_OK) { 
      print "error $ret during readdirplus\n";
      NFS_remote::free_cookie3($begin_cookie);
      return $ret; 
    }
    $entry = $dirlist->{entries};
    $eof = NFS_remote::is_eofplus($dirlist);
    while ($entry) {
      if ($entry->{name} ne "." && $entry->{name} ne "..") {
        if ($entry->{name_attributes}->{post_op_attr_u}->{attributes}->{type} == $NFS_remote::NF3DIR) {
          $ret = rm_r($hdl, $entry->{name_handle}->{post_op_fh3_u}->{handle}, $entry->{name});
          if ($ret != $NFS_remote::NFS3_OK) { 
            print "error $ret during rmdir\n";
            NFS_remote::free_cookie3($begin_cookie);
            return $ret; 
          }
        } else {
          $ret = nfs_remove3($hdl, $entry->{name});
          if ($ret != $NFS_remote::NFS3_OK) { 
            print "error $ret during remove\n";
            NFS_remote::free_cookie3($begin_cookie);
            return $ret; 
          }
        }
      }
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    $cookieverf = $cookieverfret;
  }
  NFS_remote::free_cookie3($begin_cookie);
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
  my $lookuphdl = NFS_remote::nfs_fh3::new();
  my $newpath = ".";
  my $last = 0;
  my $ret;
  
  if ($specpath =~ /^(\/)/) {
    # absolute path
    $specpath = $';
    NFS_remote::copy_nfs_fh3($roothdl, $lookuphdl);
    if ($specpath =~ /^\/$/) { # end, the path to solve is "/"
      $$retpath .= "/";
      NFS_remote::copy_nfs_fh3($roothdl, $$rethdl);
      return 0;
    }
  } else {
    # relative path, start to $globalpath and $curhdl
    NFS_remote::copy_nfs_fh3($curhdl, $lookuphdl);
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
#        NFS_remote::print_friendly_nfs_fh3($newpath, $lookuphdl);
      }
    } else { # no more element to find
      $ret = 0;
      $last = 1;
    }
  } until ($last);
  # @return the last handle found and the path corresponding
  NFS_remote::copy_nfs_fh3($lookuphdl, $$rethdl);
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
  my ($roothdl, $curpath, $curhdl, $testpath, $debug) = @_;
  my $tmphdl = NFS_remote::nfs_fh3::new();
  my $tmppath = "";
  my $ret;

  # test if exists
  $ret = solvepath($roothdl, $$curpath, $$curhdl, $testpath, \$tmphdl, \$tmppath);
  # $tmphdl is the last handle found, $tmppath is the path corresponding
  if ($ret == 2) { # NO ENTRY
    $testpath =~ /([^\/]+)\/?$/; # to be sure that the missing directory is the only and the last
    if (($tmppath eq ".") or ("./".$` eq $tmppath) or ("./".$` eq $tmppath."/")) {
      my $sattr = NFS_remote::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;
      $ret = nfs_mkdir3($tmphdl, $1, $sattr, $curhdl);
      if ($ret != 0) {
        print "Error during creating test directory : $ret\n";
        return $ret;
      }
      $$curpath = $tmppath."/".$1;
    } else {
      print "Enable to create test directory : previous directory does not exist !\n";
      return -1;
    }
  } elsif ($ret != 0) { # something is wrong
    return $ret;
  } else { # ok
    if (NFS_remote::are_nfs_fh3_equal($roothdl, $tmphdl)) { die "Impossible to work in the root directory !\n"; }
    # $tmppath = "toto/truc/blabla/parent/last"
    # delete old directory
    $ret = nfs_lookup3($tmphdl, "..", $curhdl); # find the filehandle of ".." (parent) and put it into $$curhdl
    # (yet we are in the directory "parent")
    $tmppath =~ /\/([^\/]+)$/; # name of the directory (last) to remove ?
    $ret = rm_r($$curhdl, $tmphdl, $1); # rm -r "last" ($1) in the directory "parent"
    if ($ret != 0) {
      print "Error during rm_r : $ret\n";
      return $ret;
    }
    # create a new one
    my $sattr = NFS_remote::sattr3::new();
    init_sattr3(\$sattr);
    $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
    $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;
    $ret = nfs_mkdir3($$curhdl, $1, $sattr, $curhdl); # $$curhdl become the handle of the directory "last"
    if ($ret != 0) {
      print "Error during creating test directory : $ret\n";
      return $ret;
    }
    $$curpath = $tmppath; # the path to return is the testpath
  }
#  print "Current path : \n";
#  NFS_remote::print_friendly_nfs_fh3($$curpath, $$curhdl);

  return 0;
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
  my $ffh = NFS_remote::nfs_fh3::new();
  my $dfh = NFS_remote::nfs_fh3::new();
  my $ret;

  if ($levels-- == 0) { return $NFS_remote::NFS3_OK; }
  
  my $sattr = NFS_remote::sattr3::new();
  init_sattr3(\$sattr);
  $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RW;
  
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = nfs_create3($dirfh, $fname.$f, $NFS_remote::GUARDED, $sattr, \$ffh);
    if ($ret != $NFS_remote::NFS3_OK) { return $ret; }
    $$totfiles++; 
  }
  
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;

  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = nfs_mkdir3($dirfh, $dname.$d, $sattr, \$dfh);
    if ($ret != $NFS_remote::NFS3_OK) { return $ret; }
    $$totdirs++;
    $ret = dirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != $NFS_remote::NFS3_OK) { return $ret; }
  }

  return $NFS_remote::NFS3_OK;
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
  my $ffh = NFS_remote::nfs_fh3::new();
  my $dfh = NFS_remote::nfs_fh3::new();
  my $ret;

  if ($levels-- == 0) { return 0; }
  
  # delete files
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = nfs_remove3($dirfh, $fname.$f);
    if ($ret != 0) { printf "remove $fname$f failed\n"; return $ret; }
    $$totfiles++; 
  }
  
  # delete directories
  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = nfs_lookup3($dirfh, $dname.$d, \$dfh);
    if ($ret != 0) { print "lookup $dname$d failed\n"; return $ret; }
    # rec !
    $ret = rmdirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != 0) { return $ret; }
    $$totdirs++;
    $ret = nfs_rmdir3($dirfh, $dname.$d);
    if ($ret != 0) { print "rmdir $dname$d failed\n"; return $ret; }
  }

  return 0;
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
  my $begin_cookie = NFS_remote::get_new_cookie3();
  my $cookieverf = "00000000";
  my $cookieverfret = "00000000";
  # lookup '..'
  my $parenthdl = NFS_remote::nfs_fh3::new();
  $ret = nfs_lookup3($hdl, "..", \$parenthdl);
  if ($ret != $NFS_remote::NFS3_OK) { 
    NFS_remote::free_cookie3($begin_cookie);
    print "lookup '..' failed";
    return $ret; 
  }
  # test if parenthdl == hdl
  if (NFS_remote::are_nfs_fh3_equal($hdl, $parenthdl)) {
    NFS_remote::free_cookie3($begin_cookie);
    $$pathret = ".".$path;
    return $NFS_remote::NFS3_OK;
  }
  # readdirplus parent
  my $dirlist = NFS_remote::dirlistplus3::new();
  while (!$last and !$eof) {
    my $entry;
    # call to readdirplus remote
    $ret = nfs_readdirplus3($parenthdl, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != $NFS_remote::NFS3_OK) { 
      NFS_remote::free_cookie3($begin_cookie);
      print "readdirplus failed\n";
      return $ret; 
    }
    # get first entry
    $entry = $dirlist->{entries};
    $eof = NFS_remote::is_eofplus($dirlist);
    while ($entry) { # foreach entry
      # who is $hdl ?
      if (NFS_remote::are_nfs_fh3_equal($hdl, $entry->{name_handle}->{post_op_fh3_u}->{handle})) { # here it is !
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
  NFS_remote::free_cookie3($begin_cookie);
  # loop
  $ret = getcwd($parenthdl, $path, $pathret);

  return $ret;
}

1;
