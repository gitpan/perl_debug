# This file was created by configpm when Perl was built. Any changes
# made to this file will be lost the next time perl is built.

package Config;
@EXPORT = qw(%Config);
@EXPORT_OK = qw(myconfig config_sh config_vars config_re);

my %Export_Cache = map {($_ => 1)} (@EXPORT, @EXPORT_OK);

# Define our own import method to avoid pulling in the full Exporter:
sub import {
    my $pkg = shift;
    @_ = @EXPORT unless @_;

    my @funcs = grep $_ ne '%Config', @_;
    my $export_Config = @funcs < @_ ? 1 : 0;

    my $callpkg = caller(0);
    foreach my $func (@funcs) {
	die sprintf qq{"%s" is not exported by the %s module\n},
	    $func, __PACKAGE__ unless $Export_Cache{$func};
	*{$callpkg.'::'.$func} = \&{$func};
    }

    *{"$callpkg\::Config"} = \%Config if $export_Config;
    return;
}

die "Perl lib version (v5.8.1) doesn't match executable version ($])"
    unless $^V;

$^V eq v5.8.1
    or die "Perl lib version (v5.8.1) doesn't match executable version (" .
	sprintf("v%vd",$^V) . ")";

##
## This file was produced by running the Configure script. It holds all the
## definitions figured out by Configure. Should you modify one of these values,
## do not forget to propagate your changes by running "Configure -der". You may
## instead choose to run each of the .SH files by yourself, or "Configure -S".
##
#
## Package name      : perl5
## Source directory  : /tmp/perl/perl_debug-5.8.1
## Configuration time: Fri Sep 26 23:22:09 EDT 2003
## Configured by     : #gerrit
## Target system     : cygwin_nt-4.0 loreley 1.5.5(0.9432) 2003-09-20 16:31 i686 unknown unknown cygwin 
#
## Configure command line arguments.
#PERL_PATCHLEVEL=
## Variables propagated from previous config.sh file.

our $summary : unique = <<'!END!';
Summary of my $package (revision $baserev $version_patchlevel_string) configuration:
  Platform:
    osname=$osname, osvers=$osvers, archname=$archname
    uname='$myuname'
    config_args='$config_args'
    hint=$hint, useposix=$useposix, d_sigaction=$d_sigaction
    usethreads=$usethreads use5005threads=$use5005threads useithreads=$useithreads usemultiplicity=$usemultiplicity
    useperlio=$useperlio d_sfio=$d_sfio uselargefiles=$uselargefiles usesocks=$usesocks
    use64bitint=$use64bitint use64bitall=$use64bitall uselongdouble=$uselongdouble
    usemymalloc=$usemymalloc, bincompat5005=undef
  Compiler:
    cc='$cc', ccflags ='$ccflags',
    optimize='$optimize',
    cppflags='$cppflags'
    ccversion='$ccversion', gccversion='$gccversion', gccosandvers='$gccosandvers'
    intsize=$intsize, longsize=$longsize, ptrsize=$ptrsize, doublesize=$doublesize, byteorder=$byteorder
    d_longlong=$d_longlong, longlongsize=$longlongsize, d_longdbl=$d_longdbl, longdblsize=$longdblsize
    ivtype='$ivtype', ivsize=$ivsize, nvtype='$nvtype', nvsize=$nvsize, Off_t='$lseektype', lseeksize=$lseeksize
    alignbytes=$alignbytes, prototype=$prototype
  Linker and Libraries:
    ld='$ld', ldflags ='$ldflags'
    libpth=$libpth
    libs=$libs
    perllibs=$perllibs
    libc=$libc, so=$so, useshrplib=$useshrplib, libperl=$libperl
    gnulibc_version='$gnulibc_version'
  Dynamic Linking:
    dlsrc=$dlsrc, dlext=$dlext, d_dlsymun=$d_dlsymun, ccdlflags='$ccdlflags'
    cccdlflags='$cccdlflags', lddlflags='$lddlflags'

!END!
my $summary_expanded = 0;

sub myconfig {
    return $summary if $summary_expanded;
    $summary =~ s{\$(\w+)}
		 { my $c = $Config{$1}; defined($c) ? $c : 'undef' }ge;
    $summary_expanded = 1;
    $summary;
}

our $Config_SH : unique = <<'!END!';
archlibexp='/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int'
archname='cygwin-thread-multi-64int'
cc='gcc'
ccflags='-DPERL_USE_SAFE_PUTENV -DDEBUGGING -fno-strict-aliasing -I/usr/local/include'
cppflags='-DPERL_USE_SAFE_PUTENV -DDEBUGGING -fno-strict-aliasing -I/usr/local/include'
dlsrc='dl_dlopen.xs'
dynamic_ext='B ByteLoader Cwd DB_File Data/Dumper Devel/DProf Devel/PPPort Devel/Peek Digest/MD5 Encode Fcntl File/Glob Filter/Util/Call GDBM_File I18N/Langinfo IO List/Util MIME/Base64 NDBM_File ODBM_File Opcode POSIX PerlIO/encoding PerlIO/scalar PerlIO/via SDBM_File Socket Storable Sys/Hostname Sys/Syslog Time/HiRes Unicode/Normalize XS/APItest XS/Typemap attrs re threads threads/shared'
installarchlib='/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int'
installprivlib='/usr/lib/perl5/5.8.1'
libpth='/usr/local/lib /lib /usr/lib'
libs='-lgdbm -ldb -lcrypt -lutil -lgdbm_compat'
osname='cygwin'
osvers='1.5.5(0.9432)'
prefix='/usr'
privlibexp='/usr/lib/perl5/5.8.1'
sharpbang='#!'
shsharp='true'
so='dll'
startsh='#!/bin/sh'
static_ext=' '
Author=''
CONFIG='true'
Date='$Date'
Header=''
Id='$Id'
Locker=''
Log='$Log'
Mcc='Mcc'
PATCHLEVEL='8'
PERL_API_REVISION='5'
PERL_API_SUBVERSION='0'
PERL_API_VERSION='8'
PERL_CONFIG_SH='true'
PERL_REVISION='5'
PERL_SUBVERSION='1'
PERL_VERSION='8'
RCSfile='$RCSfile'
Revision='$Revision'
SUBVERSION='1'
Source=''
State=''
_a='.a'
_exe='.exe'
_o='.o'
afs='false'
afsroot='/afs'
alignbytes='8'
ansi2knr=''
aphostname=''
api_revision='5'
api_subversion='0'
api_version='8'
api_versionstring='5.8.0'
ar='ar'
archlib='/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int'
archname64='64int'
archobjs='cygwin.o'
asctime_r_proto='REENTRANT_PROTO_B_SB'
awk='awk'
baserev='5.0'
bash=''
bin='/usr/bin'
binexp='/usr/bin'
bison='bison'
byacc='byacc'
byteorder='12345678'
c=''
castflags='0'
cat='cat'
cccdlflags=' '
ccdlflags=' -g'
ccflags_uselargefiles=''
ccname='gcc'
ccsymbols='__CYGWIN32__=1 __CYGWIN__=1 __GNUC_MINOR__=3 __GNUC_PATCHLEVEL__=1 __GNUC__=3 __unix=1 __unix__=1 unix=1'
ccversion=''
cf_by='#gerrit'
cf_email='gp@familiehaase.de'
cf_time='Fri Sep 26 23:22:09 EDT 2003'
charsize='1'
chgrp=''
chmod='chmod'
chown=''
clocktype='clock_t'
comm='comm'
compress=''
config_arg0='/tmp/perl/perl_debug-5.8.1/Configure'
config_arg1='-de'
config_arg2='-Dmksymlinks'
config_arg3='-Duse64bitint'
config_arg4='-Dusethreads'
config_arg5='-Doptimize=-g -O2'
config_arg6='-Dman3ext=3pm'
config_argc='6'
config_args='-de -Dmksymlinks -Duse64bitint -Dusethreads -Doptimize=-g -O2 -Dman3ext=3pm'
contains='grep'
cp='cp'
cpio=''
cpp='cpp'
cpp_stuff='42'
cppccsymbols=''
cpplast='-'
cppminus='-'
cpprun='gcc -E'
cppstdin='gcc -E'
cppsymbols=''
crypt_r_proto='0'
cryptlib=''
csh='csh'
ctermid_r_proto='0'
ctime_r_proto='REENTRANT_PROTO_B_SB'
d_Gconvert='sprintf((b),"%.*g",(n),(x))'
d_PRIEUldbl='define'
d_PRIFUldbl='define'
d_PRIGUldbl='define'
d_PRIXU64='define'
d_PRId64='define'
d_PRIeldbl='define'
d_PRIfldbl='define'
d_PRIgldbl='define'
d_PRIi64='define'
d_PRIo64='define'
d_PRIu64='define'
d_PRIx64='define'
d_SCNfldbl='define'
d__fwalk='undef'
d_access='define'
d_accessx='undef'
d_aintl='undef'
d_alarm='define'
d_archlib='define'
d_asctime_r='define'
d_atolf='undef'
d_atoll='undef'
d_attribut='define'
d_bcmp='define'
d_bcopy='define'
d_bsd='define'
d_bsdgetpgrp='undef'
d_bsdsetpgrp='undef'
d_bzero='define'
d_casti32='undef'
d_castneg='define'
d_charvspr='define'
d_chown='define'
d_chroot='define'
d_chsize='undef'
d_class='undef'
d_closedir='define'
d_cmsghdr_s='undef'
d_const='define'
d_copysignl='undef'
d_crypt='define'
d_crypt_r='undef'
d_csh='undef'
d_ctermid_r='undef'
d_ctime_r='define'
d_cuserid='define'
d_dbl_dig='define'
d_dbminitproto='define'
d_difftime='define'
d_dirfd='define'
d_dirnamlen='undef'
d_dlerror='define'
d_dlopen='define'
d_dlsymun='undef'
d_dosuid='undef'
d_drand48_r='undef'
d_drand48proto='define'
d_dup2='define'
d_eaccess='undef'
d_endgrent='define'
d_endgrent_r='undef'
d_endhent='define'
d_endhostent_r='undef'
d_endnent='undef'
d_endnetent_r='undef'
d_endpent='define'
d_endprotoent_r='undef'
d_endpwent='define'
d_endpwent_r='undef'
d_endsent='define'
d_endservent_r='undef'
d_eofnblk='define'
d_eunice='undef'
d_faststdio='define'
d_fchdir='define'
d_fchmod='define'
d_fchown='define'
d_fcntl='define'
d_fcntl_can_lock='define'
d_fd_macros='define'
d_fd_set='define'
d_fds_bits='define'
d_fgetpos='define'
d_finite='define'
d_finitel='undef'
d_flexfnam='define'
d_flock='undef'
d_flockproto='undef'
d_fork='define'
d_fp_class='undef'
d_fpathconf='define'
d_fpclass='undef'
d_fpclassify='undef'
d_fpclassl='undef'
d_fpos64_t='undef'
d_frexpl='undef'
d_fs_data_s='undef'
d_fseeko='define'
d_fsetpos='define'
d_fstatfs='define'
d_fstatvfs='undef'
d_fsync='define'
d_ftello='define'
d_ftime='undef'
d_getcwd='define'
d_getespwnam='undef'
d_getfsstat='undef'
d_getgrent='define'
d_getgrent_r='undef'
d_getgrgid_r='undef'
d_getgrnam_r='undef'
d_getgrps='define'
d_gethbyaddr='define'
d_gethbyname='define'
d_gethent='undef'
d_gethname='define'
d_gethostbyaddr_r='undef'
d_gethostbyname_r='undef'
d_gethostent_r='undef'
d_gethostprotos='define'
d_getitimer='define'
d_getlogin='define'
d_getlogin_r='undef'
d_getmnt='undef'
d_getmntent='define'
d_getnbyaddr='undef'
d_getnbyname='undef'
d_getnent='undef'
d_getnetbyaddr_r='undef'
d_getnetbyname_r='undef'
d_getnetent_r='undef'
d_getnetprotos='define'
d_getpagsz='define'
d_getpbyname='define'
d_getpbynumber='define'
d_getpent='define'
d_getpgid='define'
d_getpgrp2='undef'
d_getpgrp='define'
d_getppid='define'
d_getprior='undef'
d_getprotobyname_r='undef'
d_getprotobynumber_r='undef'
d_getprotoent_r='undef'
d_getprotoprotos='define'
d_getprpwnam='undef'
d_getpwent='define'
d_getpwent_r='undef'
d_getpwnam_r='define'
d_getpwuid_r='define'
d_getsbyname='define'
d_getsbyport='define'
d_getsent='define'
d_getservbyname_r='undef'
d_getservbyport_r='undef'
d_getservent_r='undef'
d_getservprotos='define'
d_getspnam='undef'
d_getspnam_r='undef'
d_gettimeod='define'
d_gmtime_r='define'
d_gnulibc='undef'
d_grpasswd='define'
d_hasmntopt='undef'
d_htonl='define'
d_ilogbl='undef'
d_index='undef'
d_inetaton='define'
d_int64_t='define'
d_isascii='define'
d_isfinite='undef'
d_isinf='define'
d_isnan='define'
d_isnanl='undef'
d_killpg='define'
d_lchown='define'
d_ldbl_dig='define'
d_link='define'
d_localtime_r='define'
d_locconv='define'
d_lockf='undef'
d_longdbl='define'
d_longlong='define'
d_lseekproto='define'
d_lstat='define'
d_madvise='undef'
d_mblen='define'
d_mbstowcs='define'
d_mbtowc='define'
d_memchr='define'
d_memcmp='define'
d_memcpy='define'
d_memmove='define'
d_memset='define'
d_mkdir='define'
d_mkdtemp='undef'
d_mkfifo='define'
d_mkstemp='define'
d_mkstemps='undef'
d_mktime='define'
d_mmap='define'
d_modfl='undef'
d_modfl_pow32_bug='undef'
d_modflproto='undef'
d_mprotect='define'
d_msg='undef'
d_msg_ctrunc='undef'
d_msg_dontroute='define'
d_msg_oob='define'
d_msg_peek='define'
d_msg_proxy='undef'
d_msgctl='undef'
d_msgget='undef'
d_msghdr_s='define'
d_msgrcv='undef'
d_msgsnd='undef'
d_msync='define'
d_munmap='define'
d_mymalloc='define'
d_nice='define'
d_nl_langinfo='define'
d_nv_preserves_uv='undef'
d_off64_t='undef'
d_old_pthread_create_joinable='undef'
d_oldpthreads='undef'
d_oldsock='undef'
d_open3='define'
d_pathconf='define'
d_pause='define'
d_perl_otherlibdirs='undef'
d_phostname='undef'
d_pipe='define'
d_poll='define'
d_portable='define'
d_procselfexe='undef'
d_pthread_atfork='define'
d_pthread_attr_setscope='undef'
d_pthread_yield='undef'
d_pwage='undef'
d_pwchange='undef'
d_pwclass='undef'
d_pwcomment='define'
d_pwexpire='undef'
d_pwgecos='define'
d_pwpasswd='define'
d_pwquota='undef'
d_qgcvt='undef'
d_quad='define'
d_random_r='undef'
d_readdir64_r='undef'
d_readdir='define'
d_readdir_r='undef'
d_readlink='define'
d_readv='define'
d_recvmsg='define'
d_rename='define'
d_rewinddir='define'
d_rmdir='define'
d_safebcpy='undef'
d_safemcpy='undef'
d_sanemcmp='define'
d_sbrkproto='define'
d_scalbnl='undef'
d_sched_yield='define'
d_scm_rights='undef'
d_seekdir='define'
d_select='define'
d_sem='undef'
d_semctl='undef'
d_semctl_semid_ds='undef'
d_semctl_semun='undef'
d_semget='undef'
d_semop='undef'
d_sendmsg='define'
d_setegid='define'
d_seteuid='define'
d_setgrent='define'
d_setgrent_r='undef'
d_setgrps='define'
d_sethent='define'
d_sethostent_r='undef'
d_setitimer='define'
d_setlinebuf='define'
d_setlocale='define'
d_setlocale_r='undef'
d_setnent='undef'
d_setnetent_r='undef'
d_setpent='define'
d_setpgid='define'
d_setpgrp2='undef'
d_setpgrp='define'
d_setprior='undef'
d_setproctitle='undef'
d_setprotoent_r='undef'
d_setpwent='define'
d_setpwent_r='undef'
d_setregid='define'
d_setresgid='undef'
d_setresuid='undef'
d_setreuid='define'
d_setrgid='undef'
d_setruid='undef'
d_setsent='define'
d_setservent_r='undef'
d_setsid='define'
d_setvbuf='define'
d_sfio='undef'
d_shm='undef'
d_shmat='undef'
d_shmatprototype='undef'
d_shmctl='undef'
d_shmdt='undef'
d_shmget='undef'
d_sigaction='define'
d_sigprocmask='define'
d_sigsetjmp='define'
d_sockatmark='undef'
d_sockatmarkproto='undef'
d_socket='define'
d_socklen_t='define'
d_sockpair='define'
d_socks5_init='undef'
d_sqrtl='undef'
d_srand48_r='undef'
d_srandom_r='undef'
d_sresgproto='undef'
d_sresuproto='undef'
d_statblks='define'
d_statfs_f_flags='undef'
d_statfs_s='define'
d_statvfs='undef'
d_stdio_cnt_lval='define'
d_stdio_ptr_lval='define'
d_stdio_ptr_lval_nochange_cnt='define'
d_stdio_ptr_lval_sets_cnt='undef'
d_stdio_stream_array='undef'
d_stdiobase='define'
d_stdstdio='define'
d_strchr='define'
d_strcoll='define'
d_strctcpy='define'
d_strerrm='strerror(e)'
d_strerror='define'
d_strerror_r='define'
d_strftime='define'
d_strtod='define'
d_strtol='define'
d_strtold='define'
d_strtoll='define'
d_strtoq='undef'
d_strtoul='define'
d_strtoull='define'
d_strtouq='undef'
d_strxfrm='define'
d_suidsafe='undef'
d_symlink='define'
d_syscall='undef'
d_syscallproto='undef'
d_sysconf='define'
d_sysernlst=''
d_syserrlst='define'
d_system='define'
d_tcgetpgrp='define'
d_tcsetpgrp='define'
d_telldir='define'
d_telldirproto='define'
d_time='define'
d_times='define'
d_tm_tm_gmtoff='undef'
d_tm_tm_zone='undef'
d_tmpnam_r='undef'
d_truncate='define'
d_ttyname_r='undef'
d_tzname='define'
d_u32align='define'
d_ualarm='define'
d_umask='define'
d_uname='define'
d_union_semun='undef'
d_unordered='undef'
d_usleep='define'
d_usleepproto='define'
d_ustat='undef'
d_vendorarch='undef'
d_vendorbin='undef'
d_vendorlib='undef'
d_vendorscript='undef'
d_vfork='undef'
d_void_closedir='undef'
d_voidsig='define'
d_voidtty=''
d_volatile='define'
d_vprintf='define'
d_wait4='define'
d_waitpid='define'
d_wcstombs='define'
d_wctomb='define'
d_writev='define'
d_xenix='undef'
date='date'
db_hashtype='u_int32_t'
db_prefixtype='size_t'
db_version_major='4'
db_version_minor='1'
db_version_patch='25'
defvoidused='15'
direntrytype='struct dirent'
dlext='dll'
doublesize='8'
drand01='drand48()'
drand48_r_proto='0'
eagain='EAGAIN'
ebcdic='undef'
echo='echo'
egrep='egrep'
emacs=''
endgrent_r_proto='0'
endhostent_r_proto='0'
endnetent_r_proto='0'
endprotoent_r_proto='0'
endpwent_r_proto='0'
endservent_r_proto='0'
eunicefix=':'
exe_ext='.exe'
expr='expr'
extensions='B ByteLoader Cwd DB_File Data/Dumper Devel/DProf Devel/PPPort Devel/Peek Digest/MD5 Encode Fcntl File/Glob Filter/Util/Call GDBM_File I18N/Langinfo IO List/Util MIME/Base64 NDBM_File ODBM_File Opcode POSIX PerlIO/encoding PerlIO/scalar PerlIO/via SDBM_File Socket Storable Sys/Hostname Sys/Syslog Time/HiRes Unicode/Normalize XS/APItest XS/Typemap attrs re threads threads/shared Errno'
extras=''
fflushNULL='define'
fflushall='undef'
find=''
firstmakefile='GNUmakefile'
flex=''
fpossize='8'
fpostype='fpos_t'
freetype='void'
from=':'
full_ar='/bin/ar'
full_csh='csh'
full_sed='/bin/sed'
gccansipedantic=''
gccosandvers=''
gccversion='3.3.1 (gph 77)'
getgrent_r_proto='0'
getgrgid_r_proto='0'
getgrnam_r_proto='0'
gethostbyaddr_r_proto='0'
gethostbyname_r_proto='0'
gethostent_r_proto='0'
getlogin_r_proto='0'
getnetbyaddr_r_proto='0'
getnetbyname_r_proto='0'
getnetent_r_proto='0'
getprotobyname_r_proto='0'
getprotobynumber_r_proto='0'
getprotoent_r_proto='0'
getpwent_r_proto='0'
getpwnam_r_proto='REENTRANT_PROTO_I_CSBWR'
getpwuid_r_proto='REENTRANT_PROTO_I_TSBWR'
getservbyname_r_proto='0'
getservbyport_r_proto='0'
getservent_r_proto='0'
getspnam_r_proto='0'
gidformat='"lu"'
gidsign='1'
gidsize='4'
gidtype='gid_t'
glibpth='/usr/shlib  /lib /usr/lib /usr/lib/386 /lib/386 /usr/ccs/lib /usr/ucblib /usr/local/lib '
gmake='gmake'
gmtime_r_proto='REENTRANT_PROTO_S_TS'
gnulibc_version=''
grep='grep'
groupcat='cat /etc/group'
groupstype='gid_t'
gzip='gzip'
h_fcntl='false'
h_sysfile='true'
hint='recommended'
hostcat='cat /etc/hosts'
html1dir=' '
html1direxp=''
html3dir=' '
html3direxp=''
i16size='2'
i16type='short'
i32size='4'
i32type='long'
i64size='8'
i64type='long long'
i8size='1'
i8type='char'
i_arpainet='define'
i_bsdioctl=''
i_crypt='define'
i_db='define'
i_dbm='define'
i_dirent='define'
i_dld='undef'
i_dlfcn='define'
i_fcntl='undef'
i_float='define'
i_fp='undef'
i_fp_class='undef'
i_gdbm='define'
i_grp='define'
i_ieeefp='define'
i_inttypes='define'
i_langinfo='define'
i_libutil='undef'
i_limits='define'
i_locale='define'
i_machcthr='undef'
i_malloc='define'
i_math='define'
i_memory='undef'
i_mntent='define'
i_ndbm='define'
i_netdb='define'
i_neterrno='undef'
i_netinettcp='define'
i_niin='define'
i_poll='define'
i_prot='undef'
i_pthread='define'
i_pwd='define'
i_rpcsvcdbm='undef'
i_sfio='undef'
i_sgtty='undef'
i_shadow='undef'
i_socks='undef'
i_stdarg='define'
i_stddef='define'
i_stdlib='define'
i_string='define'
i_sunmath='undef'
i_sysaccess='undef'
i_sysdir='undef'
i_sysfile='define'
i_sysfilio='undef'
i_sysin='undef'
i_sysioctl='define'
i_syslog='define'
i_sysmman='define'
i_sysmode='undef'
i_sysmount='define'
i_sysndir='undef'
i_sysparam='define'
i_sysresrc='define'
i_syssecrt='undef'
i_sysselct='define'
i_syssockio='undef'
i_sysstat='define'
i_sysstatfs='define'
i_sysstatvfs='undef'
i_systime='define'
i_systimek='undef'
i_systimes='define'
i_systypes='define'
i_sysuio='define'
i_sysun='define'
i_sysutsname='define'
i_sysvfs='define'
i_syswait='define'
i_termio='undef'
i_termios='define'
i_time='undef'
i_unistd='define'
i_ustat='undef'
i_utime='define'
i_values='undef'
i_varargs='undef'
i_varhdr='stdarg.h'
i_vfork='undef'
ignore_versioned_solibs=''
inc_version_list=' '
inc_version_list_init='0'
incpath=''
inews=''
installbin='/usr/bin'
installhtml1dir=''
installhtml3dir=''
installman1dir='/usr/share/man/man1'
installman3dir='/usr/share/man/man3'
installprefix='/usr'
installprefixexp='/usr'
installscript='/usr/bin'
installsitearch='/usr/lib/perl5/site_perl/5.8.1/cygwin-thread-multi-64int'
installsitebin='/usr/bin'
installsitehtml1dir=''
installsitehtml3dir=''
installsitelib='/usr/lib/perl5/site_perl/5.8.1'
installsiteman1dir='/usr/share/man/man1'
installsiteman3dir='/usr/share/man/man3'
installsitescript='/usr/bin'
installstyle='lib/perl5'
installusrbinperl='undef'
installvendorarch=''
installvendorbin=''
installvendorhtml1dir=''
installvendorhtml3dir=''
installvendorlib=''
installvendorman1dir=''
installvendorman3dir=''
installvendorscript=''
intsize='4'
issymlink='/bin/test -h'
ivdformat='"lld"'
ivsize='8'
ivtype='long long'
known_extensions='B ByteLoader Cwd DB_File Data/Dumper Devel/DProf Devel/PPPort Devel/Peek Digest/MD5 Encode Fcntl File/Glob Filter/Util/Call GDBM_File I18N/Langinfo IO IPC/SysV List/Util MIME/Base64 NDBM_File ODBM_File Opcode POSIX PerlIO/encoding PerlIO/scalar PerlIO/via SDBM_File Socket Storable Sys/Hostname Sys/Syslog Thread Time/HiRes Unicode/Normalize XS/APItest XS/Typemap attrs re threads threads/shared'
ksh=''
ld='ld2'
lddlflags=' -g -L/usr/local/lib'
ldflags=' -g -L/usr/local/lib'
ldflags_uselargefiles=''
ldlibpthname='PATH'
less='less'
lib_ext='.a'
libc='/usr/lib/libc.a'
libperl='libperl.a'
libsdirs=' /lib'
libsfiles=' libgdbm.a libdb.a libcrypt.a libutil.a libgdbm_compat.a'
libsfound=' /lib/libgdbm.a /lib/libdb.a /lib/libcrypt.a /lib/libutil.a /lib/libgdbm_compat.a'
libspath=' /usr/local/lib /lib /usr/lib'
libswanted='  sfio socket bind inet nsl nm ndbm gdbm dbm db malloc dl dld ld sun crypt sec util cposix posix ucb bsd BSD   gdbm_compat'
libswanted_uselargefiles=''
line=''
lint=''
lkflags=''
ln='ln'
lns='/bin/ln.exe -s'
localtime_r_proto='REENTRANT_PROTO_S_TS'
locincpth='/usr/local/include /opt/local/include /usr/gnu/include /opt/gnu/include /usr/GNU/include /opt/GNU/include'
loclibpth='/usr/local/lib /opt/local/lib /usr/gnu/lib /opt/gnu/lib /usr/GNU/lib /opt/GNU/lib'
longdblsize='12'
longlongsize='8'
longsize='4'
lp=''
lpr=''
ls='ls'
lseeksize='8'
lseektype='off_t'
mail=''
mailx=''
make='make'
make_set_make='#'
mallocobj='malloc.o'
mallocsrc='malloc.c'
malloctype='void *'
man1dir='/usr/share/man/man1'
man1direxp='/usr/share/man/man1'
man1ext='1'
man3dir='/usr/share/man/man3'
man3direxp='/usr/share/man/man3'
man3ext='3pm'
mips_type=''
mistrustnm=''
mkdir='mkdir'
mmaptype='void *'
modetype='mode_t'
more='more'
multiarch='undef'
mv=''
myarchname='i686-cygwin'
mydomain='.familiehaase.de'
myhostname='loreley'
myuname='cygwin_nt-4.0 loreley 1.5.5(0.9432) 2003-09-20 16:31 i686 unknown unknown cygwin '
n='-n'
need_va_copy='undef'
netdb_hlen_type='int'
netdb_host_type='const char *'
netdb_name_type='const char *'
netdb_net_type='long'
nm='nm'
nm_opt='-p'
nm_so_opt=''
nonxs_ext='Errno'
nroff='nroff'
nvEUformat='"E"'
nvFUformat='"F"'
nvGUformat='"G"'
nv_preserves_uv_bits='53'
nveformat='"e"'
nvfformat='"f"'
nvgformat='"g"'
nvsize='8'
nvtype='double'
o_nonblock='O_NONBLOCK'
obj_ext='.o'
old_pthread_create_joinable=''
optimize='-g -O2'
orderlib='false'
otherlibdirs=' '
package='perl5'
pager='/bin/less'
passcat='cat /etc/passwd'
patchlevel='8'
path_sep=':'
perl5=''
perl=''
perl_patchlevel=''
perladmin='gp@familiehaase.de'
perllibs='-lcrypt -lutil -lgdbm_compat'
perlpath='/usr/bin/perl'
pg='pg'
phostname=''
pidtype='pid_t'
plibpth='/lib'
pm_apiversion='5.005'
pmake=''
pr=''
prefixexp='/usr'
privlib='/usr/lib/perl5/5.8.1'
procselfexe=''
prototype='define'
ptrsize='4'
quadkind='3'
quadtype='long long'
randbits='48'
randfunc='drand48'
random_r_proto='0'
randseedtype='long'
ranlib=':'
rd_nodata='-1'
readdir64_r_proto='0'
readdir_r_proto='0'
revision='5'
rm='rm'
rmail=''
run=''
runnm='true'
sPRIEUldbl='"LE"'
sPRIFUldbl='"LF"'
sPRIGUldbl='"LG"'
sPRIXU64='"llX"'
sPRId64='"lld"'
sPRIeldbl='"Le"'
sPRIfldbl='"Lf"'
sPRIgldbl='"Lg"'
sPRIi64='"lli"'
sPRIo64='"llo"'
sPRIu64='"llu"'
sPRIx64='"llx"'
sSCNfldbl='"Lf"'
sched_yield='sched_yield()'
scriptdir='/usr/bin'
scriptdirexp='/usr/bin'
sed='sed'
seedfunc='srand48'
selectminbits='32'
selecttype='fd_set *'
sendmail=''
setgrent_r_proto='0'
sethostent_r_proto='0'
setlocale_r_proto='0'
setnetent_r_proto='0'
setprotoent_r_proto='0'
setpwent_r_proto='0'
setservent_r_proto='0'
sh='/bin/sh'
shar=''
shmattype=''
shortsize='2'
shrpenv='env LD_RUN_PATH=/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int/CORE'
sig_count='32'
sig_name='ZERO HUP INT QUIT ILL TRAP ABRT EMT FPE KILL BUS SEGV SYS PIPE ALRM TERM URG STOP TSTP CONT CHLD TTIN TTOU IO XCPU XFSZ VTALRM PROF WINCH LOST USR1 USR2 CLD POLL '
sig_name_init='"ZERO", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "EMT", "FPE", "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM", "URG", "STOP", "TSTP", "CONT", "CHLD", "TTIN", "TTOU", "IO", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "LOST", "USR1", "USR2", "CLD", "POLL", 0'
sig_num='0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 20 23 '
sig_num_init='0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 20, 23, 0'
sig_size='34'
signal_t='void'
sitearch='/usr/lib/perl5/site_perl/5.8.1/cygwin-thread-multi-64int'
sitearchexp='/usr/lib/perl5/site_perl/5.8.1/cygwin-thread-multi-64int'
sitebin='/usr/bin'
sitebinexp='/usr/bin'
sitehtml1dir=''
sitehtml1direxp=''
sitehtml3dir=''
sitehtml3direxp=''
sitelib='/usr/lib/perl5/site_perl/5.8.1'
sitelib_stem='/usr/lib/perl5/site_perl'
sitelibexp='/usr/lib/perl5/site_perl/5.8.1'
siteman1='/usr/share/man/man1'
siteman1dir='/usr/share/man/man1'
siteman1direxp='/usr/share/man/man1'
siteman3='/usr/share/man/man3'
siteman3dir='/usr/share/man/man3'
siteman3direxp='/usr/share/man/man3'
siteprefix='/usr'
siteprefixexp='/usr'
sitescript='/usr/bin'
sitescriptexp='/usr/bin'
sizesize='4'
sizetype='size_t'
sleep=''
smail=''
sockethdr=''
socketlib=''
socksizetype='socklen_t'
sort='sort'
spackage='Perl5'
spitshell='cat'
srand48_r_proto='0'
srandom_r_proto='0'
src='/tmp/perl/perl_debug-5.8.1'
ssizetype='ssize_t'
startperl='#!/usr/bin/perl'
stdchar='char'
stdio_base='((fp)->_ub._base ? (fp)->_ub._base : (fp)->_bf._base)'
stdio_bufsiz='((fp)->_ub._base ? (fp)->_ub._size : (fp)->_bf._size)'
stdio_cnt='((fp)->_r)'
stdio_filbuf=''
stdio_ptr='((fp)->_p)'
stdio_stream_array=''
strerror_r_proto='REENTRANT_PROTO_B_IBW'
strings='/usr/include/string.h'
submit=''
subversion='1'
sysman='/usr/share/man/man1'
tail=''
tar=''
targetarch=''
tbl=''
tee=''
test='test'
timeincl='/usr/include/sys/time.h '
timetype='time_t'
tmpnam_r_proto='0'
to=':'
touch='touch'
tr='tr'
trnl='\n'
troff=''
ttyname_r_proto='0'
u16size='2'
u16type='unsigned short'
u32size='4'
u32type='unsigned long'
u64size='8'
u64type='unsigned long long'
u8size='1'
u8type='unsigned char'
uidformat='"lu"'
uidsign='1'
uidsize='4'
uidtype='uid_t'
uname='uname'
uniq='uniq'
uquadtype='unsigned long long'
use5005threads='undef'
use64bitall='undef'
use64bitint='define'
usecrosscompile='undef'
usedl='define'
usefaststdio='define'
useithreads='define'
uselargefiles='define'
uselongdouble='undef'
usemorebits='undef'
usemultiplicity='define'
usemymalloc='y'
usenm='true'
useopcode='true'
useperlio='define'
useposix='true'
usereentrant='undef'
usesfio='false'
useshrplib='true'
usesocks='undef'
usethreads='define'
usevendorprefix='undef'
usevfork='false'
usrinc='/usr/include'
uuname=''
uvXUformat='"llX"'
uvoformat='"llo"'
uvsize='8'
uvtype='unsigned long long'
uvuformat='"llu"'
uvxformat='"llx"'
vendorarch=''
vendorarchexp=''
vendorbin=''
vendorbinexp=''
vendorhtml1dir=' '
vendorhtml1direxp=''
vendorhtml3dir=' '
vendorhtml3direxp=''
vendorlib=''
vendorlib_stem=''
vendorlibexp=''
vendorman1dir=' '
vendorman1direxp=''
vendorman3dir=' '
vendorman3direxp=''
vendorprefix=''
vendorprefixexp=''
vendorscript=''
vendorscriptexp=''
version='5.8.1'
version_patchlevel_string='version 8 subversion 1'
versiononly='undef'
vi=''
voidflags='15'
xlibpth='/usr/lib/386 /lib/386'
xs_apiversion='5.8.1'
yacc='/bin/byacc'
yaccflags=''
zcat=''
zip='zip'
!END!

# Search for it in the big string 
sub fetch_string {
    my($self, $key) = @_;

    my $quote_type = "'";
    my $marker = "$key=";

    # Check for the common case, ' delimited
    my $start = index($Config_SH, "\n$marker$quote_type");
    # If that failed, check for " delimited
    if ($start == -1) {
        $quote_type = '"';
        $start = index($Config_SH, "\n$marker$quote_type");
    }
    return undef if ( ($start == -1) &&  # in case it's first 
                      (substr($Config_SH, 0, length($marker)) ne $marker) );
    if ($start == -1) { 
        # It's the very first thing we found. Skip $start forward
        # and figure out the quote mark after the =.
        $start = length($marker) + 1;
        $quote_type = substr($Config_SH, $start - 1, 1);
    } 
    else { 
        $start += length($marker) + 2;
    }

    my $value = substr($Config_SH, $start, 
                       index($Config_SH, "$quote_type\n", $start) - $start);

    # If we had a double-quote, we'd better eval it so escape
    # sequences and such can be interpolated. Since the incoming
    # value is supposed to follow shell rules and not perl rules,
    # we escape any perl variable markers
    if ($quote_type eq '"') {
	$value =~ s/\$/\\\$/g;
	$value =~ s/\@/\\\@/g;
	eval "\$value = \"$value\"";
    }

    # So we can say "if $Config{'foo'}".
    $value = undef if $value eq 'undef';
    $self->{$key} = $value; # cache it
}

sub fetch_virtual {
    my($self, $key) = @_;

    my $value;

    if ($key =~ /^((?:cc|ld)flags|libs(?:wanted)?)_nolargefiles/) {
	# These are purely virtual, they do not exist, but need to
	# be computed on demand for largefile-incapable extensions.
	my $new_key = "${1}_uselargefiles";
	$value = $Config{$1};
	my $withlargefiles = $Config{$new_key};
	if ($new_key =~ /^(?:cc|ld)flags_/) {
	    $value =~ s/\Q$withlargefiles\E\b//;
	} elsif ($new_key =~ /^libs/) {
	    my @lflibswanted = split(' ', $Config{libswanted_uselargefiles});
	    if (@lflibswanted) {
		my %lflibswanted;
		@lflibswanted{@lflibswanted} = ();
		if ($new_key =~ /^libs_/) {
		    my @libs = grep { /^-l(.+)/ &&
                                      not exists $lflibswanted{$1} }
		                    split(' ', $Config{libs});
		    $Config{libs} = join(' ', @libs);
		} elsif ($new_key =~ /^libswanted_/) {
		    my @libswanted = grep { not exists $lflibswanted{$_} }
		                          split(' ', $Config{libswanted});
		    $Config{libswanted} = join(' ', @libswanted);
		}
	    }
	}
    }

    $self->{$key} = $value;
}

sub FETCH { 
    my($self, $key) = @_;

    # check for cached value (which may be undef so we use exists not defined)
    return $self->{$key} if exists $self->{$key};

    $self->fetch_string($key);
    return $self->{$key} if exists $self->{$key};
    $self->fetch_virtual($key);

    # Might not exist, in which undef is correct.
    return $self->{$key};
}
 
my $prevpos = 0;

sub FIRSTKEY {
    $prevpos = 0;
    substr($Config_SH, 0, index($Config_SH, '=') );
}

sub NEXTKEY {
    # Find out how the current key's quoted so we can skip to its end.
    my $quote = substr($Config_SH, index($Config_SH, "=", $prevpos)+1, 1);
    my $pos = index($Config_SH, qq($quote\n), $prevpos) + 2;
    my $len = index($Config_SH, "=", $pos) - $pos;
    $prevpos = $pos;
    $len > 0 ? substr($Config_SH, $pos, $len) : undef;
}

sub EXISTS { 
    return 1 if exists($_[0]->{$_[1]});

    return(index($Config_SH, "\n$_[1]='") != -1 or
           substr($Config_SH, 0, length($_[1])+2) eq "$_[1]='" or
           index($Config_SH, "\n$_[1]=\"") != -1 or
           substr($Config_SH, 0, length($_[1])+2) eq "$_[1]=\"" or
           $_[1] =~ /^(?:(?:cc|ld)flags|libs(?:wanted)?)_nolargefiles$/
          );
}

sub STORE  { die "\%Config::Config is read-only\n" }
*DELETE = \&STORE;
*CLEAR  = \&STORE;


sub config_sh {
    $Config_SH
}

sub config_re {
    my $re = shift;
    return map { chomp; $_ } grep /^$re=/, split /^/, $Config_SH;
}

sub config_vars {
    foreach (@_) {
	if (/\W/) {
	    my @matches = config_re($_);
	    print map "$_\n", @matches ? @matches : "$_: not found";
	} else {
	    my $v = (exists $Config{$_}) ? $Config{$_} : 'UNKNOWN';
	    $v = 'undef' unless defined $v;
	    print "$_='$v';\n";
	}
    }
}

sub TIEHASH {
    bless $_[1], $_[0];
}

# avoid Config..Exporter..UNIVERSAL search for DESTROY then AUTOLOAD
sub DESTROY { }

my $i = 0;
foreach my $c (8,7,6,5,4,3,2) { $i |= ord($c); $i <<= 8 }
$i |= ord(1);
my $value = join('', unpack('aaaaaaaa', pack('Q', $i)));


tie %Config, 'Config', {
    'archlibexp' => '/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int',
    'archname' => 'cygwin-thread-multi-64int',
    'cc' => 'gcc',
    'ccflags' => '-DPERL_USE_SAFE_PUTENV -DDEBUGGING -fno-strict-aliasing -I/usr/local/include',
    'cppflags' => '-DPERL_USE_SAFE_PUTENV -DDEBUGGING -fno-strict-aliasing -I/usr/local/include',
    'dlsrc' => 'dl_dlopen.xs',
    'dynamic_ext' => 'B ByteLoader Cwd DB_File Data/Dumper Devel/DProf Devel/PPPort Devel/Peek Digest/MD5 Encode Fcntl File/Glob Filter/Util/Call GDBM_File I18N/Langinfo IO List/Util MIME/Base64 NDBM_File ODBM_File Opcode POSIX PerlIO/encoding PerlIO/scalar PerlIO/via SDBM_File Socket Storable Sys/Hostname Sys/Syslog Time/HiRes Unicode/Normalize XS/APItest XS/Typemap attrs re threads threads/shared',
    'installarchlib' => '/usr/lib/perl5/5.8.1/cygwin-thread-multi-64int',
    'installprivlib' => '/usr/lib/perl5/5.8.1',
    'libpth' => '/usr/local/lib /lib /usr/lib',
    'libs' => '-lgdbm -ldb -lcrypt -lutil -lgdbm_compat',
    'osname' => 'cygwin',
    'osvers' => '1.5.5(0.9432)',
    'prefix' => '/usr',
    'privlibexp' => '/usr/lib/perl5/5.8.1',
    'sharpbang' => '#!',
    'shsharp' => 'true',
    'so' => 'dll',
    'startsh' => '#!/bin/sh',
    'static_ext' => ' ',
    byteorder => $value,

};

1;
