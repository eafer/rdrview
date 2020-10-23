CC = gcc

CFLAGS = -DNDEBUG -O2 -Wall -Wextra -fno-strict-aliasing
override CFLAGS += $(shell curl-config --cflags) $(shell xml2-config --cflags)

LDLIBS = $(shell curl-config --libs) $(shell xml2-config --libs) -lm
ifeq ("$(shell uname)", "Linux")
LDLIBS += -lseccomp
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
	install -d $(BINDIR)
	install -t $(BINDIR) rdrview
	install -d $(MANDIR)
	install -m 644 -t $(MANDIR) rdrview.1

uninstall:
	cd $(BINDIR) && rm rdrview
	cd $(MANDIR) && rm rdrview.1
