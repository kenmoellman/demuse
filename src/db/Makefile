# Makefile for db directory
# Modernized with proper dependencies and ANSI C compliance

# Compiler and flags
CC = gcc
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
SRCS = boolexp.c \
       db_io.c \
       destroy.c \
       inherit.c \
       warnings.c

# Object files
OBJS = $(SRCS:.c=.o)

# Dependency files
DEPS = $(SRCS:.c=.d)

# Library name
LIBRARY = libdb.a

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



#
#
## Automatically generated from "domakefile" script. Do not edit by hand.
##
#CC = gcc
#CFLAGS = -Wall -g  -I../hdrs -I../../config
#TOPDIR = ..
#LIBS = 
#
#all:  libdb.a
#
#install: all
#
#libdb.a: boolexp.o db.o destroy.o inherit.o warnings.o 
#	rm -f libdb.a
#	ar qcv libdb.a boolexp.o db.o destroy.o inherit.o warnings.o 
#	-ranlib libdb.a
#
