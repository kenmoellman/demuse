# Automatically generated from "domakefile" script. Do not edit by hand.
#
CC = gcc
CFLAGS = -Wall -g  -I../hdrs -I../../config
TOPDIR = ..
LIBS = 

all:  libprog.a

install: all

libprog.a: ctrl.o hash.o eval.o
	rm -f libprog.a
	ar qcv libprog.a ctrl.o hash.o eval.o
	-ranlib libprog.a

funcs.c: funcs.c.gp
	gperf -t -G -p -D -r -j 0 -k '*' -N lookup_funcs_gperf -H hash_funcs_gperf < funcs.c.gp > funcs.c || (rm funcs.c;false)



eval.o: funcs.c


