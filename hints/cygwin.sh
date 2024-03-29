#! /bin/sh
# cygwin.sh - hints for building perl using the Cygwin environment for Win32
#

# not otherwise settable
exe_ext='.exe'
firstmakefile='GNUmakefile'
case "$ldlibpthname" in
'') ldlibpthname=PATH ;;
esac
archobjs='cygwin.o'

# mandatory (overrides incorrect defaults)
test -z "$cc" && cc='gcc'
if test -z "$plibpth"
then
    plibpth=`gcc -print-file-name=libc.a`
    plibpth=`dirname $plibpth`
    plibpth=`cd $plibpth && pwd`
fi
so='dll'
# - eliminate -lc, implied by gcc and a symlink to libcygwin.a
libswanted=`echo " $libswanted " | sed -e 's/ c / /g'`
# - eliminate -lm, symlink to libcygwin.a
libswanted=`echo " $libswanted " | sed -e 's/ m / /g'`
# - add libgdbm_compat $libswanted
# - libcygipc doesn't work much at all with
#   the Perl SysV IPC tests so not adding it --jhi 2003-08-09
libswanted="$libswanted gdbm_compat"
test -z "$optimize" && optimize='-O2'
ccflags="$ccflags -DPERL_USE_SAFE_PUTENV"
# - otherwise i686-cygwin
archname='cygwin'

# dynamic loading
# - otherwise -fpic
cccdlflags=' '
ld='ld2'

# Win9x problem with non-blocking read from a closed pipe
d_eofnblk='define'

# build debug versions of exe's and dll's
ldflags="$ldflags -g"
ccdlflags="$ccdlflags -g"
lddlflags="$lddlflags -g"
