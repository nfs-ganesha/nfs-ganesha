#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use NFS_remote;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_create3 nfs_getattr3 nfs_lookup3 nfs_write3 nfs_read3 nfs_fsinfo3 nfs_remove3);
}

# get options
my $options = "tf:m:p:s:" ;
my %opt ;
my $usage = sprintf "Usage: %s [-t] -f <config_file> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";

# filehandle : 
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";

my $ret;

my $startw = MesureTemps::Temps::new();
my $endw = MesureTemps::Temps::new();
my $startr = MesureTemps::Temps::new();
my $endr = MesureTemps::Temps::new();

# Init RPC connections
rpcinit( SERVER       => $server, 
         MNT_VERSION  => 'mount3', 
         MNT_PROTO    => 'udp',
         MNT_PORT     => '0',
         NFS_VERSION  => 'nfs3', 
         NFS_PROTO    => 'udp',
         NFS_PORT     => $nfs_port
);

# mount FS
my $usemnt;
if (-e $rootpath and -f $rootpath) { # file to read to get filehandle ?
  open(FHDL, $rootpath) or die "Impossible to open $rootpath : $!\n";
  HDL: while (my $row = <FHDL>) {
    next HDL if $row =~ /^#/; # comments
    next HDL if !($row =~ /^@/); # not a file handle
    # else ..
    chomp $row;
    $ret = NFS_remote::get_new_nfs_fh3($row, $roothdl);
    if ($ret != 0) { die ""; }
    NFS_remote::copy_nfs_fh3($roothdl, $curhdl);
    last HDL;
  }
  $usemnt = 0;
} else {
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS_remote::copy_nfs_fh3($roothdl, $curhdl);
  $usemnt = 1;
}

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
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# run ...
my ($totfiles, $totdirs) = (0, 0);
$ret = dirtree($curhdl, $b->{levels}, $b->{files}, $b->{dirs}, $b->{fname}, $b->{dname}, \$totfiles, \$totdirs);
if ($ret != 0) {die "Basic test number five failed\n"; }

# check block size ...
my $blockw = $b->{blocksize};
my $blockr = $b->{blocksize};
my $fsinforet = NFS_remote::FSINFO3resok::new();
$ret = nfs_fsinfo3($roothdl, \$fsinforet);
my $wtmax = NFS_remote::get_wtmax_from_FSINFO3resok($fsinforet);
if ($blockw > $wtmax) {
  print "size of block to big ($blockw), set to wtmax ($wtmax)\n";
  $blockw = $wtmax;
}
my $rtmax = NFS_remote::get_rtmax_from_FSINFO3resok($fsinforet);
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
  my $hdl = NFS_remote::nfs_fh3::new();
  # if file exists, delete
  $ret = nfs_remove3($curhdl, $b->{bigfile});
  if ($ret != $NFS_remote::NFS3ERR_NOENT and $ret != $NFS_remote::NFS3_OK) {}
  # CREATE
  my $sattr = NFS_remote::sattr3::new();
  init_sattr3(\$sattr);
  $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
  $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RW;
  $ret = nfs_create3($curhdl, $b->{bigfile}, $NFS_remote::GUARDED, $sattr, \$hdl);
  if ($ret != 0) { die "can't create '".$b->{bigfile}."'\n"; }
  # GETATTR
  my $fattr = NFS_remote::fattr3::new();
  $ret = nfs_getattr3($hdl, \$fattr);
  if ($ret != 0) { die "can't stat '".$b->{bigfile}."'\n"; }
  # SIZE ?
  if (!NFS_remote::are_size3_equal($fattr->{size}, 0)) { 
    printf "'".$b->{bigfile}."' has size %llu, should be 0\n", $fattr->{size}; 
    die;
  }
  # WRITE
  my $offset = NFS_remote::get_new_offset3(0);
  my $stable = $NFS_remote::UNSTABLE;
  my $writeret = NFS_remote::WRITE3resok::new();
  my $bytes = $blockw;
  my $dataval = $data;
  my $arg = NFS_remote::nfs_arg_t::new();
  $arg->{arg_write3}->{file} = $hdl;
  $arg->{arg_write3}->{stable} = $stable;
  # -> size !
  for (my $si=$b->{size}; $si>0; $si-=$bytes) {
    if ($blockw > $si) { $bytes = $si; $dataval = substr($data, 0, $bytes); } # blockw > end of data to be wrotten
    $arg->{arg_write3}->{offset} = $offset;
    $arg->{arg_write3}->{count} = $bytes;
    $arg->{arg_write3}->{data}->{data_len} = length $dataval;
    $arg->{arg_write3}->{data}->{data_val} = $dataval;
    # call write !
    $ret = nfs_write3(\$arg, \$writeret);
    # free pointers allocated in C
    if ($ret != $NFS_remote::NFS3_OK) { die "'".$b->{bigfile}."' write failed ($ret)\n"; }
    # update the count and offset values
    NFS_remote::fill_offset3_from_WRITE3resok($offset, $writeret);
  }
  NFS_remote::free_offset3($offset); # C malloc
  $writeret->NFS_remote::WRITE3resok::DESTROY();
  # GETATTR
  $ret = nfs_getattr3($hdl, \$fattr);
  if ($ret != 0) { die "can't stat '".$b->{bigfile}."'\n"; }
  # SIZE ?
  if (!NFS_remote::are_size3_equal($fattr->{size}, $b->{size})) { 
    my $s = NFS_remote::get_print_size3($fattr->{size});
    die "'".$b->{bigfile}."' has size $s, should be ".$b->{size}."\n"; 
  }
}
MesureTemps::EndTime($startw, $endw);

MesureTemps::StartTime($startr);
for (my $i=0; $i<$b->{count}; $i++) {
  my $hdl = NFS_remote::nfs_fh3::new();
  # LOOKUP
  $ret = nfs_lookup3($curhdl, $b->{bigfile}, \$hdl);
  if ($ret != 0) { die "Lookup ".$b->{bigfile}." failed\n"; }
  # READ
  my $bytes = $blockr;
  my $offset = NFS_remote::get_new_offset3(0);
  my $readret = NFS_remote::READ3resok::new();
  my $res = NFS_remote::nfs_res_t::new();
  my $dataval = $data;
  for (my $si=$b->{size}; $si>0; $si-=$bytes) {
    if ($blockr > $si) { $bytes = $si; $dataval = substr($data, 0, $bytes); }
    # call read !
    $ret = nfs_read3($hdl, $offset, $bytes, \$res);
    NFS_remote::copy_READ3resok_from_res($res, $readret);
    if ($ret != $NFS_remote::NFS3_OK) { die "'".$b->{bigfile}."' read failed\n"; }
    # update the offset value
    NFS_remote::fill_offset3_from_READ3resok($offset, $readret);
    if (NFS_remote::get_int_from_count3($readret->{count}) != $bytes) { 
      die "'".$b->{bigfile}."' read failed (count : ".NFS_remote::get_int_from_count3($readret->{count})." - bytes : $bytes)\n"; 
    }
    if (NFS_remote::get_print_data_from_READ3resok($readret) ne $dataval) { 
      # test not in connectathon - coherency of data
      die "Data not coherent\n"; 
    }
  }
  NFS_remote::free_offset3($offset); # C malloc
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

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b5\t".$b->{size}."\t".$b->{count}."\t%s\t%s\t%d\t%d\n", $timew, $timer, $bytesw, $bytesr; 
close LOG;

print "Basic test number five OK ! \n";
