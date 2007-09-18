#! /bin/sh

# local options:  ac_help is the help message that describes them
# and LOCAL_AC_OPTIONS is the script that interprets them.  LOCAL_AC_OPTIONS
# is a script that's processed with eval, so you need to be very careful to
# make certain that what you quote is what you want to quote.


# load in the configuration file
#
TARGET=rgrep

. ./configure.inc

AC_INIT $TARGET

AC_PROG_CC
AC_CHECK_ALLOCA || AC_FAIL "$TARGET requires alloca()"
AC_CHECK_FUNCS mmap
AC_CHECK_FUNCS fgetln
AC_CHECK_FIELD dirent d_namlen sys/types.h dirent.h

# for basename
if AC_CHECK_FUNCS basename; then
    AC_CHECK_HEADERS libgen.h
fi

[ "$OS_FREEBSD" -o "$OS_DRAGONFLY" ] || AC_CHECK_HEADERS malloc.h

AC_OUTPUT Makefile rgrep.1
