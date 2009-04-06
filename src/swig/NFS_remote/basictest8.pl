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
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_symlink3 nfs_lookup3 nfs_getattr3 nfs_readlink3 nfs_remove3);
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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::EIGHT);
if (!defined($b)) { die "Missing test number eight\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number eight\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number seight\n"; }

printf "%s : symlink and readlink\n", basename($0);

# get the test directory filehandle
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# run ...
MesureTemps::StartTime($start);
for (my $ci=0; $ci<$b->{count}; $ci++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    # SYMLINK
    my $hdl = NFS_remote::nfs_fh3::new();
    my $sattr = NFS_remote::sattr3::new();
    init_sattr3(\$sattr);
    $ret = nfs_symlink3($curhdl, $b->{fname}.$fi, $sattr, $b->{sname}, \$hdl);
    if ($ret != 0) { die "can't make symlink ".$b->{fname}.$fi; }
    # GETATTR SYMLINK
    my $fattr = NFS_remote::fattr3::new();
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after symlink\n"; }
    # FTYPE ?
    if (!NFS_remote::is_symlink($fattr)) { die "type of ".$b->{fname}.$fi." not symlink\n"; }
    # READLINK
    my $readlinkret = NFS_remote::READLINK3resok::new();
    $ret = nfs_readlink3($hdl, \$readlinkret);
    if ($ret != 0) { die "Readlink failed\n"; }
    if ($readlinkret->{data} ne $b->{sname}) { die "readlink ".$b->{fname}.$fi." returned '".$readlinkret->{data}."', expect '".$b->{sname}."'\n"; }
    # REMOVE
    $ret = nfs_remove3($curhdl, $b->{fname}.$fi);
    if ($ret != 0) { die "can't remove ".$b->{fname}.$fi."\n"; }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." symlinks and readlinks on ".$b->{files}." files (size of symlink : ".(length $b->{sname}).")";
if ($opt{t}) { printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b8\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t".(length $b->{sname})."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number eight OK ! \n";
