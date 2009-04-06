#!/usr/bin/perl

print "#################################################################################\n";
print "### WARNING : THIS TEST MUST BE LAUNCHED ON THE SAME HOST THAN THE NFS SERVER ###\n";
print "#################################################################################\n";

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_readdirplus3 nfs_lookup3 nfs_setattr3);
}

# get options
my $options = "m:p:s:d:";
my %opt;
my $usage = sprintf "Usage: %s -d <test_directory> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $testpath = $opt{d} || die "Missing Test Directory\n";

# filehandle & path
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = "./";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";

my $ret;
my $dname = "dir.";
my $fname = "file.";
my $sleep = 12;
$| = 1; # auto flush

# Init RPC connections
rpcinit( SERVER       => $server, 
         MNT_VERSION  => 'mount3', 
         MNT_PROTO    => 'udp',
         MNT_PORT     => '0',
         NFS_VERSION  => 'nfs3', 
         NFS_PROTO    => 'udp',
         NFS_PORT     => $nfs_port
);

# Tool function
sub function_readdir {
  my ($fh) = @_;
  my $ret;
  my $eof = 0;
  my $begin_cookie = NFS_remote::get_new_cookie3();
  my $cookieverf = "00000000"; 
  my $cookieverfret = "00000000"; # char [8]

  my $dirlist = NFS_remote::dirlistplus3::new();
  while (!$eof) {
    my $entry;
    # readdirplus
    $ret = nfs_readdirplus3($fh, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != 0) { 
      NFS_remote::free_cookie3($begin_cookie);
      return $ret; 
    }
    $entry = $dirlist->{entries};
    $eof = NFS_remote::is_eofplus($dirlist);
    # foreach entry
    while ($entry) {
      if ($entry->{name} ne "." && $entry->{name} ne "..") {
        # if entry is directory
        if ($entry->{name_attributes}->{post_op_attr_u}->{attributes}->{type} == $NFS_remote::NF3DIR) {
          if ($ret != 0) {
            NFS_remote::free_cookie3($begin_cookie);
            return $ret;
          }
        }
      }
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    # continue the readdirplus
    $cookieverf = $cookieverfret;
  }
  NFS_remote::free_cookie3($begin_cookie);

  return $NFS_remote::NFS3_OK;
}

# mount FS
$ret = mnt_mount3($rootpath, \$roothdl);
if ($ret != 0) { die "mount failed\n"; }
NFS_remote::copy_nfs_fh3($roothdl, $curhdl);

# get the test directory filehandle
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

####### start !

# create test space
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

# readdir $dname.0 => should find $fname.0
print "Lookup $dname"."0 ...\n";
my $hdl = NFS_remote::nfs_fh3::new();
$ret = nfs_lookup3($curhdl, $dname."0", \$hdl);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 0) { die "Error $ret during readdir\n"; }

  # cd $testpath
  print "\tchmod 000 $dname"."0 ...\n";
  chdir $rootpath."/".$testpath or die "Impossible to chdir $rootpath/$testpath : $?\n";
  # chmod 000 $dname.0
  chmod 0000, $dname."0";

print "Sleeping $sleep seconds ...\n";
for (my $i=0; $i<$sleep; $i++) {
  print "".($i+1)." ... ";
  sleep 1;
}
print "\n";
# readdir $dname.0 => should return NFS3ERR_ACCESS
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 13) { print "### TEST FAILED : readdir should return NFS3ERR_ACCESS (13), returned $ret.\n"; }

    print "\tchmod 777 $dname"."0 to continue.\n";
    chmod 0777, $dname."0";

# re-create test space
$ret = dirtree($curhdl, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

print "#################################################################################\n";
# chmod 000 $dname.0
print "Lookup $dname"."0 ...\n";
$ret = nfs_lookup3($curhdl, $dname."0", \$hdl);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Setattr mode=000 $dname"."0 ...\n";
my $sattr = NFS_remote::sattr3::new();
init_sattr3(\$sattr);
$sattr->{mode}->{set_it} = $NFS_remote::TRUE;
$sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_NONE;
$ret = nfs_setattr3($hdl, $sattr);

  # chmod 000 $dname.0
  print "\tchmod 777 $dname"."0 ...\n";
  chmod 0777, $dname."0";

print "Sleeping $sleep seconds ...\n";
for (my $i=0; $i<$sleep; $i++) {
  print "".($i+1)." ... ";
  sleep 1;
}
print "\n";
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 0) { print "### TEST FAILED : readdir should return NFS3_OK (0), returned $ret.\n"; }

  print "Setattr mode=777 $dname"."0 to continue.\n";
  $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;
  $ret = nfs_setattr3($hdl, $sattr);

####### end

# Umount FS
$ret = mnt_umount3($rootpath);

print "#################################################################################\n";
