#
# $Id$
#
# This script is written intentionally in Shift JIS
# -- dankogai

BEGIN {
    if ($ENV{'PERL_CORE'}){
        chdir 't';
        unshift @INC, '../lib';
    }
    require Config; import Config;
    if ($Config{'extensions'} !~ /\bEncode\b/) {
      print "1..0 # Skip: Encode was not built\n";
      exit 0;
    }
    if (ord("A") == 193) {
        print "1..0 # Skip: EBCDIC\n";
        exit 0;
    }
    unless (PerlIO::Layer->find('perlio')){
        print "1..0 # Skip: PerlIO required\n";
        exit 0;
    }
    eval 'use Encode';
    if ($@ =~ /dynamic loading not available/) {
        print "1..0 # Skip: no dynamic loading, no Encode\n";
        exit 0;
    }
    $| = 1;
}

use strict;
#use Test::More qw(no_plan);
use Test::More tests => 6;
use Encode;
use encoding 'shiftjis';

my @hiragana =  map {chr} ord("ぁ")..ord("ん");
my @katakana =  map {chr} ord("ァ")..ord("ン");
my $hiragana = join('' => @hiragana);
my $katakana = join('' => @katakana);
my %h2k; @h2k{@hiragana} = @katakana;
my %k2h; @k2h{@katakana} = @hiragana;

# print @hiragana, "\n";

my $str;

$str = $hiragana; $str =~ tr/ぁ-ん/ァ-ン/;
is($str, $katakana, "tr// # hiragana -> katakana");
$str = $katakana; $str =~ tr/ァ-ン/ぁ-ん/;
is($str, $hiragana, "tr// # hiragana -> katakana");

$str = $hiragana; eval qq(\$str =~ tr/ぁ-ん/ァ-ン/);
is($str, $katakana, "eval qq(tr//) # hiragana -> katakana");
$str = $katakana; eval qq(\$str =~ tr/ァ-ン/ぁ-ん/);
is($str, $hiragana, "eval qq(tr//) # hiragana -> katakana");

$str = $hiragana; $str =~ s/([ぁ-ん])/$h2k{$1}/go;
is($str, $katakana, "s/// # hiragana -> katakana");
$str = $katakana; $str =~ s/([ァ-ン])/$k2h{$1}/go;
is($str, $hiragana, "s/// # hiragana -> katakana");
__END__
