# Account Authentication & Universe Architecture Design

## Overview

Replace plaintext password login with token-based authentication via a PHP web layer. This is Phase 1b of the Universe Project — it delivers secure auth immediately while laying the foundation for multi-universe support.

## Phase 1b: Account System & Web Auth (Immediate)

### Architecture

```
Browser → demuse.com → Cloudflare → external nginx
  → proxies to local Apache + PHP (game server machine)
    → PHP auth pages (login / register / token generation)
    → WebSocket proxy → netmuse:4209

Telnet client → demuse.com:4208 → netmuse (token auth only)
```

Components:
- **Apache + PHP 8.5** on the game server, handles all web-facing auth
- **MariaDB** stores accounts and tokens (same database as existing game tables)
- **netmuse** validates tokens in check_connect(), never sees passwords

### Database Schema

```sql
CREATE TABLE accounts (
    account_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(50) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,  -- bcrypt via PHP password_hash()
    email         VARCHAR(255) DEFAULT NULL,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login    TIMESTAMP NULL,
    is_verified   TINYINT(1) DEFAULT 0,
    is_disabled   TINYINT(1) DEFAULT 0,
    INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE email_verify_tokens (
    token_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_id  BIGINT UNSIGNED NOT NULL,
    token       VARCHAR(64) NOT NULL UNIQUE,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at  TIMESTAMP NOT NULL,  -- 24-hour expiry
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    INDEX idx_token (token)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE auth_tokens (
    token_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_id  BIGINT UNSIGNED NOT NULL,
    token       VARCHAR(64) NOT NULL UNIQUE,  -- crypto-random hex string
    token_type  ENUM('websocket', 'telnet') NOT NULL,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at  TIMESTAMP NOT NULL,
    consumed    TINYINT(1) DEFAULT 0,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    INDEX idx_token (token),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### PHP Auth Flow

Files served by local Apache from a `web/` directory:

1. **`login.php`** — Login form (POST over HTTPS)
   - Validates username/password against `accounts` table using `password_verify()`
   - Rejects unverified accounts (`is_verified = 0`) with "Please check your email" message
   - Rejects disabled accounts (`is_disabled = 1`) with "Account disabled" message
   - On success: generates a crypto-random token via `bin2hex(random_bytes(32))`
   - Stores token in `auth_tokens` with 60-second expiry
   - Returns page with two options:
     - "Connect via browser" button → loads xterm.js client, WebSocket auto-connects with token
     - "Telnet token" display → shows `connect token:<token>` string with copy button

2. **`register.php`** — Account registration form
   - Validates username (ok_player_name rules), password strength, email format
   - Stores password as `password_hash($password, PASSWORD_BCRYPT)`
   - Creates account with `is_verified = 0` (unverified)
   - Reads SMTP config from MariaDB `config` table (same `smtp_*` vars the C email system uses)
   - Sends verification email with a one-time link containing a crypto-random token
   - Does NOT create a player object — that happens in-game when joining a universe

3. **`verify.php`** — Email verification endpoint
   - Receives token from verification link (`verify.php?token=abc123`)
   - Validates token against `email_verify_tokens` table, checks expiry (24 hours)
   - On success: sets `is_verified = 1` on the account, deletes token
   - Shows confirmation message with link to login page

3. **`index.html`** — xterm.js web client (moved from external nginx)
   - On load, reads token from URL parameter or session
   - Opens WebSocket, sends `connect token:<token>` as first command
   - Same terminal interface as current web client

4. **`api/token.php`** — Token refresh endpoint (if token expires before use)

5. **`db.php`** — Shared database connection helper and config reader
   - Reads DB credentials from `$DEMUSE_HOME/run/db/mariadb.conf` (via `getenv('DEMUSE_HOME')`)
   - Provides `get_pdo()` for database access and `get_config($key)` to read from the MariaDB `config` table
   - SMTP settings for email verification come from the `config` table (`smtp_server`, `smtp_port`, `smtp_use_ssl`, `smtp_username`, `smtp_password`, `smtp_from`) — same values the C email system uses
   - Tracked in git (contains no secrets, just the parser and PDO connection logic)
   - Only two things needed per installation: `DEMUSE_HOME` env var (set in Apache vhost) and `run/db/mariadb.conf` (already exists)

### netmuse Token Validation

In `check_connect()` (connection_handler.c), add token path:

```c
/* In parse_connect or check_connect: */
if (strncmp(user, "token:", 6) == 0) {
    char *token = user + 6;
    long account_id = mariadb_auth_validate_token(token);
    if (account_id > 0) {
        /* Token valid — bind descriptor to account */
        d->account_id = account_id;
        /* Check for existing session — takeover if found */
        /* ... */
    } else {
        queue_string(d, "Invalid or expired token.\n");
        return;
    }
}
```

New MariaDB auth functions (`mariadb_auth.c` / `mariadb_auth.h`):
- `mariadb_auth_validate_token(token)` — returns account_id or 0, consumes (deletes) the token
- `mariadb_auth_cleanup_expired()` — periodic cleanup of expired tokens (called from timer.c)
- `mariadb_auth_get_account(account_id)` — returns account info
- `mariadb_auth_find_player(account_id, universe_id)` — returns player dbref for this account in this universe

### Session Takeover

When a valid token is presented and the account already has an active descriptor:
1. Find existing descriptor(s) for this account_id
2. Send "Connection superseded by new login." to old descriptor
3. Close old descriptor via shutdownsock()
4. Bind new descriptor to account and player

This handles dropped connections, browser tab refreshes, etc.

### Descriptor Changes

Add to `struct descriptor_data` in `net.h`:
```c
long account_id;    /* Account ID from accounts table, 0 if not authenticated */
```

### Account-to-Player Binding (Phase 1b, single universe)

After token validation, netmuse looks up the account in `universe_players`:

1. **Player exists**: Row found → bind `d->player` to that dbref → `announce_connect()` → normal login
2. **No player yet**: No row → auto-create via existing `create_player(username, NULL, CLASS_VISITOR, player_start)` → insert row into `universe_players` linking account to new player dbref → `announce_connect()` → user lands in the game

The user's account username becomes their player name. No password is passed to `create_player()` since auth is handled by the account system — the player object's password field is unused (or set to a random hash so it can't be guessed).

**Name collision**: If a player object with that name already exists but isn't linked to any account (pre-migration player), the game notifies: "That player name is already taken. Contact an admin to link your account." This gets resolved by the migration process (see below).

The mapping table:
```sql
CREATE TABLE universe_players (
    account_id   BIGINT UNSIGNED NOT NULL,
    universe_id  INT UNSIGNED NOT NULL DEFAULT 0,
    player_dbref BIGINT NOT NULL,
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (account_id, universe_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    INDEX idx_universe (universe_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

Universe 0 is the lobby (default). Universe -1 is invalid (mirrors NOTHING/-1 convention for dbrefs). For Phase 1b, `universe_id` is always 0 (the single deMUSE game acts as the lobby). Game universes start at 1 when multi-universe is added later.

### Migration Path

Existing players need accounts. Migration options:
- **On first login**: Player types `connect <name> <password>` (legacy path, kept temporarily). System auto-creates an account with same credentials, links player to account, then tells them to use web login going forward.
- **Bulk migration**: Script creates accounts from existing player objects and their passwords.
- **Deadline**: After migration period, legacy connect path is disabled.

### Apache Configuration

Template file tracked in git (`web/apache-demuse.conf.example`):
```apache
# Copy to /etc/apache2/sites-available/demuse.conf and adjust paths.
<VirtualHost *:80>
    ServerName localhost
    DocumentRoot /path/to/demuse/web

    # Set base directory for PHP to find config files
    SetEnv DEMUSE_HOME /path/to/demuse

    <Directory /path/to/demuse/web>
        AllowOverride All
        Require all granted
    </Directory>

    # WebSocket proxy to netmuse
    ProxyPass /ws ws://localhost:4209/ upgrade=websocket
    ProxyPassReverse /ws ws://localhost:4209/
</VirtualHost>
```

The actual installed config at `/etc/apache2/sites-available/demuse.conf` is a system file, not tracked in git. External nginx handles TLS termination and proxies to this Apache instance over plain HTTP (port 80).

### DEMUSE_HOME Environment Variable

All components locate files relative to a single base directory:

```
DEMUSE_HOME=/path/to/demuse
```

- **Apache** sets it via `SetEnv DEMUSE_HOME /path/to/demuse` in the vhost config
- **PHP** reads `getenv('DEMUSE_HOME') . '/run/db/mariadb.conf'` for DB credentials
- **netmuse** (future): reads `$DEMUSE_HOME` to find config files; falls back to current relative-path behavior (launched from `run/`) for backward compatibility

This means `web/db.php`, `web/login.php`, etc. are fully generic — no installation-specific paths in tracked files. One variable to set per installation.

**TODO (future)**: Rename `run/db/mariadb.conf` to `run/db/demuse.conf` (or similar) since it serves as the shared credential file for the entire project, not just MariaDB. Update `mariadb.c`, `convert_db.c`, `config/mariadb.conf.example`, and PHP code to match.

### Security Considerations

- Passwords never reach netmuse — PHP handles all password operations
- Tokens are one-time use, 60-second expiry, crypto-random (256-bit)
- `db.php` reads MariaDB credentials from `run/db/mariadb.conf` (outside webroot)
- Apache serves from `web/` directory — no access to `src/`, `run/`, `config/`
- Rate limiting on login attempts (PHP-side, per IP)
- Account lockout after N failed attempts

---

## Future: Universe Architecture (Phase 2+)

### Multi-Process Model (Option A)

```
Lobby process (netmuse --lobby)
  |-- Accepts connections, validates tokens
  |-- Universe 0: lobby commands (+universe list/join/switch)
  |-- Shared services: +mail, +com, +board, +news (account-keyed)
  |-- Monitors child processes, restarts on crash (max 3 attempts)
  |
  |-- netmuse --universe=1  (demuse-parser.so, own db)
  |-- netmuse --universe=2  (tinymush-parser.so, own db)
  |-- netmuse --universe=3  (tradewars-parser.so, own db)
```

- Same netmuse binary, different runtime mode (--lobby vs --universe)
- Lobby forks/manages child processes
- Socket handoff via Unix domain sockets (SCM_RIGHTS)
- Crash isolation: universe crash doesn't affect others or the lobby
- OS-level resource control per universe process

### Universe Table

```sql
CREATE TABLE universes (
    universe_id    INT UNSIGNED NOT NULL PRIMARY KEY,  -- 0 = lobby, 1+ = game universes
    name           VARCHAR(100) NOT NULL UNIQUE,
    description    TEXT,
    parser_plugin  VARCHAR(255) NOT NULL,  -- e.g., 'demuse-parser.so'
    db_path        VARCHAR(255) NOT NULL,  -- e.g., 'run/db/universe1.db'
    access_level   ENUM('open', 'invite', 'closed') DEFAULT 'open',
    status         ENUM('online', 'offline', 'crashed') DEFAULT 'offline',
    owner_account  BIGINT UNSIGNED NOT NULL,
    max_players    INT UNSIGNED DEFAULT 100,
    created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (owner_account) REFERENCES accounts(account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE universe_invites (
    universe_id  INT UNSIGNED NOT NULL,
    account_id   BIGINT UNSIGNED NOT NULL,
    invited_by   BIGINT UNSIGNED NOT NULL,
    invited_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (universe_id, account_id),
    FOREIGN KEY (universe_id) REFERENCES universes(universe_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### Universe Access Levels

| Level    | Who can join                                          |
|----------|-------------------------------------------------------|
| open     | Any authenticated account can create a player and join|
| invite   | Only accounts explicitly invited by a universe admin  |
| closed   | Only universe admins can connect (maintenance mode)   |

### Hard Switch Model

Users are in one universe at a time. `+universe switch=<name>`:
1. Disconnects player in current universe (announce_disconnect)
2. Lobby hands socket to target universe process
3. Connects to player in target universe (announce_connect)
4. If no player exists, offers to create one (if access level permits)

### Parser Plugin Interface

Plugins are `.so` files loaded via `dlopen()`. Each implements:
```c
typedef struct {
    const char *name;
    const char *version;
    int (*init)(universe_context *ctx);
    void (*shutdown)(universe_context *ctx);
    int (*process_command)(universe_context *ctx, dbref player, char *command);
    char *(*eval_expression)(universe_context *ctx, dbref player, char *expr);
    int (*eval_lock)(universe_context *ctx, dbref player, dbref thing, char *lock);
} parser_plugin_t;
```

The core provides the plugin with I/O primitives, db access, and shared service hooks. The plugin IS the game — it defines what commands exist and how they work.

### Cross-Universe Shared Services

All shared services are keyed on `account_id`, not `player_dbref`:
- **+mail**: One mailbox per account, accessible from any universe
- **+com/channels**: Cross-universe, messages routed via lobby process IPC
- **+board/+news**: Account-based read tracking
- **page**: Cross-universe direct messaging via lobby IPC

IPC between lobby and universe processes via Unix domain sockets.

### Research: Other Codebases

Before finalizing the core engine for multi-universe, examine:
- TinyMUSH 3.x — descriptor handling, command queue, function evaluation
- PennMUSH — I/O layer, buffer management, lock system
- RhostMUSH — multi-register, softcode features

Goal: best-in-breed core engine, not necessarily compatible command sets.

### Non-MUSH Parser Plugins

The plugin interface must support non-MUSH games (e.g., Tradewars 2002):
- Menu-driven input (not free-form commands)
- Different game state models (sectors/ships vs rooms/objects)
- Different turn/timing models
- Plugin manages its own game state; core provides I/O and platform services
