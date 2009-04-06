package Cache_inode_tools;
require Exporter;

our @ISA          = qw(Exporter);
our @EXPORT       = qw(
                      $CACHE_INODE_READDIR_SIZE
                      cacheinodeinit
                      testdir
                      mtestdir
                      dirtree
                      rmdirtree
                      getcwd
                    );

our @EXPORT_OK    = qw(
                      cacheinode_get
                      cacheinode_create
                      cacheinode_lookup
                      cacheinode_readlink
                      cacheinode_getattr
                      cacheinode_link
                      cacheinode_remove
                      cacheinode_rename
                      cacheinode_setattr
                      cacheinode_write
                      cacheinode_read
                      cacheinode_readdir
                      cacheinode_statfs
                    );

use strict;
use warnings;

use Cache_inode;
use FSAL_tools qw(fsal_str2name fsal_name2str);

our $out = Cache_inode::get_output();
our $EXPORT_ID = 1;

our $CACHE_INODE_READDIR_SIZE = 10;

######################################################
############### Cache_inode functions ################
######################################################

##### cacheinode_get #####
# cacheinode_get(
#   type fsal_handle_t *,
#   ref type cache_entry_t *,
#   ref type fsal_attrib_list_t *
# )
sub cacheinode_get {
  my ($hdl, $entry) = @_;
  my $attr = FSAL::fsal_attrib_list_t::new();
  my $fsdata = Cache_inode::cache_inode_fsal_data_t::new();

  $fsdata->{handle} = $hdl;

  my $pthr = Cache_inode::RetrieveInitializedContext();
  $$entry = Cache_inode::cache_inode_get(
      $fsdata, 
      $attr, 
      $EXPORT_ID, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $pthr->{cache_status};
}

##### cacheinode_create #####
# cacheinode_create(
#   type cache_entry_t *,
#   type fsal_name_t *,
#   int,
#   int,
#   type fsal_path_t *,
#   ref type cache_entry_t *
# )
sub cacheinode_create {
  my ($direntry, $name, $type, $mode, $content, $retentry) = @_;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  $$retentry = Cache_inode::cache_inode_create(
      $direntry, 
      $name,
      $type,
      $mode,
      $content,
      $attr,
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $pthr->{cache_status};
}

##### cacheinode_lookup #####
# cacheinode_lookup(
#   type cache_entry_t *,
#   type fsal_name_t *,
#   ref type cache_entry_t *
# )
sub cacheinode_lookup {
  my ($direntry, $name, $retentry) = @_;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  $$retentry = Cache_inode::cache_inode_lookup(
      $direntry, 
      $name, 
      $attr, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $pthr->{cache_status};
}

##### cacheinode_readlink #####
# cacheinode_readlink(
#   type cache_entry_t *,
#   ref type fsal_path_t *
# )
sub cacheinode_readlink {
  my ($entry, $content) = @_;
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_readlink(
      $entry, 
      $$content, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_getattr #####
# cacheinode_getattr(
#   type cache_entry_t *,
#   ref type fsal_attrib_list_t *
# )
sub cacheinode_getattr {
  my ($entry, $attr) = @_;
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_getattr(
      $entry, 
      $$attr, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_link #####
# cacheinode_link(
#   type cache_entry_t *,
#   type cache_entry_t *,
#   type fsal_name_t *
# )
sub cacheinode_link {
  my ($entry2link, $entrydirto, $name) = @_;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_link(
      $entry2link, 
      $entrydirto,
      $name,
      $attr, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_setattr #####
# cacheinode_setattr(
#   type cache_entry_t *,
#   type fsal_attrib_list_t *
# )
sub cacheinode_setattr {
  my ($entry, $attr) = @_;
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_setattr(
      $entry, 
      $attr, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_rename #####
# cacheinode_rename(
#   type cache_entry_t *,
#   type fsal_name_t *,
#   type cache_entry_t *,
#   type fsal_name_t *
# )
sub cacheinode_rename {
  my ($entryfrom, $namefrom, $entryto, $nameto) = @_;
  my $attrfrom = FSAL::fsal_attrib_list_t::new();
  my $attrto = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_rename(
      $entryfrom, 
      $namefrom,
      $entryto,
      $nameto,
      $attrfrom,
      $attrto,
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_remove #####
# cacheinode_remove(
#   type cache_entry_t *,
#   type fsal_name_t *
# )
sub cacheinode_remove {
  my ($direntry, $name) = @_;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_remove(
      $direntry, 
      $name, 
      $attr, 
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_write #####
# cacheinode_write(
#   type cache_entry_t *,
#   int,
#   int,
#   caddr_t,
#   ref int
# )
sub cacheinode_write {
  my ($entry, $offset, $size, $buffer, $writeamount) = @_;
  my $seek = FSAL::fsal_seek_t::new();
  $seek->{whence} = $FSAL::FSAL_SEEK_SET;
  $seek->{offset} = $offset;
  my $eof = 0;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_rdwr(
      $entry, 
      $Cache_inode::CACHE_INODE_WRITE,
      $seek,
      $size,
      $writeamount,
      $attr,
      $buffer,
      \$eof,
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_read #####
# cacheinode_read(
#   type cache_entry_t *,
#   int,
#   int,
#   caddr_t,
#   ref int,
#   ref int
# )
sub cacheinode_read {
  my ($entry, $offset, $size, $buffer, $readamount, $eof) = @_;
  my $seek = FSAL::fsal_seek_t::new();
  $seek->{whence} = $FSAL::FSAL_SEEK_SET;
  $seek->{offset} = $offset;
  my $attr = FSAL::fsal_attrib_list_t::new();
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_rdwr(
      $entry, 
      $Cache_inode::CACHE_INODE_READ,
      $seek,
      $size,
      $readamount,
      $attr,
      $$buffer,
      $eof,
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

##### cacheinode_readdir #####
# cacheinode_readdir(
#   type cache_entry_t *,
#   int,
#   int,
#   ref int,
#   ref int,
#   ref int,
#   ref type cache_inode_dir_entry_t *,
#   ref type unsigned int *
# )
sub cacheinode_readdir {
  my ($entry, $begincookie, $nbwanted, $nbfound, $endcookie, $eod, $direntarray, $cookiearray) = @_;

  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_readdir(
      $entry,
      $begincookie,
      $nbwanted,
      $nbfound,
      $endcookie,
      $eod,
      $$direntarray,
      $$cookiearray,
      $Cache_inode::ht, 
      Cache_inode::get_Client($pthr), 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );

  return $ret;
}

##### cacheinode_statfs #####
# cacheinode_statfs(
#   type cache_entry_t *,
#   ref type fsal_staticfsinfo_t *,
#   ref type fsal_dynamicfsinfo_t *
# )
sub cacheinode_statfs {
  my ($entry, $staticinfo, $dynamicinfo) = @_;
  
  my $pthr = Cache_inode::RetrieveInitializedContext();
  my $ret = Cache_inode::cache_inode_statfs(
      $entry, 
      $$staticinfo, 
      $$dynamicinfo, 
      Cache_inode::get_Context($pthr),
      Cache_inode::get_CacheStatus($pthr)
  );
  return $ret;
}

######################################################
####################### TOOLS ########################
######################################################

##### cacheinodeinit #####
# cacheinodeinit(
#   CONFIG_FILE   => '/path/to/file/conf',
#   FLAG_VERBOSE  => '1' / '0'
# )
sub cacheinodeinit {
  my %options = @_;
  my $ret;

  $ret = Cache_inode::BuddyInit(undef);
  if ($ret != $Cache_inode::BUDDY_SUCCESS) {return $ret; }
  $ret = Cache_inode::fsal_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  if ($ret != 0) { return $ret; }
  $ret = Cache_inode::cacheinode_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  return $ret;
}

##### rm_r #####
# rm_r(
#   type cache_entry_t *,
#   type cache_entry_t *,
#   type fsal_name_t *
# )
sub rm_r {
  my ($direntry, $entry, $name) = @_;
  my $ret;
  my $begincookie = 0;
  my $nbfound = 0;
  my $endcookie = 0;
  my $eod = $Cache_inode::UNASSIGNED_EOD;
  my $entryarray = Cache_inode::get_new_dir_entry_array($CACHE_INODE_READDIR_SIZE);
  my $cookiearray = Cache_inode::get_new_cookie_array($CACHE_INODE_READDIR_SIZE);

  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while ($eod != $Cache_inode::END_OF_DIR) { # while not end of dir
    # readdir
    $ret = cacheinode_readdir(
        $entry,
        $begincookie,
        $CACHE_INODE_READDIR_SIZE,
        \$nbfound,
        \$endcookie,
        \$eod,
        \$entryarray,
        \$cookiearray
    );
    if ($ret != 0) {
      print "readdir failed\n";
      Cache_inode::free_dir_entry_array($entryarray);
      Cache_inode::free_cookie_array($cookiearray);
      return $ret; 
    }

    # foreach entry found
    for (my $i=0; $i<$nbfound; $i++) {
      # get entry name
      my $entryname = Cache_inode::get_name_from_dir_entry_array($entryarray, $i);
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entryname, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        Cache_inode::free_dir_entry_array($entryarray);
        Cache_inode::free_cookie_array($cookiearray);
        return $ret; 
      }
      if (!($entrystr =~ /^\.\.?\0*$/)) { # not '.' and not '..'
        # get sub entry
        my $subentry = Cache_inode::get_entry_from_dir_entry_array($entryarray, $i);
        # dir or not ?
        my $attr = FSAL::fsal_attrib_list_t::new();
        $ret = cacheinode_getattr($subentry, \$attr);
        if ($attr->{type} == $FSAL::FSAL_TYPE_DIR) { # directory
          # loop
          $ret = rm_r($entry, $subentry, $entryname);
        } else { # other
          # unlink
          $ret = cacheinode_remove($entry, $entryname);
          if ($ret != 0) { 
            print "remove failed\n";
            Cache_inode::free_dir_entry_array($entryarray);
            Cache_inode::free_cookie_array($cookiearray);
            return $ret; 
          }
        }
      }
    }
    $begincookie = $endcookie;
  }
  Cache_inode::free_dir_entry_array($entryarray);
  Cache_inode::free_cookie_array($cookiearray);

  # unlink $self (dir)
  $ret = cacheinode_remove($direntry, $name);
  
  return $ret;
}

##### solvepath #####
# solvepath(
#   type char *,
#   type cache_entry_t *,
#   type char *,
#   ref type cache_entry_t *,
#   ref type char *
# )
sub solvepath {
  my ($rootentry, $globalstr, $curentry, $specstr, $retentry, $retstr) = @_;
  my $lookupentry = Cache_inode::get_new_entry();
  my $tmpentry = Cache_inode::get_new_entry();
  my $newstr = ".";
  my $last = 0;
  my $ret;
  
  if ($specstr =~ /^\//) {
    # absolute path
    $specstr = $';
    Cache_inode::copy_entry($rootentry, $lookupentry);
    if ($specstr =~ /^\/$/) { # end, the path to solve is "/"
      $$retstr .= "/";
      Cache_inode::copy_entry($rootentry, $$retentry);
      return 0;
    }
  } else {
    # relative path, start to $globalstr and $curentry
    Cache_inode::copy_entry($curentry, $lookupentry);
    $newstr = $globalstr;
  }

  my $lookupname = FSAL::fsal_name_t::new();
  
  do {
    if ($specstr =~ /^(([^\/]+)\/?)/) { # str not null ("toto/..." or "toto")
      # create new name
      $ret = FSAL_tools::fsal_str2name($2, \$lookupname);
      if ($ret != 0) {
        print "Error during str2name\n";
        return $ret;
      }
      # lookup this name
      $ret = cacheinode_lookup($lookupentry, $lookupname, \$tmpentry);
      if ($ret != 0) { # element not found : $retentry will be the last handle found.
        $last = 1;
      } else { # element found : continue.
        $lookupentry = $tmpentry;
      }
      $specstr = $';
      unless ($last) {
        if ($2 eq "..") {
          if ($newstr =~ /\/([^\/]+)$/) {
            $newstr = $`;
          }
        } elsif ($2 ne ".") {
          $newstr .= "/".$2;
        }
      }
    } else { # no more element to find
      $ret = 0;
      $last = 1;
    }
  } until ($last);
  # @return the last handle found and the path corresponding
  Cache_inode::copy_entry($lookupentry, $$retentry);
  $$retstr = $newstr;
  
  return $ret;
}

##### testdir #####
# testdir(
#   type cache_entry_t *,
#   ref type char *,
#   ref type cache_entry_t *,
#   type char *,
# )
sub testdir {
  my ($rootentry, $curstr, $curentry, $teststr) = @_;
  my $tmpentry = Cache_inode::get_new_entry();
  my $tmpstr = "";
  my $ret;

  # test if exists
  $ret = solvepath($rootentry, $$curstr, $$curentry, $teststr, \$tmpentry, \$tmpstr);
  
  # $tmpentry is the last handle found, $tmpstr is the path corresponding
  if ($ret == $Cache_inode::CACHE_INODE_NOT_FOUND) {
    $teststr =~ /([^\/]+)\/?$/; # to be sure that the missing directory is the only and the last
    if (($tmpstr eq ".") or ("./".$` eq $tmpstr) or ("./".$` eq $tmpstr."/")) {
      my $mode = $FSAL::CHMOD_RWX;
      my $name = FSAL::fsal_name_t::new();
      $ret = FSAL_tools::fsal_str2name($1, \$name);
      if ($ret != 0) {
        print "Error during str2name\n";
        return $ret;
      }
      $ret = cacheinode_create($tmpentry, $name, $Cache_inode::DIR_BEGINNING, $mode, undef, $curentry);
      if ($ret != 0) {
        print "Error during creating test directory\n";
        return $ret;
      }
      $$curstr = $tmpstr."/".$1;
    } else {
      print "Enable to create test directory : previous directory does not exist !\n";
      return -1;
    }
  } elsif ($ret != 0) { #something is wrong
    return $ret;
  } else { # ok
    $ret = FSAL_tools::fsal_handlecmp($rootentry->{object}->{dir_begin}->{handle}, $tmpentry->{object}->{dir_begin}->{handle});
    if ($ret == 0) { die "Impossible to work in the root directory !\n"; }
    # $tmpstr = "toto/truc/blabla/parent/last"
    # delete old directory
    my $lookupname = FSAL::fsal_name_t::new();
    $ret = FSAL_tools::fsal_str2name("..", \$lookupname);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = cacheinode_lookup($tmpentry, $lookupname, $curentry); # find the filehandle of ".." (parent) and put it into $$curentry
    # (yet we are in the directory "parent")
    $tmpstr =~ /\/([^\/]+)$/; # name of the directory (last) to remove ?
    # str to name
    my $name = FSAL::fsal_name_t::new();
    $ret = FSAL_tools::fsal_str2name($1, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = rm_r($$curentry, $tmpentry, $name); # rm -r "last" ($1) in the directory "parent"
    if ($ret != 0) {
      print "Error during rm_r\n";
      return $ret;
    }
    # create a new one
    my $mode = $FSAL::CHMOD_RWX;
    $ret = cacheinode_create($$curentry, $name, $Cache_inode::DIR_BEGINNING, $mode, undef, $curentry);
    if ($ret != 0) {
      print "Error during creating test directory\n";
      return $ret;
    }
    $$curstr = $tmpstr; # the str to return is the teststr
  }
  
  return 0;
}

##### mtestdir ##### (Just chdir the test_directory)
# mtestdir(
#   type cache_entry_t *,
#   ref type char *,
#   ref type cache_entry_t *,
#   type char *,
# )
sub mtestdir {
  my ($rootentry, $curstr, $curentry, $teststr) = @_;

  return solvepath($rootentry, $$curstr, $$curentry, $teststr, $curentry, $curstr);
}

##### dirtree #####
# dirtree(
#   type cache_entry_t *,
#   int,
#   int,
#   int,
#   char *,
#   char *,
#   ref int,
#   ref int
# )
sub dirtree {
  my ($direntry, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $fentry = Cache_inode::get_new_entry();
  my $dentry = Cache_inode::get_new_entry();
  my $ret;
  my $name = FSAL::fsal_name_t::new();

  if ($levels-- == 0) { return 0; }
  
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = fsal_str2name($fname.$f, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = cacheinode_create($direntry, $name, $Cache_inode::REGULAR_FILE, $FSAL::CHMOD_RW, undef, \$fentry);
    if ($ret != 0) { 
      return $ret; 
    }
    $$totfiles++; 
  }
  
  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = fsal_str2name($dname.$d, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = cacheinode_create($direntry, $name, $Cache_inode::DIR_BEGINNING, $FSAL::CHMOD_RWX, undef, \$dentry);
    if ($ret != 0) { 
      return $ret; 
    }
    $$totdirs++;
    $ret = dirtree($dentry, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != 0) { 
      return $ret; 
    }
  }

  return 0;
}

##### rmdirtree #####
# rmdirtree(
#   type cache_entry_t *,
#   int,
#   int,
#   int,
#   char *,
#   char *,
#   ref int,
#   ref int
# )
sub rmdirtree {
  my ($direntry, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $fentry = Cache_inode::get_new_entry();
  my $dentry = Cache_inode::get_new_entry();
  my $ret;
  my $name = FSAL::fsal_name_t::new();

  if ($levels-- == 0) { return 0; }
  
  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = fsal_str2name($fname.$f, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = cacheinode_remove($direntry, $name);
    if ($ret != 0) { print "remove $fname$f failed\n"; return $ret; }
    $$totfiles++; 
  }
  
  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = fsal_str2name($dname.$d, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = cacheinode_lookup($direntry, $name, \$dentry);
    if ($ret != 0) { print "lookup $dname$d failed\n"; return $ret; }
    $ret = rmdirtree($dentry, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != 0) { return $ret; }
    $$totdirs++;
    $ret = cacheinode_remove($direntry, $name);
    if ($ret != 0) { print "rmdir $dname$d failed\n"; return $ret; }
  }

  return 0;
}

##### getcwd #####
# getcwd(
#   type cache_entry_t *,
#   type char *,
#   ref type char *
# )
sub getcwd {
  my ($entry, $path, $pathret) = @_;
  my $ret;

  # Lookup '..'
  my $name = FSAL::fsal_name_t::new();
  $ret = fsal_str2name("..", \$name);
  if ($ret != 0) {
    print "Error during str2name\n";
    return $ret;
  }
  my $parententry = Cache_inode::get_new_entry();
  $ret = cacheinode_lookup($entry, $name, \$parententry);
  if ($ret != 0) {
    print "Error during lookup\n";
    return $ret;
  }
  # test if parententry == entry
  $ret = FSAL_tools::fsal_handlecmp($entry->{object}->{dir_begin}->{handle}, $parententry->{object}->{dir_begin}->{handle});
  if ($ret == 0) {
    $$pathret = $path;
    return 0;
  }

  my $last = 0;
  my $begincookie = 0;
  my $nbfound = 0;
  my $endcookie = 0;
  my $eod = $Cache_inode::UNASSIGNED_EOD;
  my $entryarray = Cache_inode::get_new_dir_entry_array($CACHE_INODE_READDIR_SIZE);
  my $cookiearray = Cache_inode::get_new_cookie_array($CACHE_INODE_READDIR_SIZE);

  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while (!$last and $eod != $Cache_inode::END_OF_DIR) { # while not end of dir
    # readdir
    $ret = cacheinode_readdir(
        $parententry,
        $begincookie,
        $CACHE_INODE_READDIR_SIZE,
        \$nbfound,
        \$endcookie,
        \$eod,
        \$entryarray,
        \$cookiearray
    );
    if ($ret != 0) {
      print "readdir failed\n";
      Cache_inode::free_dir_entry_array($entryarray);
      Cache_inode::free_cookie_array($cookiearray);
      return $ret; 
    }

    # foreach entry found
    for (my $i=0; $i<$nbfound; $i++) {
      # get entry name
      my $entryname = Cache_inode::get_name_from_dir_entry_array($entryarray, $i);
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entryname, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        Cache_inode::free_dir_entry_array($entryarray);
        Cache_inode::free_cookie_array($cookiearray);
        return $ret; 
      }
      # get sub entry
      my $subentry = Cache_inode::get_entry_from_dir_entry_array($entryarray, $i);
      # who is $entry ?
      $ret = FSAL_tools::fsal_handlecmp($entry->{object}->{dir_begin}->{handle}, $subentry->{object}->{dir_begin}->{handle});
      if ($ret == 0) {
        $path = "/".$entrystr.$path;
        $last = 1; # do not readdirplus the end
        last; # do not check other entries
      }
    }
    $begincookie = $endcookie;
  }
  Cache_inode::free_dir_entry_array($entryarray);
  Cache_inode::free_cookie_array($cookiearray);

  $ret = getcwd($parententry, $path, $pathret);
  
  return $ret;
}

1;
