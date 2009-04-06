package HashTable_tools;
require Exporter;

our @ISA          = qw(Exporter);
our @EXPORT       = qw( do_get do_set do_new do_del do_test);

use strict;
use warnings;

use HashTable;

sub do_get {
  my ($htable, $key, $val, $err) = @_;
  my $ret;
  my $buffkey = HashTable::hash_buffer_t::new();
  my $buffval = HashTable::hash_buffer_t::new();
  my $valret;

  HashTable::set_hash_buffer_t_pdata($buffkey, length $key, $key);
  $ret = HashTable::HashTable_Get($htable, $buffkey, $buffval);

  $valret = HashTable::get_print_pdata($buffval);
  if (($valret ne "(null)") and ($val ne $valret)) { 
    $$err = "Get FAILED : value is $valret != $val (expected)";
    return -1; 
  }

  return $ret;
}

sub do_set {
  my ($htable, $key, $val, $err) = @_;
  my $ret;
  my $buffkey = HashTable::hash_buffer_t::new();
  my $buffval = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey, length $key, $key);
  HashTable::set_hash_buffer_t_pdata($buffval, length $val, $val);
  return HashTable::HashTable_Test_And_Set($htable, $buffkey, $buffval, $HashTable::HASHTABLE_SET_HOW_SET_OVERWRITE);
}

sub do_new {
  my ($htable, $key, $val, $err) = @_;
  my $ret;
  my $buffkey = HashTable::hash_buffer_t::new();
  my $buffval = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey, length $key, $key);
  HashTable::set_hash_buffer_t_pdata($buffval, length $val, $val);
  return HashTable::HashTable_Test_And_Set($htable, $buffkey, $buffval, $HashTable::HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
}

sub do_del {
  my ($htable, $key, $err) = @_;
  my $ret;
  my $buffkey = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey, length $key, $key);
  return HashTable::HashTable_Del($htable, $buffkey, undef, undef);
}

sub do_test {
  my ($htable, $key, $err) = @_;
  my $ret;
  my $buffkey = HashTable::hash_buffer_t::new();
  my $buffval = HashTable::hash_buffer_t::new();

  HashTable::set_hash_buffer_t_pdata($buffkey, length $key, $key);
  return HashTable::HashTable_Test_And_Set($htable, $buffkey, $buffval, $HashTable::HASHTABLE_SET_HOW_TEST_ONLY);
}

1;
