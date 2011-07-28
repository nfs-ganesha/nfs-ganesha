#
# Config::General::Interpolated - special Class based on Config::General
#
# Copyright (c) 2001 by Wei-Hon Chen <plasmaball@pchome.com.tw>.
# Copyright (c) 2000-2007 by Thomas Linden <tlinden |AT| cpan.org>.
# All Rights Reserved. Std. disclaimer applies.
# Artificial License, same as perl itself. Have fun.
#

package Config::General::Interpolated;
$Config::General::Interpolated::VERSION = "2.08";

use strict;
use Carp;
use Config::General;
use Exporter ();


# Import stuff from Config::General
use vars qw(@ISA @EXPORT);
@ISA = qw(Config::General Exporter);


sub new {
  #
  # overwrite new() with our own version
  # and call the parent class new()
  #

  croak "Deprecated method Config::General::Interpolated::new() called.\n"
       ."Use Config::General::new() instead and set the -InterPolateVars flag.\n";
}



sub _set_regex {
  #
  # set the regex for finding vars
  #

  # the following regex is provided by Autrijus Tang
  # <autrijus@autrijus.org>, and I made some modifications.
  # thanx, autrijus. :)
  my $regex = qr{
		 (^|\G|[^\\])   # $1: can be the beginning of the line
		                #     or the beginning of next match
		                #     but can't begin with a '\'
		 \$		# dollar sign
		 (\{)?		# $2: optional opening curly
		 ([a-zA-Z_]\w*)	# $3: capturing variable name
		 (
		 ?(2)		# $4: if there's the opening curly...
		 \}		#     ... match closing curly
		)
	       }x;
  return $regex;
}


sub _interpolate  {
  #
  # interpolate a scalar value and keep the result
  # on the varstack.
  #
  # called directly by Config::General::_parse_value()
  #
  my ($this, $key, $value) = @_;

  my $prevkey;

  if ($this->{level} == 1) {
    # top level
    $prevkey = " ";
  }
  else {
    # incorporate variables outside current scope(block) into
    # our scope to make them visible to _interpolate()

    foreach my $key (keys %{$this->{stack}->{ $this->{level} - 1}->{ $this->{lastkey} }}) {
      if (! exists $this->{stack}->{ $this->{level} }->{ $this->{prevkey} }->{$key}) {
	# only import a variable if it is not re-defined in current scope! (rt.cpan.org bug #20742
	$this->{stack}->{ $this->{level} }->{ $this->{prevkey} }->{$key} = $this->{stack}->{ $this->{level} - 1}->{ $this->{lastkey} }->{$key};
      }
    }

    $prevkey = $this->{prevkey};
  }

  $value =~ s{$this->{regex}}{
    my $con = $1;
    my $var = $3;
    my $var_lc = $this->{LowerCaseNames} ? lc($var) : $var;
    if (exists $this->{stack}->{ $this->{level} }->{ $prevkey }->{$var_lc}) {
      $con . $this->{stack}->{ $this->{level} }->{ $prevkey }->{$var_lc};
    }
    elsif ($this->{InterPolateEnv}) {
      # may lead to vulnerabilities, by default flag turned off
      if (defined($ENV{$var})) {
	$con . $ENV{$var};
      }
      else {
	$con;
      }
    }
    else {
      if ($this->{StrictVars}) {
	croak "Use of uninitialized variable (\$$var) while loading config entry: $key = $value\n";
      }
      else {
	# be cool
	$con;
      }
    }
  }egx;

  $this->{stack}->{ $this->{level} }->{ $prevkey }->{$key} = $value;

  return $value;
};


sub _interpolate_hash {
  #
  # interpolate a complete hash and keep the results
  # on the varstack.
  #
  # called directly by Config::General::new()
  #
  my ($this, $config) = @_;

  $this->{level}     = 1;
  $this->{upperkey}  = "";
  $this->{upperkeys} = [];
  $this->{lastkey}   = "";
  $this->{prevkey}   = " ";

  $config = $this->_var_hash_stacker($config);

  $this->{level}    = 1;
  $this->{upperkey} = "";
  $this->{upperkeys} = [];
  $this->{lastkey}  = "";
  $this->{prevkey}  = " ";

  return $config;
}

sub _var_hash_stacker {
  #
  # build a varstack of a given hash ref
  #
  my ($this, $config) = @_;

  foreach my $key (keys %{$config}) {
    if (ref($config->{$key}) eq "ARRAY" ) {
      $this->{level}++;
      $this->_savelast($key);
      $config->{$key} = $this->_var_array_stacker($config->{$key}, $key);
      $this->_backlast($key);
      $this->{level}--;
    }
    elsif (ref($config->{$key}) eq "HASH") {
      $this->{level}++;
      $this->_savelast($key);
      $config->{$key} = $this->_var_hash_stacker($config->{$key});
      $this->_backlast($key);
      $this->{level}--;
    }
    else {
      # SCALAR
      $config->{$key} = $this->_interpolate($key, $config->{$key});
    }
  }

  return $config;
}


sub _var_array_stacker {
  #
  # same as _var_hash_stacker but for arrayrefs
  #
  my ($this, $config, $key) = @_;

  my @new;

  foreach my $entry (@{$config}) {
    if (ref($entry) eq "HASH") {
      $entry = $this->_var_hash_stacker($entry);
    }
    elsif (ref($entry) eq "ARRAY") {
      # ignore this. Arrays of Arrays cannot be created/supported
      # with Config::General, because they are not accessible by
      # any key (anonymous array-ref)
      next;
    }
    else {
      $entry = $this->_interpolate($key, $entry);
    }
    push @new, $entry;
  }

  return  \@new;
}


1;

__END__


=head1 NAME

Config::General::Interpolated - Parse variables within Config files


=head1 SYNOPSIS

 use Config::General;
 $conf = new Config::General(
    -ConfigFile      => 'configfile',
    -InterPolateVars => 1
 );

=head1 DESCRIPTION

This is an internal module which makes it possible to interpolate
perl style variables in your config file (i.e. C<$variable>
or C<${variable}>).

Normally you don't call it directly.


=head1 VARIABLES

Variables can be defined everywhere in the config and can be used
afterwards as the value of an option. Variables cannot be used as
keys or as part of keys.

If you define a variable inside
a block or a named block then it is only visible within this block or
within blocks which are defined inside this block. Well - let's take a
look to an example:

 # sample config which uses variables
 basedir   = /opt/ora
 user      = t_space
 sys       = unix
 <table intern>
     instance  = INTERN
     owner     = $user                 # "t_space"
     logdir    = $basedir/log          # "/opt/ora/log"
     sys       = macos
     <procs>
         misc1   = ${sys}_${instance}  # macos_INTERN
         misc2   = $user               # "t_space"
     </procs>
 </table>

This will result in the following structure:

 {
     'basedir' => '/opt/ora',
     'user'    => 't_space'
     'sys'     => 'unix',
     'table'   => {
	  'intern' => {
	        'sys'      => 'macos',
	        'logdir'   => '/opt/ora/log',
	        'instance' => 'INTERN',
	        'owner' => 't_space',
	        'procs' => {
		     'misc1' => 'macos_INTERN',
		     'misc2' => 't_space'
            }
	 }
     }

As you can see, the variable B<sys> has been defined twice. Inside
the <procs> block a variable ${sys} has been used, which then were
interpolated into the value of B<sys> defined inside the <table>
block, not the sys variable one level above. If sys were not defined
inside the <table> block then the "global" variable B<sys> would have
been used instead with the value of "unix".

Variables inside double quotes will be interpolated, but variables
inside single quotes will B<not> interpolated. This is the same
behavior as you know of perl itself.

In addition you can surround variable names with curly braces to
avoid misinterpretation by the parser.

=head1 SEE ALSO

L<Config::General>

=head1 AUTHORS

 Thomas Linden <tlinden |AT| cpan.org>
 Autrijus Tang <autrijus@autrijus.org>
 Wei-Hon Chen <plasmaball@pchome.com.tw>

=head1 COPYRIGHT

Copyright 2001 by Wei-Hon Chen E<lt>plasmaball@pchome.com.twE<gt>.
Copyright 2002-2007 by Thomas Linden <tlinden |AT| cpan.org>.

This program is free software; you can redistribute it and/or
modify it under the same terms as Perl itself.

See L<http://www.perl.com/perl/misc/Artistic.html>

=head1 VERSION

2.08

=cut

