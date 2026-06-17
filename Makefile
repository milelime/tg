# SPDX-License-Identifier: GPL-3.0-or-later
# tg - build, install, and package the tagger CLI and libtg.

VERSION := $(shell cat VERSION)
SOMAJOR := 0

PREFIX       ?= /usr/local
BINDIR       ?= $(PREFIX)/bin
LIBDIR       ?= $(PREFIX)/lib
INCLUDEDIR   ?= $(PREFIX)/include
DATADIR      ?= $(PREFIX)/share
MANDIR       ?= $(DATADIR)/man
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

CC      ?= cc
CFLAGS  ?= -O2 -g
INSTALL ?= install
ALL_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Isrc -DTG_VERSION='"$(VERSION)"' $(CFLAGS)

LIB_SRC := src/tagset.c src/xattr.c
LIB_OBJ := $(LIB_SRC:.c=.o)
CLI_SRC := src/main.c src/cmd.c
HEADERS := src/tagset.h src/xattr.h

LIBNAME   := libtg
STATICLIB := $(LIBNAME).a
SHAREDLIB := $(LIBNAME).so.$(VERSION)
SONAME    := $(LIBNAME).so.$(SOMAJOR)

all: tg $(STATICLIB) $(SHAREDLIB) tg.pc

tg: $(CLI_SRC) $(LIB_SRC) $(HEADERS)
	$(CC) $(ALL_CFLAGS) -o $@ $(CLI_SRC) $(LIB_SRC)

# Library objects are built PIC so they can serve both the static and shared lib.
%.o: %.c $(HEADERS)
	$(CC) $(ALL_CFLAGS) -fPIC -c -o $@ $<

$(STATICLIB): $(LIB_OBJ)
	$(AR) rcs $@ $(LIB_OBJ)

$(SHAREDLIB): $(LIB_OBJ)
	$(CC) $(ALL_CFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(LIB_OBJ)

tg.pc: tg.pc.in VERSION
	sed -e 's:@PREFIX@:$(PREFIX):' -e 's:@VERSION@:$(VERSION):' tg.pc.in > $@

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 tg $(DESTDIR)$(BINDIR)/tg
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 644 $(STATICLIB) $(DESTDIR)$(LIBDIR)/$(STATICLIB)
	$(INSTALL) -m 755 $(SHAREDLIB) $(DESTDIR)$(LIBDIR)/$(SHAREDLIB)
	ln -sf $(SHAREDLIB) $(DESTDIR)$(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/$(LIBNAME).so
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)/tg
	$(INSTALL) -m 644 $(HEADERS) $(DESTDIR)$(INCLUDEDIR)/tg/
	$(INSTALL) -d $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 644 tg.pc $(DESTDIR)$(PKGCONFIGDIR)/tg.pc
	$(INSTALL) -d $(DESTDIR)$(MANDIR)/man1 $(DESTDIR)$(MANDIR)/man3 $(DESTDIR)$(MANDIR)/man5
	$(INSTALL) -m 644 man/tg.1 $(DESTDIR)$(MANDIR)/man1/tg.1
	$(INSTALL) -m 644 man/libtg.3 $(DESTDIR)$(MANDIR)/man3/libtg.3
	$(INSTALL) -m 644 man/tg.5 $(DESTDIR)$(MANDIR)/man5/tg.5

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/tg
	rm -f $(DESTDIR)$(LIBDIR)/$(STATICLIB) $(DESTDIR)$(LIBDIR)/$(SHAREDLIB)
	rm -f $(DESTDIR)$(LIBDIR)/$(SONAME) $(DESTDIR)$(LIBDIR)/$(LIBNAME).so
	rm -rf $(DESTDIR)$(INCLUDEDIR)/tg
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/tg.pc
	rm -f $(DESTDIR)$(MANDIR)/man1/tg.1 $(DESTDIR)$(MANDIR)/man3/libtg.3 $(DESTDIR)$(MANDIR)/man5/tg.5

dist:
	git archive --format=tar.gz --prefix=tg-$(VERSION)/ -o tg-$(VERSION).tar.gz HEAD

clean:
	rm -f tg $(STATICLIB) $(LIBNAME).so* $(LIB_OBJ) tg.pc tg-*.tar.gz

.PHONY: all install uninstall dist clean
