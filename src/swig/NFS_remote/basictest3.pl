#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use NFS_remote;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3);
}

# get options
my $options = "tf:m:p:s:" ;
my %opt ;
my $usage = sprintf "Usage: %s [-t] -f <config_file> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";

# filehandle : 
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";

my $ret;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

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

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::THREE);
if (!defined($b)) { die "Missing test number three\n"; }

if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number three\n"; }

printf "%s : lookups across mount point\n", basename($0);

# get the test directory filehandle
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# run ...
MesureTemps::StartTime($start);
for (my $i=0; $i<$b->{count}; $i++) {
  my $pathret;
  $ret = getcwd($curhdl, "", \$pathret);
  if ($ret != 0) { die "getcwd failed\n"; }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".$b->{count}." getcwd calls";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b3\t".$b->{count}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, ""); 
close LOG;

print "Basic test number three OK ! \n";
