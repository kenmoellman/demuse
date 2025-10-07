# Makefile for comm directory
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
SRCS = broadcast.c color.c com.c create.c datetime.c dbtop.c email.c emit.c help.c info.c \
       look.c lstats.c page.c paste.c pcmds.c player.c pueblo.c rob.c set.c speech.c


# Object files
OBJS = $(SRCS:.c=.o)

# Library name
LIBRARY = libcomm.a

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
	@echo "Cleaning comm directory"
	$(RM) $(OBJS) $(LIBRARY) .depend

# Generate dependencies automatically
depend: .depend

.depend: $(SRCS)
	@echo "Generating dependencies"
	$(RM) .depend
	$(CC) $(CFLAGS) -MM $(SRCS) > .depend

# Include dependencies if they exist
-include .depend



## Automatically generated from "domakefile" script. Do not edit by hand.
##
#CC = gcc
##CFLAGS = -Wall -g  -I../hdrs -I../../config 
#CFLAGS = -Wall -g  -I../hdrs -I../../config 
#TOPDIR = ../..
#LIBS = -lcrypt
#
#all:  libcomm.a
#
#install: all
#
#libcomm.a: admin.o color.o create.o dbtop.o help.o look.o pcmds.o player.o paste.o rob.o set.o speech.o com.o info.o lstats.o time.o
#	rm -f libcomm.a
#	ar qcv libcomm.a admin.o color.o create.o dbtop.o help.o look.o pcmds.o player.o paste.o rob.o set.o speech.o com.o info.o lstats.o time.o
#	-ranlib libcomm.a
#
