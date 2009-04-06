#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

BEGIN {
  require Cache_inode_tools;
  import Cache_inode_tools qw(:DEFAULT cacheinode_get cacheinode_lookup cacheinode_readdir cacheinode_setattr);
  require FSAL_tools;
  import FSAL_tools qw(fsalinit fsal_str2name fsal_str2path fsal_lookupPath);
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
my $rootentry = Cache_inode::get_new_entry();
my $curentry = Cache_inode::get_new_entry();
my $rootstr = $opt{p} || "/";
my $curstr = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;
my $dname = "dir.";
my $fname = "file.";
my $sleep = 12;
$| = 1; # auto flush
my $name2lookup = FSAL::fsal_name_t::new();
$ret = fsal_str2name($dname."0", \$name2lookup);
if ($ret != 0) { die "str2name failed\n"; }

# Init layers
$ret = fsalinit(  CONFIG_FILE   => $server_config,
                  FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }
$ret = cacheinodeinit(  CONFIG_FILE   => $server_config,
                        FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# Tool function
sub function_readdir {
  my ($entry) = @_;
  my $ret;
  my $begincookie = 0;
  my $nbfound = 0;
  my $endcookie = 0;
  my $eod = $Cache_inode::UNASSIGNED_EOD;
  my $entryarray = Cache_inode::get_new_dir_entry_array($CACHE_INODE_READDIR_SIZE);
  my $cookiearray = Cache_inode::get_new_cookie_array($CACHE_INODE_READDIR_SIZE);

  while ($eod != $Cache_inode::END_OF_DIR) { # while not end of dir
    # readdir
    $ret = cacheinode_readdir(
        $entry,
        $begincookie,
        $CACHE_INODE_READDIR_SIZE,
        \$nbfound,
        \$endcookie,
        \$eod,
        \$entryarray,
        \$cookiearray
    );
    if ($ret != 0) {
      Cache_inode::free_dir_entry_array($entryarray);
      Cache_inode::free_cookie_array($cookiearray);
      return $ret; 
    }

    $begincookie = $endcookie;
  }
  Cache_inode::free_dir_entry_array($entryarray);
  Cache_inode::free_cookie_array($cookiearray);
  
  return 0;
}

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

# get the test directory filehandle
$ret = testdir($rootentry, \$curstr, \$curentry, $teststr);
if ($ret != 0) { die; }

print "#################################################################################\n";

####### start !

# create test space
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curentry, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

# readdir $dname.0 => should find $fname.0
print "Lookup $dname"."0 ...\n";
my $entry = FSAL::fsal_handle_t::new();
$ret = cacheinode_lookup($curentry, $name2lookup, \$entry);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($entry);
if ($ret != 0) { die "Error $ret during readdir\n"; }

  # cd $teststr
  print "\tchmod 000 $dname"."0 ...\n";
  chdir $rootstr."/".$teststr or die "Impossible to chdir $rootstr/$teststr : $?\n";
  # chmod 000 $dname.0
  chmod 0000, $dname."0";

print "Sleeping $sleep seconds ...\n";
for (my $i=0; $i<$sleep; $i++) {
  print "".($i+1)." ... ";
  sleep 1;
}
print "\n";
# readdir $dname.0 => should return CACHE_INODE_FSAL_EACCESS
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($entry);
if ($ret != 18) { print "### TEST FAILED : readdir should return CACHE_INODE_FSAL_EACCESS (18), returned $ret.\n"; }

  print "\tchmod 777 $dname"."0 to continue.\n";
  chmod 0777, $dname."0";

# re-create test space
$ret = dirtree($curentry, 2, 1, 1, $fname, $dname, \$totfiles, \$totdirs);

print "#################################################################################\n";
# chmod 000 $dname.0
print "Lookup $dname"."0 ...\n";
$ret = cacheinode_lookup($curentry, $name2lookup, \$entry);
if ($ret != 0) { die "Error $ret during lookup\n"; }
print "Setattr mode=000 $dname"."0 ...\n";
my $sattr = FSAL::fsal_attrib_list_t::new();
$sattr->{asked_attributes} = FSAL::get_mask_attr_mode();
$sattr->{mode} = $FSAL::CHMOD_NONE;
$ret = cacheinode_setattr($entry, $sattr);

  # chmod 000 $dname.0
  print "\tchmod 777 $dname"."0 ...\n";
  chmod 0777, $dname."0";

print "Sleeping $sleep seconds ...\n";
for (my $i=0; $i<$sleep; $i++) {
  print "".($i+1)." ... ";
  sleep 1;
}
print "\n";
print "Readdir $dname"."0 ...\n";
$ret = function_readdir($entry);
if ($ret != 0) { print "### TEST FAILED : readdir should return CACHE_INODE_SUCCESS (0), returned $ret.\n"; }

  print "Setattr mode=777 $dname"."0 to continue.\n";
  $sattr->{asked_attributes} = FSAL::get_mask_attr_mode();
  $sattr->{mode} = $FSAL::CHMOD_RWX;
  $ret = cacheinode_setattr($entry, $sattr);

####### end

print "#################################################################################\n";
