#!/usr/bin/perl 
open(FILE,">test.txt")  || die "Cannot open test.txt:$!";
print FILE "foobar";
close FILE;
open(FILE,">>test.txt")  || die "Cannot open test.txt:$!";
print 'not ' if defined sysseek(FILE, -1, 1);
print "ok 39\n";
if (tell(FILE) == 6)
{ print "ok 28\n"; } else { print "not ok 28\n"; }
close FILE;
