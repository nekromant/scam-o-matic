CC?=gcc
INSTALL?=install
DESTDIR=/usr/bin
CFLAGS=-Wall -Werror

all: scam-o-matic.c
	$(CC) $(CFLAGS) -o scam-o-matic scam-o-matic.c
	
install:
	$(INSTALL) scam-o-matic $(DESTDIR)
