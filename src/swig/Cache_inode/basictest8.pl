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
  import Cache_inode_tools qw(:DEFAULT cacheinode_get cacheinode_create cacheinode_getattr cacheinode_readlink cacheinode_remove);
  require FSAL_tools;
  import FSAL_tools qw(fsalinit fsal_str2path fsal_lookupPath fsal_str2name fsal_path2str);
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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::EIGHT);
if (!defined($b)) { die "Missing test number eight\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number eight\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number seight\n"; }

printf "%s : symlink and readlink\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($rootentry, \$curstr, \$curentry, $teststr);
if ($ret != 0) { die; }

my @tabpathstr;
for (my $i=0; $i<FSAL::get_fsal_max_path_len(); $i++) {
  $tabpathstr[$i] = " ";
}

# run ...
MesureTemps::StartTime($start);
for (my $ci=0; $ci<$b->{count}; $ci++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    my $linkname = FSAL::fsal_name_t::new();
    $ret = fsal_str2name($b->{fname}.$fi, \$linkname);
    if ($ret != 0) { die "str2name failed\n"; }
    my $linkcontent = FSAL::fsal_path_t::new();
    $ret = fsal_str2path($b->{sname}, \$linkcontent);
    if ($ret != 0) { die "str2path failed\n"; }
    # SYMLINK
    my $entry = Cache_inode::get_new_entry();
    $ret = cacheinode_create($curentry, $linkname, $Cache_inode::SYMBOLIC_LINK, $FSAL::CHMOD_RWX, $linkcontent, \$entry);
    if ($ret != 0) { die "can't make symlink ".$b->{fname}.$fi; }
    # GETATTR SYMLINK
    my $fattr = FSAL::fsal_attrib_list_t::new();
    $fattr->{asked_attributes} = FSAL::get_mask_attr_type();
    $ret = cacheinode_getattr($entry, \$fattr);
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after symlink\n"; }
    # FTYPE ?
    if ($fattr->{type} != $FSAL::FSAL_TYPE_LNK) { die "type of ".$b->{fname}.$fi." not symlink\n"; }
    # READLINK
    $ret = cacheinode_readlink($entry, \$linkcontent);
    if ($ret != 0) { die "Readlink failed\n"; }
    my $contentstr = join("", @tabpathstr);
    $ret = fsal_path2str($linkcontent, \$contentstr);
    if ($ret != 0) { die "path2str failed\n"; }
    if (!($contentstr =~ /^$b->{sname}\0*$/)) { die "readlink ".$b->{fname}.$fi." returned '$contentstr', expect '".$b->{sname}."'\n"; }
    # REMOVE
    $ret = cacheinode_remove($curentry, $linkname);
    if ($ret != 0) { die "can't unlink ".$b->{fname}.$fi."\n"; }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." symlinks and readlinks on ".$b->{files}." files (size of symlink : ".(length $b->{sname}).")";
if ($opt{t}) { printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b8\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t".(length $b->{sname})."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number eight OK ! \n";
