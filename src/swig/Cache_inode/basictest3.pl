#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use Cache_inode;
use FSAL;
BEGIN {
  require Cache_inode_tools;
  import Cache_inode_tools qw(:DEFAULT cacheinode_get);
  require FSAL_tools;
  import FSAL_tools qw(fsalinit fsal_str2path fsal_lookupPath);
}

# get options
my $options = "tvs:f:p:";
my %opt;
my $usage = sprintf "Usage: %s [-t] [-v] -s <server_config> -f <config_file> [-p <path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = FSAL::fsal_handle_t::new();
my $rootentry = Cache_inode::get_new_entry();
my $curentry = Cache_inode::get_new_entry();
my $rootstr = $opt{p} || "/";
my $curstr = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

# Init layers
$ret = fsalinit(  CONFIG_FILE   => $server_config,
                  FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }
$ret = cacheinodeinit(  CONFIG_FILE   => $server_config,
                        FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# go to the mount point.
my $path = FSAL::fsal_path_t::new();
$ret = fsal_str2path($rootstr, \$path);
if ($ret != 0) { die "str2path failed\n"; }
$ret = fsal_lookupPath($path, \$roothdl);
if ($ret != 0) { die "lookupPath failed\n"; }
# get the entry
$ret = cacheinode_get($roothdl, \$rootentry);
if ($ret != 0) { die "get FAILED\n"; }
Cache_inode::copy_entry($rootentry, $curentry);

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::THREE);
if (!defined($b)) { die "Missing test number three\n"; }

if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number three\n"; }

printf "%s : lookups across mount point\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($rootentry, \$curstr, \$curentry, $teststr);
if ($ret != 0) { die; }

# run ...
MesureTemps::StartTime($start);
for (my $i=0; $i<$b->{count}; $i++) {
  my $pathret;
  $ret = getcwd($curentry, "", \$pathret);
#  if ($ret != 0) { die "getcwd failed\n"; }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".$b->{count}." getcwd calls";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b3\t".$b->{count}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, ""); 
close LOG;

print "Basic test number three OK ! \n";
