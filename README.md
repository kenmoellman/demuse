## Project Overview

deMUSE is a TinyMUSE-derived Multi-User Simulation Environment (MUSE) server, originally created by Ken Moellman as a fork of TinyMUSE '97. This is a text-based MUD (Multi-User Dungeon) server written in C that allows multiple users to connect via telnet and interact in a virtual world. The branch has the goal of creating a MUSE server which would be more stable, secure and configurable, and less platform specific. It borrows code from other platforms such as TinyMAZE. The codebase has been recently modernized (2025) with significant safety improvements, ANSI C compliance, and modern memory management while maintaining compatibility with the original MUSE database format.

deMUSE is now developed on Ubuntu Linux, after previously being developed under Slackware Linux. It should compile in other versions of Linux.

Future planned improvements include moving the database from flat file to MySQL/MariaDB.

You can find the latest patches, sourcecode, etc, at https://github.com/kenmoellman/demuse


This software come with no guarentee, and you use it at your own risk.


## Build Commands

### Building the Server
```bash
cd src
make install
```

This recursively builds all subdirectories and installs the `netmuse` executable to `bin/netmuse`.

### Building Individual Components
```bash
cd src/comm   # Communication/commands
make all

cd src/db     # Database
make all

cd src/io     # Network I/O
make all

cd src/muse   # Core game logic (produces netmuse executable)
make all

cd src/prog   # Expression evaluator
make all

cd src/util   # Utilities
make all
```

### Cleaning
```bash
cd src
make clean
```

Removes object files, executables, and dependency files.

### Building Utilities
Individual utilities are built in `src/util/`:
- `wd` - Watchdog daemon
- `mycompress` - Database compression utility
- `mkindx` - Help/news index builder

## Architecture Overview

### Directory Structure

- **src/comm/** - Player commands and communication systems (broadcasting, mail, pages, speech, help)
- **src/db/** - Database management (loading, saving, object manipulation, boolean expressions, inheritance)
- **src/io/** - Network I/O layer (socket handling, connections, input/output processing, idle monitoring, guest management)
- **src/muse/** - Core game engine (main loop, command queue, game logic, player management, movement, powers, matching)
- **src/prog/** - Expression parser and function evaluator (hash-based function dispatch, merged from funcs.c)
- **src/util/** - Standalone utilities (compression, indexing, password tools)
- **src/hdrs/** - Header files
- **config/** - Configuration system (config.h and config.c)
- **run/** - Runtime directory (database, logs, message files)
- **doc/** - Installation and administration documentation

### Key Components

**Database System** (`src/db/`)
- Flat-file database stored in `run/db/`
- Objects identified by `dbref` (typedef'd as `long`)
- Object types: Player, Room, Exit, Thing
- Attribute system for extensible object properties
- Boolean expression locks for access control

**Network I/O** (`src/io/`)
- Main entry point: `server_main.c:main()`
- Descriptor-based connection management
- Text queue system for buffered output
- Idle monitoring and automatic disconnect
- Guest character management
- Connection state machine

**Expression Evaluator** (`src/prog/`)
- `eval.c` - Main expression parser (merged with function dispatch)
- Hash table-based function lookup (replaced gperf)
- Supports nested function calls with recursion limits
- Buffer overflow protection throughout

**Command System** (`src/comm/`)
- Player commands (@-commands, say, pose, etc.)
- Communication channels (pages, broadcasts, mail)
- Help system with indexed text files
- Object creation and manipulation

**Game Logic** (`src/muse/`)
- Command queue execution
- Player matching and object lookup
- Movement and teleportation
- Permission system (powers framework)
- Time-based events

### Modernization Features (2025)

The codebase has been significantly modernized with:

1. **Memory Safety**
   - `SAFE_MALLOC`/`SMART_FREE` system replaces raw malloc/free
   - Memory debugging with `MEMORY_DEBUG_LOG` (logs to `./logs/malloc-debug.log`)
   - Buffer overflow protection (all `strcpy`→`strncpy`, `sprintf`→`snprintf`)

2. **Security Hardening**
   - `GoodObject()` validation before all database access unless a deleted object is okay (such as dumping the database)
   - Bounds checking on array access
   - Recursion depth limits in expression evaluator
   - Stack protection (`-fstack-protector-strong`)

3. **Code Quality**
   - ANSI C prototypes throughout (removed K&R style)
   - Compiler warnings enabled (`-Wall -Wextra -pedantic`)
   - Security warnings (`-Wformat-security`, `-Werror=format-security`)
   - Position Independent Executable (`-fPIE -pie`)

4. **Architecture Improvements**
   - Merged `funcs.c` into `eval.c` (eliminated gperf dependency)
   - Binary search for function dispatch
   - Unified hash system across components

## Configuration

**Primary config files:**
- `config/config.h` - Compile-time configuration (network options, features, limits)
- `config/config.c` - Runtime configuration values

**Key configuration options in config.h:**
- `MULTIHOME` - Multi-homed server support
- `HOST_LOOKUPS` - Reverse DNS lookups (may cause lag on slow DNS)
- `MEMORY_DEBUG_LOG` - Enable memory allocation debugging
- `USE_UNIV` - Universe power expansion (incomplete feature)
- Network port and database paths

## Runtime Files

**Database:**
- `run/db/mdb` - Main database file
- `run/db/initial.mdb` - Starting database for new installations

**Logs:**
- `run/logs/` - Server logs (connections, commands, errors)
- `run/logs/malloc-debug.log` - Memory debugging (if enabled)

**Messages:**
- `run/msgs/helptext` - Help content
- `run/msgs/newstext` - News content
- `run/msgs/helpindx` - Help index (built with `bin/mkindx help`)
- `run/msgs/newsindx` - News index (built with `bin/mkindx news`)

## Development Notes

**Things to do**
- convert to using ANSI C instead of K&R C
- Modernize the code to replace deprecated functions and improve logic 
- Look for potential buffer overruns and prevent them 
- We should use SAFE_MALLOC and SMART_FREE to malloc and free. 
- Better safety - Use snprintf() and strncpy() instead of sprintf()/strcpy(), etc 
- Extensive validation - GoodObject() checks throughout all functions, except where finding a deleted object is okay like when dumping the database
- Better documentation - More detailed explanations of logic and functions, and better inline comments  
- Improved header - Comprehensive modernization notes 
- Explain security vulnerabilities clearly when unable to be rectified


**Converting int to long**
- In the past dbref was defined as an int, and then later it was changed to be a long, but there were still places in the code where this wasn't correctly implemented.
- Need to utilize recently-created macro DBREF_FMT defined in config.h to replace instances where variables of type dbref are being placed into a string, instead of having a hardcoded %d or %ld 
- Need to watch for and fix places where variables were type int that are getting value of strlen which is now a size_t 

### Working with the Database

All database access must validate dbrefs:
```c
if (!GoodObject(thing)) {
    // Handle invalid reference
    return;
}
```

The `dbref` type is defined as `long` in `config/config.h` and `src/hdrs/db.h`.

### Memory Management

Always use safe allocation:
```c
char *str = (char *)SAFE_MALLOC(size);
// Use str
SMART_FREE(str);
```

Never use raw `malloc()`/`free()` - the codebase has been converted to tracked allocation.

### String Operations

Use bounded functions:
- `strncpy()` instead of `strcpy()`
- `snprintf()` instead of `sprintf()`
- Always null-terminate manually after `strncpy()`

### Function Evaluation

The expression evaluator in `src/prog/eval.c` has recursion limits:
- Normal users: `MAX_FUNC_RECURSION` (15000)
- Guest users: `GUEST_FUNC_RECURSION` (1000)

### Compiler Flags

The muse/ Makefile uses strict compilation:
- C11 standard (`-std=c11`)
- Security hardening (`-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`)
- Warning as error for format security
- Position independent executable
- CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -O2 -I../hdrs -I../../config -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wformat=2 -Wformat-security -Werror=format-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -pie -Wconversion -Wsign-conversion -Wshadow -Wstrict-overflow=5

### Database Format

The database is a flat-file format that remains compatible with:
- TinyMUSE 1.8a4 and earlier
- TinyMUSE '97
- Earlier deMUSE versions

Do not change the database I/O functions without ensuring backward compatibility.

## Known Issues and Limitations

From the TODO file, known unresolved issues include:
- Idle system timing anomalies
- Prefix/suffix recursion bugs with high idle times
- Function memory leaks (being addressed)
- @booting yourself has bugs
- Most MAZE combat features not implemented (maze.c)
- Universe code incomplete

## Historical Context

- Original base: TinyMUD by Larry Foard
- Evolved through: TinyMUSH → TinyMUSE → TinyMUSE '97 → deMUSE
- Created for deMUSEcracy, an online government simulation
- Originally developed on Slackware Linux 4.0
- Ancient codebase with multiple authors over decades
- Recent modernization effort (2025) for safety and security
