#!/bin/sh

rm -rf /usr/lib/perl5/
sh ./makepkg_debug
rm -rf /usr/lib/perl5/
sh ./makepkg.sh
