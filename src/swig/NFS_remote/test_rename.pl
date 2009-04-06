#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use NFS_remote;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_rename3 nfs_lookup3);
}

# get options
my $options = "d:m:p:s:";
my %opt;
my $usage = sprintf "Usage: %s -d <test_directory> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $testpath = $opt{d} || die "Missing Test Directory\n";

# filehandle & path
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";
my $nbproc = 15;
my $nbcall = 100;
my$filename = "filefilefilefilefilefilefile.";

# children's subroutine
sub filio {
  my ($file, $go) = @_;
  my $ret; 
  
  # wait
  print scalar localtime()." : $$ should rename '$file' $nbcall times.\n";
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 10) and ($min == $go+1)) { last; }
  }
  print scalar localtime()." : $$ init.\n";

  # Init RPC connections
  rpcinit( SERVER       => $server, 
           MNT_VERSION  => 'mount3', 
           MNT_PROTO    => 'udp',
           MNT_PORT     => '0',
           NFS_VERSION  => 'nfs3', 
           NFS_PROTO    => 'udp',
           NFS_PORT     => $nfs_port
  );
  
  # mount FS
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS_remote::copy_nfs_fh3($roothdl, $curhdl);

  # get the test directory filehandle (just chdir)
  $ret = mtestdir($roothdl, \$curpath, \$curhdl, $testpath);
  if ($ret != 0) { die "chdir failed\n"; }

  # wait
  print scalar localtime()." : $$ must wait.\n";
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 20) and ($min == $go+1)) { last; }
  }
  print scalar localtime()." : $$ starts its job !\n";
  
  for (my $i=0; $i<$nbcall; $i++) {
    my $hdl = NFS_remote::nfs_fh3::new();

    # RENAME
    $ret = nfs_rename3($curhdl, $file, $curhdl, $file.".1");
    if ($ret != 0) { die "\t$$ can't rename $file to $file.1 (error $ret)\n"; }
    # LOOKUP NEW
    $ret = nfs_lookup3($curhdl, $file.".1", \$hdl);
    if ($ret != 0) { die "\t".scalar localtime()." : new file $file.1 does not exist after rename ($ret - $$)\n"; }
    # LOOKUP OLD
    $ret = nfs_lookup3($curhdl, $file, \$hdl);
    if ($ret != $NFS_remote::NFS3ERR_NOENT) { die "\t".scalar localtime()." : $file exists after rename to $file.1 ($ret - $$)\n"; }

    # RENAME
    $ret = nfs_rename3($curhdl, $file.".1", $curhdl, $file);
    if ($ret != 0) { die "\t$$ can't rename $file.1 to $file (error $ret)\n"; }
    # LOOKUP NEW
    $ret = nfs_lookup3($curhdl, $file, \$hdl);
    if ($ret != 0) { die "\t".scalar localtime()." : new file $file does not exist after rename ($ret - $$)\n"; }
    # LOOKUP OLD
    $ret = nfs_lookup3($curhdl, $file.".1", \$hdl);
    if ($ret != $NFS_remote::NFS3ERR_NOENT) { die "\t".scalar localtime()." : $file.1 exists after rename to $file ($ret - $$)\n"; }
  }
 
  # Umount FS
  $ret = mnt_umount3($rootpath);
  print scalar localtime()." : $$ exits\n";
  
  exit;
}

my $min = (localtime)[1];

for (my $i=0; $i<$nbproc; $i++) {
  my $pid = fork;
  SWITCH: {
    ($pid < 0)
      && do {
        print "Fork impossible : $!\n";
        next SWITCH;
      };
    ($pid == 0) # padre
      && do {
        next SWITCH;
      };
    filio($filename.$i, $min);
  }
}

my $ret;

# Init RPC connections
rpcinit( SERVER       => $server, 
         MNT_VERSION  => 'mount3', 
         MNT_PROTO    => 'udp',
         MNT_PORT     => '0',
         NFS_VERSION  => 'nfs3', 
         NFS_PROTO    => 'udp',
         NFS_PORT     => $nfs_port
);

# mount FS
$ret = mnt_mount3($rootpath, \$roothdl);
if ($ret != 0) { die "mount failed\n"; }
NFS_remote::copy_nfs_fh3($roothdl, $curhdl);

# get the test directory filehandle
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# Prepare work directory
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 1, $nbproc, 0, $filename, "dir.", \$totfiles, \$totdirs);
if ($ret != 0) {die "Test failed : can't create files\n"; }

# Umount FS
$ret = mnt_umount3($rootpath);

# wait children
for (my $i=0; $i<$nbproc; $i++) {
  my $pid = wait;
}
