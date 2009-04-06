#!/usr/bin/perl
use NFS_remote;

$out = NFS_remote::fopen("log", "w");

NFS_remote::rpc_init("localhost", "mount1", "tcp", $out);
NFS_remote::rpc_init("localhost", "mount3", "tcp", $out);
NFS_remote::rpc_init("localhost", "nfs2", "tcp", $out);
NFS_remote::rpc_init("localhost", "nfs3", "tcp", $out);

# mount
$mnt_hdl = NFS_remote::shell_fh3_t::new();
NFS_remote::nfs_remote_mount("/", $mnt_hdl, $out);

# getattr
$attrs = NFS_remote::fattr3::new();
NFS_remote::nfs_remote_getattr($mnt_hdl, $attrs, $out);
NFS_remote::print_nfs_attributes($attrs, $out);

#readdirplus
$begin_cookie = NFS_remote::new_cookie3();
$cookieverf = NFS_remote::new_p_cookieverf3();
$dirlist = NFS_remote::dirlistplus3::new();
$p_res = NFS_remote::new_pp_nfs_res_t;

$hdl_tmp = $mnt_hdl;

$eod_met = 0;
while (!$eod_met) {
  NFS_remote::nfs_remote_readdirplus($hdl_tmp, $begin_cookie, $cookieverf, $dirlist, $p_res, $out);

  $p_entry = NFS_remote::dirlistplus3::swig_entries_get($dirlist);

  while($p_entry) {
    print NFS_remote::entryplus3::swig_name_get($p_entry)."\n";
    $p_entry = NFS_remote::entryplus3::swig_nextentry_get($p_entry);
  }
  
  $eod_met = NFS_remote::dirlistplus3::swig_eof_get($dirlist);
}

print "ok\n";
