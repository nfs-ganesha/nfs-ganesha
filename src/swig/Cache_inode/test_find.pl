#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;
use Cache_inode;
use FSAL;

use MesureTemps;
BEGIN {
  require Cache_inode_tools;
  import Cache_inode_tools qw(:DEFAULT cacheinode_get cacheinode_readdir cacheinode_getattr);
  require FSAL_tools;
  import FSAL_tools qw(fsalinit fsal_str2path fsal_path2str fsal_name2str fsal_lookupPath);
}

# get options
my $options = "vs:p:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -s <server_config> [-p <path]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = FSAL::fsal_handle_t::new();
my $curentry = Cache_inode::get_new_entry();
my $rootstr = $opt{p} || "/";
my $curstr = "./";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;
my $start = MesureTemps::Temps::new();
my $end1 = MesureTemps::Temps::new();
my $end2 = MesureTemps::Temps::new();

# Init layers
$ret = fsalinit(  CONFIG_FILE   => $server_config,
                  FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }
$ret = cacheinodeinit(  CONFIG_FILE   => $server_config,
                        FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# function
sub loop {
  my ($entry, $str) = @_;
  my $ret;
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
      if (!($entrystr =~ /^\.\.?\0*$/)) { # not '.' and not '..'
        print $str.$entrystr."\n";
        # get sub entry
        my $subentry = Cache_inode::get_entry_from_dir_entry_array($entryarray, $i);
        # dir or not ?
        my $attr = FSAL::fsal_attrib_list_t::new();
        $ret = cacheinode_getattr(
            $subentry,
            \$attr
        );
        if ($attr->{type} == $FSAL::FSAL_TYPE_DIR) { # if dir, loop !
          $ret = loop($subentry, $str.$entrystr."/");
          if ($ret != 0) { 
            Cache_inode::free_dir_entry_array($entryarray);
            Cache_inode::free_cookie_array($cookiearray);
            return $ret; 
          }
        }
      }
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
$ret = cacheinode_get($roothdl, \$curentry);
if ($ret != 0) { die "get FAILED\n"; }

###### start !
# find
MesureTemps::StartTime($start);
$ret = loop($curentry, $curstr);
if ($ret != 0) { die "Problem with find : $ret\n"; }
MesureTemps::EndTime($start, $end1);

MesureTemps::StartTime($start);
$ret = loop($curentry, $curstr);
if ($ret != 0) { die "Problem with find : $ret\n"; }
MesureTemps::EndTime($start, $end2);

printf "First find done in %s seconds\n", MesureTemps::ConvertiTempsChaine($end1, "");
printf "Second find done in %s seconds\n", MesureTemps::ConvertiTempsChaine($end2, "");

print "ok\n";
