#
# Config::General.pm - Generic Config Module
#
# Purpose: Provide a convenient way for loading
#          config values from a given file and
#          return it as hash structure
#
# Copyright (c) 2000-2007 Thomas Linden <tlinden |AT| cpan.org>.
# All Rights Reserved. Std. disclaimer applies.
# Artificial License, same as perl itself. Have fun.
#
# namespace
package Config::General;

use strict;
use warnings;
use English '-no_match_vars';

use IO::File;
use FileHandle;
use File::Spec::Functions qw(splitpath file_name_is_absolute catfile catpath);
use File::Glob qw/:glob/;


# on debian with perl > 5.8.4 croak() doesn't work anymore without this.
# There is some require statement which dies 'cause it can't find Carp::Heavy,
# I really don't understand, what the hell they made, but the debian perl
# installation is definetly bullshit, damn!
use Carp::Heavy;


use Carp;
use Exporter;

$Config::General::VERSION = 2.33;

use vars  qw(@ISA @EXPORT_OK);
use base qw(Exporter);
@EXPORT_OK = qw(ParseConfig SaveConfig SaveConfigString);

sub new {
  #
  # create new Config::General object
  #
  my($this, @param ) = @_;
  my $class = ref($this) || $this;

  # define default options
  my $self = {
	      SlashIsDirectory      => 0,
	      AllowMultiOptions     => 1,
	      MergeDuplicateOptions => 0,
	      MergeDuplicateBlocks  => 0,
	      LowerCaseNames        => 0,
	      ApacheCompatible      => 0,
	      UseApacheInclude      => 0,
	      IncludeRelative       => 0,
	      IncludeDirectories    => 0,
	      IncludeGlob           => 0,
	      AutoLaunder           => 0,
	      AutoTrue              => 0,
	      AutoTrueFlags         => {
					true  => '^(on|yes|true|1)$',
					false => '^(off|no|false|0)$',
				       },
	      DefaultConfig         => {},
	      level                 => 1,
	      InterPolateVars       => 0,
	      InterPolateEnv        => 0,
	      ExtendedAccess        => 0,
	      SplitPolicy           => 'guess', # also possible: whitespace, equalsign and custom
	      SplitDelimiter        => 0,       # must be set by the user if SplitPolicy is 'custom'
	      StoreDelimiter        => 0,       # will be set by me unless user uses 'custom' policy
	      CComments             => 1,       # by default turned on
	      BackslashEscape       => 0,       # by default turned off, allows escaping anything using  the backslash
	      StrictObjects         => 1,       # be strict on non-existent keys in OOP mode
	      StrictVars            => 1,       # be strict on undefined variables in Interpolate mode
	      Tie                   => q(),      # could be set to a perl module for tie'ing new hashes
	      parsed                => 0,       # internal state stuff for variable interpolation
	      upperkey              => q(),
	      upperkeys             => [],
	      lastkey               => q(),
	      prevkey               => q( ),
	      files                 => {},      # which files we have read, if any
	     };

  # create the class instance
  bless $self, $class;

  if ($#param >= 1) {
    # use of the new hash interface!
    $self->_prepare(@param);
  }
  elsif ($#param == 0) {
    # use of the old style
    $self->{ConfigFile} = $param[0];
    if (ref($self->{ConfigFile}) eq 'HASH') {
      $self->{ConfigHash} = delete $self->{ConfigFile};
    }
  }
  else {
    # this happens if $#param == -1,1 thus no param was given to new!
    $self->{config} = $self->_hashref();
    $self->{parsed} = 1;
  }

  # find split policy to use for option/value separation
  $self->_splitpolicy();

  # bless into variable interpolation module if neccessary
  $self->_blessvars();

  # process as usual
  if (!$self->{parsed}) {
    $self->_process();
  }

  # bless into OOP namespace if required
  $self->_blessoop();

  return $self;
}



sub _process {
  #
  # call _read() and _parse() on the given config
  my($self) = @_;

  if ($self->{DefaultConfig} && $self->{InterPolateVars}) {
    $self->{DefaultConfig} = $self->_interpolate_hash($self->{DefaultConfig}); # FIXME: _hashref() ?
  }
  if (exists $self->{StringContent}) {
    # consider the supplied string as config file
    $self->_read($self->{StringContent}, 'SCALAR');
    $self->{config} = $self->_parse($self->{DefaultConfig}, $self->{content});
  }
  elsif (exists $self->{ConfigHash}) {
    if (ref($self->{ConfigHash}) eq 'HASH') {
      # initialize with given hash
      $self->{config} = $self->{ConfigHash};
      $self->{parsed} = 1;
    }
    else {
      croak "Parameter -ConfigHash must be a hash reference!\n";
    }
  }
  elsif (ref($self->{ConfigFile}) eq 'GLOB' || ref($self->{ConfigFile}) eq 'FileHandle') {
    # use the file the glob points to
    $self->_read($self->{ConfigFile});
    $self->{config} = $self->_parse($self->{DefaultConfig}, $self->{content});
  }
  else {
    if ($self->{ConfigFile}) {
      # open the file and read the contents in
      $self->{configfile} = $self->{ConfigFile};
      if ( file_name_is_absolute($self->{ConfigFile}) ) {
	# look if is is an absolute path and save the basename if it is absolute
	my ($volume, $path, undef) = splitpath($self->{ConfigFile});
	$path =~ s#/$##; # remove eventually existing trailing slash
	if (! $self->{ConfigPath}) {
	  $self->{ConfigPath} = [];
	}
	unshift @{$self->{ConfigPath}}, catpath($volume, $path, q());
      }
      $self->_open($self->{configfile});
      # now, we parse immdediately, getall simply returns the whole hash
      $self->{config} = $self->_hashref();
      $self->{config} = $self->_parse($self->{DefaultConfig}, $self->{content});
    }
    else {
      # hm, no valid config file given, so try it as an empty object
      $self->{config} = $self->_hashref();
      $self->{parsed} = 1;
    }
  }
}


sub _blessoop {
  #
  # bless into ::Extended if neccessary
  my($self) = @_;
  if ($self->{ExtendedAccess}) {
    # we are blessing here again, to get into the ::Extended namespace
    # for inheriting the methods available overthere, which we doesn't have.
    bless $self, 'Config::General::Extended';
    eval {
      require Config::General::Extended;
    };
    if ($EVAL_ERROR) {
      croak $EVAL_ERROR;
    }
  }
#  return $self;
}

sub _blessvars {
  #
  # bless into ::Interpolated if neccessary
  my($self) = @_;
  if ($self->{InterPolateVars} || $self->{InterPolateEnv}) {
    # InterPolateEnv implies InterPolateVars
    $self->{InterPolateVars} = 1;

    # we are blessing here again, to get into the ::InterPolated namespace
    # for inheriting the methods available overthere, which we doesn't have here.
    bless $self, 'Config::General::Interpolated';
    eval {
      require Config::General::Interpolated;
    };
    if ($EVAL_ERROR) {
      croak $EVAL_ERROR;
    }
    # pre-compile the variable regexp
    $self->{regex} = $self->_set_regex();
  }
#  return $self;
}


sub _splitpolicy {
  #
  # find out what split policy to use
  my($self) = @_;
  if ($self->{SplitPolicy} ne 'guess') {
    if ($self->{SplitPolicy} eq 'whitespace') {
      $self->{SplitDelimiter} = '\s+';
      if (!$self->{StoreDelimiter}) {
	$self->{StoreDelimiter} = q(   );
      }
    }
    elsif ($self->{SplitPolicy} eq 'equalsign') {
      $self->{SplitDelimiter} = '\s*=\s*';
      if (!$self->{StoreDelimiter}) {
	$self->{StoreDelimiter} = ' = ';
      }
    }
    elsif ($self->{SplitPolicy} eq 'custom') {
      if (! $self->{SplitDelimiter} ) {
	croak "SplitPolicy set to 'custom' but no SplitDelimiter set.\n";
      }
    }
    else {
      croak "Unsupported SplitPolicy: $self->{SplitPolicy}.\n";
    }
  }
  else {
    if (!$self->{StoreDelimiter}) {
      $self->{StoreDelimiter} = q(   );
    }
  }
}

sub _prepare {
  #
  # prepare the class parameters, mangle them, if there
  # are options to reset or to override, do it here.
  my ($self, %conf) = @_;

  # save the parameter list for ::Extended's new() calls
  $self->{Params} = \%conf;

  # be backwards compatible
  if (exists $conf{-file}) {
    $self->{ConfigFile} = delete $conf{-file};
  }
  if (exists $conf{-hash}) {
    $self->{ConfigHash} = delete $conf{-hash};
  }

  # store input, file, handle, or array
  if (exists $conf{-ConfigFile}) {
    $self->{ConfigFile} = delete $conf{-ConfigFile};
  }
  if (exists $conf{-ConfigHash}) {
    $self->{ConfigHash} = delete $conf{-ConfigHash};
  }

  # store search path for relative configs, if any
  if (exists $conf{-ConfigPath}) {
    my $configpath = delete $conf{-ConfigPath};
    $self->{ConfigPath} = ref $configpath eq 'ARRAY' ? $configpath : [$configpath];
  }

  # handle options which contains values we need (strings, hashrefs or the like)
  if (exists $conf{-String} ) {
    if (ref(\$conf{-String}) eq 'SCALAR') {
      if ( $conf{-String}) {
	$self->{StringContent} = $conf{-String};
      }
      delete $conf{-String};
    }
    else {
      croak "Parameter -String must be a SCALAR!\n";
    }
  }

  if (exists $conf{-Tie}) {
    if ($conf{-Tie}) {
      $self->{Tie} = delete $conf{-Tie};
      $self->{DefaultConfig} = $self->_hashref();
    }
  }

  if (exists $conf{-FlagBits}) {
    if ($conf{-FlagBits} && ref($conf{-FlagBits}) eq 'HASH') {
      $self->{FlagBits} = 1;
      $self->{FlagBitsFlags} = $conf{-FlagBits};
    }
    delete $conf{-FlagBits};
  }

  if (exists $conf{-DefaultConfig}) {
    if ($conf{-DefaultConfig} && ref($conf{-DefaultConfig}) eq 'HASH') {
      $self->{DefaultConfig} = $conf{-DefaultConfig};
    }
    elsif ($conf{-DefaultConfig} && ref($conf{-DefaultConfig}) eq q()) {
      $self->_read($conf{-DefaultConfig}, 'SCALAR');
      $self->{DefaultConfig} = $self->_parse($self->_hashref(), $self->{content});
      $self->{content} = ();
    }
    delete $conf{-DefaultConfig};
  }

  # handle options which may either be true or false
  # allowing "human" logic about what is true and what is not
  foreach my $entry (keys %conf) {
    my $key = $entry;
    $key =~ s/^\-//;
    if (! exists $self->{$key}) {
      croak "Unknown parameter: $entry => \"$conf{$entry}\" (key: <$key>)\n";
    }
    if ($conf{$entry} =~ /$self->{AutoTrueFlags}->{true}/io) {
      $self->{$key} = 1;
    }
    elsif ($conf{$entry} =~ /$self->{AutoTrueFlags}->{false}/io) {
      $self->{$key} = 0;
    }
    else {
      # keep it untouched
      $self->{$key} = $conf{$entry};
    }
  }

  if ($self->{MergeDuplicateOptions}) {
    # override if not set by user
    if (! exists $conf{-AllowMultiOptions}) {
      $self->{AllowMultiOptions} = 0;
    }
  }

  if ($self->{ApacheCompatible}) {
    # turn on all apache compatibility options which has
    # been incorporated during the years...
    $self->{UseApacheInclude}   = 1;
    $self->{IncludeRelative}    = 1;
    $self->{IncludeDirectories} = 1;
    $self->{IncludeGlob}        = 1;
    $self->{SpashIsDirectory}   = 1;
    $self->{SplitPolicy}        = 'whitespace';
    $self->{CComments}          = 0;
    $self->{BackslashEscape}    = 1;
  }
}

sub getall {
  #
  # just return the whole config hash
  #
  my($this) = @_;
  return (exists $this->{config} ? %{$this->{config}} : () );
}


sub files {
  #
  # return a list of files opened so far
  #
  my($this) = @_;
  return (exists $this->{files} ? keys %{$this->{files}} : () );
}


sub _open {
  #
  # open the config file, or expand a directory or glob
  #
  my($this, $configfile) = @_;
  my $fh;

  if ($this->{IncludeGlob} and $configfile =~ /[*?\[\{\\]/) {
    # Something like: *.conf (or maybe dir/*.conf) was included; expand it and
    # pass each expansion through this method again.
    my @include = grep { -f $_ } bsd_glob($configfile, GLOB_BRACE | GLOB_QUOTE);
    if (@include == 1) {
      $configfile = $include[0];
    }
    else {
      # Multiple results or no expansion results (which is fine,
      # include foo/* shouldn't fail if there isn't anything matching)
      local $this->{IncludeGlob};
      for (@include) {
	$this->_open($_);
      }
      return;
    }
  }

  if (!-e $configfile) {
    my $found;
    if (defined $this->{ConfigPath}) {
      # try to find the file within ConfigPath
      foreach my $dir (@{$this->{ConfigPath}}) {
	if( -e catfile($dir, $configfile) ) {
	  $configfile = catfile($dir, $configfile);
	  $found = 1;
	  last; # found it
	}
      }
    }
    if (!$found) {
      my $path_message = defined $this->{ConfigPath} ? q( within ConfigPath: ) . join(q(.), @{$this->{ConfigPath}}) : q();
      croak qq{The file "$configfile" does not exist$path_message!};
    }
  }

  local ($RS) = $RS;
  if (! $RS) {
    carp(q(\$RS (INPUT_RECORD_SEPARATOR) is undefined.  Guessing you want a line feed character));
    $RS = "\n";
  }

  if (-d $configfile and $this->{IncludeDirectories}) {
    # A directory was included; include all the files inside that directory in ASCII order
    local *INCLUDEDIR;
    opendir INCLUDEDIR, $configfile or croak "Could not open directory $configfile!($!)\n";
    my @files = sort grep { -f "$configfile/$_" } "$configfile/$_", readdir INCLUDEDIR;
    closedir INCLUDEDIR;
    local $this->{CurrentConfigFilePath} = $configfile;
    for (@files) {
      if (! $this->{files}->{"$configfile/$_"}) {
	$fh = IO::File->new( "$configfile/$_", 'r') or croak "Could not open $configfile/$_!($!)\n";
	$this->{files}->{"$configfile/$_"} = 1;
	$this->_read($fh);
      }
    }
  }
  elsif (-e _) {
    if (exists $this->{files}->{$configfile} ) {
      # do not read the same file twice, just return
      # FIXME: should we croak here, when some "debugging" is enabled?
      return;
    }
    else {
      $fh = IO::File->new( "$configfile", 'r') or croak "Could not open $configfile!($!)\n";

      $this->{files}->{$configfile}    = 1;

      my ($volume, $path, undef)           = splitpath($configfile);
      local $this->{CurrentConfigFilePath} = catpath($volume, $path, q());

      $this->_read($fh);
    }
  }
  return;
}


sub _read {
  #
  # store the config contents in @content
  # and prepare it somewhat for easier parsing later
  # (comments, continuing lines, and stuff)
  #
  my($this, $fh, $flag) = @_;
  my(@stuff, @content, $c_comment, $longline, $hier, $hierend, @hierdoc);
  local $_ = q();

  if ($flag && $flag eq 'SCALAR') {
    if (ref($fh) eq 'ARRAY') {
      @stuff = @{$fh};
    }
    else {
      @stuff = split /\n/, $fh;
    }
  }
  else {
    @stuff = <$fh>;
  }

  foreach (@stuff) {
    if ($this->{AutoLaunder}) {
      if (m/^(.*)$/) {
	$_ = $1;
      }
    }

    chomp;

    if ($this->{CComments}) {
      # look for C-Style comments, if activated
      if (/(\s*\/\*.*\*\/\s*)/) {
	# single c-comment on one line
	s/\s*\/\*.*\*\/\s*//;
      }
      elsif (/^\s*\/\*/) {
	# the beginning of a C-comment ("/*"), from now on ignore everything.
	if (/\*\/\s*$/) {
	  # C-comment end is already there, so just ignore this line!
	  $c_comment = 0;
	}
	else {
	  $c_comment = 1;
	}
      }
      elsif (/\*\//) {
	if (!$c_comment) {
	  warn "invalid syntax: found end of C-comment without previous start!\n";
	}
	$c_comment = 0;    # the current C-comment ends here, go on
	s/^.*\*\///;       # if there is still stuff, it will be read
      }
      next if($c_comment); # ignore EVERYTHING from now on, IF it IS a C-Comment
    }


    if ($hier) {
      # inside here-doc, only look for $hierend marker
      if (/^(\s*)\Q$hierend\E\s*$/) {
	my $indent = $1;                 # preserve indentation
	$hier .= ' ' . chr 182;         # append a "¶" to the here-doc-name, so
	                                 # _parse will also preserver indentation
	if ($indent) {
	  foreach (@hierdoc) {
	    s/^$indent//;                # i.e. the end was: "    EOF" then we remove "    " from every here-doc line
	    $hier .= $_ . "\n";          # and store it in $hier
	  }
	}
	else {
	  $hier .= join "\n", @hierdoc;  # there was no indentation of the end-string, so join it 1:1
	}
	push @{$this->{content}}, $hier; # push it onto the content stack
	@hierdoc = ();
	undef $hier;
	undef $hierend;
      }
      else {
	# everything else onto the stack
	push @hierdoc, $_;
      }
      next;
    }

    ###
    ### non-heredoc entries from now on
    ##

    # Remove comments and empty lines
    s/(?<!\\)#.+$//;
    next if /^\s*#/;
    next if /^\s*$/;


    # look for multiline option, indicated by a trailing backslash
    my $extra = $this->{BackslashEscape} ? '(?<!\\\\)' : q();
    if (/$extra\\$/) {
      chop;
      s/^\s*//;
      $longline .= $_;
      next;
    }

    # remove the \ from all characters if BackslashEscape is turned on
    if ($this->{BackslashEscape}) {
      s/\\(.)/$1/g;
    }
    else {
      # remove the \ char in front of masked "#", if any
      s/\\#/#/g;
    }


    # transform explicit-empty blocks to conforming blocks
    if (!$this->{ApacheCompatible} && /\s*<([^\/]+?.*?)\/>$/) {
      my $block = $1;
      if ($block !~ /\"/) {
	if ($block !~ /\s[^\s]/) {
	  # fix of bug 7957, add quotation to pure slash at the
	  # end of a block so that it will be considered as directory
	  # unless the block is already quoted or contains whitespaces
	  # and no quotes.
	  if ($this->{SlashIsDirectory}) {
	    push @{$this->{content}}, '<' . $block . '"/">';
	    next;
	  }
	}
      }
      my $orig  = $_;
      $orig     =~ s/\/>$/>/;
      $block    =~ s/\s\s*.*$//;
      push @{$this->{content}}, $orig, "</${block}>";
      next;
    }


    # look for here-doc identifier
    if ($this->{SplitPolicy} eq 'guess') {
      if (/^\s*(\S+?)(\s*=\s*|\s+)<<\s*(.+?)\s*$/) {
	$hier    = $1;  # the actual here-doc variable name
	$hierend = $3;  # the here-doc identifier, i.e. "EOF"
	next;
      }
    }
    else {
      # no guess, use one of the configured strict split policies
      if (/^\s*(\S+?)($this->{SplitDelimiter})<<\s*(.+?)\s*$/) {
	$hier    = $1;  # the actual here-doc variable name
	$hierend = $3;  # the here-doc identifier, i.e. "EOF"
	next;
      }
    }



    ###
    ### any "normal" config lines from now on
    ###

    if ($longline) {
      # previous stuff was a longline and this is the last line of the longline
      s/^\s*//;
      $longline .= $_;
      push @{$this->{content}}, $longline;    # push it onto the content stack
      undef $longline;
      next;
    }
    else {
      # look for include statement(s)
      my $incl_file;
      my $path = '';
      if ( $this->{IncludeRelative} and defined $this->{CurrentConfigFilePath}) {
      	$path = $this->{CurrentConfigFilePath};
      }
      elsif (defined $this->{ConfigPath}) {
	# fetch pathname of base config file, assuming the 1st one is the path of it
	$path = $this->{ConfigPath}->[0];
      }
      if (/^\s*<<include\s+(.+?)>>\s*$/i || (/^\s*include\s+(.+?)\s*$/i && $this->{UseApacheInclude})) {
	$incl_file = $1;
	if ( $this->{IncludeRelative} && $path && !file_name_is_absolute($incl_file) ) {
	  # include the file from within location of $this->{configfile}
	  $this->_open( catfile($path, $incl_file) );
	}
	else {
	  # include the file from within pwd, or absolute
	  $this->_open($incl_file);
	}
      }
      else {
	# standard entry, (option = value)
	push @{$this->{content}}, $_;
      }
    }

  }
  return 1;
}





sub _parse {
  #
  # parse the contents of the file
  #
  my($this, $config, $content) = @_;
  my(@newcontent, $block, $blockname, $chunk,$block_level);
  local $_;
  my $indichar = chr 182;  # ¶, inserted by _open, our here-doc indicator

  foreach (@{$content}) {                                  # loop over content stack
    chomp;
    $chunk++;
    $_ =~ s/^\s*//;                                        # strip spaces @ end and begin
    $_ =~ s/\s*$//;

    #
    # build option value assignment, split current input
    # using whitespace, equal sign or optionally here-doc
    # separator (ascii 182).
    my ($option,$value);
    if (/$indichar/) {
      ($option,$value) = split /\s*$indichar\s*/, $_, 2;   # separated by heredoc-finding in _open()
    }
    else {
      if ($this->{SplitPolicy} eq 'guess') {
	# again the old regex. use equalsign SplitPolicy to get the
	# 2.00 behavior. the new regexes were too odd.
	($option,$value) = split /\s*=\s*|\s+/, $_, 2;
      }
      else {
	# no guess, use one of the configured strict split policies
	($option,$value) = split /$this->{SplitDelimiter}/, $_, 2;
      }
    }

    if ($value && $value =~ /^"/ && $value =~ /"$/) {
      $value =~ s/^"//;                                    # remove leading and trailing "
      $value =~ s/"$//;
    }
    if (! defined $block) {                                # not inside a block @ the moment
      if (/^<([^\/]+?.*?)>$/) {                            # look if it is a block
	$block = $1;                                       # store block name
	if ($block =~ /^"([^"]+)"$/) {
	  # quoted block, unquote it and do not split
	  $block =~ s/"//g;
	}
	else {
	  # If it is a named block store the name separately; allow the block and name to each be quoted
	  if ($block =~ /^(?:"([^"]+)"|(\S+))(?:\s+(?:"([^"]+)"|(.*)))?$/) {
	    $block = $1 || $2;
	    $blockname = $3 || $4;
	  }
	}
	if ($this->{InterPolateVars}) {
	  # interpolate block(name), add "<" and ">" to the key, because
	  # it is sure that such keys does not exist otherwise.
	  $block     = $this->_interpolate("<$block>", $block);
	  if (defined $blockname) {
	    $blockname = $this->_interpolate("<$blockname>", "$blockname");
	  }
	}
	if ($this->{LowerCaseNames}) {
	  $block = lc $block;    # only for blocks lc(), if configured via new()
	}
	$this->{level} += 1;
	undef @newcontent;
	next;
      }
      elsif (/^<\/(.+?)>$/) {                              # it is an end block, but we don't have a matching block!
	croak "EndBlock \"<\/$1>\" has no StartBlock statement (level: $this->{level}, chunk $chunk)!\n";
      }
      else {                                               # insert key/value pair into actual node
	if ($this->{LowerCaseNames}) {
	  $option = lc $option;
	}
	if (exists $config->{$option}) {
	  if ($this->{MergeDuplicateOptions}) {
	    $config->{$option} = $this->_parse_value($option, $value);
	  }
	  else {
	    if (! $this->{AllowMultiOptions} ) {
	      # no, duplicates not allowed
	      croak "Option \"$option\" occurs more than once (level: $this->{level}, chunk $chunk)!\n";
	    }
	    else {
	      # yes, duplicates allowed
	      if (ref($config->{$option}) ne 'ARRAY') {      # convert scalar to array
		my $savevalue = $config->{$option};
		delete $config->{$option};
		push @{$config->{$option}}, $savevalue;
	      }
	      eval {
		# check if arrays are supported by the underlying hash
		my $i = scalar @{$config->{$option}};
	      };
	      if ($EVAL_ERROR) {
		$config->{$option} = $this->_parse_value($option, $value);
	      }
	      else {
		# it's already an array, just push
		push @{$config->{$option}}, $this->_parse_value($option, $value);
	      }
	    }
	  }
	}
	else {
	  # standard config option, insert key/value pair into node
	  $config->{$option} = $this->_parse_value($option, $value);
	}
      }
    }
    elsif (/^<([^\/]+?.*?)>$/) {    # found a start block inside a block, don't forget it
      $block_level++;               # $block_level indicates wether we are still inside a node
      push @newcontent, $_;         # push onto new content stack for later recursive call of _parse()
    }
    elsif (/^<\/(.+?)>$/) {
      if ($block_level) {           # this endblock is not the one we are searching for, decrement and push
	$block_level--;             # if it is 0, then the endblock was the one we searched for, see below
	push @newcontent, $_;       # push onto new content stack
      }
      else {                        # calling myself recursively, end of $block reached, $block_level is 0
	if (defined $blockname) {           # a named block, make it a hashref inside a hash within the current node
	  $this->_savelast($blockname);

	  if (! exists $config->{$block}) {
	    $config->{$block} = $this->_hashref(); # Make sure that the hash is not created implicitely
	  }

	  if (exists $config->{$block}->{$blockname}) {    # the named block already exists, make it an array
	    if ($this->{MergeDuplicateBlocks}) {
	      # just merge the new block with the same name as an existing one into
              # this one.
	      $config->{$block}->{$blockname} = $this->_parse($config->{$block}->{$blockname}, \@newcontent);
	    }
	    else {
	      if (! $this->{AllowMultiOptions}) {
		croak "Named block \"<$block $blockname>\" occurs more than once (level: $this->{level}, chunk $chunk)!\n";
	      }
	      else {                                       # preserve existing data
		my $savevalue = $config->{$block}->{$blockname};
		delete $config->{$block}->{$blockname};
		my @ar;
		if (ref $savevalue eq 'ARRAY') {
		  push @ar, @{$savevalue};                   # preserve array if any
		}
		else {
		  push @ar, $savevalue;
		}
		push @ar, $this->_parse( $this->_hashref(), \@newcontent);  # append it
		$config->{$block}->{$blockname} = \@ar;
	      }
	    }
	  }
	  elsif (ref($config->{$block}) eq 'ARRAY') {
	    croak "Cannot add named block <$block $blockname> to hash! Block <$block> occurs more than once.\n"
	         ."Turn on -MergeDuplicateBlocks or make sure <$block> occurs only once in the config.\n";
	  }
	  else {
	    # the first occurence of this particular named block
	    $config->{$block}->{$blockname} = $this->_parse($this->_hashref(), \@newcontent);
	  }
	  $this->_backlast($blockname);
	}
	else {                                             # standard block
	  $this->_savelast($block);
	  if (exists $config->{$block}) {                  # the block already exists, make it an array
	    if ($this->{MergeDuplicateBlocks}) {
	      # just merge the new block with the same name as an existing one into
              # this one.
	      $config->{$block} = $this->_parse($config->{$block}, \@newcontent);
            }
            else {
	      if (! $this->{AllowMultiOptions}) {
	        croak "Block \"<$block>\" occurs more than once (level: $this->{level}, chunk $chunk)!\n";
	      }
	      else {
		my $savevalue = $config->{$block};
		delete $config->{$block};
		my @ar;
		if (ref $savevalue eq "ARRAY") {
		  push @ar, @{$savevalue};
		}
		else {
		  push @ar, $savevalue;
		}
		push @ar, $this->_parse( $this->_hashref(), \@newcontent);
		$config->{$block} = \@ar;
	      }
	    }
	  }
	  else {
	    # the first occurence of this particular block
	    #### $config->{$block} = $this->_parse($config->{$block}, \@newcontent);
	    $config->{$block} = $this->_parse($this->_hashref(), \@newcontent);
	  }
	  $this->_backlast($block);
	}
	undef $blockname;
	undef $block;
	$this->{level} -= 1;
	next;
      }
    }
    else {                                                 # inside $block, just push onto new content stack
      push @newcontent, $_;
    }
  }
  if ($block) {
    # $block is still defined, which means, that it had
    # no matching endblock!
    croak "Block \"<$block>\" has no EndBlock statement (level: $this->{level}, chunk $chunk)!\n";
  }
  return $config;
}


sub _savelast {
  my($this, $key) = @_;
  push @{$this->{upperkeys}}, $this->{lastkey};
  $this->{lastkey}  = $this->{prevkey};
  $this->{prevkey}  = $key;
  return;
}

sub _backlast {
  my($this, $key) = @_;
  $this->{prevkey} = $this->{lastkey};
  $this->{lastkey} = pop @{$this->{upperkeys}};
  return;
}

sub _parse_value {
  #
  # parse the value if value parsing is turned on
  # by either -AutoTrue and/or -FlagBits
  # otherwise just return the given value unchanged
  #
  my($this, $option, $value) =@_;

  # avoid "Use of uninitialized value"
  if (! defined $value) {
    $value = q();
  }

  if ($this->{InterPolateVars}) {
    $value = $this->_interpolate($option, $value);
  }

  # make true/false values to 1 or 0 (-AutoTrue)
  if ($this->{AutoTrue}) {
    if ($value =~ /$this->{AutoTrueFlags}->{true}/io) {
      $value = 1;
    }
    elsif ($value =~ /$this->{AutoTrueFlags}->{false}/io) {
      $value = 0;
    }
  }

  # assign predefined flags or undef for every flag | flag ... (-FlagBits)
  if ($this->{FlagBits}) {
    if (exists $this->{FlagBitsFlags}->{$option}) {
      my %__flags = map { $_ => 1 } split /\s*\|\s*/, $value;
      foreach my $flag (keys %{$this->{FlagBitsFlags}->{$option}}) {
	if (exists $__flags{$flag}) {
	  $__flags{$flag} = $this->{FlagBitsFlags}->{$option}->{$flag};
	}
	else {
	  $__flags{$flag} = undef;
	}
      }
      $value = \%__flags;
    }
  }
  return $value;
}






sub NoMultiOptions {
  #
  # turn AllowMultiOptions off, still exists for backward compatibility.
  # Since we do parsing from within new(), we must
  # call it again if one turns NoMultiOptions on!
  #
  croak q(The NoMultiOptions() method is deprecated. Set 'AllowMultiOptions' to 'no' instead!);
}


sub save {
  #
  # this is the old version of save() whose API interface
  # has been changed. I'm very sorry 'bout this.
  #
  # I'll try to figure out, if it has been called correctly
  # and if yes, feed the call to Save(), otherwise croak.
  #
  my($this, $one, @two) = @_;

  if ( (@two && $one) && ( (scalar @two) % 2 == 0) ) {
    # @two seems to be a hash
    my %h = @two;
    $this->save_file($one, \%h);
  }
  else {
    croak q(The save() method is deprecated. Use the new save_file() method instead!);
  }
  return;
}


sub save_file {
  #
  # save the config back to disk
  #
  my($this, $file, $config) = @_;
  my $fh;
  my $config_string;

  if (!$file) {
    croak "Filename is required!";
  }
  else {
    $fh = IO::File->new( "$file", 'w') or croak "Could not open $file!($!)\n";

    if (!$config) {
      if (exists $this->{config}) {
	$config_string = $this->_store(0, %{$this->{config}});
      }
      else {
	croak "No config hash supplied which could be saved to disk!\n";
      }
    }
    else {
      $config_string = $this->_store(0,%{$config});
    }

    if ($config_string) {
      print {$fh} $config_string;
    }
    else {
      # empty config for whatever reason, I don't care
      print {$fh} q();
    }

    close $fh;
  }
  return;
}



sub save_string {
  #
  # return the saved config as a string
  #
  my($this, $config) = @_;

  if (!$config || ref($config) ne 'HASH') {
    if (exists $this->{config}) {
      return $this->_store(0, %{$this->{config}});
    }
    else {
      croak "No config hash supplied which could be saved to disk!\n";
    }
  }
  else {
    return $this->_store(0, %{$config});
  }
  return;
}



sub _store {
  #
  # internal sub for saving a block
  #
  my($this, $level, %config) = @_;
  local $_;
  my $indent = q(    ) x $level;

  my $config_string = q();

  foreach my $entry (keys %config) {
    if (ref($config{$entry}) eq 'ARRAY') {
      foreach my $line (@{$config{$entry}}) {
        if (ref($line) eq 'HASH') {
	  $config_string .= $this->_write_hash($level, $entry, $line);
        }
        else {
	  $config_string .= $this->_write_scalar($level, $entry, $line);
        }
      }
    }
    elsif (ref($config{$entry}) eq 'HASH') {
      $config_string .= $this->_write_hash($level, $entry, $config{$entry});
    }
    else {
      $config_string .= $this->_write_scalar($level, $entry, $config{$entry});
    }
  }

  return $config_string;
}


sub _write_scalar {
  #
  # internal sub, which writes a scalar
  # it returns it, in fact
  #
  my($this, $level, $entry, $line) = @_;

  my $indent = q(    ) x $level;

  my $config_string;

  if ($line =~ /\n/ || $line =~ /\\$/) {
    # it is a here doc
    my $delimiter;
    my $tmplimiter = 'EOF';
    while (!$delimiter) {
      # create a unique here-doc identifier
      if ($line =~ /$tmplimiter/s) {
	$tmplimiter .= q(%);
      }
      else {
	$delimiter = $tmplimiter;
      }
    }
    my @lines = split /\n/, $line;
    $config_string .= $indent . $entry . $this->{StoreDelimiter} . "<<$delimiter\n";
    foreach (@lines) {
      $config_string .= $indent . $_ . "\n";
    }
    $config_string .= $indent . "$delimiter\n";
  }
  else {
    # a simple stupid scalar entry
    $line =~ s/#/\\#/g;
    $config_string .= $indent . $entry . $this->{StoreDelimiter} . $line . "\n";
  }

  return $config_string;
}

sub _write_hash {
  #
  # internal sub, which writes a hash (block)
  # it returns it, in fact
  #
  my($this, $level, $entry, $line) = @_;

  my $indent = q(    ) x $level;
  my $config_string;

  if ($entry =~ /\s/) {
    # quote the entry if it contains whitespaces
    $entry = q(") . $entry . q(");
  }

  $config_string .= $indent . q(<) . $entry . ">\n";
  $config_string .= $this->_store($level + 1, %{$line});
  $config_string .= $indent . q(</) . $entry . ">\n";

  return $config_string
}


sub _hashref {
  #
  # return a probably tied new empty hash ref
  #
  my($this) = @_;
  my ($package, $filename, $line, $subroutine, $hasargs,
      $wantarray, $evaltext, $is_require, $hints, $bitmask) = caller 0;
  if ($this->{Tie}) {
    eval {
      eval qq{require $this->{Tie}};
    };
    if ($EVAL_ERROR) {
      croak q(Could not create a tied hash of type: ) . $this->{Tie} . q(: ) . $EVAL_ERROR;
    }
    my %hash;
    tie %hash, $this->{Tie};
    return \%hash;
  }
  else {
    return {};
  }
}



#
# Procedural interface
#
sub ParseConfig {
  #
  # @_ may contain everything which is allowed for new()
  #
  return (new Config::General(@_))->getall();
}

sub SaveConfig {
  #
  # 2 parameters are required, filename and hash ref
  #
  my ($file, $hash) = @_;

  if (!$file || !$hash) {
    croak q{SaveConfig(): filename and hash argument required.};
  }
  else {
    if (ref($hash) ne 'HASH') {
      croak q(The second parameter must be a reference to a hash!);
    }
    else {
      (new Config::General(-ConfigHash => $hash))->save_file($file);
    }
  }
  return;
}

sub SaveConfigString {
  #
  # same as SaveConfig, but return the config,
  # instead of saving it
  #
  my ($hash) = @_;

  if (!$hash) {
    croak q{SaveConfigString(): Hash argument required.};
  }
  else {
    if (ref($hash) ne 'HASH') {
      croak q(The parameter must be a reference to a hash!);
    }
    else {
      return (new Config::General(-ConfigHash => $hash))->save_string();
    }
  }
  return;
}



# keep this one
1;
__END__





=head1 NAME

Config::General - Generic Config Module

=head1 SYNOPSIS

 #
 # the OOP way
 use Config::General;
 $conf = new Config::General("rcfile");
 my %config = $conf->getall;

 #
 # the procedural way
 use Config::General;
 my %config = ParseConfig("rcfile");

=head1 DESCRIPTION

This module opens a config file and parses it's contents for you. The B<new> method
requires one parameter which needs to be a filename. The method B<getall> returns a hash
which contains all options and it's associated values of your config file.

The format of config files supported by B<Config::General> is inspired by the well known apache config
format, in fact, this module is 100% compatible to apache configs, but you can also just use simple
name/value pairs in your config files.

In addition to the capabilities of an apache config file it supports some enhancements such as here-documents,
C-style comments or multiline options.


=head1 SUBROUTINES/METHODS

=over

=item new()

Possible ways to call B<new()>:

 $conf = new Config::General("rcfile");

 $conf = new Config::General(\%somehash);

 $conf = new Config::General( %options ); # see below for description of possible options


This method returns a B<Config::General> object (a hash blessed into "Config::General" namespace.
All further methods must be used from that returned object. see below.

You can use the new style with hash parameters or the old style which is of course
still supported. Possible parameters to B<new()> are:

* a filename of a configfile, which will be opened and parsed by the parser

or

* a hash reference, which will be used as the config.

An alternative way to call B<new()> is supplying an option- hash with one or more of
the following keys set:

=over

=item B<-ConfigFile>

A filename or a filehandle, i.e.:

 -ConfigFile => "rcfile" or -ConfigFile => \$FileHandle



=item B<-ConfigHash>

A hash reference, which will be used as the config, i.e.:

 -ConfigHash => \%somehash



=item B<-String>

A string which contains a whole config, or an arrayref
containing the whole config line by line.
The parser will parse the contents of the string instead
of a file. i.e:

 -String => $complete_config

it is also possible to feed an array reference to -String:

 -String => \@config_lines



=item B<-AllowMultiOptions>

If the value is "no", then multiple identical options are disallowed.
The default is "yes".
i.e.:

 -AllowMultiOptions => "no"

see B<IDENTICAL OPTIONS> for details.

=item B<-LowerCaseNames>

If set to a true value, then all options found in the config will be converted
to lowercase. This allows you to provide case-in-sensitive configs. The
values of the options will B<not> lowercased.



=item B<-UseApacheInclude>

If set to a true value, the parser will consider "include ..." as valid include
statement (just like the well known apache include statement).



=item B<-IncludeRelative>

If set to a true value, included files with a relative path (i.e. "cfg/blah.conf")
will be opened from within the location of the configfile instead from within the
location of the script($0). This works only if the configfile has a absolute pathname
(i.e. "/etc/main.conf").

If the variable B<-ConfigPath> has been set and if the file to be included could
not be found in the location relative to the current config file, the module
will search within B<-ConfigPath> for the file. See the description of B<-ConfigPath>
for more details.


=item B<-IncludeDirectories>

If set to a true value, you may specify include a directory, in which case all
files inside the directory will be loaded in ASCII order.  Directory includes
will not recurse into subdirectories.  This is comparable to including a
directory in Apache-style config files.


=item B<-IncludeGlob>

If set to a true value, you may specify a glob pattern for an include to
include all matching files (e.g. <<include conf.d/*.conf>>).  Also note that as
with standard file patterns, * will not match dot-files, so <<include dir/*>>
is often more desirable than including a directory with B<-IncludeDirectories>.


=item B<-ConfigPath>

As mentioned above, you can use this variable to specify a search path for relative
config files which have to be included. Config::General will search within this
path for the file if it cannot find the file at the location relative to the
current config file.

To provide multiple search paths you can specify an array reference for the
path.  For example:

 @path = qw(/usr/lib/perl /nfs/apps/lib /home/lib);
 ..
 -ConfigPath => \@path



=item B<-MergeDuplicateBlocks>

If set to a true value, then duplicate blocks, that means blocks and named blocks,
will be merged into a single one (see below for more details on this).
The default behavior of Config::General is to create an array if some junk in a
config appears more than once.


=item B<-MergeDuplicateOptions>

If set to a true value, then duplicate options will be merged. That means, if the
same option occurs more than once, the last one will be used in the resulting
config hash.

Setting this option implies B<-AllowMultiOptions == false> unless you set
B<-AllowMultiOptions> explicit to 'true'. In this case duplicate blocks are
allowed and put into an array but dupclicate options will be merged.


=item B<-AutoLaunder>

If set to a true value, then all values in your config file will be laundered
to allow them to be used under a -T taint flag.  This could be regarded as circumventing
the purpose of the -T flag, however, if the bad guys can mess with your config file,
you have problems that -T will not be able to stop.  AutoLaunder will only handle
a config file being read from -ConfigFile.



=item B<-AutoTrue>

If set to a true value, then options in your config file, whose values are set to
true or false values, will be normalised to 1 or 0 respectively.

The following values will be considered as B<true>:

 yes, on, 1, true

The following values will be considered as B<false>:

 no, off, 0, false

This effect is case-insensitive, i.e. both "Yes" or "oN" will result in 1.


=item B<-FlagBits>

This option takes one required parameter, which must be a hash reference.

The supplied hash reference needs to define variables for which you
want to preset values. Each variable you have defined in this hash-ref
and which occurs in your config file, will cause this variable being
set to the preset values to which the value in the config file refers to.

Multiple flags can be used, separated by the pipe character |.

Well, an example will clarify things:

 my $conf = new Config::General(
         -ConfigFile => "rcfile",
         -FlagBits => {
              Mode => {
                 CLEAR    => 1,
                 STRONG   => 1,
                 UNSECURE => "32bit" }
         }
 );

In this example we are defining a variable named I<"Mode"> which
may contain one or more of "CLEAR", "STRONG" and "UNSECURE" as value.

The appropriate config entry may look like this:

 # rcfile
 Mode = CLEAR | UNSECURE

The parser will create a hash which will be the value of the key "Mode". This
hash will contain B<all> flags which you have pre-defined, but only those
which were set in the config will contain the pre-defined value, the other
ones will be undefined.

The resulting config structure would look like this after parsing:

 %config = (
             Mode => {
                       CLEAR    => 1,
                       UNSECURE => "32bit",
                       STRONG   => undef,
                     }
           );

This method allows the user (or, the "maintainer" of the configfile for your
application) to set multiple pre-defined values for one option.

Please beware, that all occurencies of those variables will be handled this
way, there is no way to distinguish between variables in different scopes.
That means, if "Mode" would also occur inside a named block, it would
also parsed this way.

Values which are not defined in the hash-ref supplied to the parameter B<-FlagBits>
and used in the corresponding variable in the config will be ignored.

Example:

 # rcfile
 Mode = BLAH | CLEAR

would result in this hash structure:

  %config = (
             Mode => {
                       CLEAR    => 1,
                       UNSECURE => undef,
                       STRONG   => undef,
                     }
           );

"BLAH" will be ignored silently.


=item B<-DefaultConfig>

This can be a hash reference or a simple scalar (string) of a config. This
causes the module to preset the resulting config hash with the given values,
which allows you to set default values for particular config options directly.


=item B<-Tie>

B<-Tie> takes the name of a Tie class as argument that each new hash should be
based off of.

This hash will be used as the 'backing hash' instead of a standard perl hash,
which allows you to affect the way, variable storing will be done. You could, for
example supply a tied hash, say Tie::DxHash, which preserves ordering of the
keys in the config (which a standard perl hash won't do). Or, you could supply
a hash tied to a DBM file to save the parsed variables to disk.

There are many more things to do in tie-land, see L<tie> to get some interesting
ideas.

If you want to use the B<-Tie> feature together with B<-DefaultConfig> make sure
that the hash supplied to B<-DefaultConfig> must be tied to the same Tie class.

Make sure that the hash which receives the generated hash structure (e.g. which
you are using in the assignment: %hash = $config->getall()) must be tied to
the same Tie class.

Example:

 use Config::General;
 use Tie::IxHash;
 tie my %hash, "Tie::IxHash";
 %hash = ParseConfig(
           -ConfigFile => shift(),
           -Tie => "Tie::IxHash"
	 );


=item B<-InterPolateVars>

If set to a true value, variable interpolation will be done on your config
input. See L<Config::General::Interpolated> for more informations.

=item B<-InterPolateEnv>

If set to a true value, environment variables can be used in
configs.

This implies B<-InterPolateVars>.

=item B<-ExtendedAccess>

If set to a true value, you can use object oriented (extended) methods to
access the parsed config. See L<Config::General::Extended> for more informations.

=item B<-StrictObjects>

By default this is turned on, which causes Config::General to croak with an
error if you try to access a non-existent key using the oop-way (B<-ExtendedAcess>
enabled). If you turn B<-StrictObjects> off (by setting to 0 or "no") it will
just return an empty object/hash/scalar. This is valid for OOP-access 8via AUTOLOAD
and for the methods obj(), hash() and value().


=item B<-StrictVars>

By default this is turned on, which causes Config::General to croak with an
error if an undefined variable with B<InterPolateVars> turned on occurs
in a config. Set to I<false> (i.e. 0) to avoid such error messages.

=item B<-SplitPolicy>

You can influence the way how Config::General decides which part of a line
in a config file is the key and which one is the value. By default it tries
it's best to guess. That means you can mix equalsign assignments and whitespace
assignments.

However, somtimes you may wish to make it more strictly for some reason. In
this case you can set B<-SplitPolicy>. The possible values are: 'guess' which
is the default, 'whitespace' which causes the module to split by whitespace,
'equalsign' which causes it to split strictly by equal sign, or 'custom'. In the
latter case you must also set B<-SplitDelimiter> to some regular expression
of your choice. For example:

 -SplitDelimiter => '\s*:\s*'

will cause the module to split by colon while whitespaces which surrounds
the delimiter will be removed.

Please note that the delimiter used when saving a config (save_file() or save_string())
will be choosen accordingto the current B<-SplitPolicy>. If -SplitPolicy is
set to 'guess' or 'whitespace', 3 whitespaces will be used to delimit saved
options. If 'custom' is set, then you need to set B<-StoreDelimiter>.

=item B<-SplitDelimiter>

Set this to any arbitrary regular expression which will be used for option/value
splitting. B<-SplitPolicy> must be set to 'custom' to make this work.

=item B<-StoreDelimiter>

You can use this parameter to specify a custom delimiter to use when saving
configs to a file or string. You only need to set it if you want to store
the config back to disk and if you have B<-SplitPolicy> set to 'custom'.

Be very carefull with this parameter.


=item B<-CComments>

Config::General is able to notice c-style comments (see section COMMENTS).
But for some reason you might no need this. In this case you can turn
this feature off by setting B<-CComments> to a false value('no', 0, 'off').

By default B<-CComments> is turned on.


=item B<-BackslashEscape>

If you turn on this parameter, a backslash can be used to escape any special
character within configurations.

By default it is turned off.


=item B<-SlashIsDirectory>

If you turn on this parameter, a single slash as the last character
of a named block will be considered as a directory name.

By default this flag is turned off, which makes the module somewhat
incompatible to apache configs, since such a setup will be normally
considered as an explicit empty block, just as XML defines it.

For example, if you have the following config:

 <Directory />
   Index index.awk
 </Directory>

you will get such an error message from the parser:

 EndBlock "</Directory>" has no StartBlock statement (level: 1, chunk 10)!

This is caused by the fact that the config chunk below will be
internally converted to:

 <Directory><Directory />
   Index index.awk
 </Directory>

Now there is one '</Directory>' too much. The proper solution is
to use quotation to circumvent this error:

 <Directory "/">
   Index index.awk
 </Directory>

However, a raw apache config comes without such quotes. In this
case you may consider to turn on B<-SlashIsDirectory>.

Please note that this is a new option (incorporated in version 2.30),
it may lead to various unexpected sideeffects or other failures.
You've been warned.

=item B<-ApacheCompatible>

Over the past years a lot of options has been incorporated
into Config::General to be able to parse real apache configs.

The new B<-ApacheCompatible> option now makes it possible to
tweak all options in a way that apache configs can be parsed.

This is called "apache compatibility mode" - if you will ever
have problems with parsing apache configs without this option
being set, you'll get no help by me. Thanks :)

The following options will be set:

 UseApacheInclude   = 1
 IncludeRelative    = 1
 IncludeDirectories = 1
 IncludeGlob        = 1
 SpashIsDirectory   = 1
 SplitPolicy        = 'equalsign'
 CComments          = 0
 BackslashEscape    = 1

Take a look into the particular documentation sections what
those options are doing.

Beside setting some options it also turns off support for
explicit empty blocks.

=back




=item getall()

Returns a hash structure which represents the whole config.

=item files()

Returns a list of all files read in.

=item save_file()

Writes the config hash back to the harddisk. This method takes one or two
parameters. The first parameter must be the filename where the config
should be written to. The second parameter is optional, it must be a
reference to a hash structure, if you set it. If you do not supply this second parameter
then the internal config hash, which has already been parsed, will be
used.

Please note, that any occurence of comments will be ignored by getall()
and thus be lost after you call this method.

You need also to know that named blocks will be converted to nested blocks
(which is the same from the perl point of view). An example:

 <user hans>
   id 13
 </user>

will become the following after saving:

 <user>
   <hans>
      id 13
   </hans>
 </user>

Example:

 $conf_obj->save_file("newrcfile", \%config);

or, if the config has already been parsed, or if it didn't change:

 $conf_obj->save_file("newrcfile");


=item save_string()

This method is equivalent to the previous save_file(), but it does not
store the generated config to a file. Instead it returns it as a string,
which you can save yourself afterwards.

It takes one optional parameter, which must be a reference to a hash structure.
If you omit this parameter, the internal config hash, which has already been parsed,
will be used.

Example:

 my $content = $conf_obj->save_string(\%config);

or:

 my $content = $conf_obj->save_string();


=back


=head1 CONFIG FILE FORMAT

Lines begining with B<#> and empty lines will be ignored. (see section COMMENTS!)
Spaces at the begining and the end of a line will also be ignored as well as tabulators.
If you need spaces at the end or the beginning of a value you can use
apostrophs B<">.
An optionline starts with it's name followed by a value. An equalsign is optional.
Some possible examples:

 user    max
 user  = max
 user            max

If there are more than one statements with the same name, it will create an array
instead of a scalar. See the example below.

The method B<getall> returns a hash of all values.


=head1 BLOCKS

You can define a B<block> of options. A B<block> looks much like a block
in the wellknown apache config format. It starts with E<lt>B<blockname>E<gt> and ends
with E<lt>/B<blockname>E<gt>. An example:

 <database>
    host   = muli
    user   = moare
    dbname = modb
    dbpass = D4r_9Iu
 </database>

Blocks can also be nested. Here is a more complicated example:

 user   = hans
 server = mc200
 db     = maxis
 passwd = D3rf$
 <jonas>
        user    = tom
        db      = unknown
        host    = mila
        <tablestructure>
                index   int(100000)
                name    char(100)
                prename char(100)
                city    char(100)
                status  int(10)
                allowed moses
                allowed ingram
                allowed joice
        </tablestructure>
 </jonas>

The hash which the method B<getall> returns look like that:

 print Data::Dumper(\%hash);
 $VAR1 = {
          'passwd' => 'D3rf$',
          'jonas'  => {
                       'tablestructure' => {
                                             'prename' => 'char(100)',
                                             'index'   => 'int(100000)',
                                             'city'    => 'char(100)',
                                             'name'    => 'char(100)',
                                             'status'  => 'int(10)',
                                             'allowed' => [
                                                            'moses',
                                                            'ingram',
                                                            'joice',
                                                          ]
                                           },
                       'host'           => 'mila',
                       'db'             => 'unknown',
                       'user'           => 'tom'
                     },
          'db'     => 'maxis',
          'server' => 'mc200',
          'user'   => 'hans'
        };

If you have turned on B<-LowerCaseNames> (see new()) then blocks as in the
following example:

 <Dir>
   <AttriBUTES>
     Owner  root
   </attributes>
 </dir>

would produce the following hash structure:

 $VAR1 = {
          'dir' => {
                    'attributes' => {
                                     'owner  => "root",
                                    }
                   }
         };

As you can see, the keys inside the config hash are normalized.

Please note, that the above config block would result in a
valid hash structure, even if B<-LowerCaseNames> is not set!
This is because I<Config::General> does not
use the blocknames to check if a block ends, instead it uses an internal
state counter, which indicates a block end.

If the module cannot find an end-block statement, then this block will be ignored.



=head1 NAMED BLOCKS

If you need multiple blocks of the same name, then you have to name every block.
This works much like apache config. If the module finds a named block, it will
create a hashref with the left part of the named block as the key containing
one or more hashrefs with the right part of the block as key containing everything
inside the block(which may again be nested!). As examples says more than words:

 # given the following sample
 <Directory /usr/frisco>
        Limit Deny
        Options ExecCgi Index
 </Directory>
 <Directory /usr/frik>
        Limit DenyAll
        Options None
 </Directory>

 # you will get:
 $VAR1 = {
          'Directory' => {
                           '/usr/frik' => {
                                            'Options' => 'None',
                                            'Limit' => 'DenyAll'
                                          },
                           '/usr/frisco' => {
                                              'Options' => 'ExecCgi Index',
                                              'Limit' => 'Deny'
                                            }
                         }
        };

You cannot have more than one named block with the same name because it will
be stored in a hashref and therefore be overwritten if a block occurs once more.


=head1 WHITESPACES IN BLOCKS

The normal behavior of Config::General is to look for whitespaces in
block names to decide if it's a named block or just a simple block.

Sometimes you may need blocknames which have whitespaces in their names.

With named blocks this is no problem, as the module only looks for the
first whitespace:

 <person hugo gera>
 </person>

would be parsed to:

 $VAR1 = {
          'person' => {
                       'hugo gera' => {
                                      },
                      }
         };

The problem occurs, if you want to have a simple block containing whitespaces:

 <hugo gera>
 </hugo gera>

This would be parsed as a named block, which is not what you wanted. In this
very case you may use quotation marks to indicate that it is not a named block:

 <"hugo gera">
 </"hugo gera">

The save() method of the module inserts automatically quotation marks in such
cases.


=head1 EXPICIT EMPTY BLOCKS

Beside the notation of blocks mentioned above it is possible to use
explicit empty blocks.

Normally you would write this in your config to define an empty
block:

 <driver Apache>
 </driver>

To save writing you can also write:

 <driver Apache/>

which is the very same as above. This works for normal blocks and
for named blocks.



=head1 IDENTICAL OPTIONS

You may have more than one line of the same option with different values.

Example:
 log  log1
 log  log2
 log  log2

You will get a scalar if the option occured only once or an array if it occured
more than once. If you expect multiple identical options, then you may need to
check if an option occured more than once:

 $allowed = $hash{jonas}->{tablestructure}->{allowed};
 if(ref($allowed) eq "ARRAY") {
     @ALLOWED = @{$allowed};
 else {
     @ALLOWED = ($allowed);
 }

The same applies to blocks and named blocks too (they are described in more detail
below). For example, if you have the following config:

 <dir blah>
   user max
 </dir>
 <dir blah>
   user hannes
 </dir>

then you would end up with a data structure like this:

 $VAR1 = {
          'dir' => {
                    'blah' => [
                                {
                                  'user' => 'max'
                                },
                                {
                                  'user' => 'hannes'
                                }
                              ]
                    }
          };

As you can see, the two identical blocks are stored in a hash which contains
an array(-reference) of hashes.

Under some rare conditions you might not want this behavior with blocks (and
named blocks too). If you want to get one single hash with the contents of
both identical blocks, then you need to turn the B<new()> parameter B<-MergeDuplicateBlocks>
on (see above). The parsed structure of the example above would then look like
this:

 $VAR1 = {
          'dir' => {
                    'blah' => [
                                  'user' => 'max',
                                  'user' => 'hannes'
                              ]
                    }
          };

As you can see, there is only one hash "dir->{blah}" containing multiple
"user" entries. As you can also see, turning on  B<-MergeDuplicateBlocks>
does not affect scalar options (i.e. "option = value"). In fact you can
tune merging of duplicate blocks and options independent from each other.

If you don't want to allow more than one identical options, you may turn it off
by setting the flag I<AllowMultiOptions> in the B<new()> method to "no".
If turned off, Config::General will complain about multiple occuring options
with identical names!



=head1 LONG LINES

If you have a config value, which is too long and would take more than one line,
you can break it into multiple lines by using the backslash character at the end
of the line. The Config::General module will concatenate those lines to one single-value.

Example:

command = cat /var/log/secure/tripwire | \
           mail C<-s> "report from tripwire" \
           honey@myotherhost.nl

command will become:
 "cat /var/log/secure/tripwire | mail C<-s> 'report from twire' honey@myotherhost.nl"


=head1 HERE DOCUMENTS

You can also define a config value as a so called "here-document". You must tell
the module an identifier which identicates the end of a here document. An
identifier must follow a "<<".

Example:

 message <<EOF
   we want to
   remove the
   homedir of
   root.
 EOF

Everything between the two "EOF" strings will be in the option I<message>.

There is a special feature which allows you to use indentation with here documents.
You can have any amount of whitespaces or tabulators in front of the end
identifier. If the module finds spaces or tabs then it will remove exactly those
amount of spaces from every line inside the here-document.

Example:

 message <<EOF
         we want to
         remove the
         homedir of
         root.
      EOF

After parsing, message will become:

   we want to
   remove the
   homedir of
   root.

because there were the string "     " in front of EOF, which were cutted from every
line inside the here-document.



=head1 INCLUDES

You can include an external file at any posision in your config file using the following statement
in your config file:

 <<include externalconfig.rc>>

If you turned on B<-UseApacheInclude> (see B<new()>), then you can also use the following
statement to include an external file:

 include externalconfig.rc

This file will be inserted at the position where it was found as if the contents of this file
were directly at this position.

You can also recurively include files, so an included file may include another one and so on.
Beware that you do not recursively load the same file, you will end with an errormessage like
"too many open files in system!".

By default included files with a relative pathname will be opened from within the current
working directory. Under some circumstances it maybe possible to
open included files from the directory, where the configfile resides. You need to turn on
the option B<-IncludeRelative> (see B<new()>) if you want that. An example:

 my $conf = Config::General(
                             -ConfigFile => "/etc/crypt.d/server.cfg"
                             -IncludeRelative => 1
                           );

 /etc/crypt.d/server.cfg:
  <<include acl.cfg>>

In this example Config::General will try to include I<acl.cfg> from I</etc/crypt.d>:

 /etc/crypt.d/acl.cfg

The default behavior (if B<-IncludeRelative> is B<not> set!) will be to open just I<acl.cfg>,
whereever it is, i.e. if you did a chdir("/usr/local/etc"), then Config::General will include:

 /usr/local/etc/acl.cfg

Include statements can be case insensitive (added in version 1.25).

Include statements will be ignored within C-Comments and here-documents.



=head1 COMMENTS

A comment starts with the number sign B<#>, there can be any number of spaces and/or
tabstops in front of the #.

A comment can also occur after a config statement. Example:

 username = max  # this is the comment

If you want to comment out a large block you can use C-style comments. A B</*> signals
the begin of a comment block and the B<*/> signals the end of the comment block.
Example:

 user  = max # valid option
 db    = tothemax
 /*
 user  = andors
 db    = toand
 */

In this example the second options of user and db will be ignored. Please beware of the fact,
if the Module finds a B</*> string which is the start of a comment block, but no matching
end block, it will ignore the whole rest of the config file!

B<NOTE:> If you require the B<#> character (number sign) to remain in the option value, then
you can use a backlsash in front of it, to escape it. Example:

 bgcolor = \#ffffcc

In this example the value of $config{bgcolor} will be "#ffffcc", Config::General will not treat
the number sign as the begin of a comment because of the leading backslash.

Inside here-documents escaping of number signs is NOT required!


=head1 OBJECT ORIENTED INTERFACE

There is a way to access a parsed config the OO-way.
Use the module B<Config::General::Extended>, which is
supplied with the Config::General distribution.

=head1 VARIABLE INTERPOLATION

You can use variables inside your configfiles if you like. To do
that you have to use the module B<Config::General::Interpolated>,
which is supplied with the Config::General distribution.


=head1 EXPORTED FUNCTIONS

Config::General exports some functions too, which makes it somewhat
easier to use it, if you like this.

How to import the functions:

 use Config::General qw(ParseConfig SaveConfig SaveConfigString);

=over

=item B<ParseConfig()>

This function takes exactly all those parameters, which are
allowed to the B<new()> method of the standard interface.

Example:

 use Config::General;
 my %config = ParseConfig(-ConfigFile => "rcfile", -AutoTrue => 1);


=item B<SaveConfig()>

This function requires two arguments, a filename and a reference
to a hash structure.

Example:

 use Config::General;
 ..
 SaveConfig("rcfile", \%some_hash);


=item B<SaveConfigString()>

This function requires a reference to a config hash as parameter.
It generates a configuration based on this hash as the object-interface
method B<save_string()> does.

Example:

 use Config::General;
 my %config = ParseConfig(-ConfigFile => "rcfile");
 .. # change %config something
 my $content = SaveConfigString(\%config);


=back

=head1 CONFIGURATION AND ENVIRONMENT

No environment variables will be used.

=head1 SEE ALSO

I recommend you to read the following documentations, which are supplied with perl:

 perlreftut                     Perl references short introduction
 perlref                        Perl references, the rest of the story
 perldsc                        Perl data structures intro
 perllol                        Perl data structures: arrays of arrays

 Config::General::Extended      Object oriented interface to parsed configs
 Config::General::Interpolated  Allows to use variables inside config files

=head1 LICENSE AND COPYRIGHT

Copyright (c) 2000-2007 Thomas Linden

This library is free software; you can redistribute it and/or
modify it under the same terms as Perl itself.

=head1 BUGS AND LIMITATIONS

See rt.cpan.org for current bugs, if any.

=head1 INCOMPATIBILITIES

None known.

=head1 DIAGNOSTICS

To debug Config::General use the perl debugger, see L<perldebug>.

=head1 DEPENDENCIES

Config::General depends on the modules L<FileHandle>,
L<File::Spec::Functions>, L<File::Glob>, which all are
shipped with perl.

=head1 AUTHOR

Thomas Linden <tlinden |AT| cpan.org>

=head1 VERSION

2.33

=cut

