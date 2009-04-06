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
my $root = $dir_conf."/b5.";
my $dir_test = $layer."/basic5";

my $num = 0;

my $size;
my $count;

for ($size=0; $size<=100000000; $size+=10000000) {
  for ($count=0; $count<=5; $count+=1) {
    $name = $root . sprintf "%010d", $num;
    open(CONF, ">$name.conf") or die "Ouverture impossible : $!\n";
    print "conf number $num : size=$size - count=$count\n";
    printf CONF "test_params {\n";
    printf CONF "\ttest_directory = \"%s\";\n", $dir_test;
    printf CONF "\tlog_file = \"%s\";\n", $log_file;
    printf CONF "\tbasic_test {\n";
    printf CONF "\t\tbtest 5 {\n";
    printf CONF "\t\t\tsize = %d;\n", $size;
    printf CONF "\t\t\tblocksize = 4096;\n";
    printf CONF "\t\t\tcount = %d;\n", $count;
    printf CONF "\t\t};\n";
    printf CONF "\t};\n";
    printf CONF "};\n";
    close CONF;
    $num++;
  }
}
