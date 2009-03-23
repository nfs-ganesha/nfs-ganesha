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
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_lookupPath fsal_str2name fsal_name2str fsal_opendir fsal_readdir fsal_closedir fsal_unlink);
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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::SIX);
if (!defined($b)) { die "Missing test number six\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} > $b->{files}) { die "count (".$b->{count}.") can't be greater than files (".$b->{files}.")"; }

printf "%s : readdir\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curstr, \$curhdl, $teststr);
if ($ret != 0) { die; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 1, $b->{files}, 0, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number four failed : can't create files\n"; }
my $entries = 0;
my %files;

MesureTemps::StartTime($start);
for (my $i=0; $i<$b->{count}; $i++) {
  # INITIALIZATION
  my $err = 0;
  $files{"."} = 0; # dot
  $files{".."} = 0; # dotdot
  for (my $fi=0; $fi<$b->{files}; $fi++) { # each file
    $files{$b->{fname}.$fi} = 0; # not seen
  }

  # READDIR
  # prepare needed attributes mask
  my $mask = FSAL::get_mask_attr_type();

  my $eod = 0; # end of dir
  my $from = $FSAL::FSAL_READDIR_FROM_BEGINNING;
  my $to = FSAL::fsal_cookie_t::new();
  my $dirent = FSAL::get_new_dirent_buffer();
  my $nbentries = 0;
  
  # for name2str
  my @tabnamestr;
  for (my $j=0; $j<FSAL::get_fsal_max_name_len(); $j++) {
    $tabnamestr[$j] = " ";
  }

  while (!$eod) {
    # opendir
    my $dir = FSAL::fsal_dir_t::new();
    $ret = fsal_opendir($curhdl, \$dir);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      die "opendir failed\n"; 
    }

    # readdir
    $ret = fsal_readdir($dir, $from, $mask, FSAL::get_new_buffersize(), \$dirent, \$to, \$nbentries, \$eod);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      die "readdir failed\n"; 
    }
    
    # closedir
    $ret = fsal_closedir($dir);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      die "close failed\n"; 
    }

    # read readdir result
    my $entry = $dirent;
    while ($nbentries > 0 and $entry) { # foreach entry
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entry->{name}, \$entrystr);
      $entries++;
      if (!($entrystr =~ /^(\.|\.\.|$b->{fname}\d+)\0*$/)) {
        if ($opt{i}) {
          $entry = $entry->{nextentry};
          next;
        } else {
          FSAL::free_dirent_buffer($dirent);
          die "unexpected dir entry '$entrystr'\n";
        }
      }
      my $str = $1;
      if ($files{$str} == 1) { die "'$str' dir entry read twice\n"; }
      $files{$str} = 1; # yet seen
      $entry = $entry->{nextentry};
    }
    $from = $to;
  }
  FSAL::free_dirent_buffer($dirent);

  if ($files{'.'} == 0) { # dot not seen
    print "didn't read '.' dir entry, pass $i\n";
    $err++;
  }
  if ($files{'..'} == 0) { # dotdot not seen
    print "didn't read '..' dir entry, pass $i\n";
    $err++;
  }
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
  $ret = fsal_unlink($curhdl, $name);
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
