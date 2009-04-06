#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use NFS_remote;
BEGIN {
  require NFS_remote_tools;
  import NFS_remote_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_rename3 nfs_lookup3);
}

# get options
my $options = "m:p:s:";
my %opt;
my $usage = sprintf "Usage: %s [-m <mount_path>] [-p <nfs_port>] [-s <server_name>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";

# filehandle & path
my $roothdl = NFS_remote::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $nfs_port = $opt{p} || 0;
my $server = $opt{s} || "localhost";
my $nbproc = 30;
my $nbcall = 30;

# children's subroutine
sub filio {
  my ($go) = @_;
  my $ret;

  print scalar localtime()." : $$ must init and wait.\n";
  # Init RPC connections
  rpcinit( SERVER       => $server, 
           MNT_VERSION  => 'mount3', 
           MNT_PROTO    => 'udp',
           MNT_PORT     => '0',
           NFS_VERSION  => 'nfs3', 
           NFS_PROTO    => 'udp',
           NFS_PORT     => $nfs_port
  );

  # wait
  while (1) {
    my ($sec, $min) = localtime;
    if (($sec == 5) and ($min == $go+1)) { last; }
  }
  print scalar localtime()." : $$ starts its job !\n";

  
  for (my $i=1; $i<=$nbcall; $i++) {
    # mount FS
    $ret = mnt_mount3($rootpath, \$roothdl);
    if ($ret != 0) { die "mount failed\n"; }
    print "\t".scalar localtime()." : $$ called mount.\n";

    # Umount FS
    $ret = mnt_umount3($rootpath);
    if ($ret != 0) { die "umount failed\n"; }
    print "\t".scalar localtime()." : $$ called umount.\n";
  }
  
  exit;
}

my $min = (localtime)[1];

for (my $i=0; $i<$nbproc; $i++) {
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
    filio($min);
  }
}

# wait children
for (my $i=0; $i<$nbproc; $i++) {
  my $pid = wait;
}
