#!/bin/env perl

my $line;

while ( $line = <> )
{
	if ( $line =~ m/file: FSAL handle=([0-9A-Fa-f]{8})([0-9A-Fa-f]+)/ )
	{
		my $old_handle_head=$1;
		my $old_handle_next=$2;

		# 1) the first 8 bytes are now on 16 bytes
		# 2) extra padding at the end of the handle: 8 bytes
		print "file: FSAL handle=$1"."00000000".$2."00000000";
		
	}
	else
	{
		print $line;
	}

}
