package FSAL_tools;
require Exporter;

our @ISA          = qw(Exporter);
our @EXPORT       = qw( 
                      fsalinit
                      testdir
                      mtestdir
                      dirtree
                      rmdirtree
                      getcwd
                    );

our @EXPORT_OK    = qw(
                      fsal_str2name
                      fsal_name2str
                      fsal_str2path
                      fsal_path2str
                      fsal_handlecmp
                      fsal_lookup
                      fsal_lookupPath
                      fsal_create
                      fsal_mkdir
                      fsal_getattrs
                      fsal_setattrs
                      fsal_link
                      fsal_opendir
                      fsal_readdir
                      fsal_closedir
                      fsal_open
                      fsal_read
                      fsal_write
                      fsal_close
                      fsal_readlink
                      fsal_symlink
                      fsal_rename
                      fsal_unlink
                      fsal_staticfsinfo
                    );

use strict;
use warnings;

use FSAL;

our $out = FSAL::get_output();

=head1 NAME

  FSAL_tools

=head1 SYNOPSIS

  FSAL perl module

=head1 SEE ALSO

  ....

=head1 COPYRIGHT

  ...

=head1 DESCRIPTION

=head2 FSAL Strings handling

=cut

######################################################
############### FSAL Strings handling ################
######################################################

=over

=item C<fsal_str2name(char *, ref fsal_name_t *)>

=cut

sub fsal_str2name {
  my ($str, $name) = @_;
  my $st;

  $st = FSAL::FSAL_str2name($str, FSAL::get_fsal_max_name_len(), $$name);

  return $st->{major};
}

=item C<fsal_name2str(fsal_name_t *, ref char *)>

=cut

sub fsal_name2str {
  my ($name, $str) = @_;
  my $st;

  $st = FSAL::FSAL_name2str($name, $$str, FSAL::get_fsal_max_name_len());

  return $st->{major};
}

=item C<fsal_str2path(char *, ref fsal_path_t *)>

=cut

sub fsal_str2path {
  my ($str, $path) = @_;
  my $st;

  $st = FSAL::FSAL_str2path($str, FSAL::get_fsal_max_path_len(), $$path);

  return $st->{major};
}

=item C<fsal_path2str(fsal_path_t *, ref char *)>

=cut

sub fsal_path2str {
  my ($path, $str) = @_;
  my $st;

  $st = FSAL::FSAL_path2str($path, $$str, FSAL::get_fsal_max_path_len());

  return $st->{major};
}

=pod

Functions to convert strings from/to fsal types.

=back

=head2 FSAL handles management

=over

=cut

######################################################
############## FSAL handles management ###############
######################################################

=item C<fsal_handlecmp(fsal_handle_t *, fsal_handle_t *)>

Compare two fsal_handle_t. Returns 0 if the handles are the same, another value else.

=cut

sub fsal_handlecmp {
  my ($hdl1, $hdl2) = @_;
  my $st = FSAL::fsal_status_t::new();
  my $ret;

  $ret = FSAL::FSAL_handlecmp($hdl1, $hdl2, $st);

  return $ret;
}

=back

=head2 Common FSAL calls

=over

=cut

######################################################
############## Common Filesystem calls ###############
######################################################

=item C<fsal_lookup(fsal_handle_t *, fsal_name_t *, ref fsal_handle_t *)>

description...

=cut

sub fsal_lookup {
  my ($hdl, $name, $hdlret) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_lookup($hdl, $name, $context->{context}, $$hdlret, undef);

  return $st->{major};
}

=item C<fsal_lookupPath(fsal_path_t *,ref fsal_handle_t *)>

description...

=cut

sub fsal_lookupPath {
  my ($path, $hdlret) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_lookupPath($path, $context->{context}, $$hdlret, undef);

  return $st->{major};
}

=item C<fsal_lookupJunction>

=item C<fsal_test_access>

I<Not yet implemented !>

=item C<fsal_create(fsal_handle_t *, fsal_name_t *, fsal_accessmode_t *, ref fsal_handle_t *)>

description...

=cut

sub fsal_create {
  my ($hdl, $name, $mode, $hdlret) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_create($hdl, $name, $context->{context}, $mode, $$hdlret, undef);

  return $st->{major};
}

=item C<fsal_mkdir(fsal_handle_t *, fsal_name_t *, fsal_accessmode_t *, ref fsal_handle_t *)>

description...

=cut

sub fsal_mkdir {
  my ($hdl, $name, $mode, $hdlret) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_mkdir($hdl, $name, $context->{context}, $mode, $$hdlret, undef);

  return $st->{major};
}

=item C<fsal_truncate>

I<Not yet implemented !>

=item C<fsal_getattrs(fsal_handle_t *, ref fsal_attrib_list_t *)>

description...

=cut

sub fsal_getattrs {
  my ($hdl, $attr) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_getattrs($hdl, $context->{context}, $$attr);

  return $st->{major};
}

=item C<fsal_setattrs(fsal_handle_t *, fsal_attrib_list_t *)>

description...

=cut

sub fsal_setattrs {
  my ($hdl, $attr) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_setattrs($hdl, $context->{context}, $attr, undef);

  return $st->{major};
}

=item C<fsal_link(fsal_handle_t *, fsal_handle_t *, fsal_name_t *)>

description...

=cut

sub fsal_link {
  my ($hdl2link, $hdlto, $nameto) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_link($hdl2link, $hdlto, $nameto, $context->{context}, undef);

  return $st->{major};
}

=item C<fsal_opendir(fsal_handle_t *, ref fsal_dir_t *)>

description...

=cut

sub fsal_opendir {
  my ($hdl, $dir) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_opendir($hdl, $context->{context}, $$dir, undef);

  return $st->{major};
}

=item C<fsal_readdir(fsal_dir_t *dir, fsal_cookie_t startcookie, fsal_attrib_mask_t attrmask, fsal_mdsize_t buffersize, ref fsal_dirent_t *dirent, ref fsal_cookie_t *endcookie, ref int nbentries, ref int eod)>

description...

=cut

sub fsal_readdir {
  my ($dir, $startcookie, $attrmask, $buffersize, $dirent, $endcookie, $nbentries, $eod) = @_;
  my $st;

  $st = FSAL::FSAL_readdir($dir, $startcookie, $attrmask, $buffersize, $$dirent, $$endcookie, $nbentries, $eod);

  return $st->{major};
}

=item C<fsal_closedir(fsal_dir_t *)>

Close a directory.

=cut

sub fsal_closedir {
  my ($dir) = @_;
  my $st;

  $st = FSAL::FSAL_closedir($dir);

  return $st->{major};
}

=item C<fsal_open(fsal_handle_t *, type fsal_openflags_t, ref type fsal_file_t *)>

Open a file.

=cut

sub fsal_open {
  my ($hdl, $flag, $file) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_open($hdl, $context->{context}, $flag, $$file, undef);

  return $st->{major};
}

=item C<fsal_read(fsal_file_t *, int, int, ref caddr_t, ref int, ref int)>

read some datas from an opened file.

=cut

sub fsal_read {
  my ($file, $offset, $size, $buffer, $readamount, $eof) = @_;
  my $st;

  my $seek = FSAL::fsal_seek_t::new();
  $seek->{whence} = $FSAL::FSAL_SEEK_SET;
  $seek->{offset} = $offset;

  $st = FSAL::FSAL_read($file, $seek, $size, $$buffer, $readamount, $eof);

  return $st->{major};
}

=item C<fsal_write(fsal_file_t *, int, int, type caddr_t, ref int)>

Write some datas to an opened file.

=cut

sub fsal_write {
  my ($file, $offset, $size, $buffer, $writeamount) = @_;
  my $st;
  my $ret;

  my $seek = FSAL::fsal_seek_t::new();
  $seek->{whence} = $FSAL::FSAL_SEEK_SET;
  $seek->{offset} = $offset;

  $st = FSAL::FSAL_write($file, $seek, $size, $buffer, $writeamount);

  return $st->{major};
}

=item C<fsal_close(fsal_file_t *)>

Close an opened file.

=cut

sub fsal_close {
  my ($file) = @_;
  my $st;

  $st = FSAL::FSAL_close($file);

  return $st->{major};
}

=item C<fsal_readlink(fsal_handle_t *, ref fsal_path_t *)>

Read the content of a symlink

=cut

sub fsal_readlink {
  my ($hdl, $linkcontent) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_readlink($hdl, $context->{context}, $$linkcontent, undef);

  return $st->{major};
}

=item C<fsal_symlink(fsal_handle_t *, fsal_name_t *, fsal_path_t *, fsal_accessmode_t, ref fsal_handle_t *)>

Create a symlink.

=cut

sub fsal_symlink {
  my ($hdl, $linkname, $linkcontent, $mode, $hdlret) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_symlink($hdl, $linkname, $linkcontent, $context->{context}, $mode, $$hdlret, undef);

  return $st->{major};
}

=item C<fsal_rename(fsal_handle_t *, fsal_name_t *, ref fsal_handle_t *, fsal_name_t *)>

Rename a file.

=cut

sub fsal_rename {
  my ($hdlfrom, $namefrom, $hdlto, $nameto) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_rename($hdlfrom, $namefrom, $hdlto, $nameto, $context->{context}, undef, undef);
  
  return $st->{major};
}

=item C<fsal_unlink(fsal_handle_t *, fsal_name_t *)>

Remove a hardlink on the filesystem.

=cut

sub fsal_unlink {
  my ($hdl, $name) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_unlink($hdl, $name, $context->{context}, undef);

  return $st->{major};
}

=item C<fsal_staticfsinfo(fsal_handle_t *, ref fsal_staticfsinfo_t *)>

Fill the static information about the filesystem.

=cut

sub fsal_staticfsinfo {
  my ($hdl, $fsinfo) = @_;
  my $st;
  my $ret;

  my $context = FSAL::GetFSALCmdContext();
  if ($context->{is_thread_ok} != $FSAL::TRUE) {
    $ret = FSAL::Init_Thread_Context($out, $context);
    if ($ret != 0) { die; }
  }

  $st = FSAL::FSAL_static_fsinfo($hdl, $context->{context}, $$fsinfo);

  return $st->{major};
}

=back

=head2 Tools

=over

=cut

######################################################
####################### TOOLS ########################
######################################################

=item C<fsalinit(%hashmap)>

Initialize the FSAL layer.

%hashmap = (
   CONFIG_FILE   => '/path/to/file/conf',
   FLAG_VERBOSE  => '1' / '0'
)

=cut

sub fsalinit {
  my %options = @_;
  my $ret;

  $ret = FSAL::BuddyInit(undef);
  if ($ret != $FSAL::BUDDY_SUCCESS) {return $ret; }
  $ret = FSAL::fsal_init($options{CONFIG_FILE}, $options{FLAG_VERBOSE}, $out);
  return $ret;
}

=item C<rm_r(fsal_handle_t *dirhdl, fsal_handle_t *hdl, fsal_name_t *name)>

C<rm -r> a directory.

=cut

sub rm_r {
  my ($dirhdl, $hdl, $name) = @_;
  my $ret;

  # prepare needed attributes mask
  my $mask = FSAL::get_mask_attr_type();

  my $eod = 0; # end of dir
  my $from = $FSAL::FSAL_READDIR_FROM_BEGINNING;
  my $to = FSAL::fsal_cookie_t::new();
  my $dirent = FSAL::get_new_dirent_buffer();
  my $nbentries = 0;

  # for name2str
  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while (!$eod) {
    # opendir
    my $dir = FSAL::fsal_dir_t::new();
    $ret = fsal_opendir($hdl, \$dir);
    if ($ret != 0) { 
      print "opendir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # readdir
    $ret = fsal_readdir($dir, $from, $mask, FSAL::get_new_buffersize(), \$dirent, \$to, \$nbentries, \$eod);
    if ($ret != 0) { 
      print "readdir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret;
    }

    # closedir
    $ret = fsal_closedir($dir);
    if ($ret != 0) { 
      print "close failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # read readdir result
    my $entry = $dirent;
    while ($nbentries > 0 and $entry) { # foreach entry
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entry->{name}, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        FSAL::free_dirent_buffer($dirent);
        return $ret; 
      }
      if (!($entrystr =~ /^\.\.?\0*$/)) { # not '.' and not '..'
        if ($entry->{attributes}->{type} == $FSAL::FSAL_TYPE_DIR) { # if dir, loop !
          $ret = rm_r($hdl, $entry->{handle}, $entry->{name});
          if ($ret != 0) { 
            FSAL::free_dirent_buffer($dirent);
            return $ret; 
          }
        } else {
          $ret = fsal_unlink($hdl, $entry->{name});
          if ($ret != 0) { 
            FSAL::free_dirent_buffer($dirent);
            return $ret; 
          }
        }
      }
      # next entry
      $entry = $entry->{nextentry};
    }
    # to get the value pointed by $to
    $from = $to;
  }
  FSAL::free_dirent_buffer($dirent);
  $ret = fsal_unlink($dirhdl, $name);

  return $ret;
}

=item C<solvepath(char *, fsal_handle_t *, char *, ref fsal_handle_t *, ref char * )>

Give the Handle of an object known by its path.

=cut

sub solvepath {
  my ($globalstr, $curhdl, $specstr, $rethdl, $retstr) = @_;
  my $ret;

  if ($specstr =~ /^\//) {
    # absolute path
    my $path = FSAL::fsal_path_t::new();
    $ret = fsal_str2path($specstr, \$path);
    if ($ret != 0) { 
      print "str2path failed\n"; 
      return $ret;
    }
    $ret = fsal_lookupPath($path, $rethdl); # $rethdl is a reference, $$rethdl contains the result handle
    if ($ret != 0) { 
      print "lookupPath failed\n"; 
      return $ret;
    }
    $$retstr = $specstr;
    # end
    return 0;
  }

  # else
  my $lookuphdl = FSAL::fsal_handle_t::new();
  my $newstr = ".";
  my $last = 0;

  # relative path, start to $globalstr and $curhdl
  FSAL::copy_fsal_handle_t($curhdl, $lookuphdl);
  $newstr = $globalstr;

  my $lookupname = FSAL::fsal_name_t::new();

  do {
    if ($specstr =~ /^(([^\/]+)\/?)/) { # str not null ("toto/..." or "toto")
      # create new name
      $ret = fsal_str2name($2, \$lookupname);
      if ($ret != 0) {
        print "Error during str2name\n";
        return $ret;
      }
      # lookup this name
      $ret = fsal_lookup($lookuphdl, $lookupname, \$lookuphdl);
      if ($ret != 0) { # element not found : $rethdl will be the last handle found.
        $last = 1;
      } # element found : continue.
      $specstr = $';
      unless ($last) {
        if ($2 eq "..") {
          if ($newstr =~ /\/([^\/]+)$/) {
            $newstr = $`;
          }
        } elsif ($2 ne ".") {
          $newstr .= "/".$2;
        }
#        FSAL::print_friendly_fsal_handle_t($newstr, $lookuphdl);
      }
    } else { # no more element to find
      $ret = 0;
      $last = 1;
    }
  } until ($last);
  # @return the last handle found and the path corresponding
  FSAL::copy_fsal_handle_t($lookuphdl, $$rethdl);
  $$retstr = $newstr;

  return $ret;
}

=item C<testdir(fsal_handle_t *, ref char *, ref fsal_handle_t *, char *)>

description...

=cut

sub testdir {
  my ($roothdl, $curstr, $curhdl, $teststr) = @_;
  my $tmphdl = FSAL::fsal_handle_t::new();
  my $tmpstr = "";
  my $ret;

  # test if exists
  $ret = solvepath($$curstr, $$curhdl, $teststr, \$tmphdl, \$tmpstr);
  # $tmphdl is the last handle found, $tmpstr is the path corresponding
  if ($ret == 2) { # NO ENTRY
    $teststr =~ /([^\/]+)\/?$/; # to be sure that the missing directory is the only and the last
    if (($tmpstr eq ".") or ("./".$` eq $tmpstr) or ("./".$` eq $tmpstr."/")) {
      my $mode = $FSAL::CHMOD_RWX;
      my $name = FSAL::fsal_name_t::new();
      $ret = fsal_str2name($1, \$name);
      if ($ret != 0) {
        print "Error during str2name\n";
        return $ret;
      }
      $ret = fsal_mkdir($tmphdl, $name, $mode, $curhdl);
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
    $ret = fsal_handlecmp($roothdl, $tmphdl);
    if ($ret == 0) { die "Impossible to work in the root directory !\n"; }
    # $tmpstr = "toto/truc/blabla/parent/last"
    # delete old directory
    my $lookupname = FSAL::fsal_name_t::new();
    $ret = fsal_str2name("..", \$lookupname);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = fsal_lookup($tmphdl, $lookupname, $curhdl); # find the filehandle of ".." (parent) and put it into $$curhdl
    # (yet we are in the directory "parent")
    $tmpstr =~ /\/([^\/]+)$/; # name of the directory (last) to remove ?
    # str to name
    my $name = FSAL::fsal_name_t::new();
    $ret = fsal_str2name($1, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = rm_r($$curhdl, $tmphdl, $name); # rm -r "last" ($1) in the directory "parent"
    if ($ret != 0) {
      print "Error during rm_r\n";
      return $ret;
    }
    # create a new one
    my $mode = $FSAL::CHMOD_RWX;
    $ret = fsal_mkdir($$curhdl, $name, $mode, $curhdl); # $$curhdl become the handle of the directory "last"
    if ($ret != 0) {
      print "Error during creating test directory\n";
      return $ret;
    }
    $$curstr = $tmpstr; # the str to return is the teststr
  }

  return 0;
}

=item C<mtestdir(fsal_handle_t *, ref char *, ref fsal_handle_t *, char *)>

Just chdir the test_directory.

=cut

sub mtestdir {
  my ($roothdl, $curstr, $curhdl, $teststr) = @_;

  return solvepath($$curstr, $$curhdl, $teststr, $curhdl, $curstr);
}

=item C<dirtree(fsal_handle_t *, int, int, int, char *, char *, ref int, ref int)>

description...

=cut

sub dirtree {
  my ($dirfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $ffh = FSAL::fsal_handle_t::new();
  my $dfh = FSAL::fsal_handle_t::new();
  my $ret;
  my $name = FSAL::fsal_name_t::new();

  if ($levels-- == 0) { return 0; }

  my $fmode = $FSAL::CHMOD_RW;

  for (my $f=0; $f<$files; $f++) {
#    print $fname.$f."\n";
    $ret = fsal_str2name($fname.$f, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = fsal_create($dirfh, $name, $fmode, \$ffh);
    if ($ret != 0) { 
      return $ret; 
    }
    $$totfiles++; 
  }

  my $dmode = $FSAL::CHMOD_RWX;

  for (my $d=0; $d<$dirs; $d++) {
#    print $dname.$d."\n";
    $ret = fsal_str2name($dname.$d, \$name);
    if ($ret != 0) {
      print "Error during str2name\n";
      return $ret;
    }
    $ret = fsal_mkdir($dirfh, $name, $dmode, \$dfh);
    if ($ret != 0) { 
      return $ret; 
    }
    $$totdirs++;
    $ret = dirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != 0) { 
      return $ret; 
    }
  }

  return 0;
}

=item C<rmdirtree(fsal_handle_t *, int, int, int, char *, char *, ref int, ref int)>

Remove what has been created by dirtree.

=cut

sub rmdirtree {
  my ($dirfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs) = @_; 
  my $ffh = FSAL::fsal_handle_t::new();
  my $dfh = FSAL::fsal_handle_t::new();
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
    $ret = fsal_unlink($dirfh, $name);
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
    $ret = fsal_lookup($dirfh, $name, \$dfh); # existe-t-il un moyen de ne pas faire ça ?
    if ($ret != 0) { print "lookup $dname$d failed\n"; return $ret; }
    $ret = rmdirtree($dfh, $levels, $files, $dirs, $fname, $dname, $totfiles, $totdirs);
    if ($ret != 0) { return $ret; }
    $$totdirs++;
    $ret = fsal_unlink($dirfh, $name);
    if ($ret != 0) { print "rmdir $dname$d failed\n"; return $ret; }
  }

  return 0;
}

=item C<getcwd(fsal_handle_t *, char *, ref char *)>

Get the path of the current directory.

=cut

sub getcwd {
  my ($hdl, $path, $pathret) = @_;
  my $ret;

  # Lookup '..'
  my $name = FSAL::fsal_name_t::new();
  $ret = fsal_str2name("..", \$name);
  if ($ret != 0) {
    print "Error during str2name\n";
    return $ret;
  }
  my $parenthdl = FSAL::fsal_handle_t::new();
  $ret = fsal_lookup($hdl, $name, \$parenthdl);
  if ($ret != 0) {
    print "Error during lookup\n";
    return $ret;
  }
  # test if parenthdl == hdl
  $ret = fsal_handlecmp($hdl, $parenthdl);
  if ($ret == 0) {
    $$pathret = $path;
    return 0;
  }

  # prepare needed attributes mask
  my $mask = FSAL::get_mask_attr_type();

  my $last = 0;
  my $eod = 0; # end of dir
  my $from = $FSAL::FSAL_READDIR_FROM_BEGINNING;
  my $to = FSAL::fsal_cookie_t::new();
  my $dirent = FSAL::get_new_dirent_buffer();
  my $nbentries = 0;

  my @tabnamestr;
  for (my $i=0; $i<FSAL::get_fsal_max_name_len(); $i++) {
    $tabnamestr[$i] = " ";
  }

  while (!$last and !$eod) {
    # opendir
    my $dir = FSAL::fsal_dir_t::new();
    $ret = fsal_opendir($parenthdl, \$dir);
    if ($ret != 0) { 
      print "opendir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # readdir
    $ret = fsal_readdir($dir, $from, $mask, FSAL::get_new_buffersize(), \$dirent, \$to, \$nbentries, \$eod);
    if ($ret != 0) { 
      print "readdir failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret;
    }

    # closedir
    $ret = fsal_closedir($dir);
    if ($ret != 0) { 
      print "close failed\n"; 
      FSAL::free_dirent_buffer($dirent);
      return $ret; 
    }

    # read readdir result
    my $entry = $dirent;
    while ($nbentries > 0 and $entry) { # foreach entry
      my $entrystr = join("", @tabnamestr);
      $ret = fsal_name2str($entry->{name}, \$entrystr);
      if ($ret != 0) { 
        print "name2str failed\n"; 
        FSAL::free_dirent_buffer($dirent);
        return $ret; 
      }
      # who is $hdl ?
      $ret = fsal_handlecmp($hdl, $entry->{handle});
      if ($ret == 0) {
        $path = "/".$entrystr.$path;
        $last = 1; # do not readdirplus the end
        last; # do not check other entries
      }
      # next entry
      $entry = $entry->{nextentry};
    }
    # to get the value pointed by $to
    $from = $to;
  }
  FSAL::free_dirent_buffer($dirent);
  # loop
  $ret = getcwd($parenthdl, $path, $pathret);

  return $ret;
}

=back

=cut

1;
