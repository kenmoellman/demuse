# Automatically generated from "domakefile" script. Do not edit by hand.
#
CC = gcc
#CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -DREDHAT -I../hdrs -I../../config
CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -I../hdrs -I../../config 
TOPDIR = ..
LIBS = 

all:  libio.a

install: all

libio.a: bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
	rm -f libio.a
	ar qcv libio.a bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
	-ranlib libio.a

