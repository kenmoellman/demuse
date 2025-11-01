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

LDFLAGS = -pie
LIBS = -lm -lcrypt

# Top directory
TOPDIR = ..

# Source files
SRCS = admin.c cque.c game.c match.c move.c player.c powerlist.c powers.c \
       predicates.c stringutil.c timer.c unparse.c utils.c wild.c conf.c maze.c

# Object files
OBJS = $(SRCS:.c=.o)

# Library dependencies
LIBDEPS = ../comm/libcomm.a ../db/libdb.a ../io/libio.a ../prog/libprog.a

# Executable name
EXECUTABLE = netmuse

# Targets
.PHONY: all netmuse install clean depend

all: $(EXECUTABLE)

netmuse: all

install: all
	-mv -f ../../bin/$(EXECUTABLE) ../../bin/$(EXECUTABLE)~
	install $(EXECUTABLE) ../../bin

$(EXECUTABLE): $(OBJS) $(LIBDEPS)
	@echo "Linking: $@"
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBDEPS) $(LIBS)

# Pattern rule for object files
%.o: %.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	@echo "Cleaning directory"
	$(RM) $(OBJS) $(EXECUTABLE) .depend

# Generate dependencies automatically
depend: .depend

.depend: $(SRCS)
	@echo "Generating dependencies"
	$(RM) .depend
	$(CC) $(CFLAGS) -MM $(SRCS) > .depend

# Include dependencies if they exist
-include .depend
