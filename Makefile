SYSTEM = $(shell uname)
ifeq ($(SYSTEM), OpenBSD)
	CC := cc
else
	CC := gcc
endif
GIT_COMMIT = $(shell git rev-parse --short HEAD)

CFLAGS = -DNDEBUG -O2 -Wall -Wextra -fno-strict-aliasing
override CFLAGS += $(shell curl-config --cflags) $(shell xml2-config --cflags)

LDLIBS = $(shell curl-config --libs) $(shell xml2-config --libs) -lm
ifeq ($(SYSTEM), Linux)
	LDLIBS += -lseccomp
else ifeq ($(SYSTEM), FreeBSD)
	LDLIBS += -liconv
else ifeq ($(SYSTEM), OpenBSD)
	LDLIBS += -liconv
else ifeq ($(SYSTEM), Darwin)
	LDLIBS += -liconv
endif

PREFIX = /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin
ifeq ($(SYSTEM), OpenBSD)
	MANDIR = $(DESTDIR)$(PREFIX)/man/man1
else
	MANDIR = $(DESTDIR)$(PREFIX)/share/man/man1
endif

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

rdrview: $(OBJS)
	$(CC) $(LDFLAGS) -o rdrview $(OBJS) $(LDLIBS)

src/rdrview.o: src/rdrview.c src/rdrview.h src/version.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%.o: %.c src/rdrview.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

src/version.h: COMMIT
	@printf '#define GIT_COMMIT\t"%s"\n' $(GIT_COMMIT) > src/version.h

.PHONY: COMMIT
COMMIT:

clean:
	rm -f $(OBJS) rdrview src/version.h
install:
	mkdir -p $(BINDIR)
	cp -f rdrview $(BINDIR)
	mkdir -p $(MANDIR)
	cp -f rdrview.1 $(MANDIR)
	chmod 0644 $(MANDIR)/rdrview.1

uninstall:
	cd $(BINDIR) && rm rdrview
	cd $(MANDIR) && rm rdrview.1
