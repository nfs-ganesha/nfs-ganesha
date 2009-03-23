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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::ONE);
if (!defined($b)) { die "Missing test number one\n"; }

if ($b->{levels} == -1) { die "Missing 'levels' parameter in the config file $conf_file for the basic test number one\n"; }
if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number one\n"; }
if ($b->{dirs} == -1) { die "Missing 'dirs' parameter in the config file $conf_file for the basic test number one\n"; }

printf "%s : File and directory creation test\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($rootentry, \$curstr, \$curentry, $teststr);
if ($ret != 0) { die; }

# run ...
MesureTemps::StartTime($start);
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curentry, $b->{levels}, $b->{files}, $b->{dirs}, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number one failed\n"; }
MesureTemps::EndTime($start, $end);

# print results
print "\tcreated $totfiles files $totdirs directories ".$b->{levels}." levels deep";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b1\t$totfiles\t$totdirs\t".$b->{levels}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, ""); 
close LOG;

print "Basic test number one OK ! \n";
