package SNMP::Trapinfo;

use 5.008004;
use strict;
use warnings;
use Carp;
use Safe;		# Safe module, creates a compartment for eval's and tests for disabled commands

our $VERSION = '1.0';

sub AUTOLOAD {
        my $self = shift;
        my $attr = our $AUTOLOAD;
        $attr =~ s/.*:://;
        return if $attr =~ /^DESTROY$/;
        if (@_) {
                return $self->{$attr} = shift;
        } else {
                return $self->{$attr};
        }
}

sub new {
	my ($class, $data) = @_;
	croak "Must specify data source (either GLOB or scalar ref)" unless $data;
	my $self = { 
			data => {},
			P => [],
			V => [],
			};
	$self = bless $self, $class;

	return $self->read($data);
}

sub trapname {
	my $self = shift;
	my $trapname = $self->data->{"SNMPv2-MIB::snmpTrapOID"};
	return $trapname || undef;
}

sub packet {
	my $self = shift;
	if ($_[0] && ref \$_[0] eq "SCALAR") {
		return $self->{packet} = shift;
	}
	my $opts;
	$opts = shift if (ref $_[0] eq "HASH");
	$_ = $self->{packet};
	if ($opts->{hide_passwords}) {
		s/\nSNMP-COMMUNITY-MIB::snmpTrapCommunity.0 "(.*?)"/\nSNMP-COMMUNITY-MIB::snmpTrapCommunity.0 "*****"/;
	}
	return $_;
}

sub expand {
	my $self = shift;
	my $string = shift;
	return "" if ! defined $string;
	my $key;
	while ( ($key) = ($string =~ /\${([\w\-\.\*:]+)}/) ) {
		my $newval;
		my ($action, $line) = $key =~ /^([PV])(\d+)?$/;
		if ($action && $line) {
			$newval = $self->$action($line) || "(null)";
		} elsif ($key eq "DUMP") {
			my %h = %{$self->data};
			delete $h{"SNMP-COMMUNITY-MIB::snmpTrapCommunity"};
			$newval = join(" ", map {"$_=".$self->data->{$_}} (sort keys %h) );
		} elsif ($key eq "TRAPNAME") {
			$newval = $self->trapname;
		} elsif ($key eq "HOSTIP") {
			$newval = $self->hostip;
		} else {
			if ($key =~ /\*/) {
				$newval = $self->match_key($key) || "(null)";
			} else {
				$newval = $self->data->{$key} || "(null)";
			}
		}

		# Must use same match as while loop
		# Otherwise possible infinite loop
		# though not sure why (see tests for examples)
		#$string =~ s/\${$key}/$newval/;
		$string =~ s/\${([\w\-\.\*:]+)}/$newval/;	

	}
	return $string;
}

# Initialise the Safe compartment once
# Operators can be listed as follows
# perl -MOpcode=opdump -e 'opdump'
#
# http://search.cpan.org/~nwclark/perl-5.8.8/ext/Opcode/Opcode.pm
#
# We want to allow // m// s/// && || ! !~ != >= > == < <= =~ lt gt le ge ne eq not and or + - % * x .
#
my $cmp = new Safe;
$cmp->permit_only( qw( :base_core :base_mem :base_loop print sprintf prtf padsv padav padhv padany  ) );

sub eval {
	my ($self, $string) = @_;
	my $code = $self->expand($string);
	$self->last_eval_string($code);

	my $rc = $cmp->reval("$code", 1);	# ($code to run, 1 for 'use strict;')
	if ($@) { 
		return undef;
	} else {
		return $rc ? 1 : 0;
	}
}

sub match_key {
	my ($self, $key) = @_;
	my @parts = split('\.', $key);
	POSSIBLE: foreach my $possible (keys %{$self->data}) {
		my @possible = split('\.', $possible);
		next unless @possible == @parts;
		for (my $i=0; $i < @parts; $i++) {
			next if ($parts[$i] eq "*");
			if ($parts[$i] ne $possible[$i]) {
				next POSSIBLE;
			}
		}
		return $self->data->{$possible};
	}
	return undef;
}

sub cleanup_string {
	my $self = shift;
	my $string = shift;
	# This is an SNMP OID name...
	if ($string =~ /^[A-Za-z].*\:\:[A-Za-z].*$/) {
		# Drop single trailing digits
		if (! ($string =~ /\d\.\d+$/)) {
			$string =~ s/\.\d+$//;
		}
	}
	# Remove trailing spaces
	$string =~ s/ +$//;
	return $string;
}

sub read {
	my ($self, $data) = @_;
	if (ref \$data eq "GLOB") {
		local $/="#---next trap---#\n"; 
		$self->{packet} = <$data> || return undef;
		chomp($self->{packet});
	} elsif (ref \$data eq "REF") {
		$self->{packet} = $$data;
	} else {
		croak "Bad ref";
	}
	$self->{packet} =~ s/\n*$//;
	my @packet = split("\n", $self->{packet});
	chomp($_ = shift @packet);
	$self->hostname($_);
	$self->{P}->[0] = $_;

	return undef if (!@packet);	# No IP address given. This is a malformed packet

	chomp($_ = shift @packet);
	$self->{P}->[1] = $_;
	# Extra stuff around the IP packet in Net-SNMP 5.2.1
	s/^.*\[//;
	s/\].*$//;
	$self->hostip($_);

	my $i = 1;	# Start at 1 because want to increment array at beginning because of next
	foreach $_ (@packet) {
		$i++;
		# Ignore spaces in middle
		my ($key, $value) = /^([^ ]+) +([^ ].*)$/;
		# If syntax is wrong, ignore this line
		next unless defined $key;
		$key = $self->cleanup_string($key);
		if ($key ne "SNMPv2-MIB::snmpTrapOID") {
			$value = $self->cleanup_string($value);
		}
		$self->data->{$key} = $value;
		$key =~ s/^[^:]+:://;
		$self->{P}->[$i] = $key;
		$self->{V}->[$i] = $value;
	}
	return $self;
}

sub fully_translated {
	my $self = shift;
	if ($self->trapname =~ /\.\d+$/) {
		return 0;
	} else {
		return 1;
	}
}

sub P {
	my ($self, $line) = @_;
	$_ = $self->{P}->[--$line];
	$_ = "" unless defined $_;
	return $_;
}

sub V {
	my ($self, $line) = @_;
	$_ = $self->{V}->[--$line];
	$_ = "" unless defined $_;
	return $_;
}

1;
__END__

=head1 NAME

SNMP::Trapinfo - Read and process an SNMP trap from Net-SNMP's snmptrapd

=head1 SYNOPSIS

  use SNMP::Trapinfo;
  $trap = SNMP::Trapinfo->new(*STDIN);

  open F, ">> /tmp/trap.log";
  print F $trap->packet;
  close F;

  if (! defined $trap->trapname) {
    die "No trapname in packet";
  } elsif ($trap->trapname eq "IF-MIB::linkUp" or $trap->trapname eq "IF-MIB::linkDown") {
    # $mailer is a Mail::Mailer object, for example
    print $mailer "Received trap :", $trap->trapname, $/,
      "From host: ", $trap->hostname, $/,
      "Message: ", $trap->expand('Interface ${V5} received ${TRAPNAME}'), $/;
  } else {
    # not expected trap
  }

  # Do some complex evaluation of the packet
  my $result = $trap->eval('"${IF-MIB::ifType}" eq "ppp" && ${IF-MIB::ifIndex} < 5');
  if ($result) {
    print "Got a trap for ppp where index is less than 5", $/;
  } elsif ($result == 0) {
    print "Packet not desired", $/;
  } else {
    print "Error evaluating: " . $trap->last_eval_string . "; result: $@", $/;
  }

=head1 DESCRIPTION

This module allows the user to get to the useful parts of an snmptrapd
packet, as provided by the Net-SNMP software (http://www.net-snmp.org). 
You can evaluate the packet to match whatever rules you define and then 
take whatever action with the packet, such as sending an email, post an 
IM or submit it as a passive check to Nagios (http://www.nagios.org).

Rules are defined as little perl snippets of code - run using the eval method. 
You use macros to pull out specific bits of the trap to then evaluate against. 
See the expand method for the macro definitions.

=head1 IMPLEMENTATION

=over 4

=item 1

Create your perl script (such as the example above).

=item 2

Edit snmptrapd.conf so that the default traphandle calls your perl script.

=item 3

Startup snmptrapd and let it do all the OID translations (no -On option) and let it
do hostname translations (no -n option).

=item 4

Create a trap and check that it has been received and processed
correctly.

=back

=head1 METHODS

=over 4

=item SNMP::Trapinfo->new(*STDIN)

Reads STDIN, expecting input from snmptrapd, and returns the object holding all the information 
about this packet. An example packet is:

  cisco2611.lon.altinity
  192.168.10.20
  SNMPv2-MIB::sysUpTime.0 9:16:47:53.80
  SNMPv2-MIB::snmpTrapOID.0 IF-MIB::linkUp
  IF-MIB::ifIndex.2 2
  IF-MIB::ifDescr.2 Serial0/0
  IF-MIB::ifType.2 ppp
  SNMPv2-SMI::enterprises.9.2.2.1.1.20.2 "PPP LCP Open"
  SNMP-COMMUNITY-MIB::snmpTrapAddress.0 192.168.10.20
  SNMP-COMMUNITY-MIB::snmpTrapCommunity.0 "public"
  SNMPv2-MIB::snmpTrapEnterprise.0 SNMPv2-SMI::enterprises.9.1.186

Any trailing linefeeds will be stripped.

Apart from the first two lines, expects each line to be of the format: key value. If not, then will silently ignore
the line.

If you want to use multiple packets within a stream, you have to put a marker in between
each trap: "#---next trap---#\n". Then call SNMP::Trapinfo->new(*STDIN) again. Will receive an undef if 
there are no more packets to read or the packet is malformed (such as no IP on the 2nd line).

=item SNMP::Trapinfo->new(\$data)

Instead of a filehandle, can specify a scalar reference that holds the packet data.

=item hostname

Returns the first line of the packet, which should be the hostname as
resolved by snmptrapd.

=item hostip

Returns the IP address in the 2nd line of the packet, which should be the 
originating host.

=item trapname

Returns the value of the parameter SNMPv2-MIB::snmpTrapOID. In the
example above, this method would return IF-MIB::linkUp. 

If the SNMPv2-MIB::snmpTrapOID is not found, then will return undef.
This could mean that the MIB for snmpTrapOID has not been loaded.

=item fully_translated

Returns 0 if the trapname has more than 1 set of trailing digits
(a single .\d+ would be removed automatically) - this would mean that a
MIB is missing. Otherwise returns 1.

=item packet( {hide_passwords => 1} )

Returns a scalar with the full packet, as originally received. If hide_passwords is specified,
will replace the value of snmpTrapCommunity.0 with 5 asterisks.

=item data

Returns a hash ref where the keys consist of the SNMP parameter and
the values are the string values of thos parameters. For the example 
trap above, a Data::Dumper of $trap->data would give:

  $VAR1 = {
          'SNMPv2-MIB::snmpTrapEnterprise' => 'SNMPv2-SMI::enterprises.9.1.186',
          'SNMP-COMMUNITY-MIB::snmpTrapAddress' => '192.168.10.20',
          'IF-MIB::ifType' => 'ppp',
          'IF-MIB::ifIndex' => '2',
          'SNMPv2-MIB::snmpTrapOID' => 'IF-MIB::linkUp',
          'IF-MIB::ifDescr' => 'Serial0/0',
          'SNMP-COMMUNITY-MIB::snmpTrapCommunity' => '"public"',
          'SNMPv2-MIB::sysUpTime' => '9:16:47:53.80',
          'SNMPv2-SMI::enterprises.9.2.2.1.1.20.2' => '"PPP LCP Open"'
        };

=item expand($string)

Takes $string and expands it so that macros within the string will be expanded
out based on the packet details. Available macros are:

=over 4

=item *

${Px} - Returns the parameter for line x

=item *

${Vx} - Returns the value for line x

=item *

${TRAPNAME} - Returns the trapname (as called from $trap->trapname)

=item *

${HOSTIP} - Returns the IP of the originating packet

=item *

${IF-MIB::ifType} - Returns the value for the specified parameter. 

=item *

${SNMPv2-SMI::enterprises.9.*.2.1.1.20.2} - Returns the value for the specified parameter. The use of the wildcard 
means any value can be in that dot area. If there are multiple matches, there is no guarantee which one is returned.
This is only really for MIBs that have variables within the OID - in this particular case, there is a missing MIB file. 
Multiple *s can be used.

=item *

${DUMP} - Returns all key, value pairs (stripping out snmpTrapCommunity)

=back

For the example trap above, if you ran:

  $trap->expand('Port ${IF-MIB::ifIndex} (${P7}=${V7}) is Up with message ${V8}'); 

this would return:

  Port 2 (ifType=ppp) is Up with message "PPP LCP Open"

=item eval($string)

$string is passed into expand to expand any macros. Then the entire string is eval'd.
This method is useful for creating SNMP rules, using perl syntax. Will return 1 if true,
0 if false, or undef if eval failure ($@ will be set with the error).

For the example trap above, if you ran:

  $trap->eval('"${IF-MIB::ifType}" eq "ppp" && ${IF-MIB::ifIndex} < 5');

this would expand to 

  "ppp" eq "ppp" && 2 < 5

and this would return 1.

The perl code executed is run in a Safe compartment so only numeric comparisons or regexps 
are allowed. Other calls, such as open or system, will return undef with the error in $@

=item last_eval_string

Returns the last string used in an eval, with all macros expanded. Useful for debugging

=back

=head1 VERSION NUMBERING

After a brief flirtation with 3 digit version numbering, I've changed back to X.YY 
format as perlmodstyle recommends.

=head1 REFERENCES

Net-SNMP - http://www.net-snmp.org. This module has been tested on versions 
5.1.2 and 5.2.1.

=head1 AUTHOR

Ton Voon, E<lt>ton.voon@altinity.comE<gt>

=head1 CREDITS

Thanks to Brand Hilton for documentation suggestions and Rob Moss for integrating Safe.pm.

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2006 by Altinity Limited

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.


=cut
