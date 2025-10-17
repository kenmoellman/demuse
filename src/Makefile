# This script was created by Bobby Newmark. Chances are you'll need to 
# tinker with this if you compile on anything other than Slackware Linux.
# In the future I hope to fix domakefile.

CC = gcc
CFLAGS = -Wall -g  -I../src/hdrs -I../config  -lcrypt
TOPDIR = ..
LIBS = 

all: suball

install: all
	cd comm;${MAKE} install
	cd db;${MAKE} install
	cd io;${MAKE} install
	cd prog;${MAKE} install
	cd util;${MAKE} install
	cd muse;${MAKE} install

suball:
	cd comm;${MAKE} all
	cd db;${MAKE} all
	cd io;${MAKE} all
	cd prog;${MAKE} all
	cd util;${MAKE} all
	cd muse;${MAKE} all

symbols: all
	nm -pog */*.o | egrep -v '^$$' | awk -F: '$$2 != "" {print $$1 " " substr($$2,10)}' | awk '$$3~/^_/' | sort +2 > symbols
	gawk -f findsym.awk symbols

clean:
	rm -f */*.[oad] util/wd util/mycompress util/mkindx muse/netmuse */.depend


