# Automatically generated from "domakefile" script. Do not edit by hand.
#
CC = gcc
CFLAGS = -Wall -g  -I../hdrs -I../../config
TOPDIR = ..
LIBS = -lcrypt

all:  netmuse

install: all
	-mv -f ../../bin/netmuse ../../bin/netmuse~
	install netmuse ../../bin

netmuse: cque.o game.o match.o move.o players.o powerlist.o powers.o predicates.o stringutil.o timer.o unparse.o utils.o wild.o conf.o maze.o ../comm/libcomm.a ../db/libdb.a ../io/libio.a ../prog/libprog.a
#	$(CC) $(CFLAGS) $(LIBS) -o netmuse cque.o game.o match.o move.o  players.o powerlist.o powers.o predicates.o stringutil.o timer.o unparse.o utils.o wild.o conf.o maze.o ../comm/libcomm.a ../db/libdb.a ../io/libio.a ../prog/libprog.a -lc -lm
	$(CC) $(CFLAGS) -o netmuse cque.o game.o match.o move.o  players.o powerlist.o powers.o predicates.o stringutil.o timer.o unparse.o utils.o wild.o conf.o maze.o ../comm/libcomm.a ../db/libdb.a ../io/libio.a ../prog/libprog.a -lc -lm $(LIBS)

