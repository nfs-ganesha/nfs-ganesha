#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use MesureTemps;
use HashTable;

# get options
my $options = "dv" ;
my %opt ;
my $usage = sprintf "Usage: %s [-d] [-v]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";

# constantes
my $MAXTEST = 10000;
my $MAXDESTROY = 50;
my $MAXGET = 30;
my $NB_PREALLOC = 10000;
my $PRIME = 109;
my $CRITERE = 12;
my $CRITERE_2 = 14;

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
printf "=========================================\n";

##########
# Check cohenrency of read values
my $buffkey = HashTable::hash_buffer_t::new();
my $buffval = HashTable::hash_buffer_t::new();
HashTable::set_hash_buffer_t_pdata($buffkey, length "$CRITERE", "$CRITERE");

# first try
MesureTemps::StartTime($start);
$ret = HashTable::HashTable_Get($htable, $buffkey, $buffval);
MesureTemps::EndTime($start, $end);

printf "Get key number %d --> %d\n", $CRITERE, $ret;
printf "Getting duration : %s\n", MesureTemps::ConvertiTempsChaine($end, "");
if ($ret != $HashTable::HASHTABLE_SUCCESS) { die "Test FAILED : key not found\n"; }

print "-----------------------------------------\n";

# second try (should be faster)
MesureTemps::StartTime($start);
$ret = HashTable::HashTable_Get($htable, $buffkey, $buffval);
MesureTemps::EndTime($start, $end);

printf "Get key number %d (second try) --> %d\n", $CRITERE, $ret;
printf "Getting duration : %s\n", MesureTemps::ConvertiTempsChaine($end, "");
if ($ret != $HashTable::HASHTABLE_SUCCESS) { die "Test FAILED : key not found\n"; }

# cohenrecy of the read value ?
my $val = HashTable::get_print_pdata($buffval);
printf "----> Gotten value = len %d ; val = %s\n", $buffval->{len}, $val;

if ($val ne $CRITERE) { die "Test FAILED : bad read value\n"; }

print "-----------------------------------------\n";

# random get
print "Now, try to get $MAXGET entries (random)\n";
MesureTemps::StartTime($start);
for (my $i=0; $i<$MAXGET; $i++) {
  my $random_val = int rand $MAXTEST;
  my $buffkey2 = HashTable::hash_buffer_t::new();
  my $buffval2 = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey2, length "$random_val", "$random_val");
  $ret = HashTable::HashTable_Get($htable, $buffkey2, $buffval2);
  
  if ($opt{d}) { 
    my $val = HashTable::get_print_pdata($buffval2);
    printf "\tRead key = %s --> %s\n", $random_val, $val; 
  }

  if ($ret != $HashTable::HASHTABLE_SUCCESS) { 
    my $val = HashTable::get_print_pdata($buffval2);
    printf "\tError when reading key = %s --> %s\n", $random_val, $val; 
    die "Test FAILED : bad read value\n"; 
  }
}
MesureTemps::EndTime($start, $end);
printf "Reading %d elements duration : %s\n", $MAXGET, MesureTemps::ConvertiTempsChaine($end, "");

print "=========================================\n";

##########
# first del test
$ret = HashTable::HashTable_Del($htable, $buffkey, undef, undef);
printf "Del key number %d --> %d\n", $CRITERE, $ret;
if ($ret != $HashTable::HASHTABLE_SUCCESS) { die "Test FAILED : bad del\n"; }

print "-----------------------------------------\n";

# second del test
$ret = HashTable::HashTable_Del($htable, $buffkey, undef, undef);
printf "Del key number %d (second try) --> %d\n", $CRITERE, $ret;
if ($ret != $HashTable::HASHTABLE_ERROR_NO_SUCH_KEY) { die "Test FAILED : bad del\n"; }

print "-----------------------------------------\n";

# is it realy del ?
$ret = HashTable::HashTable_Get($htable, $buffkey, $buffval);
printf "Get key number %d (del) (should return HASH_ERROR_NO_SUCH_KEY) = %d --> %d\n", 
          $CRITERE, $HashTable::HASHTABLE_ERROR_NO_SUCH_KEY, $ret;
if ($ret != $HashTable::HASHTABLE_ERROR_NO_SUCH_KEY) { die "Test FAILED : bad value\n"; }

print "-----------------------------------------\n";

# random del
MesureTemps::StartTime($start);
print "Now, try to del $MAXDESTROY entries (random)\n";
my %del;
my $err_notfound = 0;
for (my $i=0; $i<$MAXDESTROY; $i++) {
  my $random_val = int rand $MAXTEST;
  my $buffkey2 = HashTable::hash_buffer_t::new();
  $del{$random_val} = 1;

  if ($opt{d}) { printf "Try to del entry number %d\n", $random_val; }
  HashTable::set_hash_buffer_t_pdata($buffkey2, length "$random_val", "$random_val");
  $ret = HashTable::HashTable_Del($htable, $buffkey2, undef, undef);

  if (defined $del{$random_val} and $ret == $HashTable::HASHTABLE_ERROR_NO_SUCH_KEY) {
    $err_notfound++;
    if ($opt{v}) { printf "\tKey already deleted = %s\n", $random_val, $ret; }
  }
  if (!defined $del{$random_val} and $ret != $HashTable::HASHTABLE_SUCCESS) { 
    printf "\tError when delling key = %s --> %s\n", $random_val, $ret;
    die "Test FAILED : bad del\n";
  }
}
MesureTemps::EndTime($start, $end);
printf "Delling %d elements duration : %s\n", $MAXDESTROY, MesureTemps::ConvertiTempsChaine($end, "");

print "-----------------------------------------\n";

# random get (possibility of getting non existing entry, because of random del)
print "Now, try to get $MAXGET entries (random - may be non existing entries)\n";
MesureTemps::StartTime($start);
for (my $i=0; $i<$MAXGET; $i++) {
  my $random_val = int rand $MAXTEST;
  my $buffkey2 = HashTable::hash_buffer_t::new();
  my $buffval2 = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey2, length "$random_val", "$random_val");
  $ret = HashTable::HashTable_Get($htable, $buffkey2, $buffval2);

  if ($opt{v} and defined $del{$random_val} and $ret == $HashTable::HASHTABLE_ERROR_NO_SUCH_KEY) {
    printf "\tTry to get a non existing entry = %s !\n", $random_val;
  }
}
MesureTemps::EndTime($start, $end);
printf "Reading %d elements duration : %s\n", $MAXGET, MesureTemps::ConvertiTempsChaine($end, "");

print "=========================================\n";

##########
# Add an existing key
HashTable::set_hash_buffer_t_pdata($buffkey, length "$CRITERE_2", "$CRITERE_2");
$ret = HashTable::HashTable_Test_And_Set($htable, $buffkey, $buffval, $HashTable::HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
printf "Return value should be HASHTABLE_SET_HOW_SET_NO_OVERWRITE = %d --> %d\n", $HashTable::HASHTABLE_ERROR_KEY_ALREADY_EXISTS, $ret;
if ($ret != $HashTable::HASHTABLE_ERROR_KEY_ALREADY_EXISTS) { die "Test FAILED : redoundant key\n"; }

print "=========================================\n";

##########
# stats
print "Printing stats\n";
my $stats = HashTable::hash_stat_t::new();
HashTable::HashTable_GetStats($htable, $stats);

printf "Number of entries : %d\n", $stats->{dynamic}->{nb_entries};

printf "\tNumber of succeeded operations : Set = %d, Get = %d, Del = %d, Test = %d\n",
       $stats->{dynamic}->{ok}->{nb_set}, $stats->{dynamic}->{ok}->{nb_get}, 
       $stats->{dynamic}->{ok}->{nb_del}, $stats->{dynamic}->{ok}->{nb_test};

printf "\tNumber of Failed operations : Set = %d, Get = %d, Del = %d, Test = %d\n",
       $stats->{dynamic}->{err}->{nb_set}, $stats->{dynamic}->{err}->{nb_get}, 
       $stats->{dynamic}->{err}->{nb_del}, $stats->{dynamic}->{err}->{nb_test};

printf "\tNumber of 'NotFound' operations : Set = %d, Get = %d, Del = %d, Test = %d\n",
       $stats->{dynamic}->{notfound}->{nb_set}, $stats->{dynamic}->{notfound}->{nb_get}, 
       $stats->{dynamic}->{notfound}->{nb_del}, $stats->{dynamic}->{notfound}->{nb_test};

printf "\tCalculated Stats : min_rbt_node = %d, max_rbt_node = %d, average_rbt_node = %d\n",
       $stats->{computed}->{min_rbt_num_node}, $stats->{computed}->{max_rbt_num_node}, $stats->{computed}->{average_rbt_num_node};

if ($stats->{dynamic}->{ok}->{nb_set} != $MAXTEST) { die "Test FAILED : bad stats ok.nb_set\n"; }
if ($stats->{dynamic}->{ok}->{nb_get} + $stats->{dynamic}->{notfound}->{nb_get} != 2*$MAXGET+3) { die "Test FAILED : bad stats *.nb_get\n"; }
if ($stats->{dynamic}->{ok}->{nb_del} + $stats->{dynamic}->{notfound}->{nb_del} != $MAXDESTROY + 2 || $stats->{dynamic}->{notfound}->{nb_del} != $err_notfound + 1) { die "Test FAILED : bad stats *.nb_del\n"; }
if ($stats->{dynamic}->{err}->{nb_test} != 1) { die "Test FAILED : bad stats err.nb_test\n"; }

if ($opt{d}) {
  print "=========================================\n";
  HashTable::BuddyDumpMem(HashTable::get_output());
}

print "================ TEST OK ================\n";
