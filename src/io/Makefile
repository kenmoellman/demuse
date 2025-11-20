# Makefile for io directory
# Modernized with proper dependencies and ANSI C compliance

# Compiler and flags
CC = gcc
#CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -O2 \
#         -I../hdrs -I../../config \
#         -D_POSIX_C_SOURCE=200809L \
#         -D_DEFAULT_SOURCE \
#         -Wformat=2 -Wformat-security \
#         -D_FORTIFY_SOURCE=2 -fstack-protector-strong

CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -O2 \
         -I../hdrs -I../../config \
         -D_POSIX_C_SOURCE=200809L \
         -D_DEFAULT_SOURCE \
         -Wformat=2 -Wformat-security \
         -Werror=format-security \
         -D_FORTIFY_SOURCE=2 \
         -fstack-protector-strong \
         -fPIE -pie \
         -Wconversion -Wsign-conversion \
         -Wshadow -Wstrict-overflow=5

LDFLAGS = 
LIBS = -lcrypt 

# Get curl flags dynamically (for rlpage if needed)
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)

# Add curl to existing flags if available
ifneq ($(CURL_CFLAGS),)
CFLAGS += $(CURL_CFLAGS)
endif
ifneq ($(CURL_LIBS),)
LDFLAGS += $(CURL_LIBS)
endif

# Top directory
TOPDIR = ../..

# Source files - NEW MODULAR STRUCTURE
# Refactored from bsd.c into logical modules:
SRCS = server_main.c \
       descriptor_mgmt.c \
       output_handler.c \
       input_handler.c \
       connection_handler.c \
       idle_monitor.c \
       text_queue.c \
       guest_mgmt.c \
       who.c \
       signal.c \
       sock.c \
       log.c \
       newconc.c \
       rlpage.c \
       nalloc.c \
       io_globals.c \
       color.c \
       lstats.c

# NOTE: color.c and lstats.c moved from comm/ directory (2025 reorganization)
# These are I/O infrastructure files:
# - color.c: ANSI/Pueblo color output formatting
# - lstats.c: Login statistics tracking

# Object files
OBJS = $(SRCS:.c=.o)

# Dependency files
DEPS = $(SRCS:.c=.d)

# Library name
LIBRARY = libio.a

# Targets
.PHONY: all install clean depend

all: $(LIBRARY)

install: all

$(LIBRARY): $(OBJS)
	@echo "Building library: $@"
	$(RM) $@
	$(AR) rcs $@ $(OBJS)
	ranlib $@

# Pattern rule for object files with automatic dependency generation
%.o: %.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Clean target
clean:
	@echo "Cleaning io directory"
	$(RM) $(OBJS) $(LIBRARY) $(DEPS)

# Generate dependencies automatically
depend: .depend

.depend: $(SRCS)
	@echo "Generating dependencies"
	$(RM) .depend
	$(CC) $(CFLAGS) -MM $(SRCS) > .depend

# Include dependencies if they exist
-include $(DEPS)

# Help target
help:
	@echo "Available targets:"
	@echo "  all      - Build libio.a (default)"
	@echo "  clean    - Remove all generated files"
	@echo "  depend   - Generate dependency information"
	@echo "  help     - Show this help message"
	@echo ""
#	@echo "Refactored modules (from bsd.c):"
#	@echo "  server_main.c         - Main loop and initialization"
#	@echo "  descriptor_mgmt.c     - Descriptor lifecycle"
#	@echo "  output_handler.c      - Output and notifications"
#	@echo "  input_handler.c       - Input processing"
#	@echo "  connection_handler.c  - Connection and authentication"
#	@echo "  idle_monitor.c        - Idle detection"
#	@echo "  text_queue.c          - Text queue management"
#	@echo "  guest_mgmt.c          - Guest accounts"





#
## Makefile for io directory
## Modernized with proper dependencies and ANSI C compliance
#
## Compiler and flags
#CC = gcc
#CFLAGS = -Wall -Wextra -pedantic -ansi -std=c99 -g -O2 \
#         -I../hdrs -I../../config \
#         -D_POSIX_C_SOURCE=200809L \
#         -D_DEFAULT_SOURCE
#LDFLAGS = 
#LIBS = -lcrypt 
#
## Get curl flags dynamically
#CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
#CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)
#
## Add curl to existing CFLAGS and LDFLAGS
#CFLAGS += $(CURL_CFLAGS)
#LDFLAGS += $(CURL_LIBS)
#
## Top directory
#TOPDIR = ../..
#
## Source files
#SRCS = io_globals.c bsd.c who.c signal.c sock.c log.c newconc.c rlpage.c nalloc.c
#
#
## Object files
#OBJS = $(SRCS:.c=.o)
#
## Library name
#LIBRARY = libio.a
#
## Targets
#.PHONY: all install clean depend
#
#all: $(LIBRARY)
#
#install: all
#
#$(LIBRARY): $(OBJS)
#	@echo "Building library: $@"
#	$(RM) $@
#	$(AR) qcv $@ $(OBJS)
#	ranlib $@
#
## Pattern rule for object files
#%.o: %.c
#	@echo "Compiling: $<"
#	$(CC) $(CFLAGS) -c $< -o $@
#
## Clean target
#clean:
#	@echo "Cleaning io directory"
#	$(RM) $(OBJS) $(LIBRARY) .depend
#
## Generate dependencies automatically
#depend: .depend
#
#.depend: $(SRCS)
#	@echo "Generating dependencies"
#	$(RM) .depend
#	$(CC) $(CFLAGS) -MM $(SRCS) > .depend
#
## Include dependencies if they exist
#-include .depend
#
#
#
##
##
### Automatically generated from "domakefile" script. Do not edit by hand.
###
##CC = gcc
###CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -DREDHAT -I../hdrs -I../../config
##CFLAGS = -Wall -g -DHAVE_ANSIHEADERS -I../hdrs -I../../config 
##TOPDIR = ..
##LIBS = 
##
##all:  libio.a
##
##install: all
##
##libio.a: io_globals.o bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
##	rm -f libio.a
##	ar qcv libio.a io_globals.o bsd.o who.o signal.o sock.o log.o newconc.o rlpage.o nalloc.o
##	-ranlib libio.a
##
