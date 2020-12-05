_CFLAGS=$(CFLAGS) -Wall -D_GNU_SOURCE
_LDFLAGS=$(LDFLAGS) -lcrypt
PREFIX?=/usr/local
MANDIR?=$(DESTDIR)$(PREFIX)/man

OBJS=doas.o env.o shadowauth.o persist.o y.tab.o			\
	 bsd-compat/closefrom.o bsd-compat/errc.o 			\
	 bsd-compat/explicit_bzero.o bsd-compat/pledge.o		\
	 bsd-compat/readpassphrase.o bsd-compat/reallocarray.o		\
	 bsd-compat/setprogname.o bsd-compat/strlcat.o			\
	 bsd-compat/strlcpy.o bsd-compat/strtonum.o bsd-compat/unveil.o

ifdef STATE_DIR
_CFLAGS += -DDOAS_STATE_DIR='"'$(STATE_DIR)'"'
endif
ifdef PERSIST_TIMEOUT
_CFLAGS += -DDOAS_PERSIST_TIMEOUT='"'$(PERSIST_TIMEOUT)'"'
endif
ifdef CONF_FILE
_CFLAGS += -DDOAS_CONF_FILE='"'$(CONF_FILE)'"'
endif
ifdef SAFE_PATH
_CFLAGS += -DDOAS_SAFE_PATH='"'$(SAFE_PATH)'"'
endif
ifdef DEFAULT_PATH
_CFLAGS += -DDOAS_DEFAULT_PATH='"'$(DEFAULT_PATH)'"'
endif
ifdef DEFAULT_UMASK
_CFLAGS += -DDOAS_DEFAULT_UMASK='"'$(DEFAULT_UMASK)'"'
endif

all: doas

doas: $(OBJS)
	$(CC) -o doas *.o bsd-compat/*.o $(_LDFLAGS)

%.o: %.c version.h
	$(CC) $(_CFLAGS) -c $< -o $@

version.h:
	printf "const char *version = \"doas r%s.%s\";\n" \
		$$(git rev-list --count HEAD) \
		$$(git rev-parse --short HEAD) > version.h

y.tab.o:
	yacc parse.y
	$(CC) $(_CFLAGS) -c y.tab.c -o y.tab.o

clean:
	rm -f doas
	rm -f $(OBJS) y.tab.c
	rm -f version.h

install: doas
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp doas $(DESTDIR)$(PREFIX)/bin/
	chmod 4755 $(DESTDIR)$(PREFIX)/bin/doas
	mkdir -p $(MANDIR)/man1
	cp doas.1 $(MANDIR)/man1/doas.1
	mkdir -p $(MANDIR)/man5
	cp doas.conf.5 $(MANDIR)/man5/doas.conf.5

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/doas
	rm -f $(MANDIR)/man1/doas.1
	rm -f $(MANDIR)/man5/doas.conf.5
