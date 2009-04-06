#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use MesureTemps;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_readdirplus3);
}

# get options
my $options = "m:p:s:d:";
my %opt;
my $usage = sprintf "Usage: %s -d <test_directory> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $test_directory = $opt{d} || die "Missing Test Directory\n";

# filehandle & path
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = "./";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";

my $ret;

my $start = MesureTemps::Temps::new();
my $end1 = MesureTemps::Temps::new();
my $end2 = MesureTemps::Temps::new();

# Init RPC connections
rpcinit( SERVER       => $server, 
         MNT_VERSION  => 'mount3', 
         MNT_PROTO    => 'udp',
         MNT_PORT     => '0',
         NFS_VERSION  => 'nfs3', 
         NFS_PROTO    => 'udp',
         NFS_PORT     => $nfs_port
);

# Function
sub loop {
  my ($fh, $path) = @_;
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
        print $path.$entry->{name}."\n";
        # if entry is directory
        if ($entry->{name_attributes}->{post_op_attr_u}->{attributes}->{type} == $NFS_remote::NF3DIR) {
          # rec !
          $ret = loop($entry->{name_handle}->{post_op_fh3_u}->{handle}, $path.$entry->{name}."/");
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
my $usemnt;
if (-e $rootpath and -f $rootpath) { # file to read to get filehandle ?
  open(FHDL, $rootpath) or die "Impossible to open $rootpath : $!\n";
  HDL: while (my $row = <FHDL>) {
    next HDL if $row =~ /^#/; # comments
    next HDL if !($row =~ /^@/); # not a file handle
    # else ..
    chomp $row;
    $ret = NFS_remote::get_new_nfs_fh3($row, $roothdl);
    if ($ret != 0) { die ""; }
    NFS_remote::copy_nfs_fh3($roothdl, $curhdl);
    last HDL;
  }
  $usemnt = 0;
} else {
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS_remote::copy_nfs_fh3($roothdl, $curhdl);
  $usemnt = 1;
}

###### start !
# goto the test_directory
$ret = mtestdir($roothdl, \$curpath, \$curhdl, $test_directory);
if ($ret != 0) { die "can not chdir the test directory : $ret\n"; }
# find
MesureTemps::StartTime($start);
$ret = loop($curhdl, $curpath);
if ($ret != 0) { die "Problem with find : $ret\n"; }
MesureTemps::EndTime($start, $end1);

MesureTemps::StartTime($start);
$ret = loop($curhdl, $curpath);
if ($ret != 0) { die "Problem with find : $ret\n"; }
MesureTemps::EndTime($start, $end2);

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

printf "First find done in %s seconds\n", MesureTemps::ConvertiTempsChaine($end1, "");
printf "Second find done in %s seconds\n", MesureTemps::ConvertiTempsChaine($end2, "");

print "ok\n";
