#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

BEGIN {
  require NFS_tools;
  import NFS_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_getattr3 nfs_readdir3 nfs_lookup3);
}

# get options
my $options = "vs:m:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -s <server_config> [-m <mount_path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = NFS::nfs_fh3::new();
my $curhdl = NFS::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = "./";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

# Init layers
$ret = nfsinit( CONFIG_FILE   => $server_config,
                FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# Function
sub loop {
  my ($fh, $path) = @_;
  my $ret;
  my $eof = 0;
  my $begin_cookie = NFS::get_new_cookie3();
  my $cookieverf = "00000000"; # char[8]
  my $cookieverfret = "00000000";

  my $dirlist = NFS::dirlist3::new();
  while (!$eof) {
    my $entry;
    $ret = nfs_readdir3($fh, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != 0) { 
      NFS::free_cookie3($begin_cookie);
      return $ret; 
    }
    $entry = $dirlist->{entries};
    $eof = NFS::is_eof($dirlist);
    while ($entry) {
      if ($entry->{name} ne "." && $entry->{name} ne "..") {
        my $hdl = NFS::nfs_fh3::new();
        $ret = nfs_lookup3($fh, $entry->{name}, \$hdl);
        if ($ret != 0) { 
          NFS::free_cookie3($begin_cookie);
          return $ret; 
        }
        my $fattr = NFS::fattr3::new();
        $ret = nfs_getattr3($hdl, \$fattr);
        if ($ret != 0) { 
          NFS::free_cookie3($begin_cookie);
          return $ret; 
        }
        print $path.$entry->{name}."\n";
        if ($fattr->{type} == 2) { # NF3DIR
          loop($hdl, $path.$entry->{name}."/");
        }
      }
#      NFS::print_nfs_fh3($entry->{name_handle}->{post_op_fh3_u}->{handle});
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    $cookieverf = $cookieverfret;
  }
  NFS::free_cookie3($begin_cookie);

  return 0;
}

# mount FS
my $usemnt;
if (-e $rootpath and -f $rootpath) { # file to read to get filehandle ?
  open(FHDL, $rootpath) or die "Impossible to open $rootpath : $!\n";
  HDL: while (my $row = <FHDL>) {
    next HDL if $row =~ /^#/; # comments
    next HDL if !($row =~ /^@/); # not a file handle
    # else ..
    chomp $row;
    $ret = NFS::get_new_nfs_fh3($row, $roothdl);
    if ($ret != 0) { die ""; }
    NFS::copy_nfs_fh3($roothdl, $curhdl);
    last HDL;
  }
  $usemnt = 0;
} else {
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS::copy_nfs_fh3($roothdl, $curhdl);
  $usemnt = 1;
}

# start !
loop($curhdl, $curpath);

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

print "ok\n";
