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
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_lookupPath fsal_staticfsinfo fsal_str2name fsal_unlink fsal_create fsal_getattrs fsal_open fsal_close fsal_write fsal_read fsal_lookup);
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
my $curhdl = FSAL::fsal_handle_t::new();
my $rootstr = $opt{p} || "/";
my $curstr = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

my $startw = MesureTemps::Temps::new();
my $startr = MesureTemps::Temps::new();
my $endw = MesureTemps::Temps::new();
my $endr = MesureTemps::Temps::new();

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

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::FIVE);
if (!defined($b)) { die "Missing test number five\n"; }

if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number five\n"; }
if ($b->{size} == -1) { die "Missing 'size' parameter in the config file $conf_file for the basic test number five\n"; }
if ($b->{blocksize} == -1) { die "Missing 'blocksize' parameter in the config file $conf_file for the basic test number five\n"; }

printf "%s : read and write\n", basename($0);

# get the test directory filehandle
my $teststr = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curstr, \$curhdl, $teststr);
if ($ret != 0) { die "$ret\n"; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, $b->{levels}, $b->{files}, $b->{dirs}, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number five failed\n"; }

# check block size ...
my $blockw = $b->{blocksize};
my $blockr = $b->{blocksize};
my $fsinfo = FSAL::fsal_staticfsinfo_t::new();
$ret = fsal_staticfsinfo($roothdl, \$fsinfo);
my $wtmax = $fsinfo->{maxwrite} ? $fsinfo->{maxwrite} : 4096;
if ($blockw > $wtmax) {
  print "size of block to big ($blockw), set to wtmax ($wtmax)\n";
  $blockw = $wtmax;
}
my $rtmax = $fsinfo->{maxread} ? $fsinfo->{maxread} : 4096;
if ($blockr > $rtmax) {
  print "size of block to big ($blockr), set to rtmax ($rtmax)\n";
  $blockr = $rtmax;
}

# data
my @valtab;
if ($blockw > $blockr) {
  for (my $j=0; $j<$blockw; $j++) { $valtab[$j] = "x"; }
} else {
  for (my $j=0; $j<$blockr; $j++) { $valtab[$j] = "x"; }
}
my $data = join("", @valtab);
undef @valtab;

MesureTemps::StartTime($startw);
for (my $i=0; $i<$b->{count}; $i++) {
  my $hdl = FSAL::fsal_handle_t::new();
  # if file exists, delete
  my $bigfilename = FSAL::fsal_name_t::new();
  $ret = fsal_str2name($b->{bigfile}, \$bigfilename);
  if ($ret != 0) { die "str2name failed\n"; }
  $ret = fsal_unlink($curhdl, $bigfilename);
  if ($ret != 2 and $ret != 0) { die "unlink failed\n"; } # != NO_ENT & OK
  # CREATE
  my $mode = $FSAL::CHMOD_RW;
  $ret = fsal_create($curhdl, $bigfilename, $mode, \$hdl);
  if ($ret != 0) { die "can't create '".$b->{bigfile}."'\n"; }
  # GETATTR
  my $fattr = FSAL::fsal_attrib_list_t::new();
  $fattr->{asked_attributes} = FSAL::get_mask_attr_size();
  $ret = fsal_getattrs($hdl, \$fattr);
  if ($ret != 0) { die "can't stat '".$b->{bigfile}."'\n"; }
  # SIZE ?
  if ($fattr->{filesize} != 0) { 
    die "'".$b->{bigfile}."' has size ".$fattr->{filesize}.", should be 0\n"; 
  }
  my $file = FSAL::fsal_file_t::new();
  # WRITE
  my $bytes = $blockw;
  my $dataval = $data;
  my $offset = 0;
  # -> size !
  for (my $si=$b->{size}; $si>0; $si-=$bytes) {
    # OPEN
    $ret = fsal_open($hdl, $FSAL::FSAL_O_WRONLY, \$file);
    if ($ret != 0) { die "open failed\n"; }
    if ($blockw > $si) { $bytes = $si; $dataval = substr($data, 0, $bytes); } # blockw > end of data to be wrotten
    my $caddr = FSAL::get_caddr_from_string($dataval);
    my $writeamount = 0;
    # call write !
    $ret = fsal_write($file, $offset, $bytes, $caddr, \$writeamount);
    if ($ret != 0) { die "'".$b->{bigfile}."' write failed ($ret)\n"; }
    # update the offset value
    $offset += $writeamount;
    # CLOSE
    $ret = fsal_close($file);
    if ($ret != 0) { die "close failed\n"; }
  }
  # GETATTR
  $ret = fsal_getattrs($hdl, \$fattr);
  if ($ret != 0) { die "can't stat '".$b->{bigfile}."'\n"; }
  # SIZE ?
  if ($fattr->{filesize} != $b->{size}) { 
    die "'".$b->{bigfile}."' has size ".$fattr->{filesize}.", should be ".$b->{size}."\n"; 
  }
}
MesureTemps::EndTime($startw, $endw);

MesureTemps::StartTime($startr);
for (my $i=0; $i<$b->{count}; $i++) {
  my $hdl = FSAL::fsal_handle_t::new();
  # LOOKUP
  my $bigfilename = FSAL::fsal_name_t::new();
  $ret = fsal_str2name($b->{bigfile}, \$bigfilename);
  if ($ret != 0) { die "str2name failed\n"; }
  $ret = fsal_lookup($curhdl, $bigfilename, \$hdl);
  if ($ret != 0) { die "Lookup ".$b->{bigfile}." failed\n"; }
  # READ
  my $file = FSAL::fsal_file_t::new();
  my $bytes = $blockr;
  my $offset = 0;
  my $dataval = $data;
  for (my $si=$b->{size}; $si>0; $si-=$bytes) {
    # OPEN
    $ret = fsal_open($hdl, $FSAL::FSAL_O_WRONLY, \$file);
    if ($ret != 0) { die "open failed\n"; }
    if ($blockr > $si) { $bytes = $si; $dataval = substr($data, 0, $bytes); }
    my $caddr = FSAL::get_new_caddr($bytes);
    my $readamount = 0;
    my $eof = 0;
    # call read !
    $ret = fsal_read($file, $offset, $bytes, \$caddr, \$readamount, \$eof);
    if ($ret != 0) { die "'".$b->{bigfile}."' read failed\n"; }
    # update the offset value
    $offset += $readamount;
    if ($readamount != $bytes) { 
      die "'".$b->{bigfile}."' read failed (count : $readamount - bytes : $bytes)\n"; 
    }
#    print $readamount."\n";
#    my $readval = FSAL::get_string_from_caddr($caddr);
#    if (!($readval =~ /^$dataval\0*$/)) { 
#      # test not in connectathon - coherency of data
#      die "Data not coherent\n"; 
#    }
    FSAL::free_caddr($caddr);
    # CLOSE
    $ret = fsal_close($file);
    if ($ret != 0) { die "close failed\n"; }
    }
}
MesureTemps::EndTime($startr, $endr);

# print results
print "\twrote ".$b->{size}." byte file ".$b->{count}." times";
my $timew = MesureTemps::ConvertiTempsChaine($endw, "");
my $bytesw;
if ($opt{t}) {
  if ($timew + 0) { # "0.00" true, BUT "0.00" + 0 false
    $bytesw = $b->{size}*$b->{count}/$timew;
    printf " in %.2f seconds (%d bytes/sec)", $timew, $bytesw; 
  } else {
    $bytesw = $b->{size}*$b->{count};
    printf " in %.2f seconds (>%d bytes/sec)", $timew, $bytesw; 
  }
}
print "\n";
print "\tread ".$b->{size}." byte file ".$b->{count}." times";
my $timer = MesureTemps::ConvertiTempsChaine($endr, "");
my $bytesr;
if ($opt{t}) {
  if ($timer + 0) {
    $bytesr = $b->{size}*$b->{count}/$timer;
    printf " in %.2f seconds (%d bytes/sec)", $timer, $bytesr; 
  } else {
    $bytesr = $b->{size}*$b->{count};
    printf " in %.2f seconds (>%d bytes/sec)", $timer, $bytesr; 
  }
}
print "\n";

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b5\t".$b->{size}."\t".$b->{count}."\t%s\t%s\t%d\t%d\n", $timew, $timer, $bytesw, $bytesr; 
close LOG;

print "Basic test number five OK ! \n";
