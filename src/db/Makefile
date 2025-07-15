# Automatically generated from "domakefile" script. Do not edit by hand.
#
CC = gcc
CFLAGS = -Wall -g  -I../hdrs -I../../config
TOPDIR = ..
LIBS = 

all:  libdb.a

install: all

libdb.a: boolexp.o db.o destroy.o inherit.o warnings.o mail.o board.o
	rm -f libdb.a
	ar qcv libdb.a boolexp.o db.o destroy.o inherit.o warnings.o mail.o board.o
	-ranlib libdb.a

