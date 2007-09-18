/*
 * rgrep - recursive egrep, sort of like the one that ships with
 *         the Jed editor, except that this one DOES NOT USE Slang
 * 
 * 
 *		Copyright (C) 2004-2005 David Loren Parsons.
 *			All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation files
 *  (the "Software"), to deal in the Software without restriction,
 *  including without limitation the rights to use, copy, modify, merge,
 *  publish, distribute, sublicence, and/or sell copies of the Software,
 *  and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *   1) Redistributions of source code must retain the above copyright
 *      notice, this list of conditions, and the following disclaimer.
 *
 *   2) Except as contained in this notice, the name of David Loren
 *      Parsons shall not be used in advertising or otherwise to promote
 *      the sale, use or other dealings in this Software without prior
 *      written authorization from David Loren Parsons.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL DAVID LOREN PARSONS BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 *  OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#if HAVE_LIBGEN_H
#   include <libgen.h>
#endif
#include <string.h>
#include <errno.h>
#include <sysexits.h>
#include <ctype.h>
#include <dirent.h>
#include <curses.h>
#include <term.h>

#include "regex/regex.h"


char *pgm = "rgrep";
regex_t pattern;

int countmatches = 0;	/* count matches, don't show them */
enum {NONE,LINE,EXACT} highlight = NONE;
int except = 0;		/* show lines that DON'T match the pattern */
int ignorecase = 0;
int listonly = 0;	/* list files that match the pattern */
int linenumbers = 0;	/* print linenumbers of matching files */
int needfilenames = 0;
int followlinks = 0;
int recursive = 1;
regex_t *filepattern = 0;	/* grep only files matching /filepattern/ */
int debug = 0;		/* convert rgrep into `find -type d -name /pat/' */
int reclen = 0;

/* for highlighting;  termcap SO/SE */
char *SO=0, *SE=0;


char *
regoops(int code, regex_t *pat)
{
    static char regerrbuf[200];
    size_t ret;

    if ( (ret = regerror(code, pat, regerrbuf, sizeof regerrbuf)) > 0 )
	return regerrbuf;
    return "unknown error code - you found a bug";
}


void
error(char *fmt, ...)
{
    va_list args;

    va_start(args,fmt);
    fprintf(stderr, "%s: ", pgm);
    vfprintf(stderr, fmt, args);
    putc('\n',stderr);
    va_end(args);
    exit(1);
}


void
whine(char *object)
{
    fprintf(stderr, "%s: ", pgm);
    perror(object);
}


void
usage(int retcode, char *fmt, ...)
{
    va_list args;
    static char *flagstr = "\
   -c      Count matches.\n\
   -e      Use extended regular expression syntax\n\
   -h      Highlight lines containing matches.\n\
   -H      Highlight match instead of entire line containing match.\n\
   -i      Ignore case.\n\
   -l      List filename only.\n\
   -n      Print line number of match.\n\
   -F      Follow links.\n\
   -r      Recursively scan through directory tree.\n\
   -N      Do NOT perform a recursive search.\n\
   -R'pat' Like '-r' except that only those files matching 'pat' are checked.\n\
   -v      Print only lines that do NOT match the specified pattern.\n\
   -x'ext' Checks only files with extension given by 'ext'.\n\
   -D      Print all directories that would be searched.  This option is for\n\
	   debugging purposes only.  No file is grepped with this option.\n\
   -V      Print the version#\n\
   -W'len' Lines are 'len' characters long (not newline terminated).\n";

    if (fmt) {
	va_start(args,fmt);
	fprintf(stderr, "%s: ", pgm);
	vfprintf(stderr, fmt, args);
	putc('\n', stderr);
	va_end(args);
    }
    fprintf(stderr, "usage: %s [opts] /re/ [path {...}]\n", pgm);
    fwrite(flagstr, 1, strlen(flagstr), stderr);
    exit(retcode);
}


void
safewrite(regmatch_t lim[], char *start, char *end)
{
    register so=0, i, c;
    
    if ( lim && (highlight == LINE) && (lim[0].rm_so >= 0) ) {
	so = 1;
	tputs(SO, 1, putchar);
    }
    
    i = 0;
    while (start+i < end) {
	if ( lim && (highlight == EXACT) && (lim[0].rm_so == i) ) {
	    so = 1;
	    tputs(SO, 1, putchar);
	}
	c = start[i++];
	if ( (c == '\t') || (isprint(c) && c != '\r' && c != '\n') )
	    putchar(c);
	else
	    putchar('?');
	if ( lim && (highlight == EXACT) && (lim[0].rm_eo == i) ) {
	    so = 0;
	    tputs(SE, 1, putchar);
	}
    }

    if (so)
	tputs(SE, 1, putchar);
}


void
showmatch(regmatch_t matches[], char *path, int lineno, char *start, char *end)
{
    if (needfilenames)
	printf("%s:", path);
    if (linenumbers)
	printf("%d:", lineno);
    safewrite(matches, start, end);
    putchar('\n');
}


int
match(char *path, int lineno, char *start, char *end)
{
    int rc;
    regmatch_t matches[11];
	
    bzero( matches, 11 * sizeof(regmatch_t) );
    matches[0].rm_so = 0;
    matches[0].rm_eo = (int)(end-start);
    
    rc = regexec(&pattern, start, 11, matches, REG_STARTEND|REG_BACKR);

    if (rc == 0) { /* found */
	if ( except /*&& (highlight == NONE)*/ ) return 0;
	if ( listonly || countmatches ) return 1;
	showmatch(matches, path, lineno, start, end);
	return (rc == 0);
    }
    else if (rc == REG_NOMATCH) {
	if ( !except /*&& (highlight == NONE)*/ ) return 0;
	if ( listonly || countmatches ) return 1;
	showmatch(0, path, lineno, start, end);
	return (rc == REG_NOMATCH);
    }
    error("runtime error: %s", regoops(rc, &pattern));
    return 0;
}


char *
fgetblk(FILE *f, size_t *size)
{
    static char *recbfr = 0;
    static int bufsize = 0;
    int sz = 0;
    
    
    if (reclen) {
	if ( (recbfr == 0) && ((recbfr = malloc(bufsize = reclen)) == 0) )
	    error("runtime error: %s", strerror(errno));
	sz = fread(recbfr, 1, reclen, f);

	if (sz < 1) return 0;
	if (sz < reclen) bzero(recbfr+sz, reclen-sz);
	return recbfr;
    }

#if HAVE_FGETLN
    return fgetln(f, size);
#else
    sz = 0;
    while (1) {
	register c;

	if (sz >= bufsize) {
	    bufsize += 1000;
	    recbfr = recbfr ? realloc(recbfr, bufsize) : malloc(bufsize);

	    if (recbfr == 0)
		error("runtime error: %s", strerror(errno));
	}

	if ( ((c = getc(f)) == EOF) || (c == '\n') ) {
	    recbfr[sz] = 0;
	    *size = sz;
	    return (c == '\n') || (sz > 0) ? recbfr : 0;
	}
	
	recbfr[sz++] = c;
    }
#endif
}

int
stream_grep(char *path, FILE *f)
{
    char  *line;
    size_t size;
    int matched = 0;
    int lineno = 0;

    while ( (line = fgetblk(f, &size)) ) {
	if ( (size > 0) && (line[size-1] == '\n') )
	    size--;
	match(path, ++lineno, line, line+size) && matched++;
	if (listonly && matched) return 1;
    }

    return matched;
}


#if HAVE_MMAP
int
mmap_grep(char *path, FILE *f)
{
}
#endif


#if HAVE_MMAP
int
grep(char *path, FILE *f)
{
    char *map, *emap;
    char *start, *end;
    int matched=0, lineno=0;
    struct stat stb;

    if ( (fstat(fileno(f), &stb) != 0) || !(stb.st_mode & S_IFREG) )
	return stream_grep(path, f);

    /* don't mmap super-large files, just in case the machine does
     * evil things like trying to load the whole memory map into
     * core
     */
    if ( stb.st_size > 1024*1024*100 )
	return stream_grep(path, f);
	
    map = mmap(0, stb.st_size, PROT_READ, MAP_FILE, fileno(f), 0);

    if (map == (char*)(-1)) return stream_grep(path, f);

    emap = map + stb.st_size;
    start = map;
    
    do {
    
	if (reclen) {
	    end = start + reclen;
	    if (end > emap) end = emap;
	}
	else {
	    end = memchr(start, '\n', emap-start);
	    if (end == 0) end = emap;
	}

	match(path, ++lineno, start, end) && matched++;
	if (listonly && matched) { munmap(map, stb.st_size); return 1; }
	start = end+1;
	
    } while (start < emap);
    
    munmap(map, stb.st_size);
    return matched;
}
#else
#define grep(path,f) stream_grep(path,f)
#endif


int
filegrep(char *path)
{
    FILE *f;
    int rc;

    if (debug) return 0;

    if ( (f = fopen(path, "r")) ) {
	rc = grep(path,f);
	fclose(f);
	return rc;
    }
    whine(path);
    return 0;
}


int
dirgrep(char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    char *newpath = 0;
    int newpathsize = 0;
    int pathsize = strlen(path);
    int rc = 0;
    int isdir;

    if ( !recursive )
	return 0;

    if (d == 0) {
	whine(path);
	return 0;
    }

    if (debug)
	puts(path);
    
    while (e = readdir(d)) {
#if HAVE_DIRENT_D_NAMLEN && !defined(d_namlen)
#   define NAMLEN(e) (e)->d_namlen
#else
#   undef d_namlen
	int d_namlen = strlen(e->d_name);
#   define NAMLEN(e) d_namlen
#endif
	if ( (e->d_name[0] == '.') && ( ((e->d_name[1] == '.')
					 && (NAMLEN(e) == 2))
					|| (NAMLEN(e) == 1)) )
	continue;

	if (pathsize + NAMLEN(e) + 2 > newpathsize) {
	    newpathsize = pathsize + NAMLEN(e) + 2;
	    if (newpath)
		newpath = realloc(newpath, newpathsize);
	    else
		newpath = malloc(newpathsize);

	    if ( !newpath )
		error("runtime error: %s", strerror(errno));
	    strcpy(newpath, path);
	    strcat(newpath, "/");
	}
	strcpy(newpath+pathsize+1, e->d_name);
	
	isdir = isdirectory(newpath);

	if (isdir) {
	    rc += dirgrep(newpath);
	    continue;
	}
	    
	if ( filepattern && !isdirectory(newpath) ) {
	    regmatch_t lim[1];
    
	    lim[0].rm_so = 0;
	    lim[0].rm_eo = NAMLEN(e);

	    if (regexec(filepattern, e->d_name, 0, lim, REG_STARTEND) != 0)
		continue;
	}

	rc += filegrep(newpath);
    }
    closedir(d);
    return rc;
}


int
rgrep(char *path)
{
    return isdirectory(path) ? dirgrep(path) : filegrep(path);
}


int
isdirectory(char *path)
{
    struct stat stb;
    int (*statftn)(const char*,struct stat*) = followlinks ? lstat : stat;

    if ( (*statftn)(path, &stb) != 0)
	return 0; /* if we can't stat it, treat it like a file */

    return (stb.st_mode & S_IFDIR);
}

double
main(argc,argv)
int argc;
char **argv;
{
    register ret, opt;
    regex_t filetemplate; /* for -R, -x; pointed to by *filepattern */
    char *vv;
    int regflags = REG_BASIC;
    int rc;

#if HAVE_BASENAME
    if (argv[0]) pgm = basename(argv[0]);
#else
    if (pgm = strrchr(argv[0], '/'))
	++pgm;
    else
	pgm = argv[0];
#endif
    
    opterr = 1;
    
    while ( (opt = getopt(argc,argv,"cehHilnFrNR:vx:DVW:?")) != EOF ) {
	switch (opt) {
	case 'c':   countmatches = 1; break;
	case 'e':   regflags = REG_EXTENDED;
	case 'h':   highlight = LINE; break;
	case 'H':   highlight = EXACT; break;
	case 'i':   ignorecase = 1; break;
	case 'l':   listonly = 1; break;
	case 'n':   linenumbers = 1; break;
	case 'F':   followlinks = 1; break;
	case 'r':   recursive = 1; break;
	case 'N':   recursive = 0; break;
	case 'x':   if ( !(vv = alloca(strlen(optarg) + 4)) )
			error("couldn't parse -x '%s': %s",
				optarg, strerror(errno));
		    sprintf(vv, "\\.%s$", optarg);
		    optarg = vv;
		    /* then fall into the 'R' case immediately below... */
	case 'R':   ret = regcomp(&filetemplate, optarg, REG_BASIC);
		    if (ret)
			error("couldn't parse -%c '%s': %s",
				opt, optarg, regoops(ret, 0));
		    filepattern = &filetemplate;
		    break;
	case 'v':   except = 1; break;
	case 'V':   printf("rgrep v%s\n", VERSION); exit(EX_OK);
	case 'D':   debug = 1; break;
	case 'W':   {   char *n;
			reclen = strtoul(optarg, &n, 10);
			if (*n)  error("unrecognisable length in -W%s", optarg);
		    }
		    break;
	default:    usage( (opt == '?') ? EX_OK : EX_USAGE, 0);
	}
    }


    argc -= optind;
    argv += optind;

    if (argc == 0)	/* hey!  where's our pattern to match? */
	usage(EX_USAGE, "no pattern to match");
    
    if (ignorecase) regflags |= REG_ICASE;
    if (listonly) regflags |= REG_NOSUB;
    
    ret = regcomp(&pattern, argv[0], regflags);

    if (ret != 0)
	error("unrecognisable pattern /%s/: %s", argv[0], regoops(ret, 0));

    if (highlight != NONE) {
#if USES_TERMCAP
	static char tcbuf[2048], tcstrings[1024];
	char *bufp = tcstrings;

	if ( (vv = getenv("TERM")) && tgetent(tcbuf, vv) != 0) {
	    SO = tgetstr("so", &bufp);
	    SE = tgetstr("se", &bufp);
	}
#elif USES_TERMINFO
	if ( (vv = getenv("TERM")) && setupterm(vv, fileno(stdout), 0) == OK) {
	    SO = tigetstr("smso");
	    SE = tigetstr("rmso");
	}
#endif
	if ( (SO == 0) || (SE == 0) )
	    error("cannot highlight matches on this terminal");
    }

    if (argc <= 1) {
	needfilenames = 0;
	rc = grep(0, stdin);
	if (countmatches)
	    printf("%d\n", rc);
    }
    else {
	int i;

	needfilenames = (argc > 2) || isdirectory(argv[1]);
	
	for (i=1; i < argc; i++)
	    rc += rgrep(argv[i]);
    }
    exit( rc ? EX_OK : 1);
}
