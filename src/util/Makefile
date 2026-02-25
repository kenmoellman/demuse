# Makefile for util directory
# Modernized with MariaDB convert_db tool support

CC = gcc
CFLAGS = -Wall -g  -I../hdrs -I../../config
TOPDIR = ../..
LIBS =

# Get MariaDB flags dynamically (for convert_db)
MARIADB_CFLAGS := $(shell pkg-config --cflags libmariadb 2>/dev/null || mariadb_config --cflags 2>/dev/null)
MARIADB_LIBS := $(shell pkg-config --libs libmariadb 2>/dev/null || mariadb_config --libs 2>/dev/null)

# Base targets
TARGETS = mkindx mycompress wd

# Add convert_db if MariaDB is available
ifneq ($(MARIADB_CFLAGS),)
TARGETS += convert_db
endif

all: $(TARGETS)

install: all
	-mv -f ../../bin/mkindx ../../bin/mkindx~
	install mkindx ../../bin
	-mv -f ../../bin/mycompress ../../bin/mycompress~
	install mycompress ../../bin
	-mv -f ../../bin/wd ../../bin/wd~
	install wd ../../bin
ifneq ($(MARIADB_CFLAGS),)
	-mv -f ../../bin/convert_db ../../bin/convert_db~
	install convert_db ../../bin
endif

# convert_db - database conversion tool (requires MariaDB)
convert_db: convert_db.c
	$(CC) $(CFLAGS) $(MARIADB_CFLAGS) -o convert_db convert_db.c $(MARIADB_LIBS)

clean:
	$(RM) mkindx mycompress wd convert_db
