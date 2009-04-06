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
  import Cache_inode_tools qw(:DEFAULT cacheinode_get cacheinode_readdir cacheinode_remove);
  require FSAL_tools;
  import FSAL_tools qw(fsalinit fsal_str2path fsal_lookupPath fsal_name2str fsal_str2name);
}

# get options
my $options = "itvs:f:p:";
my %opt;
my $usage = sprintf "Usage: %s [-i] [-t] [-v] -s <server_config> -f <config_file> [-p <path>]", basename($0);
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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::SIX);
if (!defined($b)) { die "Missing test number six\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} > $b->{files}) { die "count (".$b->{count}.") can't be greater than files (".$b->{files}.")"; }

printf "%s : readdir\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($rootentry, \$curstr, \$curentry, $teststr);
if ($ret != 0) { die; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curentry, 1, $b->{files}, 0, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number four failed : can't create files\n"; }
my $entries = 0;
my %files;

MesureTemps::StartTime($start);
for (my $i=0; $i<$b->{count}; $i++) {
  # INITIALIZATION
  my $err = 0;
  # dot and dotdot do not exist in this layer
  for (my $fi=0; $fi<$b->{files}; $fi++) { # each file
    $files{$b->{fname}.$fi} = 0; # not seen
  }

  # READDIR
  my $begincookie = 0;
  my $nbfound = 0;
  my $endcookie = 0;
  my $eod = $Cache_inode::UNASSIGNED_EOD;
  my $entryarray = Cache_inode::get_new_dir_entry_array($CACHE_INODE_READDIR_SIZE);
  my $cookiearray = Cache_inode::get_new_cookie_array($CACHE_INODE_READDIR_SIZE);

  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while ($eod != $Cache_inode::END_OF_DIR) { # while not end of dir
    # readdir
    $ret = cacheinode_readdir(
        $curentry,
        $begincookie,
        $CACHE_INODE_READDIR_SIZE,
        \$nbfound,
        \$endcookie,
        \$eod,
        \$entryarray,
        \$cookiearray
    );
    if ($ret != 0) {
      print "readdir failed\n";
      Cache_inode::free_dir_entry_array($entryarray);
      Cache_inode::free_cookie_array($cookiearray);
      return $ret; 
    }

    # foreach entry found
    for (my $i=0; $i<$nbfound; $i++) {
      # get entry name
      my $entryname = Cache_inode::get_name_from_dir_entry_array($entryarray, $i);
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entryname, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        Cache_inode::free_dir_entry_array($entryarray);
        Cache_inode::free_cookie_array($cookiearray);
        return $ret; 
      }
      $entries++;
      if (!($entrystr =~ /^($b->{fname}\d+)\0*$/)) {
        if ($opt{i}) {
          next;
        } else {
          Cache_inode::free_dir_entry_array($entryarray);
          Cache_inode::free_cookie_array($cookiearray);
          die "unexpected dir entry '$entrystr'\n";
        }
      }
      my $str = $1;
      if ($files{$str} == 1) { die "'$str' dir entry read twice\n"; }
      $files{$str} = 1; # yet seen
    }
    $begincookie = $endcookie;
  }
  Cache_inode::free_dir_entry_array($entryarray);
  Cache_inode::free_cookie_array($cookiearray);

  for (my $fi=0; $fi<$i; $fi++) { # removed files
    if ($files{$b->{fname}.$fi} > 0) { # should be removed !
      print "unlinked '".$b->{fname}.$fi."' dir entry read pass $i\n";
      $err++;
    }
  }
  for (my $fi=$i; $fi<$b->{files}; $fi++) { # other files
    if ($files{$b->{fname}.$fi} == 0) { # not seen but should exist !
      print "didn't read expected '".$b->{fname}.$fi."' dir entry, pass $i\n";
      $err++;
    }
  }

  if ($err > 0) { die "Test failed with $err errors\n"; }

  # REMOVE 1 FILE
  my $name = FSAL::fsal_name_t::new();
  $ret = fsal_str2name($b->{fname}.$i, \$name);
  if ($ret != 0) { die "str2name failed\n"; }
  $ret = cacheinode_remove($curentry, $name);
  if ($ret != 0) { die "can't unlink ".$b->{fname}.$i."\n"; }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t$entries entries read, ".$b->{files}." files";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b6\t$entries\t".$b->{files}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, ""); 
close LOG;

print "Basic test number six OK ! \n";
