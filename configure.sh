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
unset _MK_LIBRARIAN

if [ "IS_BROKEN_CC" ]; then
    case "$AC_CC $AC_CFLAGS" in
    *-pedantic*) ;;
    *)  # hack around deficiencies in gcc and clang
	#
	AC_DEFINE 'while(x)' 'while( (x) != 0 )'
	AC_DEFINE 'if(x)' 'if( (x) != 0 )'

	if [ "$IS_CLANG" ]; then
	    AC_CC="$AC_CC -Wno-implicit-int"
	elif [ "$IS_GCC" ]; then
	    AC_CC="$AC_CC -Wno-return-type -Wno-implicit-int"
	fi ;;
    esac
fi

AC_CHECK_ALLOCA || AC_FAIL "$TARGET requires alloca()"
AC_CHECK_FUNCS mmap
AC_CHECK_FUNCS fgetln
AC_CHECK_FIELD dirent d_namlen sys/types.h dirent.h

# for basename
if AC_CHECK_FUNCS basename; then
    AC_CHECK_HEADERS libgen.h
fi

# need termcap, curses, or ncurses for termcap functions

LIBORDER="-lncurses -ltermcap -lcurses"

if AC_LIBRARY tgetent $LIBORDER; then
    # our -libtermcap might be (n)curses in disguise.  If so,
    # it might have a colliding mvcur() that we need to define
    # ourselves out from.
    AC_QUIET AC_CHECK_FUNCS mvcur && AC_DEFINE mvcur __mvcur
    AC_DEFINE USES_TERMCAP 1
elif AC_LIBRARY tigetstr $LIBORDER; then
    AC_DEFINE USES_TERMINFO 1
else
    TLOG "(no)"
    AC_FAIL "rgrep needs termcap, curses, or ncurses for match highlighting"
fi
AC_CHECK_HEADERS term.h || AC_FAIL "rgrep needs <term.h>"

[ "$OS_FREEBSD" -o "$OS_DRAGONFLY" ] || AC_CHECK_HEADERS malloc.h

AC_OUTPUT Makefile regex/Makefile rgrep.1
