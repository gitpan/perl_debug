#!/bin/sh

# run the build with ntsec on if possible...
pkg=perl
ver=5.8.1
release=-2
dir=`pwd`
shortver=`echo ${ver}${release} | sed 's/-.*//'`
buildperl=buildperl

## checkout builddir
rm -rf ${dir}/${buildperl}
mkdir -p -m 777 ${dir}/${buildperl}

## unpack
rm -rf ${pkg}-${ver}
tar jxf perl-5.8.1.tar.bz2
#mv perl-5.8.1-RC5/ ${pkg}-${ver}/
mkdir -p /usr/share/doc/${pkg}-${ver}${release}
/bin/install -c -m 644 ${pkg}-${ver}/Artistic ${pkg}-${ver}/Copying ${pkg}-${ver}/README /usr/share/doc/${pkg}-${ver}${release}

## patch to strip the binaries & dll's
(cd ${pkg}-${ver} ; patch -p1<../hints.cygwin.sh.patch)

## configure perl
cd ${dir}/${buildperl}

cp ${dir}/Policy.sh ${dir}/${buildperl}

sh ${dir}/${pkg}-${ver}/Configure -de	\
		-Dmksymlinks	\
		-Duse64bitint	\
		-Dusethreads	\
		-Doptimize='-O2'	\
		-Dman3ext='3pm'	\
		2>&1 | tee ${dir}/log.configure

# Implied by usethreads
#		-Dusemultiplicity	\
#		-Uusemymalloc	\

## build perl
make 2>&1 | tee ${dir}/log.make

## make the tests
make test 2>&1 | tee ${dir}/log.test
(cd t;./perl harness) 2>&1 | tee ${dir}/log.harness

export PERLIO=perlio
## make the tests
make test 2>&1 | tee ${dir}/log.test.perlio
echo 'PERLIO=perlio' >> ${dir}/log.test.perlio
(cd t;./perl harness) 2>&1 | tee ${dir}/log.harness.perlio
echo 'PERLIO=perlio' >> ${dir}/log.harness.perlio

# export PERLIO=raw
# ## make the tests
# make test 2>&1 | tee ${dir}/log.test.raw
# echo 'PERLIO=perlio' >> ${dir}/log.test.raw
# (cd t;./perl harness) 2>&1 | tee ${dir}/log.harness.raw
# echo 'PERLIO=raw' >> ${dir}/log.harness.raw
# 
# export PERLIO=crlf
# ## make the tests
# make test 2>&1 | tee ${dir}/log.test.crlf
# echo 'PERLIO=perlio' >> ${dir}/log.test.crlf
# (cd t;./perl harness) 2>&1 | tee ${dir}/log.harness.crlf
# echo 'PERLIO=crlf' >> ${dir}/log.harness.crlf

## install
make install 2>&1 | tee ${dir}/log.install

cd ${dir}
for i in Archive-Tar-1.05	\
         Compress-Zlib-1.22	\
         MD5-2.02	\
         Term-ReadLine-Perl-1.0203 \
         Net-Telnet-3.03	\
         TermReadKey-2.21;	\
do (tar xzf ${i}.tar.gz &&	\
    (cd ${i} &&	\
    perl Makefile.PL &&	\
    make &&	\
    make install UNINST=1));	\
done