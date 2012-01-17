CC?=gcc
INSTALL?=install
DESTDIR=/usr/bin

all: scam-o-matic.c
	$(CC) -o scam-o-matic scam-o-matic.c
	
install:
	$(INSTALL) scam-o-matic $(DESTDIR) 