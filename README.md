## Project Overview

deMUSE is a TinyMUSE-derived Multi-User Simulation Environment (MUSE) server, originally created by Ken Moellman as a fork of TinyMUSE '97. This is a text-based MUD (Multi-User Dungeon) server written in C that allows multiple users to connect via telnet and interact in a virtual world. The branch has the goal of creating a MUSE server which would be more stable, secure and configurable, and less platform specific. It borrows code from other platforms such as TinyMAZE. The codebase has been recently modernized (2025) with significant safety improvements, ANSI C compliance, and modern memory management while maintaining compatibility with the original MUSE database format.

deMUSE is now developed on Ubuntu Linux, after previously being developed under Slackware Linux. It should compile in other versions of Linux.

MariaDB is now required for server operation, handling configuration, mail, boards, news, help, channels, and lockouts. The object database still uses the flat-file format; migrating it to MariaDB is a future goal.

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
- Private mail (`+mail`), public board (`+board`), and admin news (`+news`)
- Help system (MariaDB-backed, two-level command/subcommand hierarchy)
- Object creation and manipulation

**Game Logic** (`src/muse/`)
- Command queue execution
- Player matching and object lookup
- Movement and teleportation
- Permission system (powers framework)
- Time-based events

### Modernization Features (2025-2026)

The codebase has been significantly modernized with:

1. **Memory Safety**
   - `SAFE_MALLOC`/`SMART_FREE` system replaces raw malloc/free (complete)
   - Memory debugging with `MEMORY_DEBUG_LOG` (logs to `./logs/malloc-debug.log`)
   - Buffer overflow protection: all `strcpy`→`strncpy`, `sprintf`→`snprintf` (complete)

2. **Security Hardening**
   - `GoodObject()` validation before all database access unless a deleted object is okay (such as dumping the database) — in progress, see eval.c audit notes
   - Fixed crash bugs in `@join` and `@summon` (unvalidated `lookup_player()` results)
   - Fixed crash risk in `calc_stats()` (NULL pows pointer, out-of-bounds array index)
   - Fixed crash risk in `do_teleport()` universe chain (three-deep unvalidated db[] access)
   - Bounds checking on array access
   - Recursion depth limits in expression evaluator
   - Stack protection (`-fstack-protector-strong`)

3. **Code Quality**
   - ANSI C prototypes throughout (K&R style fully removed, complete)
   - `DBREF_FMT` macro used for all dbref format specifiers (complete)
   - Compiler warnings enabled (`-Wall -Wextra -pedantic`)
   - Security warnings (`-Wformat-security`, `-Werror=format-security`)
   - Position Independent Executable (`-fPIE -pie`)

4. **Architecture Improvements**
   - Merged `funcs.c` into `eval.c` (eliminated gperf dependency)
   - Binary search for function dispatch
   - Unified hash system across components
   - `colorize()` rewritten: reads from const input, builds output in separate buffer (fixes adjacent color code bugs)
   - Unified MOTD system: `motd_msg` and `motd_msg_player` config vars in MariaDB, set via `+motd`
   - Message files (welcome, create, guest, register, leave, motd) moved to MariaDB config vars, editable via `@config`
   - Welcome message now sent on new direct socket connections (was missing from `initializesock()`)
   - Combat commands `#ifdef`'d out of parser (`USE_COMBAT_TM97`)
   - Unified +board and +news: news stored in board table with `NEWS_ROOM` sentinel, shared display/read/delete/undelete/purge code in messaging.c. Both support soft delete, per-player read tracking, and position-based numbering.

### MariaDB Integration (2026)

MariaDB is now required for server operation. The following subsystems use MariaDB:

1. **Configuration** - All `@config` settings stored in MariaDB `config` table (config.c eliminated)
2. **Private Mail (+mail)** - Player-to-player mail stored in MariaDB `mail` table
3. **Public Board (+board)** - Board posts stored in MariaDB `board` table with per-player read tracking (`board_read`)
4. **Admin News (+news)** - News articles stored in the `board` table with sentinel `board_room = -2` (`NEWS_ROOM`). `mariadb_news.c` functions are thin wrappers around `mariadb_board_*`. Only Wizards/POW_ANNOUNCE can post; all players can read. Guests can read news (unlike board).
5. **Help** - Help topics stored in MariaDB `help_topics` table with two-level hierarchy (command/subcommand)
6. **Channels** - Channel definitions and memberships stored in MariaDB `channels` and `channel_members` tables (TYPE_CHANNEL objects deprecated)
7. **Lockouts** - IP bans, player bans, and guest-IP bans stored in MariaDB `lockouts` table with in-memory cache

The object database continues to use the flat-file format for backward compatibility.

**Automatic migration:** On first startup after upgrading, the server detects legacy data and automatically converts it:
- **Mail/Board:** Legacy mail/board data appended to the flat-file database is converted via `bin/convert_db`.
- **Channels:** Legacy TYPE_CHANNEL objects in the flat-file database are converted to MariaDB `channels` and `channel_members` tables. Player A_CHANNEL and A_BANNED attributes are parsed and migrated, then cleared. Old channel objects are marked for garbage collection. This is a one-time process; subsequent startups skip conversion.

**Key files:**
- `src/db/mariadb.c` - Connection management and config operations
- `src/db/mariadb_mail.c` - SQL operations for private mail
- `src/db/mariadb_board.c` - SQL operations for board posts and per-player read tracking
- `src/db/mariadb_news.c` - News wrapper functions (delegates to mariadb_board with NEWS_ROOM)
- `src/db/mariadb_help.c` - SQL operations for help topics (two-level command/subcommand)
- `src/db/mariadb_channel.c` - SQL operations and in-memory cache for channels
- `src/db/mariadb_lockout.c` - SQL operations and in-memory cache for lockouts
- `src/hdrs/mariadb_mail.h` / `mariadb_board.h` / `mariadb_news.h` / `mariadb_help.h` / `mariadb_channel.h` / `mariadb_lockout.h` - Declarations and stubs
- `src/util/convert_db.c` - Standalone database migration tool (`--mail`, `--board`, `--channels`, `--all`)
- `config/setup_mariadb.sql` - Table definitions
- `config/migrate_news_to_board.sql` - One-time migration: copies news articles into board table with NEWS_ROOM
- `config/help_seed.sql` - Help topic seed data

**Dependencies:** `libmariadb-dev`, `libwebsockets-dev` (auto-detected by Makefiles)

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
  `create`, `destroy`, `join`, `leave`, `default`, `alias`, `op`, `owner`, `lock`, `password`, `boot`, `ban`, `unban`, `list`, `search`, `log`, `color`, `who`, `mute`, `unmute`, `paste`, `pastecode`, `npaste`
- **`+com <channel>=<message>`** - Speaking on channels (say, pose, think, directed messages)
- **`=<message>`** - Shortcut to speak on default channel

**Examining players** shows a `Channels:` line listing the player's default channel first, followed by remaining channels sorted alphabetically (ignoring access-level prefixes).

**Function naming convention** in `src/comm/com.c`:
- `channel_*` - Subcommand handlers (e.g., `channel_create`, `channel_join`)
- `channel_cache_*` - Cache operations (e.g., `channel_cache_lookup`, `channel_cache_get_member`)
- `channel_int_*` - Legacy compatibility wrappers (thin wrappers around cache, kept during transition)
- `do_channel`, `do_com` - Parser hooks

**System channels** are hardcoded (not configurable via `@config`):
- `dbinfo` - Database info (dumps, etc.)
- `dc` - Disconnect notifications (min_level=3, director-only)
- `pub_io` - Public I/O monitoring
- `connect` - Connection announcements
- `warn_*` - Warning channels (e.g. `warn_security`, `warn_roomdesc`)

**Log channels** are also hardcoded system channels. Each log writes to a file in `run/logs/` and broadcasts to its channel:
- `log_imp` - Important events (shutdowns, name changes, admin commands)
- `log_sens` - Sensitive events (min_level=3, director-only)
- `log_err` - Errors
- `log_io` - I/O events (min_level=3, director-only)
- `log_gripe` - Player gripes
- `log_force` - @force usage (min_level=3, director-only)
- `log_prayer` - Player prayers
- `log_combat` - Combat events
- `log_suspect` - Suspect activity (min_level=3, director-only)

**Channel ownership:**
- `+channel owner=<channel>:<player>` transfers ownership. The channel owner, directors, and admins with `POW_CHANNEL` can transfer ownership.
- Non-admin owners cannot transfer to a player who has blacklisted them (`A_BLACKLIST` check).
- System channels (`is_system=1`) cannot have their owner changed — they are always owned by root.
- When the owner leaves a channel via `+channel leave`, ownership auto-transfers to the longest-standing member (lowest `member_id`). The new owner is notified.
- When the last member leaves a non-system channel, the channel is automatically destroyed.
- All ownership changes are logged to `log_imp`.

**Channel visibility:**
- `+channel search all` shows all visible channels. Directors see all channels including hidden ones (marked `HID`).

**System channels** have `is_system=1` in the database and cannot be destroyed or have their owner changed. Default system channels are seeded by `config/defaults.sql`. The 13 built-in system channels are: the 9 log channels above plus `dbinfo`, `dc`, `pub_io`, and `connect`.

## Configuration

**Primary config files:**
- `config/config.h` - Compile-time configuration (network options, features, limits)
- `config/setup_mariadb.sql` - MariaDB table definitions (config, mail, board, board_read, help_topics, channels, channel_members, lockouts)
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
- MariaDB tables: `config`, `mail`, `board`, `board_read`, `help_topics`, `channels`, `channel_members`, `lockouts` (created automatically on startup)

**Logs:**
- `run/logs/` - Server logs (connections, commands, errors)
- `run/logs/malloc-debug.log` - Memory debugging (if enabled)

**Messages (legacy):**
- `run/msgs/helptext` - Legacy help content (migrated to MariaDB `help_topics` table)
- `run/msgs/newstext` - Legacy news content (migrated to MariaDB `board` table with NEWS_ROOM)
- `run/msgs/helpindx` / `newsindx` - Legacy index files (built with `bin/mkindx`)

## Development Notes

**Completed**
- Convert to ANSI C instead of K&R C — done
- Use SAFE_MALLOC and SMART_FREE instead of raw malloc/free — done
- Use snprintf()/strncpy() instead of sprintf()/strcpy() — done
- Convert dbref format specifiers to DBREF_FMT macro — done
- Fix int/size_t mismatches from strlen() — done

**In Progress**
- Modernize the code to replace deprecated functions and improve logic — always in progress
- Look for potential buffer overruns and prevent them — always in progress
- Extensive validation — GoodObject() checks throughout all functions, except where finding a deleted object is okay like when dumping the database. Identity macros (Wizard, Guest, Robot, Builder, Dark, Alive) are self-defending with GoodObject(). eval.c fun_foreach and udef_fun validated.
- Better documentation — more detailed explanations of logic and functions, and better inline comments
- Improved header — comprehensive modernization notes

**Future Work**
- ~~Help topic cleanup~~ — DONE: 97 auto-generated help entries added (marked with disclaimer). Audit existing topics for correct syntax
- ~~Unify +board and +news code~~ — DONE: news articles stored in board table with NEWS_ROOM (-2), +board has undelete/purge/ban/unban
- Migrate object database from flat-file to MariaDB (three-table design: players, objects, attributes)
- ~~Upgrade Pueblo 1.0 support to MXP~~ — SUPERSEDED by Universe Project Phase 1 (web client)
- ~~Overhaul universe system~~ — SUPERSEDED by Universe Project (revert and reimplement, see below)
- ~~Fix signal.c SIGCHLD bug~~ — TESTING: `signal(SIGCHLD, SIG_IGN)` commented out 2026-03-12, reaper() handler now active
- Move powers/typenames/classnames arrays from config.h to database to eliminate compiler warnings
- ~~WebSocket connectivity (Phase 1a)~~ — DONE: libwebsockets integration, xterm.js web client, nginx proxy config
- ~~Move powers/typenames/classnames arrays from config.h~~ — DEFERRED to Universe Project: these become per-universe configuration in MariaDB (different class names, type names, and power matrices per universe)

### Universe Project: Revert and Reimplement

The existing universe system (`USE_UNIV`, `TYPE_UNIVERSE`, universe attributes in `db[]`, `@ucreate`/`@ulink`/`@uconfig` commands) is an incomplete implementation of an older design. It will be reverted and replaced with a new architecture that enables hosting multiple MU* game variants (TinyMUSH, PennMUSH, TinyMUSE, TinyMARE, etc.) as pluggable parser extensions within a single server, sharing common infrastructure.

**Core Design Principles:**

- **Account-based identity.** One login identity per person, with separate player objects per universe. Mail, channels, boards, and news operate at the account level, not the player object level. A player's `+mail` follows them across universes.
- **Player-per-universe model.** Objects exist in exactly one universe. Players don't cross between universes — they "switch" by detaching from their player object in one universe and attaching to their player object in another. Multi-connection support allows simultaneous play in multiple universes on separate connections.
- **Two-tier command dispatch.** Shared `+` commands (+mail, +board, +news, +com, +motd, etc.) and basic interaction (say, pose, page, look, WHO, QUIT) are handled by the core server before the parser extension. Building, admin, softcode functions, and lock evaluation are handled by the per-universe parser plugin.
- **Parser plugins as shared objects.** Each parser extension is a `.so` loaded at startup via `dlopen()`. Plugins register their commands and functions into a `parser_t` hash table. Each plugin provides its own expression evaluator, lock evaluator, and command preprocessor (to handle dialect-specific syntax like MUSH `/switches`).
- **Full-duplex real-time connections.** All connection types (telnet, TLS telnet, WebSocket) are full-duplex — server pushes output immediately when events occur (someone enters a room, a channel message fires, mail arrives). No polling or user action required.

**Phase 1: Web Client and Account Layer**

Replace the plain-text telnet login with a secure web-based interface using WebSocket, and introduce the account/identity system that the universe architecture requires.

*Network architecture (nginx reverse proxy):*

All web traffic runs through an existing nginx reverse proxy which handles TLS termination and SSL certificates. The game server only listens on localhost — it never handles TLS directly.

```
Browser → HTTPS (nginx) → static HTML/JS/CSS files (served by nginx)
Browser → WSS  (nginx) → proxy_pass → WS (netmuse localhost:4209)
Telnet  → TCP  (netmuse:4208, existing, unchanged)
```

Nginx configuration for the WebSocket proxy:
```nginx
location /ws {
    proxy_pass http://127.0.0.1:4209;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_read_timeout 86400s;   # MUD connections stay open for hours/days
    proxy_send_timeout 86400s;
}
```

This means:
- The game server adds a plain WebSocket listener on a local port (e.g., 4209) — no TLS handling needed
- Nginx provides HTTPS/WSS encryption using existing SSL certificates
- The web terminal page is served as static files by nginx (no HTTP serving in the game server)
- The existing telnet port (4208) remains unchanged for traditional MUD clients
- TLS telnet is deferred — nginx WSS covers encrypted access; traditional clients can connect via telnet as they do today

*Account system (MariaDB):*
- `accounts` table: `account_id`, `username`, `password_hash` (bcrypt/argon2), `email`, `created_at`, `last_login`
- `account_players` table: `account_id`, `universe_id`, `player_dbref`
- On first startup, auto-migrate existing player passwords to accounts (one account per player object)
- Player object `password` field becomes vestigial after migration
- All shared `+` commands key on `account_id` rather than player `dbref`

*WebSocket connection layer (DONE):*
- libwebsockets integrated via "foreign loop" pattern: game's `select()` loop drives lws via `lws_service_fd()` / `lws_service_tsi()`
- `C_WEBSOCKET` flag and `void *wsi` field on descriptor struct (same void* pattern as MariaDB to avoid header dependencies)
- WebSocket connections become standard descriptors — identical to telnet after handshake
- `websocket_port` config var (default 4209), auto-detected by Makefiles
- Implementation: `src/io/websocket.c` (init, fd tracking, output, protocol callback), `src/hdrs/websocket.h` (API with `#ifdef USE_WEBSOCKET` stubs)
- WebSocket descriptors can't survive @reload (exec) — they're cleanly disconnected with a reboot message

*Web terminal client (DONE):*
- Single HTML page with xterm.js (renders ANSI colors, local line editing, auto-reconnect)
- Served as static files by Apache (behind the nginx reverse proxy)
- Example config: `web/nginx.conf.example`, client: `web/index.html`

*Account system and secure auth (TODO):*
- Authentication: HTTPS login form → validate against `accounts` table → session token → WebSocket connects with token
- No plain-text passwords ever cross the wire
- Real-time server push via WebSocket — room events, channel messages, notifications appear instantly

*Dependencies:* libwebsockets (server-side), libargon2 or libbcrypt (password hashing, TODO), xterm.js (client-side, served by web server)

**Phase 2: Parser Plugin Architecture**

Extract the current deMUSE command table and function evaluator into a shared-object plugin, establishing the plugin interface.

*Plugin interface:*
- Each `.so` exports a `parser_plugin_t` struct with: `init()`, `shutdown()`, `eval_expression()`, `eval_lock()`, `preprocess_command()`, and universe lifecycle hooks (`on_player_enter`, `on_player_leave`)
- `init()` registers commands and functions into the parser's hash table (same mechanism as today's `init_parsers()`)
- The expression evaluator and lock evaluator are per-plugin — this is where MU* dialects diverge most

*Refactor:*
- Move current command registrations from parser.c into `parsers/demuse.so`
- Move eval.c function table and evaluation logic into the deMUSE plugin
- Core server retains only Tier 1 shared commands (+mail, +board, +news, +com, say, pose, page, look, WHO, QUIT, movement)
- Add `universe_id` to descriptor struct; command dispatch routes through the universe's parser

*Validation:* Build and test with only the deMUSE plugin. The game should behave identically to today.

**Phase 3: Universe Management**

Implement the runtime universe system: creation, player switching, and cross-universe infrastructure.

- `@universe create <name>=<parser>` — create a universe using a loaded parser plugin
- `@universe switch <name>` — detach from current player object, attach to player object in target universe (create if first visit)
- `@universe list` — show available universes and which parser each uses
- `@universe who` — show players across all universes
- `WHO` output shows universe indicator per player
- Universe-aware connect screen: after account login, present character selection (universe + player name)
- Per-universe object numbering within the shared `db[]` array (dbrefs are globally unique)

**Phase 4: Additional Parser Plugins**

With the architecture proven, implement additional MU* dialect parsers:
- TinyMUSH 3.x parser (closest relative, shares the most code)
- PennMUSH parser (larger function library, different evaluation rules)
- Minimal/sandbox parser (restricted command set for limited-capability universes)

Each parser brings its own command table, function library, expression evaluator, and lock syntax.

**Existing Universe Code to Revert:**
- `TYPE_UNIVERSE` object type in db.h
- `universe`, `ua_string`, `ua_int`, `ua_float` fields in struct object
- Universe attributes in universe.h (UA_TELEPORT, UA_DESTREXIT, etc.)
- `@ucreate`, `@uconfig`, `@uinfo`, `@ulink`, `@unulink` commands
- `@guniverse`, `@gzone` commands
- `fun_universe()`, `fun_uinfo()` in eval.c
- TYPE_UNIVERSE movement handling in move.c
- Universe-related code in zones.c
- `USE_UNIV` remnants throughout

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

Mail, board, and news messages are stored in MariaDB (migrated from the flat-file format as of 2026). News articles share the `board` table using a sentinel `board_room` value. Legacy databases with messages appended after `***END OF DUMP***` are automatically converted on first startup.

## Known Issues and Limitations

- ~~@booting yourself has bugs~~ — FIXED: self-boot already prevented, updated error message
- MAZE combat features not implemented (maze.c behind `#ifdef USE_COMBAT`, combat.h doesn't exist)
- ~~Universe code incomplete~~ — will be reverted and reimplemented (see Universe Project)
- signal.c: `signal(SIGCHLD, SIG_IGN)` was overriding the `sigaction(SIGCHLD, reaper)` handler — commented out 2026-03-12, testing to confirm no side effects
- ~~eval.c: `fun_foreach()` accesses `db[doer]` without GoodObject validation~~ — FIXED: added GoodObject(doer) checks to fun_foreach and udef_fun

## Historical Context

- Original base: TinyMUD by Larry Foard
- Evolved through: TinyMUSH → TinyMUSE → TinyMUSE '97 → deMUSE
- Created for deMUSEcracy, an online government simulation
- Originally developed on Slackware Linux 4.0
- Ancient codebase with multiple authors over decades
- Recent modernization effort (2025-2026) for safety, security, and MariaDB integration
- Current version: 2.26.3.2 beta 1
