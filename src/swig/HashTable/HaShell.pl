#!/usr/bin/perl

use strict;
no strict 'refs';
use warnings;

use File::Basename;
use Getopt::Std;

use MesureTemps;
use HashTable;
BEGIN {
  require HashTable_tools;
  import HashTable_tools;
}

# get options
my $name = "HaShell";
my $help = "Supported commands :
\t- help            : print this help

\t- get key val ret : find the Hash(key) => value in the hashtable, expect the value val and the status ret
\t- set key val ret : try to add Hash(key) => val, expect the status ret (with overwrite)
\t- new key val ret : try to add Hash(key) => val, expect the status ret (without overwrite)
\t- test key ret    : check if Hash(key) exist, expect the status ret
\t- del key ret     : try to del Hash(key), expect the status ret

\t- print           : print Hash
\t- start_clock     : start a clock
\t- stop_clock      : stop the clock and print duration
";

my $options = "c:";
my %opt;
my $usage = sprintf "Usage: %s [-c <command_file>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $command_file = $opt{c} || "";
my $stdin;
if ($command_file ne "") {
  open(HANDLE, "<$command_file") or die "Unable to open '$command_file' : $!\n";
  $stdin = *HANDLE;
} else {
  $stdin = *STDIN;
}

# constantes
my $MAXTEST = 10000;
my $NB_PREALLOC = 10000;
my $PRIME = 109;

# variables
my $ret;
my $hparam = HashTable::hash_parameter_t::new();
my $htable;
my @strtab;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

# init
HashTable::Swig_BuddyInit();

$hparam->{index_size} = $PRIME;
$hparam->{alphabet_length} = 10;
$hparam->{nb_node_prealloc} = $NB_PREALLOC;
HashTable::set_hash_parameter_t_hash_func_key_simple($hparam);
HashTable::set_hash_parameter_t_hash_func_rbt($hparam);
HashTable::set_hash_parameter_t_compare_key($hparam);
HashTable::set_hash_parameter_t_key_to_str($hparam);
HashTable::set_hash_parameter_t_val_to_str($hparam);

$htable = HashTable::HashTable_Init($hparam);
if (! defined $htable) { die "Test FAILED : Init error\n"; }

##########
# HashTable INIT
MesureTemps::StartTime($start);
print "HashTable init\n";
for (my $i=0; $i<$MAXTEST; $i++) {
  $strtab[$i] = "$i";
  my $buffkey = HashTable::hash_buffer_t::new();
  my $buffval = HashTable::hash_buffer_t::new();
  
  HashTable::set_hash_buffer_t_pdata($buffkey, length $strtab[$i], $strtab[$i]);
  HashTable::set_hash_buffer_t_pdata($buffval, length $strtab[$i], $strtab[$i]);

  $ret = HashTable::HashTable_Test_And_Set($htable, $buffkey, $buffval, $HashTable::HASHTABLE_SET_HOW_SET_OVERWRITE);
  if ($ret != $HashTable::HASHTABLE_SUCCESS) { die "Test FAILED : can not add entry $i : $ret\n"; }
  if ($opt{d}) { printf "Added %s , %d , ret = %d\n", "$i", $i, $ret; }
}
MesureTemps::EndTime($start, $end);
printf "Adding %d entries duration : %s\n", $MAXTEST, MesureTemps::ConvertiTempsChaine($end, "");
if ($opt{d}) {
  print "-----------------------------------------\n";
  HashTable::HashTable_Print($htable);
}

printf "====== Start of interactivity ===========\n";

print "$name>";
SWITCH: while (my $row = <$stdin>) {
  chomp $row;
  $_ = $row;
  /^#/        && do { # Comment
                print "$name>";
                next SWITCH;
              };
  /^quit/     && do { # quit !
                last SWITCH;
              };
  /^exit/     && do { # exit !
                last SWITCH;
              };
  /^[\t ]*(set|new|get) (\d+) (\d+) (\d+)$/ 
              && do { # which command ?
                print "$1 $2 $3 -> $4 ?\n";
                my $function = "do_${1}";
                my $err = "";
                # call to do_(set/new/get)
                $ret = &$function($htable, $2, $3, \$err);
                if ($ret >= 0) { # HashTable Error
                  if ($err ne "") { print "... ".$err."\n"; }
                  if ($ret != $4) { # Unexpected ret value
                    print "... $1 FAILED : returned $ret != $4 (expected)\n";
                  }
                } else { # Value error
                  print "... ".$err."\n";
                }
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*(del|test) (\d+) (\d+)$/
              && do { # wich command ?
                print "$1 $2 -> $3 ?\n";
                my $function = "do_${1}";
                my $err = "";
                # call to do_(del/test)
                $ret = &$function($htable, $2, \$err);
                if ($err ne "") { print "... ".$err."\n"; }
                if ($ret != $3) { # Unexpected ret value
                  print "... $1 FAILED : returned $ret != $3 (expected)\n";
                }
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*print$/
              && do {
                HashTable::HashTable_Print($htable);
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*help$/
              && do {
                print $help;
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*start_clock$/
              && do {
                print "=========================================\n";
                MesureTemps::StartTime($start);
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*stop_clock$/
              && do {
                MesureTemps::EndTime($start, $end);
                printf "========= Duration : %s ==========\n", MesureTemps::ConvertiTempsChaine($end, "");
                print "$name>";
                next SWITCH;
              };
  /^[\t ]*interactive/
              && do {
                $stdin = *STDIN;
                next SWITCH;
              };
  print "... Syntaxe error\n";
  print "$name>";
}

print "================ TEST OK ================\n";
