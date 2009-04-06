#
# Config::General::Extended - special Class based on Config::General
#
# Copyright (c) 2000-2007 Thomas Linden <tlinden |AT| cpan.org>.
# All Rights Reserved. Std. disclaimer applies.
# Artificial License, same as perl itself. Have fun.
#

# namespace
package Config::General::Extended;

# yes we need the hash support of new() in 1.18 or higher!
use Config::General 1.18;

use FileHandle;
use Carp;
use Exporter ();
use vars qw(@ISA @EXPORT);

# inherit new() and so on from Config::General
@ISA = qw(Config::General Exporter);

use strict;


$Config::General::Extended::VERSION = "2.02";


sub new {
  croak "Deprecated method Config::General::Extended::new() called.\n"
       ."Use Config::General::new() instead and set the -ExtendedAccess flag.\n";
}


sub obj {
  #
  # returns a config object from a given key
  # or from the current config hash if the $key does not exist
  # or an empty object if the content of $key is empty.
  #
  my($this, $key) = @_;

  # just create the empty object, just in case
  my $empty = $this->SUPER::new( -ExtendedAccess => 1, -ConfigHash => {}, %{$this->{Params}} );

  if (exists $this->{config}->{$key}) {
    if (!$this->{config}->{$key}) {
      # be cool, create an empty object!
      return $empty
    }
    elsif (ref($this->{config}->{$key}) eq "ARRAY") {
      my @objlist;
      foreach my $element (@{$this->{config}->{$key}}) {
	if (ref($element) eq "HASH") {
	  push @objlist,
	    $this->SUPER::new( -ExtendedAccess => 1,
			       -ConfigHash     => $element,
			       %{$this->{Params}} );
	}
	else {
	  if ($this->{StrictObjects}) {
	    croak "element in list \"$key\" does not point to a hash reference!\n";
	  }
	  # else: skip this element
	}
      }
      return \@objlist;
    }
    elsif (ref($this->{config}->{$key}) eq "HASH") {
      return $this->SUPER::new( -ExtendedAccess => 1,
				-ConfigHash => $this->{config}->{$key}, %{$this->{Params}} );
    }
    else {
      # nothing supported
      if ($this->{StrictObjects}) {
	croak "key \"$key\" does not point to a hash reference!\n";
      }
      else {
	# be cool, create an empty object!
	return $empty;
      }
    }
  }
  else {
    # even return an empty object if $key does not exist
    return $empty;
  }
}


sub value {
  #
  # returns a value of the config hash from a given key
  # this can be a hashref or a scalar
  #
  my($this, $key, $value) = @_;
  if (defined $value) {
    $this->{config}->{$key} = $value;
  }
  else {
    if (exists $this->{config}->{$key}) {
      return $this->{config}->{$key};
    }
    else {
      if ($this->{StrictObjects}) {
	croak "Key \"$key\" does not exist within current object\n";
      }
      else {
	return "";
      }
    }
  }
}


sub hash {
  #
  # returns a value of the config hash from a given key
  # as hash
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    return %{$this->{config}->{$key}};
  }
  else {
    if ($this->{StrictObjects}) {
      croak "Key \"$key\" does not exist within current object\n";
    }
    else {
      return ();
    }
  }
}


sub array {
  #
  # returns a value of the config hash from a given key
  # as array
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    return @{$this->{config}->{$key}};
  }
  if ($this->{StrictObjects}) {
      croak "Key \"$key\" does not exist within current object\n";
    }
  else {
    return ();
  }
}



sub is_hash {
  #
  # return true if the given key contains a hashref
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    if (ref($this->{config}->{$key}) eq "HASH") {
      return 1;
    }
    else {
      return;
    }
  }
  else {
    return;
  }
}



sub is_array {
  #
  # return true if the given key contains an arrayref
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    if (ref($this->{config}->{$key}) eq "ARRAY") {
      return 1;
    }
    else {
      return;
    }
  }
  else {
    return;
  }
}


sub is_scalar {
  #
  # returns true if the given key contains a scalar(or number)
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key} && !ref($this->{config}->{$key})) {
    return 1;
  }
  return;
}



sub exists {
  #
  # returns true if the key exists
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    return 1;
  }
  else {
    return;
  }
}


sub keys {
  #
  # returns all keys under in the hash of the specified key, if
  # it contains keys (so it must be a hash!)
  #
  my($this, $key) = @_;
  if (!$key) {
    if (ref($this->{config}) eq "HASH") {
      return map { $_ } keys %{$this->{config}};
    }
    else {
      return ();
    }
  }
  elsif (exists $this->{config}->{$key} && ref($this->{config}->{$key}) eq "HASH") {
    return map { $_ } keys %{$this->{config}->{$key}};
  }
  else {
    return ();
  }
}


sub delete {
  #
  # delete the given key from the config, if any
  # and return what is deleted (just as 'delete $hash{key}' does)
  #
  my($this, $key) = @_;
  if (exists $this->{config}->{$key}) {
    return delete $this->{config}->{$key};
  }
  else {
    return undef;
  }
}


#
# removed, use save() of General.pm now
# sub save {
#  #
#  # save the config back to disk
#  #
#  my($this,$file) = @_;
#  my $fh = new FileHandle;
#
#  if (!$file) {
#    $file = $this->{configfile};
#  }
#
#  $this->save_file($file);
# }


sub configfile {
  #
  # sets or returns the config filename
  #
  my($this,$file) = @_;
  if ($file) {
    $this->{configfile} = $file;
  }
  return $this->{configfile};
}



sub AUTOLOAD {
  #
  # returns the representing value, if it is a scalar.
  #
  my($this, $value) = @_;
  my $key = $Config::General::Extended::AUTOLOAD;  # get to know how we were called
  $key =~ s/.*:://; # remove package name!

  if ($value) {
    # just set $key to $value!
    $this->{config}->{$key} = $value;
  }
  elsif (exists $this->{config}->{$key}) {
    if ($this->is_hash($key)) {
      croak "Key \"$key\" points to a hash and cannot be automatically accessed\n";
    }
    elsif ($this->is_array($key)) {
      croak "Key \"$key\" points to an array and cannot be automatically accessed\n";
    }
    else {
      return $this->{config}->{$key};
    }
  }
  else {
    if ($this->{StrictObjects}) {
      croak "Key \"$key\" does not exist within current object\n";
    }
    else {
      # be cool
      return "";
    }
  }
}

sub DESTROY {
  my $this = shift;
  $this = ();
}

# keep this one
1;





=head1 NAME

Config::General::Extended - Extended access to Config files


=head1 SYNOPSIS

 use Config::General;

 $conf = new Config::General(
    -ConfigFile     => 'configfile',
    -ExtendedAccess => 1
 );

=head1 DESCRIPTION

This is an internal module which makes it possible to use object
oriented methods to access parts of your config file.

Normally you don't call it directly.

=head1 METHODS

=over

=item configfile('filename')

Set the filename to be used by B<save> to "filename". It returns the current
configured filename if called without arguments.


=item obj('key')

Returns a new object (of Config::General::Extended Class) from the given key.
Short example:
Assume you have the following config:

 <individual>
      <martin>
         age   23
      </martin>
      <joseph>
         age   56
      </joseph>
 </individual>
 <other>
      blah     blubber
      blah     gobble
      leer
 </other>

and already read it in using B<Config::General::Extended::new()>, then you can get a
new object from the "individual" block this way:

 $individual = $conf->obj("individual");

Now if you call B<getall> on I<$individual> (just for reference) you would get:

 $VAR1 = (
    martin => { age => 13 }
         );

Or, here is another use:

 my $individual = $conf->obj("individual");
 foreach my $person ($conf->keys("individual")) {
    $man = $individual->obj($person);
    print "$person is " . $man->value("age") . " years old\n";
 }

See the discussion on B<hash()> and B<value()> below.

If the key from which you want to create a new object is empty, an empty
object will be returned. If you run the following on the above config:

 $obj = $conf->obj("other")->obj("leer");

Then $obj will be empty, just like if you have had run this:

 $obj = new Config::General::Extended( () );

Read operations on this empty object will return nothing or even fail.
But you can use an empty object for I<creating> a new config using write
operations, i.e.:

 $obj->someoption("value");

See the discussion on B<AUTOLOAD METHODS> below.

If the key points to a list of hashes, a list of objects will be
returned. Given the following example config:

 <option>
   name = max
 </option>
 <option>
   name = bea
 </option>

you could write code like this to access the list the OOP way:

 my $objlist = $conf->obj("option");
 foreach my $option (@{$objlist}) {
  print $option->name;
 }

Please note that the list will be returned as a reference to an array.

Empty elements or non-hash elements of the list, if any, will be skipped.

=item hash('key')

This method returns a hash(if it B<is> one!) from the config which is referenced by
"key". Given the sample config above you would get:

 my %sub_hash = $conf->hash("individual");
 print Dumper(\%sub_hash);
 $VAR1 = {
    martin => { age => 13 }
         };

=item array('key')

This the equivalent of B<hash()> mentioned above, except that it returns an array.
Again, we use the sample config mentioned above:

 $other = $conf->obj("other");
 my @blahs = $other->array("blah");
 print Dumper(\@blahs);
 $VAR1 = [ "blubber", "gobble" ];


=item value('key')

This method returns the scalar value of a given key. Given the following sample
config:

 name  = arthur
 age   = 23

you could do something like that:

 print $conf->value("name") . " is " . $conf->value("age") . " years old\n";



You can use this method also to set the value of "key" to something if you give over
a hash reference, array reference or a scalar in addition to the key. An example:

 $conf->value("key", \%somehash);
 # or
 $conf->value("key", \@somearray);
 # or
 $conf->value("key", $somescalar);

Please note, that this method does not complain about existing values within "key"!

=item is_hash('key') is_array('key') is_scalar('key')

As seen above, you can access parts of your current config using hash, array or scalar
methods. But you are right if you guess, that this might become problematic, if
for example you call B<hash()> on a key which is in real not a hash but a scalar. Under
normal circumstances perl would refuse this and die.

To avoid such behavior you can use one of the methods is_hash() is_array() is_scalar() to
check if the value of "key" is really what you expect it to be.

An example(based on the config example from above):

 if($conf->is_hash("individual") {
    $individual = $conf->obj("individual");
 }
 else {
    die "You need to configure a "individual" block!\n";
 }


=item exists('key')

This method returns just true if the given key exists in the config.


=item keys('key')

Returns an array of the keys under the specified "key". If you use the example
config above you yould do that:

 print Dumper($conf->keys("individual");
 $VAR1 = [ "martin", "joseph" ];

If no key name was supplied, then the keys of the object itself will be returned.

You can use this method in B<foreach> loops as seen in an example above(obj() ).


=item delete ('key')

This method removes the given key and all associated data from the internal
hash structure. If 'key' contained data, then this data will be returned,
otherwise undef will be returned.

=back


=head1 AUTOLOAD METHODS

Another usefull feature is implemented in this class using the B<AUTOLOAD> feature
of perl. If you know the keynames of a block within your config, you can access to
the values of each individual key using the method notation. See the following example
and you will get it:

We assume the following config:

 <person>
    name    = Moser
    prename = Peter
    birth   = 12.10.1972
 </person>

Now we read it in and process it:

 my $conf = new Config::General::Extended("configfile");
 my $person = $conf->obj("person");
 print $person->prename . " " . $person->name . " is " . $person->age . " years old\n";

This notation supports only scalar values! You need to make sure, that the block
<person> does not contain any subblock or multiple identical options(which will become
an array after parsing)!

If you access a non-existent key this way, Config::General will croak an error.
You can turn this behavior off by setting B<-StrictObjects> to 0 or "no". In
this case undef will be returned.

Of course you can use this kind of methods for writing data too:

 $person->name("Neustein");

This changes the value of the "name" key to "Neustein". This feature behaves exactly like
B<value()>, which means you can assign hash or array references as well and that existing
values under the given key will be overwritten.


=head1 COPYRIGHT

Copyright (c) 2000-2007 Thomas Linden

This library is free software; you can redistribute it and/or
modify it under the same terms as Perl itself.


=head1 BUGS

none known yet.


=head1 AUTHOR

Thomas Linden <tlinden |AT| cpan.org>

=head1 VERSION

2.02

=cut

