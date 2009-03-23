#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use MesureTemps;
use NFS_remote;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_mkdir3 nfs_create3 nfs_remove3 nfs_rename3 nfs_lookup3);
}

# get options
my $options = "vd:m:p:s:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -d <test_directory> [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $testpath = $opt{d} || die "Missing Test Directory\n";

# filehandle & path
my $roothdl = NFS_remote::nfs_fh3::new();
my $curhdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";
my $nbcall = 50;

# children's subroutine
sub filio {
  my ($proc, $go) = @_;
  my $ret;
  
  my $dirAhdl = NFS_remote::nfs_fh3::new();
  my $dirBhdl = NFS_remote::nfs_fh3::new();
  my $fileAhdl = NFS_remote::nfs_fh3::new();
  my $fileBhdl = NFS_remote::nfs_fh3::new();

  my $start = MesureTemps::Temps::new();
  my $endbefore = MesureTemps::Temps::new();
  my $endafter = MesureTemps::Temps::new();

  
  MesureTemps::StartTime($start);
  # wait
  print scalar localtime()." : Proccess $proc ($$).\n";
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 10) and ($min == $go+1)) { last; }
  }
  if ($opt{v}) { print scalar localtime()." : Process $proc init.\n"; }

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
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS_remote::copy_nfs_fh3($roothdl, $curhdl);

  # get the test directory filehandle (just chdir)
  $ret = mtestdir($roothdl, \$curpath, \$curhdl, $testpath);
  if ($ret != 0) { die "chdir failed\n"; }

  
  # create directories
  ($proc == 1) # should create dir.A
    && do {
      print scalar localtime()." : Process $proc should create dir.A\n";
      
      my $sattr = NFS_remote::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;
      
      $ret = nfs_mkdir3($curhdl, "dir.A", $sattr, \$dirAhdl);
      if ($ret != 0) { die "\tProcess $proc failed to create dir.A\n";}
    };
  
  ($proc == 2) # should create dir.B
    && do {
      print scalar localtime()." : Process $proc should create dir.B\n";

      my $sattr = NFS_remote::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RWX;
      
      $ret = nfs_mkdir3($curhdl, "dir.B", $sattr, \$dirBhdl);
      if ($ret != 0) { die "\tProcess $proc failed to create dir.B\n";}
    };

  
  # wait to be sure that dir.A and dir.B are created
  if ($opt{v}) { print scalar localtime()." : Proccess $proc must wait.\n"; }
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 15) and ($min == $go+1)) { last; }
  }
  
  
  ($proc == 3) # lookup dir.B and dir.B
    && do {
      print scalar localtime()." : Process $proc should lookup dir.A and dir.B\n";
      
      $ret = nfs_lookup3($curhdl, "dir.A", \$dirAhdl);
      if ($ret != 0) { die "\tProcess $proc failed to lookup dir.A\n"; }
     
      $ret = nfs_lookup3($curhdl, "dir.B", \$dirBhdl);
      if ($ret != 0) { die "\tProcess $proc failed to lookup dir.B\n"; }
    };

  
  # wait 
  if ($opt{v}) { print scalar localtime()." : Proccess $proc must wait.\n"; }
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 20) and ($min == $go+1)) { last; }
  }
  if ($opt{v}) { print scalar localtime()." : Proccess $proc starts its job.\n"; }

  
  # start !
  ($proc == 1) # should create file.A, remove file.A
    && do {
      print scalar localtime()." : Process $proc should create file.A, remove file.A $nbcall times\n";
      
      my $sattr = NFS_remote::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RW;
      
      for (my $i=0; $i<$nbcall; $i++) {
        
        MesureTemps::EndTime($start, $endbefore);
        $ret = nfs_create3($dirAhdl, "file.A", $NFS_remote::GUARDED, $sattr, \$fileAhdl);
        MesureTemps::EndTime($start, $endafter);
        
        if ($opt{v}) { print "\t\tProcess $proc created file.A : $ret (".MesureTemps::ConvertiTempsChaine($endbefore, "")." - ".MesureTemps::ConvertiTempsChaine($endafter, "").").\n"; }
        if ($ret != $NFS_remote::NFS3_OK) { print "\tProcess $proc failed to create file.A : $ret\n"; }
        
        MesureTemps::EndTime($start, $endbefore);
        $ret = nfs_remove3($dirAhdl, "file.A");
        MesureTemps::EndTime($start, $endafter);
      
        if ($opt{v}) { print "\t\tProcess $proc removed file.A : $ret (".MesureTemps::ConvertiTempsChaine($endbefore, "")." - ".MesureTemps::ConvertiTempsChaine($endafter, "").").\n"; }
        if (($ret != $NFS_remote::NFS3_OK) and ($ret != $NFS_remote::NFS3ERR_NOENT)) { print "\tProcess $proc failed to remove file.A : $ret\n"; }
      
      }
    };
  
  ($proc == 2) # should create file.B, remove file.B
    && do {
      print scalar localtime()." : Process $proc should create file.B, remove file.B $nbcall times\n";
      
      my $sattr = NFS_remote::sattr3::new();
      init_sattr3(\$sattr);
      $sattr->{mode}->{set_it} = $NFS_remote::TRUE;
      $sattr->{mode}->{set_mode3_u}->{mode} = $NFS_remote::CHMOD_RW;
      
      for (my $i=0; $i<$nbcall; $i++) {
        
        MesureTemps::EndTime($start, $endbefore);
        $ret = nfs_create3($dirBhdl, "file.B", $NFS_remote::GUARDED, $sattr, \$fileBhdl);
        MesureTemps::EndTime($start, $endafter);
        
        if ($opt{v}) { print "\t\tProcess $proc created file.B : $ret (".MesureTemps::ConvertiTempsChaine($endbefore, "")." - ".MesureTemps::ConvertiTempsChaine($endafter, "").").\n"; }
        if (($ret != $NFS_remote::NFS3_OK) and ($ret != $NFS_remote::NFS3ERR_EXIST)) { print "\tProcess $proc failed to create file.B : $ret\n"; }
        
        MesureTemps::EndTime($start, $endbefore);
        $ret = nfs_remove3($dirBhdl, "file.B");
        MesureTemps::EndTime($start, $endafter);
      
        if ($opt{v}) { print "\t\tProcess $proc removed file.B : $ret (".MesureTemps::ConvertiTempsChaine($endbefore, "")." - ".MesureTemps::ConvertiTempsChaine($endafter, "").").\n"; }
        if ($ret != $NFS_remote::NFS3_OK) { print "\tProcess $proc failed to remove file.B : $ret\n"; }
      
      }
    };
  
  ($proc == 3) # lookup dir.B and dir.B
    && do {
      print scalar localtime()." : Process $proc should move dir.A/file.A to dir.B/file.B $nbcall times\n";
      
      for (my $i=0; $i<$nbcall*2; $i++) {
        
        MesureTemps::EndTime($start, $endbefore);
        $ret = nfs_rename3($dirAhdl, "file.A", $dirBhdl, "file.B");
        MesureTemps::EndTime($start, $endafter);
        
        if ($opt{v}) { print "\t\tProcess $proc moved dir.A/file.A to dir.B/file.B : $ret (".MesureTemps::ConvertiTempsChaine($endbefore, "")." - ".MesureTemps::ConvertiTempsChaine($endafter, "").").\n"; }
        if (($ret != $NFS_remote::NFS3_OK) and ($ret != $NFS_remote::NFS3ERR_NOENT) and ($ret != $NFS_remote::NFS3ERR_EXIST)) { print "\tProcess $proc failed to move dir.A/file.A to dir.B/file.B : $ret\n"; }
      
      }
    };

  
  # Umount FS
  $ret = mnt_umount3($rootpath);
  if ($opt{v}) { print scalar localtime()." : Process $proc exits.\n"; }
  
  exit;
}

my $min = (localtime)[1];

for (my $i=0; $i<3; $i++) {
  my $pid = fork;
  SWITCH: {
    ($pid < 0)
      && do {
        print "Fork impossible : $!\n";
        next SWITCH;
      };
    ($pid == 0) # padre
      && do {
        next SWITCH;
      };
    filio($i+1, $min);
  }
}

my $ret;

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
$ret = mnt_mount3($rootpath, \$roothdl);
if ($ret != 0) { die "mount failed\n"; }
NFS_remote::copy_nfs_fh3($roothdl, $curhdl);

# get the test directory filehandle
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# Umount FS
$ret = mnt_umount3($rootpath);

# wait children
for (my $i=0; $i<3; $i++) {
  my $pid = wait;
}
