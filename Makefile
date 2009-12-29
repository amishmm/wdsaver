CC=cc -Wall -O3
BINDIR=/usr/bin

all: wdsaver

install: all
	strip wdsaver 
	install -g root -o root -p -v --mode=700 wdsaver $(BINDIR)

wdsaver:
	$(CC) wdsaver.c -o wdsaver

.PHONY: clean
clean: 
	rm -f wdsaver
