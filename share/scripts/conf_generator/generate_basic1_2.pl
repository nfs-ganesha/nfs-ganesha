#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

# get options
my $options = "d:l:f:";
my %opt;
my $usage = sprintf "Usage: %s -d <dir_conf> -l <layer> -f <log_file>", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $dir_conf = $opt{d} || die "Missing directory containing conf files\n";
my $layer = $opt{l} || die "Missing layer\n";
my $log_file = $opt{f} || die "Missing log file\n";

my $name;
my $root = $dir_conf."/b12.";
my $dir_test = $layer."/basic12";

my $num = 0;

my $levels;
my $files;
my $dirs;

for ($levels=1; $levels<=5; $levels++) {
  for ($files=0; $files<=10; $files++) {
    for ($dirs=0; $dirs<=10; $dirs++) {
      $name = $root . sprintf "%010d", $num;
      open(CONF, ">$name.conf") or die "Ouverture impossible : $!\n";
      print "conf number $num : levels=$levels - files=$files - dirs=$dirs\n";
      printf CONF "test_params {\n";
      printf CONF "\ttest_directory = \"%s\";\n", $dir_test;
      printf CONF "\tlog_file = \"%s\";\n", $log_file;
      printf CONF "\tbasic_test {\n";
      printf CONF "\t\tbtest 1 and 2 {\n";
      printf CONF "\t\t\tlevels = %d;\n", $levels;
      printf CONF "\t\t\tfiles = %d;\n", $files;
      printf CONF "\t\t\tdirs = %d;\n", $dirs;
      printf CONF "\t\t};\n";
      printf CONF "\t};\n";
      printf CONF "};\n";
      close CONF;
      $num++;
    }
  }
}
