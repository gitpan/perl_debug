# Original based on info from
# Carl M. Fongheiser <cmf@ins.infonet.net>
# Date: Thu, 28 Jul 1994 19:17:05 -0500 (CDT)
#
# Additional 1.1.5 defines from 
# Ollivier Robert <Ollivier.Robert@keltia.frmug.fr.net>
# Date: Wed, 28 Sep 1994 00:37:46 +0100 (MET)
#
# Additional 2.* defines from
# Ollivier Robert <Ollivier.Robert@keltia.frmug.fr.net>
# Date: Sat, 8 Apr 1995 20:53:41 +0200 (MET DST)
#
# Additional 2.0.5 and 2.1 defined from
# Ollivier Robert <Ollivier.Robert@keltia.frmug.fr.net>
# Date: Fri, 12 May 1995 14:30:38 +0200 (MET DST)
#
# Additional 2.2 defines from
# Mark Murray <mark@grondar.za>
# Date: Wed, 6 Nov 1996 09:44:58 +0200 (MET)
#
# Modified to ensure we replace -lc with -lc_r, and
# to put in place-holders for various specific hints.
# Andy Dougherty <doughera@lafayette.edu>
# Date: Tue Mar 10 16:07:00 EST 1998
#
# Support for FreeBSD/ELF
# Ollivier Robert <roberto@keltia.freenix.fr>
# Date: Wed Sep  2 16:22:12 CEST 1998
#
# The two flags "-fpic -DPIC" are used to indicate a
# will-be-shared object.  Configure will guess the -fpic, (and the
# -DPIC is not used by perl proper) but the full define is included to 
# be consistent with the FreeBSD general shared libs building process.
#
# setreuid and friends are inherently broken in all versions of FreeBSD
# before 2.1-current (before approx date 4/15/95). It is fixed in 2.0.5
# and what-will-be-2.1
#

case "$osvers" in
0.*|1.0*)
	usedl="$undef"
	;;
1.1*)
	malloctype='void *'
	groupstype='int'
	d_setregid='undef'
	d_setreuid='undef'
	d_setrgid='undef'
	d_setruid='undef'
	;;
2.0-release*)
	d_setregid='undef'
	d_setreuid='undef'
	d_setrgid='undef'
	d_setruid='undef'
	;;
#
# Trying to cover 2.0.5, 2.1-current and future 2.1/2.2
# It does not covert all 2.1-current versions as the output of uname
# changed a few times.
#
# Even though seteuid/setegid are available, they've been turned off
# because perl isn't coded with saved set[ug]id variables in mind.
# In addition, a small patch is requried to suidperl to avoid a security
# problem with FreeBSD.
#
2.0.5*|2.0-built*|2.1*)
 	usevfork='true'
	case "$usemymalloc" in
	    "") usemymalloc='n'
	        ;;
	esac
	d_setregid='define'
	d_setreuid='define'
	d_setegid='undef'
	d_seteuid='undef'
	test -r ./broken-db.msg && . ./broken-db.msg
	;;
#
# 2.2 and above have phkmalloc(3).
# don't use -lmalloc (maybe there's an old one from 1.1.5.1 floating around)
2.2*)
 	usevfork='true'
	case "$usemymalloc" in
	    "") usemymalloc='n'
	        ;;
	esac
	libswanted=`echo $libswanted | sed 's/ malloc / /'`
	libswanted=`echo $libswanted | sed 's/ bind / /'`
	# iconv gone in Perl 5.8.1, but if someone compiles 5.8.0 or earlier.
	libswanted=`echo $libswanted | sed 's/ iconv / /'`
	d_setregid='define'
	d_setreuid='define'
	d_setegid='define'
	d_seteuid='define'
	# d_dosuid='define' # Obsolete.
	;;
*)	usevfork='true'
	case "$usemymalloc" in
	    "") usemymalloc='n'
	        ;;
	esac
	libswanted=`echo $libswanted | sed 's/ malloc / /'`
	;;
esac

# Dynamic Loading flags have not changed much, so they are separated
# out here to avoid duplicating them everywhere.
case "$osvers" in
0.*|1.0*) ;;

1*|2*)	cccdlflags='-DPIC -fpic'
	lddlflags="-Bshareable $lddlflags"
	;;

*)
        objformat=`/usr/bin/objformat`
        if [ x$objformat = xelf ]; then
            libpth="/usr/lib /usr/local/lib"
            glibpth="/usr/lib /usr/local/lib"
            ldflags="-Wl,-E "
            lddlflags="-shared "
        else
            if [ -e /usr/lib/aout ]; then
                libpth="/usr/lib/aout /usr/local/lib /usr/lib"
                glibpth="/usr/lib/aout /usr/local/lib /usr/lib"
            fi
            lddlflags='-Bshareable'
        fi
        cccdlflags='-DPIC -fPIC'
        ;;
esac

case "$osvers" in
0*|1*|2*|3*) ;;

*)
	ccflags="${ccflags} -DHAS_FPSETMASK -DHAS_FLOATINGPOINT_H"
	if /usr/bin/file -L /usr/lib/libc.so | /usr/bin/grep -vq "not stripped" ; then
	    usenm=false
	fi
        ;;
esac

cat <<'EOM' >&4

Some users have reported that Configure halts when testing for
the O_NONBLOCK symbol with a syntax error.  This is apparently a
sh error.  Rerunning Configure with ksh apparently fixes the
problem.  Try
	ksh Configure [your options]

EOM

# From: Anton Berezin <tobez@plab.ku.dk>
# To: perl5-porters@perl.org
# Subject: [PATCH 5.005_54] Configure - hints/freebsd.sh signal handler type
# Date: 30 Nov 1998 19:46:24 +0100
# Message-ID: <864srhhvcv.fsf@lion.plab.ku.dk>

signal_t='void'
d_voidsig='define'

# set libperl.so.X.X for 2.2.X
case "$osvers" in
2.2*)
    # unfortunately this code gets executed before
    # the equivalent in the main Configure so we copy a little
    # from Configure XXX Configure should be fixed.
    if $test -r $src/patchlevel.h;then
       patchlevel=`awk '/define[ 	]+PERL_VERSION/ {print $3}' $src/patchlevel.h`
       subversion=`awk '/define[ 	]+PERL_SUBVERSION/ {print $3}' $src/patchlevel.h`
    else
       patchlevel=0
       subversion=0
    fi
    libperl="libperl.so.$patchlevel.$subversion"
    unset patchlevel
    unset subversion
    ;;
esac

# This script UU/usethreads.cbu will get 'called-back' by Configure 
# after it has prompted the user for whether to use threads.
cat > UU/usethreads.cbu <<'EOCBU'
case "$usethreads" in
$define|true|[yY]*)
        lc_r=`/sbin/ldconfig -r|grep ':-lc_r'|awk '{print $NF}'|sed -n '$p'`
        case "$osvers" in  
	0*|1*|2.0*|2.1*)   cat <<EOM >&4
I did not know that FreeBSD $osvers supports POSIX threads.

Feel free to tell perlbug@perl.org otherwise.
EOM
	      exit 1
	      ;;

        2.2.[0-7]*)
              cat <<EOM >&4
POSIX threads are not supported well by FreeBSD $osvers.

Please consider upgrading to at least FreeBSD 2.2.8,
or preferably to the most recent -RELEASE or -STABLE
version (see http://www.freebsd.org/releases/).

(While 2.2.7 does have pthreads, it has some problems
 with the combination of threads and pipes and therefore
 many Perl tests will either hang or fail.)
EOM
	      exit 1
	      ;;

	*)
	      if [ ! -r "$lc_r" ]; then
	      cat <<EOM >&4
POSIX threads should be supported by FreeBSD $osvers --
but your system is missing the shared libc_r.
(/sbin/ldconfig -r doesn't find any).

Consider using the latest STABLE release.
EOM
		 exit 1
	      fi
	      case "$osvers" in
	      # 500016 is the first osreldate in which one could
	      # just link against libc_r without disposing of libc
	      # at the same time.  500016 ... up to whatever it was
	      # on the 31st of August 2003 can still be used with -pthread,
	      # but it is not necessary.
	      5.*)	if [ `/sbin/sysctl -n kern.osreldate` -lt 500016 ]; then
                                ldflags="-pthread $ldflags"
                        fi
			;;
	      *)	ldflags="-pthread $ldflags"
			;;
	      esac
	      # Both in 4.x and 5.x gethostbyaddr_r exists but
	      # it is "Temporary function, not threadsafe"...
	      # Presumably earlier it didn't even exist.
	      d_gethostbyaddr_r="undef"
	      d_gethostbyaddr_r_proto="0"
	      ;;

	esac

	set `echo X "$libswanted "| sed -e 's/ c / c_r /'`
	shift
	libswanted="$*"
	# Configure will probably pick the wrong libc to use for nm scan.
	# The safest quick-fix is just to not use nm at all...
	usenm=false

        case "$osvers" in
        2.2.8*)
            # ... but this does not apply for 2.2.8 - we know it's safe
            libc="$lc_r"
            usenm=true
           ;;
        esac

        unset lc_r

	# Even with the malloc mutexes the Perl malloc does not
	# seem to be threadsafe in FreeBSD?
	case "$usemymalloc" in
	'') usemymalloc=n ;;
	esac
esac
EOCBU

