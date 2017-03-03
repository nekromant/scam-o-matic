CC?=gcc
INSTALL?=install
DESTDIR=/usr/bin
CFLAGS=-Wall -Werror -O3 -flto

all: scam-o-matic

scam-o-matic: scam-o-matic.c Makefile
	$(CC) $(CFLAGS) -o scam-o-matic scam-o-matic.c

clean:
	-rm scam-o-matic

install:
	$(INSTALL) scam-o-matic $(DESTDIR)

.PHONY: all clean install
