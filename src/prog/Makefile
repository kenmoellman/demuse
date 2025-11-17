# src/prog/Makefile - Build libprog.a (function evaluation library)
#
# MODERNIZATION NOTES (2025):
# - Removed gperf dependency (was used for perfect hash generation)
# - Now uses simple binary search for function lookup
# - Much simpler build process

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

TOPDIR = ..
LIBS = 

# Target library
LIB = libprog.a

# Source files
SRCS = ctrl.c hash_table.c hash.c eval.c
OBJS = ctrl.o hash_table.o hash.o eval.o

# Build rules
all: $(LIB)

$(LIB): $(OBJS)
	rm -f $(LIB)
	ar qcv $(LIB) $(OBJS)
	-ranlib $(LIB)

# eval.o depends on funcs.c being included
eval.o: eval.c 
	$(CC) $(CFLAGS) -c eval.c

ctrl.o: ctrl.c
	$(CC) $(CFLAGS) -c ctrl.c

hash_table.o: hash_table.c
	$(CC) $(CFLAGS) -c hash_table.c

hash.o: hash.c
	$(CC) $(CFLAGS) -c hash.c

# Installation
install: all

# Cleanup
clean:
	rm -f *.o $(LIB)

# Remove old generated files if they exist
distclean: clean
	rm -f funcs.c.gp.bak
	@echo "Old gperf files removed (if any existed)"

.PHONY: all install clean distclean
