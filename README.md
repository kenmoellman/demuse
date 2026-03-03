## Project Overview

deMUSE is a TinyMUSE-derived Multi-User Simulation Environment (MUSE) server, originally created by Ken Moellman as a fork of TinyMUSE '97. This is a text-based MUD (Multi-User Dungeon) server written in C that allows multiple users to connect via telnet and interact in a virtual world. The branch has the goal of creating a MUSE server which would be more stable, secure and configurable, and less platform specific. It borrows code from other platforms such as TinyMAZE. The codebase has been recently modernized (2025) with significant safety improvements, ANSI C compliance, and modern memory management while maintaining compatibility with the original MUSE database format.

deMUSE is now developed on Ubuntu Linux, after previously being developed under Slackware Linux. It should compile in other versions of Linux.

Future planned improvements include moving the object database from flat file to MariaDB.

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
- `convert_db` - Database migration tool (flat-file mail/board/channels to MariaDB)

## Architecture Overview

### Directory Structure

- **src/comm/** - Player commands and communication systems (broadcasting, mail, pages, speech, help)
- **src/db/** - Database management (loading, saving, object manipulation, boolean expressions, inheritance)
- **src/io/** - Network I/O layer (socket handling, connections, input/output processing, idle monitoring, guest management)
- **src/muse/** - Core game engine (main loop, command queue, game logic, player management, movement, powers, matching)
- **src/prog/** - Expression parser and function evaluator (hash-based function dispatch, merged from funcs.c)
- **src/util/** - Standalone utilities (compression, indexing, password tools, database conversion)
- **src/hdrs/** - Header files
- **config/** - Configuration system (config.h, MariaDB setup SQL, defaults)
- **run/** - Runtime directory (database, logs, message files)
- **doc/** - Installation and administration documentation

### Key Components

**Database System** (`src/db/`)
- Flat-file database stored in `run/db/`
- Objects identified by `dbref` (typedef'd as `long`)
- Object types: Player, Room, Exit, Thing (Channel type deprecated — migrated to MariaDB)
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
- Communication channels (`+channel` for admin/config, `+com`/`=` for speaking)
- Private mail (`+mail`) and public board (`+board`)
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

### MariaDB Integration (2026)

MariaDB is now required for server operation. The following subsystems use MariaDB:

1. **Configuration** - All `@config` settings stored in MariaDB `config` table (config.c eliminated)
2. **Private Mail (+mail)** - Player-to-player mail stored in MariaDB `mail` table
3. **Public Board (+board)** - Board posts stored in MariaDB `board` table
4. **Channels** - Channel definitions and memberships stored in MariaDB `channels` and `channel_members` tables (TYPE_CHANNEL objects deprecated)

The object database continues to use the flat-file format for backward compatibility.

**Automatic migration:** On first startup after upgrading, the server detects legacy data and automatically converts it:
- **Mail/Board:** Legacy mail/board data appended to the flat-file database is converted via `bin/convert_db`.
- **Channels:** Legacy TYPE_CHANNEL objects in the flat-file database are converted to MariaDB `channels` and `channel_members` tables. Player A_CHANNEL and A_BANNED attributes are parsed and migrated, then cleared. Old channel objects are marked for garbage collection. This is a one-time process; subsequent startups skip conversion.

**Key files:**
- `src/db/mariadb.c` - Connection management and config operations
- `src/db/mariadb_mail.c` - SQL operations for private mail
- `src/db/mariadb_board.c` - SQL operations for board posts
- `src/db/mariadb_channel.c` - SQL operations and in-memory cache for channels
- `src/hdrs/mariadb_mail.h` / `mariadb_board.h` / `mariadb_channel.h` - Declarations and stubs
- `src/util/convert_db.c` - Standalone database migration tool (`--mail`, `--board`, `--channels`, `--all`)
- `config/setup_mariadb.sql` - Table definitions

**Dependencies:** `libmariadb-dev` (auto-detected by Makefiles via pkg-config)

### Channel System (2026)

Channels are stored in MariaDB with an in-memory cache for performance. The old TYPE_CHANNEL db[] objects have been deprecated. Channel definitions live in the `channels` table and per-player memberships (including bans, operators, aliases, per-player colors, mute state) live in the `channel_members` table.

**Architecture:**
- Three hash tables cache all channel data in memory: `channel_name_hash`, `channel_id_hash`, and `channel_members` (per-player linked lists)
- All mutations use write-through: MariaDB first, then cache update
- Message delivery (`com_send_int`) reads entirely from cache — no SQL queries per message
- Lock evaluation uses `channel_eval_lock()` with the channel owner as context object
- Channel access levels are controlled by `min_level` (0=anyone, 1=official, 2=builder, 3=director) instead of name prefixes; prefixes (`*`, `.`, `_`) are displayed but not stored

**Commands:**

- **`+channel <subcommand>`** - All channel administration and control:
  `create`, `destroy`, `join`, `leave`, `default`, `alias`, `op`, `lock`, `password`, `boot`, `ban`, `unban`, `list`, `search`, `log`, `color`, `who`, `mute`, `unmute`
- **`+com <channel>=<message>`** - Speaking on channels (say, pose, think, directed messages)
- **`=<message>`** - Shortcut to speak on default channel

**Examining players** shows a `Channels:` line listing the player's default channel first, followed by remaining channels sorted alphabetically (ignoring access-level prefixes).

**Function naming convention** in `src/comm/com.c`:
- `channel_*` - Subcommand handlers (e.g., `channel_create`, `channel_join`)
- `channel_cache_*` - Cache operations (e.g., `channel_cache_lookup`, `channel_cache_get_member`)
- `channel_int_*` - Legacy compatibility wrappers (thin wrappers around cache, kept during transition)
- `do_channel`, `do_com`, `do_chemit` - Parser hooks (unchanged)

**Configurable system channels** (via `@config`):
- `chan_dbinfo` - Database info channel (default: `dbinfo`)
- `chan_dc` - Disconnect channel (default: `*dc`)
- `chan_pubio` - Public I/O monitoring channel (default: `pub_io`)
- `chan_connect` - Connection announcement channel (default: `connect`)
- `chan_warn_prefix` - Prefix for warning channels (default: `warn_`), e.g. `warn_security`, `warn_roomdesc`

**Log channels** are hardcoded system channels (not configurable via `@config`). Each log writes to a file in `run/logs/` and broadcasts to its channel:
- `log_imp` - Important events (shutdowns, name changes, admin commands)
- `log_sens` - Sensitive events (min_level=3, director-only)
- `log_err` - Errors
- `log_io` - I/O events (min_level=3, director-only)
- `log_gripe` - Player gripes
- `log_force` - @force usage (min_level=3, director-only)
- `log_prayer` - Player prayers
- `log_combat` - Combat events
- `log_suspect` - Suspect activity (min_level=3, director-only)

**System channels** have `is_system=1` in the database and cannot be destroyed. Default system channels are seeded by `config/defaults.sql`. The 13 built-in system channels are: the 9 log channels above plus `dbinfo`, `dc`, `pub_io`, and `connect`.

## Configuration

**Primary config files:**
- `config/config.h` - Compile-time configuration (network options, features, limits)
- `config/setup_mariadb.sql` - MariaDB table definitions (config, mail, board, channels, channel_members)
- `config/defaults.sql` - Default configuration values (seeded on install)
- `run/db/mariadb.conf` - MariaDB credentials (not in version control)

**Key configuration options in config.h:**
- `MULTIHOME` - Multi-homed server support
- `HOST_LOOKUPS` - Reverse DNS lookups (may cause lag on slow DNS)
- `MEMORY_DEBUG_LOG` - Enable memory allocation debugging
- `USE_UNIV` - Universe power expansion (incomplete feature)
- Network port and database paths

## Runtime Files

**Database:**
- `run/db/mdb` - Main object database file (flat-file format)
- `run/db/initial.mdb` - Starting database for new installations
- `run/db/mariadb.conf` - MariaDB connection credentials
- MariaDB tables: `config`, `mail`, `board`, `channels`, `channel_members` (created automatically on startup)

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
- convert to using ANSI C instead of K&R C - should be completed, but need to verify.
- Modernize the code to replace deprecated functions and improve logic - always in progress
- Look for potential buffer overruns and prevent them - always in progress
- We should use SAFE_MALLOC and SMART_FREE to malloc and free - should be completed, but need to verify.
- Better safety - Use snprintf() and strncpy() instead of sprintf()/strcpy(), etc - should be completed, but need to verify.
- Extensive validation - GoodObject() checks throughout all functions, except where finding a deleted object is okay like when dumping the database - should be completed, but need to verify.
- Better documentation - More detailed explanations of logic and functions, and better inline comments  - always in progress
- Improved header - Comprehensive modernization notes  - in progress

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

The object database is a flat-file format that remains compatible with:
- TinyMUSE 1.8a4 and earlier
- TinyMUSE '97
- Earlier deMUSE versions

Do not change the database I/O functions without ensuring backward compatibility.

Mail and board messages are stored in MariaDB (migrated from the flat-file format as of 2026). Legacy databases with messages appended after `***END OF DUMP***` are automatically converted on first startup.

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
