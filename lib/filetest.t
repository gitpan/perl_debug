#!./perl

BEGIN {
	chdir 't' if -d 't';
	@INC = '../lib';
}

use Test::More tests => 15;

# these two should be kept in sync with the pragma itself
# if hint bits are changed there, other things *will* break
my $hint_bits = 0x00400000;
my $error = "filetest: the only implemented subpragma is 'access'.\n";

# can't use it yet, because of the import death
ok( require filetest, 'required pragma successfully' );

# and here's one culprit, right here
eval { filetest->import('bad subpragma') };
is( $@, $error, 'filetest dies with bad subpragma on import' );

is( $^H & $hint_bits, 0, 'hint bits not set without pragma in place' );

# now try the normal usage
# can't check $^H here; it's lexically magic (see perlvar)
# the test harness unintentionally hoards the goodies for itself
use_ok( 'filetest', 'access' );

# and import again, to see it here
filetest->import('access');
ok( $^H & $hint_bits, 'hint bits set with pragma loaded' );

# and now get rid of it
filetest->unimport('access');
is( $^H & $hint_bits, 0, 'hint bits not set with pragma unimported' );

eval { filetest->unimport() };
is( $@, $error, 'filetest dies without subpragma on unimport' );

# there'll be a compilation aborted failure here, with the eval string
eval "no filetest 'fake pragma'";
like( $@, qr/^$error/, 'filetest dies with bad subpragma on unuse' );

eval "use filetest 'bad subpragma'";
like( $@, qr/^$error/, 'filetest dies with bad subpragma on use' );

eval "use filetest";
like( $@, qr/^$error/, 'filetest dies with missing subpragma on use' );

eval "no filetest";
like( $@, qr/^$error/, 'filetest dies with missing subpragma on unuse' );

SKIP: {
    # A real test for filetest.
    # This works for systems with /usr/bin/chflags (i.e. BSD4.4 systems).
    my $chflags = "/usr/bin/chflags";
    my $tstfile = "filetest.tst";
    skip("No $chflags available", 4) if !-x $chflags;

    skip("Test does not work on OpenBSD and BSD/OS", 4)
	if $^O =~ /^(?:openbsd|bsdos)$/;

  SKIP: {
	eval {
	    if (!-e $tstfile) {
		open(T, ">$tstfile") or die "Can't create $tstfile: $!";
		close T;
	    }
	    system($chflags, "uchg", $tstfile);
	    die "Can't exec $chflags uchg" if $? != 0;
	};
	skip("Errors in test using chflags: $@", 4) if $@;

	{
	    use filetest 'access';
	    is(-w $tstfile, undef, "$tstfile should not be recognized as writable");
	    is(-W $tstfile, undef, "$tstfile should not be recognized as writable");
	}
	{
	    no filetest 'access';
	    is(-w $tstfile, 1, "$tstfile should be recognized as writable");
	    is(-W $tstfile, 1, "$tstfile should be recognized as writable");
	}
    }

    # cleanup
    system($chflags, "nouchg", $tstfile);
    unlink $tstfile;
    warn "Can't remove $tstfile: $!" if -e $tstfile;
}
