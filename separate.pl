#!/usr/bin/perl

my $shortver = "5.8.1";

open (FULL_LIST, "<pkg.files");
open (MANS_LIST, ">pkg.files.manpages");
open (PERL_LIST, ">pkg.files.perl");

while (<FULL_LIST>) {
	if ($_ =~ m#^usr/share/man/#) {
		print MANS_LIST $_;
	} elsif (($_ =~ m#^usr/lib/perl5/#) || ($_ =~ m#^usr/bin/#) || ($_ =~ m#^usr/share/doc/#)) {
		print PERL_LIST $_;
	} else {
		next;
	}
}
close FULL_LIST;
close MANS_LIST;
close PERL_LIST;

