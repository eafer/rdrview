SYSTEM = $(shell uname)
CC = gcc

CFLAGS = -DNDEBUG -O2 -Wall -Wextra -fno-strict-aliasing
override CFLAGS += $(shell curl-config --cflags) $(shell xml2-config --cflags)

LDLIBS = $(shell curl-config --libs) $(shell xml2-config --libs) -lm
ifeq ($(SYSTEM), Linux)
	LDLIBS += -lseccomp
else ifeq ($(SYSTEM), FreeBSD)
	LDLIBS += -liconv
endif

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

rdrview: $(OBJS)
	$(CC) $(CFLAGS) -o rdrview $(OBJS) $(LDLIBS)

%.o: %.c src/rdrview.h
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(OBJS) rdrview
install:
	mkdir -p $(BINDIR)
	cp -f rdrview $(BINDIR)
	mkdir -p $(MANDIR)
	cp -f rdrview.1 $(MANDIR)
	chmod 0644 $(MANDIR)/rdrview.1

uninstall:
	cd $(BINDIR) && rm rdrview
	cd $(MANDIR) && rm rdrview.1
