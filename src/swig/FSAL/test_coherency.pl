#!/usr/bin/perl

use strict;
use warnings;

use lib "/cea/home/gpocre/guerin/perl/lib/perl5/site_perl/5.8.5/i386-linux-thread-multi";
use DBI;

use File::Basename;
use File::Copy;
use Getopt::Std;
use FSAL;
BEGIN {
  require FSAL_tools;
  import FSAL_tools qw(:DEFAULT fsal_str2path fsal_lookupPath fsal_str2name fsal_lookup fsal_getattrs fsal_rename fsal_unlink);
}

# Connection to database
my $datasource = "DBI:Pg:dbname=test";
my $username;
my $auth;
my $dbh = DBI->connect($datasource, $username, $auth) or die "Couldn't connect to database: " . DBI->errstr;

# get options
my $options = "vs:p:d:";
my %opt;
my $usage = sprintf "Usage: %s [-v] -d <test_directory> -s <server_config> [-p <path]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";
my $teststr = $opt{d} || die "Missing Test Directory\n";

# filehandle & path
my $roothdl = FSAL::fsal_handle_t::new();
my $curhdl = FSAL::fsal_handle_t::new();
my $rootstr = $opt{p} || "/";
my $curstr = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;
my $file1 = "toto";
my $file2 = "titi";

# Init FSAL layer
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

# get the test directory filehandle
$ret = testdir($roothdl, \$curstr, \$curhdl, $teststr);
if ($ret != 0) { die; }


#############################################
################ MAIN TEST ##################
#############################################


print "#####################################\n";

# create $file1 without FSAL layer
chdir $rootstr."/".$teststr or die "Impossible to chdir $rootstr/$teststr : $?\n";

print "touch $file1\n";
open FILE, ">$file1" or die "Impossible to create $file1 : $!\n";
close FILE;


    # lookup $file1 with FSAL layer
    my $hdl = FSAL::fsal_handle_t::new();
    my $name = FSAL::fsal_name_t::new();

    $ret = fsal_str2name($file1, \$name);
    if ($ret != 0) { die "str2name failed\n"; }
    
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    # getattr $file1
    my $fattr = FSAL::fsal_attrib_list_t::new();

    print "\tgetattr handle\n";
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 0) { die "getattr $file1 failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        my $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        my $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }
    

# create $file2 without FSAL layer
print "touch $file2\n";
open FILE, ">$file2" or die "Impossible to create $file2 : $!\n";
close FILE;

# cp $file2 $file1
print "mv $file2 $file1\n";
move $file2, $file1 or die "Cannot copy $file2 to $file1 : $!\n";


    # getattr $file1 (with the old handle !) : we should have error stale
    print "\tgetattr handle\n";
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 151) { print "\t##### test failed : getattr $file1 should return ERR_FSAL_STALE (151), returned $ret\n"; } # ERR_FSAL_STALE

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }
    

    # lookup $file1
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    
#############################################
################# BONUS 1 ###################
#############################################

    
print "#####################################\n";

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }
    

# create $file2 without FSAL layer
print "touch $file2\n";
open FILE, ">$file2" or die "Impossible to create $file2 : $!\n";
close FILE;

# cp $file2 $file1
print "mv $file2 $file1\n";
move $file2, $file1 or die "Cannot copy $file2 to $file1 : $!\n";


    # rename $file1 $file1.".1"(with the old handle !) : we should have error stale
    my $newname = FSAL::fsal_name_t::new();

    $ret = fsal_str2name($file1.".1", \$newname);
    if ($ret != 0) { die "str2name failed\n"; }
    
    print "\trename $file1 $file1.1\n";
    $ret = fsal_rename($curhdl, $name, $curhdl, $newname);
    if ($ret != 0) { die "rename failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }
    

    # lookup $file1
    print "\tlookup $file1.1 => handle\n";
    $ret = fsal_lookup($curhdl, $newname, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }

    
# create $file2 without FSAL layer
print "touch $file2\n";
open FILE, ">$file2" or die "Impossible to create $file2 : $!\n";
close FILE;

# cp $file2 $file1
print "mv $file2 $file1.1\n";
move $file2, $file1.".1" or die "Cannot copy $file2 to $file1.1 : $!\n";


    # unlink $file1.".1"
    print "\tunlink $file1.1\n";
    $ret = fsal_unlink($curhdl, $newname);
    if ($ret != 0) { die "unlink failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }

    
#############################################
################# BONUS 2 ###################
#############################################

    
print "#####################################\n";
my $i = 0;

# create $file1 without FSAL layer
LOOPGETATTR: print "touch $file1\n";
open FILE, ">$file1" or die "Impossible to create $file1 : $!\n";
close FILE;

# stat to get inode number
print "stat $file1\n";
my $inode1 = (stat($file1))[1];


    # lookup $file1 => $hdl
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }


# unlink $file1
print "unlink $file1\n";
unlink $file1 or die "Impossible to unlink $file1 : $!\n";

# mkdir $file1
print "mkdir $file1\n";
mkdir $file1 or die "Impossible to mkdir $file1 : $!\n";

# stat to get inode number
print "stat $file1\n";
my $inode2 = (stat($file1))[1];

# we must have the same inode number !
if ($inode1 != $inode2) {
  print "Failed : $inode1 != $inode2\n";
  rmdir $file1 or die "Impossible to rmdir $file1 : $!\n";
  $i++;
  goto LOOPGETATTR unless $i>=100;
  die "Impossible to create $file1 to have the same inode number\n";
}


    # getattr $file1 (with the old handle !) : we should have error stale
    print "\tgetattr handle\n";
    $ret = fsal_getattrs($hdl, \$fattr);
    if ($ret != 151) { print "\t##### test failed : getattr $file1 should return ERR_FSAL_STALE (151), returned $ret\n"; } # ERR_FSAL_STALE

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }
    

    # lookup $file1
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    # rmdir to clear test directory and FSAL
    print "\trmdir $file1\n";
    $ret = fsal_unlink($curhdl, $name);
    if ($ret != 0) { die "unlink failed\n"; }


print "#####################################\n";
$i = 0;

# create $file1 without FSAL layer
LOOPGETATTR: print "touch $file1\n";
open FILE, ">$file1" or die "Impossible to create $file1 : $!\n";
close FILE;

# stat to get inode number
print "stat $file1\n";
$inode1 = (stat($file1))[1];


    # lookup $file1 => $hdl
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }


# unlink $file1
print "unlink $file1\n";
unlink $file1 or die "Impossible to unlink $file1 : $!\n";

# mkdir $file1
print "mkdir $file1\n";
mkdir $file1 or die "Impossible to mkdir $file1 : $!\n";

# stat to get inode number
print "stat $file1\n";
$inode2 = (stat($file1))[1];

# we must have the same inode number !
if ($inode1 != $inode2) {
  print "Failed : $inode1 != $inode2\n";
  rmdir $file1 or die "Impossible to rmdir $file1 : $!\n";
  $i++;
  goto LOOPGETATTR unless $i>=100;
  die "Impossible to create $file1 to have the same inode number\n";
}


    # rename $file1 $file1.".1"(with the old handle !) : we should have error stale
    $newname = FSAL::fsal_name_t::new();

    $ret = fsal_str2name($file1.".1", \$newname);
    if ($ret != 0) { die "str2name failed\n"; }
    
    print "\trename $file1 $file1.1\n";
    $ret = fsal_rename($curhdl, $name, $curhdl, $newname);
    if ($ret != 0) { die "rename failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }
    

    # lookup $file1
    print "\tlookup $file1.1 => handle\n";
    $ret = fsal_lookup($curhdl, $newname, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    # rmdir to clear test directory and FSAL
    print "\trmdir $file1.1\n";
    $ret = fsal_unlink($curhdl, $newname);
    if ($ret != 0) { die "unlink failed\n"; }


print "#####################################\n";
$i = 0;

# create $file1 without FSAL layer
LOOPGETATTR: print "touch $file1\n";
open FILE, ">$file1" or die "Impossible to create $file1 : $!\n";
close FILE;

# stat to get inode number
print "stat $file1\n";
$inode1 = (stat($file1))[1];


    # lookup $file1 => $hdl
    print "\tlookup $file1 => handle\n";
    $ret = fsal_lookup($curhdl, $name, \$hdl);
    if ($ret != 0) { die "lookup $file1 failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (!(my @row = $sth->fetchrow_array)) {
          print "\t\t##### test failed : handle is not in database\n";
        }


# unlink $file1
print "unlink $file1\n";
unlink $file1 or die "Impossible to unlink $file1 : $!\n";

# mkdir $file1
print "mkdir $file1\n";
mkdir $file1 or die "Impossible to mkdir $file1 : $!\n";

# stat to get inode number
print "stat $file1\n";
$inode2 = (stat($file1))[1];

# we must have the same inode number !
if ($inode1 != $inode2) {
  print "Failed : $inode1 != $inode2\n";
  rmdir $file1 or die "Impossible to rmdir $file1 : $!\n";
  $i++;
  goto LOOPGETATTR unless $i>=100;
  die "Impossible to create $file1 to have the same inode number\n";
}


    # unlink $file1.".1"
    print "\tunlink $file1\n";
    $ret = fsal_unlink($curhdl, $name);
    if ($ret != 0) { die "unlink failed\n"; }

    
        # look in database if the entry exists
        print "\t\tSELECT handle in database\n";
        $query = "SELECT handle.* from handle WHERE handle.handleid='".$hdl->{id}."' AND handle.handlets='".$hdl->{ts}."';";
        $sth = $dbh->prepare ($query)
          or die "Cannot \$dbh->prepare ($query): ".$dbh->errstr;
        $sth->execute
          or die "Cannot \$sth->execute (query=$query): ".$dbh->errstr;
        if (my @row = $sth->fetchrow_array) {
          print "\t\t##### test failed : handle should not be in database (Row : @row)\n";
        }


print "#####################################\n";

$dbh->disconnect();
