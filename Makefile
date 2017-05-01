_CFLAGS=$(CFLAGS) -D_GNU_SOURCE
_LDFLAGS=$(LDFLAGS) -lcrypt
CC=gcc

OBJS=doas.o env.o shadowauth.o persist.o y.tab.o			\
	 bsd-compat/closefrom.o bsd-compat/errc.o bsd-compat/pledge.o	\
	 bsd-compat/readpassphrase.o bsd-compat/reallocarray.o		\
	 bsd-compat/setprogname.o bsd-compat/strlcat.o			\
	 bsd-compat/strlcpy.o bsd-compat/strtonum.o

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
