
# Makefile for io directory
# Modernized with proper dependencies and ANSI C compliance

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -std=c99 -g -O2 \
         -I../hdrs -I../../config \
         -D_POSIX_C_SOURCE=200809L \
         -D_DEFAULT_SOURCE
LDFLAGS = 
LIBS = -lcrypt 

# Get curl flags dynamically
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)

# Add curl to existing CFLAGS and LDFLAGS
CFLAGS += $(CURL_CFLAGS)
LDFLAGS += $(CURL_LIBS)

# Top directory
TOPDIR = ../..

# Source files
SRCS = io_globals.c bsd.c who.c signal.c sock.c log.c newconc.c rlpage.c nalloc.c


# Object files
OBJS = $(SRCS:.c=.o)

# Library name
LIBRARY = libio.a

# Targets
.PHONY: all install clean depend

all: $(LIBRARY)

install: all

$(LIBRARY): $(OBJS)
	@echo "Building library: $@"
	$(RM) $@
	$(AR) qcv $@ $(OBJS)
	ranlib $@

# Pattern rule for object files
%.o: %.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	@echo "Cleaning io directory"
	$(RM) $(OBJS) $(LIBRARY) .depend

# Generate dependencies automatically
depend: .depend

.depend: $(SRCS)
	@echo "Generating dependencies"
	$(RM) .depend
	$(CC) $(CFLAGS) -MM $(SRCS) > .depend

# Include dependencies if they exist
-include .depend



#
#
## Automatically generated from "domakefile" script. Do not edit by hand.
##
#CC = gcc
##CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -DREDHAT -I../hdrs -I../../config
#CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -I../hdrs -I../../config 
#TOPDIR = ..
#LIBS = 
#
#all:  libio.a
#
#install: all
#
#libio.a: io_globals.o bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
#	rm -f libio.a
#	ar qcv libio.a io_globals.o bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
#	-ranlib libio.a
#
