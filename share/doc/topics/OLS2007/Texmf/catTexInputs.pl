#!/usr/bin/perl

# Purpose:
#   Combine all the \input{author-subsection} statements in a paper
#   so as to make copyediting and global search/replace easier.
#
# Usage:
#   cat author.tex | catTexInputs.pl > new-author.tex
#

my $ln, $infile, $infileP, $l;

while (defined($ln = <STDIN>)) {
    if ($ln =~ m/^\s*\\input\s*{([^}]+)}.*/) {
	$infileP = $1;
	chomp $infileP;  # kill any annoying spaces for "\input { foo }"
	# could be foo/bar, bar, foo/bar.tex etc.
	if ($infileP =~ m/\.tex$/) {
	    $infile = $infileP;
	} else {
	    $infile = $infileP . '.tex';
	}

	print "\n%%% $ln\n";

	open(OUT,"<$infile") || die "cannot open $infile";
	while (defined($l = <OUT>)) {
	    print $l;
	}
	close(OUT);
    } else {
	print $ln;
    }
}
