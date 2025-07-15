# Automatically generated from "domakefile" script. Do not edit by hand.
#
CC = gcc
#CFLAGS = -Wall -g  -I../hdrs -I../../config 
CFLAGS = -Wall -g  -I../hdrs -I../../config 
TOPDIR = ../..
LIBS = -lcrypt

all:  libcomm.a

install: all

libcomm.a: admin.o color.o create.o dbtop.o help.o look.o pcmds.o player.o paste.o rob.o set.o speech.o com.o info.o lstats.o time.o
	rm -f libcomm.a
	ar qcv libcomm.a admin.o color.o create.o dbtop.o help.o look.o pcmds.o player.o paste.o rob.o set.o speech.o com.o info.o lstats.o time.o
	-ranlib libcomm.a

