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
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_getattr3 nfs_rename3 nfs_lookup3 nfs_link3 nfs_remove3);
}

# get options
my $options = "tvf:m:p:s:" ;
my %opt ;
my $usage = sprintf "Usage: %s [-t] [-v] -f <config_file> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";

# filehandle : 
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $nfs_port = $opt{p} || 0;
my $debug = $opt{v}; 
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

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::SEVEN);
if (!defined($b)) { die "Missing test number seven\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number seven\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number seven\n"; }

printf "%s : link and rename\n", basename($0);

############
# mount FS #
############
my $usemnt;
print "Mounting filesystem... " if $debug;
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
print "OK\n" if $debug;

#####################################
# get the test directory filehandle #
#####################################
print "Get into test directory... " if $debug;
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath, $debug);
if ($ret != 0) { die; }
print "OK\n" if $debug;

# run ...
print "< Run test >\n" if $debug;
my ($totfiles, $totdirs) = (0, 0);
print "Creating files for the test... " if $debug;
$ret = dirtree($curhdl, 1, $b->{files}, 0, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number seven failed : can't create files\n"; }
print "OK\n" if $debug;

MesureTemps::StartTime($start);
for (my $ci=0; $ci<$b->{count}; $ci++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    # RENAME 
    printf("Renaming %s to %s... ", $b->{fname}.$fi, $b->{nname}.$fi) if $debug;
    $ret = nfs_rename3($curhdl, $b->{fname}.$fi, $curhdl, $b->{nname}.$fi);
    if ($ret != 0) { die "can't rename ".$b->{fname}.$fi." to ".$b->{nname}.$fi." ($ret)\n"; }
    print "OK\n" if $debug;

    # LOOKUP OLD
    my $hdl = NFS_remote::nfs_fh3::new();
    $ret = nfs_lookup3($curhdl, $b->{fname}.$fi, \$hdl);
    if ($ret != 2) { die $b->{fname}.$fi." exists after rename to ".$b->{nname}.$fi."\n"; } # NFS3ERR_NOENT
    
    # LOOKUP NEW
    $ret = nfs_lookup3($curhdl, $b->{nname}.$fi, \$hdl);
    if ($ret != 0) { die "Lookup ".$b->{nname}.$fi." failed\n"; }
    
    # GETATTR NEW
    my $fattr = NFS_remote::fattr3::new();
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{nname}.$fi." after rename from ".$b->{fname}.$fi."\n"; }
    
    # NLINK VALUE
    if (!NFS_remote::are_nlink_equal($fattr->{nlink}, 1)) { 
      die $b->{nname}.$fi." has ".NFS_remote::get_int_from_nlink($fattr->{nlink})." links after rename (expect 1)\n";
    }

    # LINK
    $ret = nfs_lookup3($curhdl, $b->{nname}.$fi, \$hdl);
    printf("Linking %s to %s... ", $b->{nname}.$fi, $b->{fname}.$fi) if $debug;
    $ret = nfs_link3($hdl, $curhdl, $b->{fname}.$fi);
    if ($ret != 0) { die "can't link ".$b->{nname}.$fi." to ".$b->{fname}.$fi." ($ret)\n"; }
    print "OK\n" if $debug;
    
    # GETATTR NEW 
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{nname}.$fi." after link\n"; }
    # NLINK VALUE
    if (!NFS_remote::are_nlink_equal($fattr->{nlink}, 2)) { 
      die $b->{nname}.$fi." has ".NFS_remote::get_int_from_nlink($fattr->{nlink})." links after link (expect 2)\n";
    }
    # LOOKUP HARDLINK
    $ret = nfs_lookup3($curhdl, $b->{fname}.$fi, \$hdl);
    if ($ret != 0) { die "Lookup ".$b->{fname}.$fi." failed \n"; }
    # GETATTR HARDLINK
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after link\n"; }
    # NLINK VALUE
    if (!NFS_remote::are_nlink_equal($fattr->{nlink}, 2)) { 
      die $b->{fname}.$fi." has ".NFS_remote::get_int_from_nlink($fattr->{nlink})." links after link (expect 2)\n";
    }
    # REMOVE NEW 
    $ret = nfs_remove3($curhdl, $b->{nname}.$fi);
    if ($ret != 0) { die "can't remove ".$b->{nname}.$fi."\n"; }
    # GETATTR HARDLINK
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after link\n"; }
    # NLINK VALUE
    if (!NFS_remote::are_nlink_equal($fattr->{nlink}, 1)) { 
      die $b->{fname}.$fi." has ".NFS_remote::get_int_from_nlink($fattr->{nlink})." links after unlink (expect 1)\n";
    }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." renames and links on ".$b->{files}." files";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b7\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number seven OK ! \n";
