#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

BEGIN {
  require FSAL_tools;
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_lookupPath fsal_lookup fsal_str2name fsal_opendir fsal_readdir fsal_closedir fsal_setattrs);
}

# get options
my $options = "vs:p:d:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -s <config_file> -d <test_directory> [-p <path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $teststr = $opt{d} || die "Missing Test Directory\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & str
my $roothdl = FSAL::fsal_handle_t::new();
my $curhdl = FSAL::fsal_handle_t::new();
my $rootstr = $opt{p} || "/";
my $curstr = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;
my $dname = "dir.";
my $fname = "file.";
my $name2lookup = FSAL::fsal_name_t::new();
$ret = fsal_str2name($dname."0", \$name2lookup);
if ($ret != 0) { die "str2name failed\n"; }

# Init layers
$ret = fsalinit(  CONFIG_FILE   => $server_config,
                  FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# Tool function
sub function_readdir {
  my ($hdl) = @_;
  my $ret;

  # prepare needed attributes mask
  my $mask = FSAL::get_mask_attr_type();
  
  my $eod = 0; # end of dir
  my $from = $FSAL::FSAL_READDIR_FROM_BEGINNING;
  my $to = FSAL::fsal_cookie_t::new();
  my $dirent = FSAL::get_new_dirent_buffer();
  my $nbentries = 0;
  
  while (!$eod) {
    # opendir
    my $dir = FSAL::fsal_dir_t::new();
    $ret = fsal_opendir($hdl, \$dir);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # readdir
    $ret = fsal_readdir($dir, $from, $mask, FSAL::get_new_buffersize(), \$dirent, \$to, \$nbentries, \$eod);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      return $ret;
    }
    
    # closedir
    $ret = fsal_closedir($dir);
    if ($ret != 0) { 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }
    
    # read readdir result
    my $entry = $dirent;
    while ($nbentries > 0 and $entry) { # foreach entry
      # next entry
      $entry = $entry->{nextentry};
    }
    # to get the value pointed by $to
    $from = $to;
  }
  FSAL::free_dirent_buffer($dirent);
  
  return 0;
}

# go to the mount point.
my $path = FSAL::fsal_path_t::new();
$ret = fsal_str2path($rootstr, \$path);
if ($ret != 0) { die "str2path failed\n"; }
$ret = fsal_lookupPath($path, \$roothdl);
if ($ret != 0) { die "lookupPath failed\n"; }
FSAL::copy_fsal_handle_t($roothdl, $curhdl);

# get the test directory filehandle
$ret = testdir($roothdl, \$curstr, \$curhdl, $teststr);
if ($ret != 0) { die; }

print "#################################################################################\n";

####### start !

# create test space
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

# readdir $dname.0 => should find $fname.0
print "Lookup $dname"."0 ...\n";
my $hdl = FSAL::fsal_handle_t::new();
$ret = fsal_lookup($curhdl, $name2lookup, \$hdl);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 0) { die "Error $ret during readdir\n"; }

  # cd $teststr
  print "\tchmod 000 $dname"."0 ...\n";
  chdir $rootstr."/".$teststr or die "Impossible to chdir $rootstr/$teststr : $?\n";
  # chmod 000 $dname.0
  chmod 0000, $dname."0";

# readdir $dname.0 => should return ERR_FSAL_ACCESS
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 13) { print "### TEST FAILED : readdir should return ERR_FSAL_ACCESS (13), returned $ret.\n"; }

  print "\tchmod 777 $dname"."0 to continue.\n";
  chmod 0777, $dname."0";

# re-create test space
$ret = dirtree($curhdl, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

print "#################################################################################\n";
# chmod 000 $dname.0
print "Lookup $dname"."0 ...\n";
$ret = fsal_lookup($curhdl, $name2lookup, \$hdl);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Setattr mode=000 $dname"."0 ...\n";
my $sattr = FSAL::fsal_attrib_list_t::new();
$sattr->{asked_attributes} = FSAL::get_mask_attr_mode();
$sattr->{mode} = $FSAL::CHMOD_NONE;
$ret = fsal_setattrs($hdl, $sattr);

  # chmod 000 $dname.0
  print "\tchmod 777 $dname"."0 ...\n";
  chmod 0777, $dname."0";

print "Readdir $dname"."0 ...\n";
$ret = function_readdir($hdl);
if ($ret != 0) { print "### TEST FAILED : readdir should return ERR_FSAL_NO_ERROR (0), returned $ret.\n"; }

  print "Setattr mode=777 $dname"."0 to continue.\n";
  $sattr->{asked_attributes} = FSAL::get_mask_attr_mode();
  $sattr->{mode} = $FSAL::CHMOD_RWX;
  $ret = fsal_setattrs($hdl, $sattr);

####### end

print "#################################################################################\n";
