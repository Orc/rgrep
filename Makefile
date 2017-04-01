CC=cc
bindir=/usr/local/bin
mandir=/usr/local/man

rgrep: rgrep.c config.h regex/libregex.a VERSION
	$(CC) -o rgrep -DVERSION=\"`cat VERSION`\" rgrep.c regex/libregex.a  -lncurses

regex/libregex.a:
	cd regex && make lib

install: install-bin install-man

install-bin:
	./config.md $(DESTDIR)$(bindir)
	/usr/bin/install -s -m 755 rgrep $(DESTDIR)$(bindir)/rgrep

install-man:
	./config.md $(DESTDIR)$(mandir)/man1
	/usr/bin/install -m 444 rgrep.1 $(DESTDIR)$(mandir)/man1

spotless distclean: clean
	rm -f Makefile regex/Makefile rgrep.1 config.cmd config.sub config.h config.mak config.log config.md librarian.sh
	
clean:
	cd regex && make clean
	rm -f rgrep version.c
