#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use NFS;
BEGIN {
  require NFS_tools;
  import NFS_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_readdirplus3 nfs_remove3);
}

# get options
my $options = "tivs:f:m:";
my %opt;
my $usage = sprintf "Usage: %s [-t] [-i] [-v] -s <server_config> -f <config_file> [-m <mount_path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = NFS::nfs_fh3::new();
my $curhdl = NFS::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

# Init layers
$ret = nfsinit( CONFIG_FILE   => $server_config,
                FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# mount FS
my $usemnt;
if (-e $rootpath and -f $rootpath) { # file to read to get filehandle ?
  open(FHDL, $rootpath) or die "Impossible to open $rootpath : $!\n";
  HDL: while (my $row = <FHDL>) {
    next HDL if $row =~ /^#/; # comments
    next HDL if !($row =~ /^@/); # not a file handle
    # else ..
    chomp $row;
    $ret = NFS::get_new_nfs_fh3($row, $roothdl);
    if ($ret != 0) { die ""; }
    NFS::copy_nfs_fh3($roothdl, $curhdl);
    last HDL;
  }
  $usemnt = 0;
} else {
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS::copy_nfs_fh3($roothdl, $curhdl);
  $usemnt = 1;
}

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::SIX);
if (!defined($b)) { die "Missing test number six\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number six\n"; }
if ($b->{count} > $b->{files}) { die "count (".$b->{count}.") can't be greater than files (".$b->{files}.")"; }

printf "%s : readdirplus\n", basename($0);

# get the test directory filehandle
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die "$ret\n"; }

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
  my $eof = 0;
  my $begin_cookie = NFS::get_new_cookie3();
  my $cookieverf = "00000000";
  my $cookieverfret = "00000000";
  my $dirlist = NFS::dirlistplus3::new();
  while (!$eof) {
    my $entry;
    $ret = nfs_readdirplus3($curhdl, $begin_cookie, $cookieverf, \$cookieverfret, \$dirlist);
    if ($ret != 0) { die "readdir failed\n"; }
    $entry = $dirlist->{entries};
    $eof = NFS::is_eofplus($dirlist);
    while ($entry) {
      $entries++;
      if (!($entry->{name} =~ /^(\.|\.\.|$b->{fname}\d+)$/)) {
        if ($opt{i}) {
          $begin_cookie = $entry->{cookie};
          $entry = $entry->{nextentry};
          next;
        } else {
          die "unexpected dir entry '".$entry->{name}."'\n";
        }
      }
#      print "\t".$entry->{name}."\n";
      if ($files{$entry->{name}} == 1) { die "'$entry->{name}' dir entry read twice\n"; }
      $files{$entry->{name}} = 1; # yet seen
      $begin_cookie = $entry->{cookie};
      $entry = $entry->{nextentry};
    }
    $cookieverf = $cookieverfret;
  }
  NFS::free_cookie3($begin_cookie);
  if ($files{'.'} == 0) { # dot not seen
    print "didn't read '.' dir entry, pass $i";
    $err++;
  }
  if ($files{'..'} == 0) { # dotdot not seen
    print "didn't read '..' dir entry, pass $i";
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
  $ret = nfs_remove3($curhdl, $b->{fname}.$i);
  if ($ret != 0) { die "can't unlink ".$b->{fname}.$i."\n"; }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t$entries entries read, ".$b->{files}." files";
if ($opt{t}) {printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b6\t$entries\t".$b->{files}."\t%s\n", MesureTemps::ConvertiTempsChaine($end, ""); 
close LOG;

print "Basic test number six OK ! \n";
