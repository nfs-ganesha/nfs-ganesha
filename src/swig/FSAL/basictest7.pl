#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use FSAL;
BEGIN {
  require FSAL_tools;
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_lookupPath fsal_str2name fsal_rename fsal_lookup fsal_getattrs fsal_link fsal_unlink); 
}

# get options
my $options = "itvs:f:p:";
my %opt;
my $usage = sprintf "Usage: %s [-t] [-i] [-v] -s <server_config> -f <config_file> [-p <path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = FSAL::fsal_handle_t::new();
my $curhdl = FSAL::fsal_handle_t::new();
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

# go to the mount point.
my $path = FSAL::fsal_path_t::new();
$ret = fsal_str2path($rootstr, \$path);
if ($ret != 0) { die "str2path failed\n"; }
$ret = fsal_lookupPath($path, \$roothdl);
if ($ret != 0) { die "lookupPath failed\n"; }
FSAL::copy_fsal_handle_t($roothdl, $curhdl);

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::SEVEN);
if (!defined($b)) { die "Missing test number seven\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number seven\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number seven\n"; }

printf "%s : link and rename\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curstr, \$curhdl, $teststr);
if ($ret != 0) { die; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 1, $b->{files}, 0, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number seven failed : can't create files\n"; }

MesureTemps::StartTime($start);
for (my $ci=0; $ci<$b->{count}; $ci++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    my $fname = FSAL::fsal_name_t::new();
    my $nname = FSAL::fsal_name_t::new();
    $ret = fsal_str2name($b->{fname}.$fi, \$fname);
    if ($ret != 0) { die "str2name failed\n"; }
    $ret = fsal_str2name($b->{nname}.$fi, \$nname);
    if ($ret != 0) { die "str2name failed\n"; }
    # RENAME
    $ret = fsal_rename($curhdl, $fname, $curhdl, $nname);
    if ($ret != 0) { die "can't rename ".$b->{fname}.$fi." to ".$b->{nname}.$fi." ($ret)\n"; }
    # LOOKUP OLD
    my $hdl = FSAL::fsal_handle_t::new();
    $ret = fsal_lookup($curhdl, $fname, \$hdl);
    if ($ret != 2) { die $b->{fname}.$fi." exists after rename to ".$b->{nname}.$fi." ($ret)\n"; } # FSAL3ERR_NOENT
    # LOOKUP NEW
    $ret = fsal_lookup($curhdl, $nname, \$hdl);
    if ($ret != 0) { die "Lookup ".$b->{nname}.$fi." failed\n"; }
    FSAL::print_friendly_fsal_handle_t($b->{nname}.$fi, $hdl);
    # GETATTR NEW
    my $fattr = FSAL::fsal_attrib_list_t::new();
    $fattr->{asked_attributes} = FSAL::get_mask_attr_numlinks();
    $ret = fsal_getattrs($hdl, \$fattr);
    FSAL::print_friendly_fsal_handle_t($b->{nname}.$fi, $hdl);
    if ($ret != 0) { die "can't stat ".$b->{nname}.$fi." after rename from ".$b->{fname}.$fi." ($ret)\n"; }
    # NLINK VALUE
    if ($fattr->{numlinks} != 1) { 
      die $b->{nname}.$fi." has ".$fattr->{numlinks}." links after rename (expect 1)\n"; 
    }
    # LINK
    $ret = fsal_link($hdl, $curhdl, $fname);
    if ($ret != 0) { die "can't link ".$b->{nname}.$fi." to ".$b->{fname}.$fi."\n"; }
    # GETATTR NEW 
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{nname}.$fi." after link ($ret)\n"; }
    # NLINK VALUE
    if ($fattr->{numlinks} != 2) { 
      die $b->{nname}.$fi." has ".$fattr->{numlinks}." links after link (expect 2)\n"; 
    }
    # LOOKUP HARDLINK
    $ret = fsal_lookup($curhdl, $fname, \$hdl);
    if ($ret != 0) { die "Lookup ".$b->{fname}.$fi." failed \n"; }
    # GETATTR HARDLINK
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after link\n"; }
    # NLINK VALUE
    if ($fattr->{numlinks} != 2) { 
      die $b->{fname}.$fi." has ".$fattr->{numlinks}." links after link (expect 2)\n"; 
    }
    # REMOVE NEW 
    $ret = fsal_unlink($curhdl, $nname);
    if ($ret != 0) { die "can't unlink ".$b->{nname}.$fi."\n"; }
    # GETATTR HARDLINK
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after link\n"; }
    # NLINK VALUE
    if ($fattr->{numlinks} != 1) { 
      die $b->{fname}.$fi." has ".$fattr->{numlinks}." links after unlink (expect 1)\n"; 
    }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." renames and links on ".$b->{files}." files";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b7\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number seven OK ! \n";
