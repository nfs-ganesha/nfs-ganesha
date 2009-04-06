#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;
use FSAL;
BEGIN {
  require FSAL_tools;
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_path2str fsal_name2str fsal_lookupPath fsal_opendir fsal_readdir fsal_closedir);
}

# get options
my $options = "vs:p:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -s <server_config> [-p <path]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = FSAL::fsal_handle_t::new();
my $curhdl = FSAL::fsal_handle_t::new();
my $rootstr = $opt{p} || "/";
my $curstr = "./";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

# Init FSAL layer
$ret = fsalinit(  CONFIG_FILE   => $server_config,
                  FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# Function
sub loop {
  my ($hdl, $str) = @_;
  my $ret;

  # prepare needed attributes mask
  my $mask = FSAL::get_mask_attr_type();
  
  my $eod = 0; # end of dir
  my $from = $FSAL::FSAL_READDIR_FROM_BEGINNING;
  my $to = FSAL::fsal_cookie_t::new();
  my $dirent = FSAL::get_new_dirent_buffer();
  my $nbentries = 0;
  
  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while (!$eod) {
    # opendir
    my $dir = FSAL::fsal_dir_t::new();
    $ret = fsal_opendir($hdl, \$dir);
    if ($ret != 0) { 
      print "opendir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # readdir
    $ret = fsal_readdir($dir, $from, $mask, FSAL::get_new_buffersize(), \$dirent, \$to, \$nbentries, \$eod);
    if ($ret != 0) { 
      print "readdir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret;
    }
    
    # closedir
    $ret = fsal_closedir($dir);
    if ($ret != 0) { 
      print "close failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }
    
    # read readdir result
    my $entry = $dirent;
    while ($nbentries > 0 and $entry) { # foreach entry
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entry->{name}, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        FSAL::free_dirent_buffer($dirent);
        return $ret; 
      }
      if (!($entrystr =~ /^\.\.?\0*$/)) { # not '.' and not '..'
        print $str.$entrystr."\n";
        if ($entry->{attributes}->{type} == $FSAL::FSAL_TYPE_DIR) { # if dir, loop !
          $ret = loop($entry->{handle}, $str.$entrystr."/");
          if ($ret != 0) { 
            FSAL::free_dirent_buffer($dirent);
            return $ret; 
          }
        }
      }
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

my @tabpathstr;
for (my $i=0; $i<FSAL::get_fsal_max_path_len(); $i++) {
  $tabpathstr[$i] = " ";
}

my $blahstr = join("", @tabpathstr);
$ret = fsal_path2str($path, \$blahstr);
if ($ret != 0) { die "path2str failed\n"; }
print "Current directory is :\n";
FSAL::print_friendly_fsal_handle_t("\t$blahstr", $curhdl);

$ret = loop($curhdl, $curstr);
if ($ret != 0) {die "find failed\n"; }

print "ok\n";
