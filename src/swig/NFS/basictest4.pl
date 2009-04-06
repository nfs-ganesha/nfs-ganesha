#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use NFS;
BEGIN {
  require NFS_tools;
  import NFS_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_lookup3 nfs_setattr3 nfs_getattr3);
}

# get options
my $options = "tvs:f:m:";
my %opt;
my $usage = sprintf "Usage: %s [-t] [-v] -s <server_config> -f <config_file> [-m <mount_path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = NFS::nfs_fh3::new();
my $curhdl = NFS::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

# Init layers
$ret = nfsinit( CONFIG_FILE   => $server_config,
                FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

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

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::FOUR);
if (!defined($b)) { die "Missing test number four\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number four\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number four\n"; }

printf "%s : setattr, getattr, and lookup\n", basename($0);

# get the test directory filehandle
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 1, $b->{files}, 0, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number four failed : can't create files\n"; }

MesureTemps::StartTime($start);
for (my $i=0; $i<$b->{count}; $i++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    # LOOKUP 
    my $hdl = NFS::nfs_fh3::new();
    $ret = nfs_lookup3($curhdl, $b->{fname}.$fi, \$hdl);
    if ($ret != 0) { die; }
    # CHMOD NONE (SETATTR)
    my $sattr = NFS::sattr3::new();
    init_sattr3(\$sattr);
    $sattr->{mode}->{set_it} = $NFS::TRUE;
    $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_NONE;
    $ret = nfs_setattr3($hdl, $sattr);
    if ($ret != 0) { die "can't chmod CHMOD_NONE ".$b->{fname}.$fi."\n"; }
    # GETATTR
    my $fattr = NFS::fattr3::new();
    $ret = nfs_getattr3($hdl, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after CHMOD_NONE\n"; }
    # MODE VALUE
    if (!NFS::are_mode3_equal($fattr->{mode}, $NFS::CHMOD_NONE)) { 
      print $b->{fname}.$fi." has mode 0x%04x after chmod 0\n", NFS::get_int_from_mode3($fattr->{mode}); 
      die;
    }
    # CHMOD RW
    $sattr->{mode}->{set_mode3_u}->{mode} = $NFS::CHMOD_RW;
    $ret = nfs_setattr3($hdl, $sattr);
    if ($ret != 0) { die "can't chmod CHMOD_RW ".$b->{fname}.$fi."\n"; }
    # GETATTR
    $ret = nfs_getattr3($hdl, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after CHMOD_RW\n"; }
    # MODE VALUE
    if (!NFS::are_mode3_equal($fattr->{mode}, $NFS::CHMOD_RW)) { 
      printf $b->{fname}.$fi." has mode 0x%04x after chmod RW (0x%04x)\n", NFS::get_int_from_mode3($fattr->{mode}), $NFS::CHMOD_RW; 
      die;
    }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." chmods and stats on ".$b->{files}." files";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b4\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number four OK ! \n";
