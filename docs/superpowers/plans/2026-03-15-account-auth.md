# Account Authentication Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace plaintext password login with token-based auth via PHP web layer and MariaDB accounts.

**Architecture:** PHP 8.5 on local Apache handles account registration, email verification, and login. On successful login, PHP generates a one-time token stored in MariaDB. The xterm.js web client (or telnet user) sends `connect token:<token>` to netmuse, which validates and consumes the token, then binds the descriptor to the account's player object (auto-creating if needed).

**Tech Stack:** PHP 8.5 (bcrypt, PDO), Apache 2.4, MariaDB (existing), C (netmuse), libcurl (existing SMTP)

**Spec:** `docs/superpowers/specs/2026-03-15-account-auth-design.md`

---

## File Map

### New Files — PHP (web/)

| File | Responsibility |
|------|---------------|
| `web/db.php` | Parse `run/db/mariadb.conf` using `DEMUSE_HOME`, provide `get_pdo()` and `get_config($key)` |
| `web/register.php` | Account registration form + POST handler, email verification send |
| `web/verify.php` | Email verification link handler |
| `web/login.php` | Login form + POST handler, token generation, connect options |
| `web/client.php` | xterm.js terminal page (replaces `index.html`), receives token via URL param |
| `web/api/token.php` | Token refresh endpoint |
| `web/mailer.php` | SMTP email helper using PHPMailer or native `mail()`, reads config from MariaDB |
| `web/.htaccess` | Deny access to `db.php`, `mailer.php` from direct browser requests |
| `web/apache-demuse.conf.example` | Apache vhost template with `DEMUSE_HOME` placeholder |

### New Files — C (src/)

| File | Responsibility |
|------|---------------|
| `src/db/mariadb_auth.c` | Token validation, account lookup, player mapping, expired token cleanup |
| `src/hdrs/mariadb_auth.h` | Header with function declarations and `#else` stubs |

### Modified Files — C

| File | Change |
|------|--------|
| `src/hdrs/net.h` | Add `long account_id` to `struct descriptor_data` |
| `src/io/connection_handler.c` | Add token auth path in `check_connect()` |
| `src/muse/timer.c` | Add periodic `mariadb_auth_cleanup_expired()` call |
| `src/db/Makefile` | Add `mariadb_auth.c` to SRCS |
| `src/hdrs/externs.h` | Add extern declarations for new auth functions |

### Modified Files — SQL

| File | Change |
|------|--------|
| `config/setup_mariadb.sql` | Add `accounts`, `auth_tokens`, `email_verify_tokens`, `universe_players` tables |

### Modified Files — Other

| File | Change |
|------|--------|
| `web/index.html` | Replaced by `web/client.php` (token-aware version) |
| `.gitignore` | No changes needed (no new secret files) |

---

## Chunk 1: Database Schema & PHP Foundation

### Task 1: Add account tables to MariaDB schema

**Files:**
- Modify: `config/setup_mariadb.sql`

- [ ] **Step 1: Add account tables to setup_mariadb.sql**

Append to the end of the file, before any closing comments:

```sql
/* ===================================================================
 * ACCOUNT AUTHENTICATION TABLES
 * =================================================================== */

CREATE TABLE IF NOT EXISTS accounts (
    account_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(50)  NOT NULL,
    password_hash VARCHAR(255) NOT NULL COMMENT 'bcrypt via PHP password_hash()',
    email         VARCHAR(255) DEFAULT NULL,
    created_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login    TIMESTAMP    NULL,
    is_verified   TINYINT(1)   NOT NULL DEFAULT 0,
    is_disabled   TINYINT(1)   NOT NULL DEFAULT 0,
    UNIQUE INDEX idx_username (username)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='User accounts for web-based authentication';

CREATE TABLE IF NOT EXISTS email_verify_tokens (
    token_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_id  BIGINT UNSIGNED NOT NULL,
    token       VARCHAR(64)  NOT NULL,
    created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at  TIMESTAMP    NOT NULL COMMENT '24-hour expiry',
    UNIQUE INDEX idx_token (token),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='Email verification tokens for account registration';

CREATE TABLE IF NOT EXISTS auth_tokens (
    token_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_id  BIGINT UNSIGNED NOT NULL,
    token       VARCHAR(64)  NOT NULL COMMENT 'crypto-random hex string',
    token_type  ENUM('websocket', 'telnet') NOT NULL,
    created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at  TIMESTAMP    NOT NULL COMMENT '60-second expiry',
    consumed    TINYINT(1)   NOT NULL DEFAULT 0,
    UNIQUE INDEX idx_token (token),
    INDEX idx_expires (expires_at),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='One-time login tokens for WebSocket and telnet connections';

CREATE TABLE IF NOT EXISTS universe_players (
    account_id   BIGINT UNSIGNED NOT NULL,
    universe_id  INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT '0 = lobby/default',
    player_dbref BIGINT          NOT NULL,
    created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (account_id, universe_id),
    INDEX idx_universe (universe_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='Maps accounts to player objects per universe';
```

- [ ] **Step 2: Run the SQL against the live database**

```bash
mysql -h 192.168.203.21 -u demuse -p demuse < config/setup_mariadb.sql
```

Verify with: `SHOW TABLES LIKE 'accounts';` and `SHOW TABLES LIKE 'auth_tokens';`

- [ ] **Step 3: Commit**

```bash
git add config/setup_mariadb.sql
git commit -m "Add account authentication tables: accounts, auth_tokens, email_verify_tokens, universe_players"
```

---

### Task 2: Create PHP database helper (db.php)

**Files:**
- Create: `web/db.php`

- [ ] **Step 1: Create web/db.php**

This file parses `run/db/mariadb.conf` and provides database access to all PHP pages. It uses `DEMUSE_HOME` env var set by Apache's `SetEnv` directive.

```php
<?php
/**
 * db.php - Database connection helper for deMUSE web interface
 *
 * Reads MariaDB credentials from run/db/mariadb.conf (same file the C
 * code uses). Path is resolved via DEMUSE_HOME environment variable.
 *
 * Provides:
 *   get_pdo()         - Returns a PDO connection (singleton)
 *   get_config($key)  - Reads a value from the MariaDB config table
 */

/**
 * Parse the mariadb.conf key=value file.
 *
 * @return array Associative array with keys: host, port, user, password, database
 */
function parse_mariadb_conf(): array
{
    $home = getenv('DEMUSE_HOME');
    if (!$home) {
        throw new RuntimeException('DEMUSE_HOME environment variable is not set');
    }

    $conf_path = $home . '/run/db/mariadb.conf';
    if (!file_exists($conf_path)) {
        throw new RuntimeException("Cannot find config file: $conf_path");
    }

    $config = [];
    $lines = file($conf_path, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    foreach ($lines as $line) {
        $line = trim($line);
        if ($line === '' || $line[0] === '#') {
            continue;
        }
        $pos = strpos($line, '=');
        if ($pos === false) {
            continue;
        }
        $key = trim(substr($line, 0, $pos));
        $value = trim(substr($line, $pos + 1));
        $config[$key] = $value;
    }

    return $config;
}

/**
 * Get a PDO database connection (singleton).
 *
 * @return PDO
 */
function get_pdo(): PDO
{
    static $pdo = null;
    if ($pdo !== null) {
        return $pdo;
    }

    $conf = parse_mariadb_conf();
    $host = $conf['host'] ?? 'localhost';
    $port = $conf['port'] ?? '3306';
    $db   = $conf['database'] ?? 'demuse';
    $user = $conf['user'] ?? 'demuse';
    $pass = $conf['password'] ?? '';

    $dsn = "mysql:host=$host;port=$port;dbname=$db;charset=utf8mb4";
    $pdo = new PDO($dsn, $user, $pass, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,
    ]);

    return $pdo;
}

/**
 * Read a config value from the MariaDB config table.
 *
 * The config table uses name/value columns (same table the C code reads via
 * mariadb_config_load). Returns null if the key doesn't exist.
 *
 * @param string $key Config key name (e.g., 'smtp_server')
 * @return string|null
 */
function get_config(string $key): ?string
{
    $pdo = get_pdo();
    $stmt = $pdo->prepare('SELECT value FROM config WHERE name = :name LIMIT 1');
    $stmt->execute(['name' => $key]);
    $row = $stmt->fetch();
    return $row ? $row['value'] : null;
}
```

- [ ] **Step 2: Verify db.php can connect**

Create a temporary test script `web/test_db.php`:
```php
<?php
require_once __DIR__ . '/db.php';
try {
    $pdo = get_pdo();
    echo "Connected OK\n";
    echo "smtp_server = " . get_config('smtp_server') . "\n";
} catch (Exception $e) {
    echo "Error: " . $e->getMessage() . "\n";
}
```

Run: `DEMUSE_HOME=/home/ken/homeserver/ken/Games/deMUSE/demuse php web/test_db.php`

Expected: "Connected OK" and the SMTP server value.

Delete `web/test_db.php` after verification.

- [ ] **Step 3: Create web/.htaccess to protect internal files**

```apache
# Deny direct browser access to internal PHP files
<FilesMatch "^(db|mailer)\.php$">
    Require all denied
</FilesMatch>
```

- [ ] **Step 4: Commit**

```bash
git add web/db.php web/.htaccess
git commit -m "Add PHP database helper: reads credentials from run/db/mariadb.conf via DEMUSE_HOME"
```

---

### Task 3: Create email helper (mailer.php)

**Files:**
- Create: `web/mailer.php`

- [ ] **Step 1: Create web/mailer.php**

Reads SMTP config from MariaDB `config` table (same values the C email system uses) and sends email via PHP's built-in SMTP or `mail()` function.

```php
<?php
/**
 * mailer.php - Email sending helper for deMUSE web interface
 *
 * Reads SMTP configuration from the MariaDB config table (same smtp_*
 * variables used by the C email system in comm/email.c).
 */

require_once __DIR__ . '/db.php';

/**
 * Send an email using SMTP settings from the MariaDB config table.
 *
 * @param string $to      Recipient email address
 * @param string $subject Email subject
 * @param string $body    Plain text email body
 * @return bool True on success, false on failure
 */
function send_email(string $to, string $subject, string $body): bool
{
    $from = get_config('smtp_from');
    if (!$from) {
        error_log('deMUSE mailer: smtp_from not configured');
        return false;
    }

    $smtp_server   = get_config('smtp_server');
    $smtp_port     = (int)(get_config('smtp_port') ?: 25);
    $smtp_use_ssl  = (int)(get_config('smtp_use_ssl') ?: 0);
    $smtp_username = get_config('smtp_username');
    $smtp_password = get_config('smtp_password');

    /* If no SMTP server configured, fall back to PHP mail() */
    if (!$smtp_server) {
        $headers = "From: $from\r\n";
        $headers .= "Content-Type: text/plain; charset=UTF-8\r\n";
        $headers .= "X-Mailer: deMUSE-Web/1.0\r\n";
        return mail($to, $subject, $body, $headers);
    }

    /* Use fsockopen for direct SMTP delivery */
    $prefix = $smtp_use_ssl ? 'ssl://' : '';
    $fp = @fsockopen($prefix . $smtp_server, $smtp_port, $errno, $errstr, 10);
    if (!$fp) {
        error_log("deMUSE mailer: cannot connect to $smtp_server:$smtp_port - $errstr");
        return false;
    }

    /* Helper to read SMTP response */
    $read_response = function() use ($fp): string {
        $response = '';
        while ($line = fgets($fp, 512)) {
            $response .= $line;
            if (isset($line[3]) && $line[3] === ' ') break;
        }
        return $response;
    };

    /* Helper to send command and check response code */
    $send_cmd = function(string $cmd, int $expect) use ($fp, $read_response): bool {
        fwrite($fp, $cmd . "\r\n");
        $resp = $read_response();
        return (int)substr($resp, 0, 3) === $expect;
    };

    $ok = true;

    /* Read greeting */
    $read_response();

    /* EHLO */
    $ok = $ok && $send_cmd('EHLO demuse', 250);

    /* AUTH if credentials provided */
    if ($ok && $smtp_username && $smtp_password) {
        $ok = $ok && $send_cmd('AUTH LOGIN', 334);
        $ok = $ok && $send_cmd(base64_encode($smtp_username), 334);
        $ok = $ok && $send_cmd(base64_encode($smtp_password), 235);
    }

    /* MAIL FROM, RCPT TO, DATA */
    $ok = $ok && $send_cmd("MAIL FROM:<$from>", 250);
    $ok = $ok && $send_cmd("RCPT TO:<$to>", 250);
    $ok = $ok && $send_cmd('DATA', 354);

    if ($ok) {
        $date = date('r');
        $msg  = "Date: $date\r\n";
        $msg .= "From: $from\r\n";
        $msg .= "To: $to\r\n";
        $msg .= "Subject: $subject\r\n";
        $msg .= "Content-Type: text/plain; charset=UTF-8\r\n";
        $msg .= "X-Mailer: deMUSE-Web/1.0\r\n";
        $msg .= "\r\n";
        $msg .= str_replace("\n.", "\n..", $body);  /* Dot-stuffing */
        $msg .= "\r\n";

        fwrite($fp, $msg);
        $ok = $send_cmd('.', 250);
    }

    $send_cmd('QUIT', 221);
    fclose($fp);

    if (!$ok) {
        error_log("deMUSE mailer: SMTP transaction failed sending to $to");
    }

    return $ok;
}
```

- [ ] **Step 2: Commit**

```bash
git add web/mailer.php
git commit -m "Add PHP email helper: reads SMTP config from MariaDB config table"
```

---

## Chunk 2: Registration & Verification

### Task 4: Create registration page (register.php)

**Files:**
- Create: `web/register.php`

- [ ] **Step 1: Create web/register.php**

```php
<?php
/**
 * register.php - Account registration for deMUSE
 *
 * Creates an unverified account and sends a verification email.
 * Player objects are NOT created here — that happens on first game login.
 */

require_once __DIR__ . '/db.php';
require_once __DIR__ . '/mailer.php';

session_start();

$error = '';
$success = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim($_POST['username'] ?? '');
    $password = $_POST['password'] ?? '';
    $confirm  = $_POST['confirm'] ?? '';
    $email    = trim($_POST['email'] ?? '');

    /* Validate username */
    if (strlen($username) < 3 || strlen($username) > 50) {
        $error = 'Username must be between 3 and 50 characters.';
    } elseif (!preg_match('/^[A-Za-z][A-Za-z0-9_-]*$/', $username)) {
        $error = 'Username must start with a letter and contain only letters, numbers, hyphens, and underscores.';
    } elseif (preg_match('/[:;,\'"!@#$%^&*()+=\[\]{}|\\\\<>\/`~]/', $username)) {
        $error = 'Username contains invalid characters.';
    }

    /* Validate password */
    if (!$error) {
        if (strlen($password) < 8) {
            $error = 'Password must be at least 8 characters.';
        } elseif ($password !== $confirm) {
            $error = 'Passwords do not match.';
        }
    }

    /* Validate email */
    if (!$error) {
        if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
            $error = 'Please enter a valid email address.';
        }
    }

    /* Check for duplicate username */
    if (!$error) {
        $pdo = get_pdo();
        $stmt = $pdo->prepare('SELECT account_id FROM accounts WHERE username = :u');
        $stmt->execute(['u' => $username]);
        if ($stmt->fetch()) {
            $error = 'That username is already taken.';
        }
    }

    /* Check for duplicate email */
    if (!$error) {
        $stmt = $pdo->prepare('SELECT account_id FROM accounts WHERE email = :e');
        $stmt->execute(['e' => $email]);
        if ($stmt->fetch()) {
            $error = 'An account with that email address already exists.';
        }
    }

    /* Create the account */
    if (!$error) {
        $hash = password_hash($password, PASSWORD_BCRYPT);

        $stmt = $pdo->prepare(
            'INSERT INTO accounts (username, password_hash, email, is_verified, is_disabled)
             VALUES (:u, :h, :e, 0, 0)'
        );
        $stmt->execute(['u' => $username, 'h' => $hash, 'e' => $email]);
        $account_id = (int)$pdo->lastInsertId();

        /* Generate email verification token (24-hour expiry) */
        $token = bin2hex(random_bytes(32));
        $stmt = $pdo->prepare(
            'INSERT INTO email_verify_tokens (account_id, token, expires_at)
             VALUES (:a, :t, DATE_ADD(NOW(), INTERVAL 24 HOUR))'
        );
        $stmt->execute(['a' => $account_id, 't' => $token]);

        /* Send verification email */
        $verify_url = (isset($_SERVER['HTTPS']) ? 'https' : 'http')
                    . '://' . $_SERVER['HTTP_HOST']
                    . '/verify.php?token=' . urlencode($token);

        $body = "Welcome to deMUSE!\n\n"
              . "Please verify your account by clicking the link below:\n\n"
              . "$verify_url\n\n"
              . "This link expires in 24 hours.\n\n"
              . "If you did not create this account, you can ignore this email.\n";

        $sent = send_email($email, 'deMUSE Account Verification', $body);

        if ($sent) {
            $success = 'Account created! Please check your email to verify your account.';
        } else {
            $success = 'Account created, but we could not send the verification email. '
                     . 'Please contact an administrator.';
        }
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Register</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #1a1a2e; color: #c0c0c0;
            display: flex; justify-content: center; align-items: center;
            min-height: 100vh;
        }
        .container {
            background: #16213e; padding: 2rem; border-radius: 8px;
            width: 100%; max-width: 400px; box-shadow: 0 4px 20px rgba(0,0,0,0.5);
        }
        h1 { color: #e0e0e0; margin-bottom: 1.5rem; text-align: center; }
        label { display: block; margin-bottom: 0.3rem; color: #a0a0a0; font-size: 0.9rem; }
        input[type="text"], input[type="password"], input[type="email"] {
            width: 100%; padding: 0.6rem; margin-bottom: 1rem;
            background: #0f3460; border: 1px solid #333; border-radius: 4px;
            color: #e0e0e0; font-size: 1rem;
        }
        input:focus { outline: none; border-color: #00a8cc; }
        button {
            width: 100%; padding: 0.7rem; background: #00a8cc; color: #fff;
            border: none; border-radius: 4px; font-size: 1rem; cursor: pointer;
        }
        button:hover { background: #0090b0; }
        .error { color: #ff6b6b; margin-bottom: 1rem; font-size: 0.9rem; }
        .success { color: #51cf66; margin-bottom: 1rem; font-size: 0.9rem; }
        .links { margin-top: 1rem; text-align: center; font-size: 0.9rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="container">
    <h1>Create Account</h1>
    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
    <?php endif; ?>
    <?php if ($success): ?>
        <div class="success"><?= htmlspecialchars($success) ?></div>
        <div class="links"><a href="login.php">Go to Login</a></div>
    <?php else: ?>
        <form method="post">
            <label for="username">Username</label>
            <input type="text" id="username" name="username" required
                   value="<?= htmlspecialchars($username ?? '') ?>"
                   minlength="3" maxlength="50" autocomplete="username">

            <label for="email">Email</label>
            <input type="email" id="email" name="email" required
                   value="<?= htmlspecialchars($email ?? '') ?>"
                   autocomplete="email">

            <label for="password">Password</label>
            <input type="password" id="password" name="password" required
                   minlength="8" autocomplete="new-password">

            <label for="confirm">Confirm Password</label>
            <input type="password" id="confirm" name="confirm" required
                   minlength="8" autocomplete="new-password">

            <button type="submit">Register</button>
        </form>
        <div class="links"><a href="login.php">Already have an account? Log in</a></div>
    <?php endif; ?>
</div>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add web/register.php
git commit -m "Add account registration page with email verification"
```

---

### Task 5: Create email verification page (verify.php)

**Files:**
- Create: `web/verify.php`

- [ ] **Step 1: Create web/verify.php**

```php
<?php
/**
 * verify.php - Email verification for deMUSE accounts
 *
 * Validates the token from the verification email link.
 * On success, marks the account as verified.
 */

require_once __DIR__ . '/db.php';

$error = '';
$success = '';

$token = $_GET['token'] ?? '';

if (!$token) {
    $error = 'No verification token provided.';
} else {
    $pdo = get_pdo();

    /* Look up token, check expiry */
    $stmt = $pdo->prepare(
        'SELECT t.account_id, a.username
         FROM email_verify_tokens t
         JOIN accounts a ON a.account_id = t.account_id
         WHERE t.token = :t AND t.expires_at > NOW()'
    );
    $stmt->execute(['t' => $token]);
    $row = $stmt->fetch();

    if (!$row) {
        $error = 'Invalid or expired verification link. Please register again.';
    } else {
        /* Mark account as verified */
        $stmt = $pdo->prepare('UPDATE accounts SET is_verified = 1 WHERE account_id = :a');
        $stmt->execute(['a' => $row['account_id']]);

        /* Delete the used token */
        $stmt = $pdo->prepare('DELETE FROM email_verify_tokens WHERE token = :t');
        $stmt->execute(['t' => $token]);

        /* Clean up any other tokens for this account */
        $stmt = $pdo->prepare('DELETE FROM email_verify_tokens WHERE account_id = :a');
        $stmt->execute(['a' => $row['account_id']]);

        $success = 'Your account has been verified! You can now log in.';
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Verify Account</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #1a1a2e; color: #c0c0c0;
            display: flex; justify-content: center; align-items: center;
            min-height: 100vh;
        }
        .container {
            background: #16213e; padding: 2rem; border-radius: 8px;
            width: 100%; max-width: 400px; box-shadow: 0 4px 20px rgba(0,0,0,0.5);
            text-align: center;
        }
        h1 { color: #e0e0e0; margin-bottom: 1.5rem; }
        .error { color: #ff6b6b; margin-bottom: 1rem; }
        .success { color: #51cf66; margin-bottom: 1rem; }
        .links { margin-top: 1rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="container">
    <h1>Account Verification</h1>
    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
        <div class="links"><a href="register.php">Register again</a></div>
    <?php else: ?>
        <div class="success"><?= htmlspecialchars($success) ?></div>
        <div class="links"><a href="login.php">Log in</a></div>
    <?php endif; ?>
</div>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add web/verify.php
git commit -m "Add email verification endpoint"
```

---

## Chunk 3: Login & Token Generation

### Task 6: Create login page (login.php)

**Files:**
- Create: `web/login.php`

- [ ] **Step 1: Create web/login.php**

```php
<?php
/**
 * login.php - Login page for deMUSE
 *
 * Authenticates against the accounts table, generates a one-time token,
 * and presents options for browser (WebSocket) or telnet connection.
 */

require_once __DIR__ . '/db.php';

session_start();

$error = '';
$token = null;
$token_type = null;

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim($_POST['username'] ?? '');
    $password = $_POST['password'] ?? '';
    $connect_via = $_POST['connect_via'] ?? 'websocket';

    if (!$username || !$password) {
        $error = 'Please enter your username and password.';
    } else {
        $pdo = get_pdo();

        /* Look up account */
        $stmt = $pdo->prepare(
            'SELECT account_id, password_hash, is_verified, is_disabled
             FROM accounts WHERE username = :u'
        );
        $stmt->execute(['u' => $username]);
        $account = $stmt->fetch();

        if (!$account || !password_verify($password, $account['password_hash'])) {
            $error = 'Invalid username or password.';
        } elseif (!$account['is_verified']) {
            $error = 'Your account has not been verified. Please check your email.';
        } elseif ($account['is_disabled']) {
            $error = 'Your account has been disabled. Please contact an administrator.';
        } else {
            /* Authentication successful — generate one-time token */
            $token = bin2hex(random_bytes(32));
            $token_type = ($connect_via === 'telnet') ? 'telnet' : 'websocket';

            $stmt = $pdo->prepare(
                'INSERT INTO auth_tokens (account_id, token, token_type, expires_at)
                 VALUES (:a, :t, :tt, DATE_ADD(NOW(), INTERVAL 60 SECOND))'
            );
            $stmt->execute([
                'a'  => $account['account_id'],
                't'  => $token,
                'tt' => $token_type,
            ]);

            /* Update last login time */
            $stmt = $pdo->prepare('UPDATE accounts SET last_login = NOW() WHERE account_id = :a');
            $stmt->execute(['a' => $account['account_id']]);
        }
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Login</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #1a1a2e; color: #c0c0c0;
            display: flex; justify-content: center; align-items: center;
            min-height: 100vh;
        }
        .container {
            background: #16213e; padding: 2rem; border-radius: 8px;
            width: 100%; max-width: 400px; box-shadow: 0 4px 20px rgba(0,0,0,0.5);
        }
        h1 { color: #e0e0e0; margin-bottom: 1.5rem; text-align: center; }
        label { display: block; margin-bottom: 0.3rem; color: #a0a0a0; font-size: 0.9rem; }
        input[type="text"], input[type="password"] {
            width: 100%; padding: 0.6rem; margin-bottom: 1rem;
            background: #0f3460; border: 1px solid #333; border-radius: 4px;
            color: #e0e0e0; font-size: 1rem;
        }
        input:focus { outline: none; border-color: #00a8cc; }
        button, .btn {
            display: inline-block; width: 100%; padding: 0.7rem;
            background: #00a8cc; color: #fff; border: none; border-radius: 4px;
            font-size: 1rem; cursor: pointer; text-align: center;
            text-decoration: none; margin-bottom: 0.5rem;
        }
        button:hover, .btn:hover { background: #0090b0; }
        .btn-secondary { background: #2d4059; }
        .btn-secondary:hover { background: #3d5069; }
        .error { color: #ff6b6b; margin-bottom: 1rem; font-size: 0.9rem; }
        .links { margin-top: 1rem; text-align: center; font-size: 0.9rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
        .token-box {
            background: #0f3460; padding: 1rem; border-radius: 4px;
            font-family: monospace; font-size: 0.95rem; color: #51cf66;
            word-break: break-all; margin: 1rem 0; cursor: pointer;
            border: 1px solid #333; text-align: center;
        }
        .token-box:hover { border-color: #00a8cc; }
        .hint { font-size: 0.8rem; color: #888; margin-bottom: 1rem; text-align: center; }
        .connect-options { margin-top: 1rem; }
        .radio-group {
            display: flex; gap: 1rem; margin-bottom: 1rem;
            justify-content: center;
        }
        .radio-group label {
            display: flex; align-items: center; gap: 0.3rem;
            cursor: pointer; color: #c0c0c0;
        }
    </style>
</head>
<body>
<div class="container">
    <h1>deMUSE Login</h1>

    <?php if ($token && $token_type === 'websocket'): ?>
        <!-- Redirect to web client with token -->
        <p style="text-align:center; margin-bottom:1rem;">Authentication successful. Connecting...</p>
        <a class="btn" href="client.php?token=<?= urlencode($token) ?>">Connect via Browser</a>
        <div class="links"><a href="login.php">Back to login</a></div>

    <?php elseif ($token && $token_type === 'telnet'): ?>
        <!-- Show telnet token -->
        <p style="text-align:center; margin-bottom:0.5rem;">Paste this into your MUD client:</p>
        <div class="token-box" onclick="navigator.clipboard.writeText(this.textContent).then(()=>{this.style.borderColor='#51cf66'})">connect token:<?= htmlspecialchars($token) ?></div>
        <p class="hint">Click to copy. Token expires in 60 seconds.</p>
        <div class="links"><a href="login.php">Back to login</a></div>

    <?php else: ?>
        <?php if ($error): ?>
            <div class="error"><?= htmlspecialchars($error) ?></div>
        <?php endif; ?>
        <form method="post">
            <label for="username">Username</label>
            <input type="text" id="username" name="username" required
                   value="<?= htmlspecialchars($username ?? '') ?>"
                   autocomplete="username" autofocus>

            <label for="password">Password</label>
            <input type="password" id="password" name="password" required
                   autocomplete="current-password">

            <div class="radio-group">
                <label><input type="radio" name="connect_via" value="websocket" checked> Browser</label>
                <label><input type="radio" name="connect_via" value="telnet"> MUD Client</label>
            </div>

            <button type="submit">Log In</button>
        </form>
        <div class="links"><a href="register.php">Create an account</a></div>
    <?php endif; ?>
</div>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add web/login.php
git commit -m "Add login page with token generation for browser and telnet connections"
```

---

### Task 7: Create web client page (client.php)

**Files:**
- Create: `web/client.php`
- Modify: `web/index.html` (will be superseded)

- [ ] **Step 1: Create web/client.php**

This replaces `index.html`. It receives a token via URL parameter and automatically sends `connect token:<token>` on WebSocket open.

```php
<?php
/**
 * client.php - xterm.js web client for deMUSE
 *
 * Receives a one-time auth token via URL parameter and automatically
 * authenticates over WebSocket. Falls back to manual reconnect prompt
 * on disconnect.
 */

$token = $_GET['token'] ?? '';
if (!$token) {
    header('Location: login.php');
    exit;
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.min.css">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        html, body { height: 100%; background: #000; overflow: hidden; }
        #terminal { width: 100%; height: 100%; }
        #status {
            position: fixed;
            top: 4px;
            right: 8px;
            font-family: monospace;
            font-size: 12px;
            color: #666;
            z-index: 100;
        }
        #status.connected { color: #0a0; }
        #status.disconnected { color: #a00; }
    </style>
</head>
<body>
    <div id="status" class="disconnected">disconnected</div>
    <div id="terminal"></div>

    <script src="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-fit@0.10.0/lib/addon-fit.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-web-links@0.11.0/lib/addon-web-links.min.js"></script>
    <script>
    (function() {
        'use strict';

        var authToken = <?= json_encode($token) ?>;
        var tokenSent = false;

        var term = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            fontFamily: '"Fira Code", "Cascadia Code", "Consolas", monospace',
            theme: {
                background: '#000000',
                foreground: '#c0c0c0',
                cursor: '#c0c0c0'
            },
            scrollback: 10000,
            convertEol: true
        });

        var fitAddon = new FitAddon.FitAddon();
        var webLinksAddon = new WebLinksAddon.WebLinksAddon();

        term.loadAddon(fitAddon);
        term.loadAddon(webLinksAddon);

        var container = document.getElementById('terminal');
        var statusEl = document.getElementById('status');
        term.open(container);
        fitAddon.fit();

        var ws = null;
        var inputLine = '';
        var waitingForReconnect = false;

        function setStatus(text, cls) {
            statusEl.textContent = text;
            statusEl.className = cls;
        }

        function connect() {
            if (ws && ws.readyState <= 1) return;

            var proto = (location.protocol === 'https:') ? 'wss:' : 'ws:';
            var url = proto + '//' + location.host + '/ws';

            setStatus('connecting...', 'disconnected');
            ws = new WebSocket(url, 'demuse');

            ws.onopen = function() {
                setStatus('connected', 'connected');
                inputLine = '';

                /* Send auth token automatically on first connect */
                if (authToken && !tokenSent) {
                    ws.send('connect token:' + authToken + '\r\n');
                    tokenSent = true;
                    authToken = null;  /* Clear from memory */
                }
            };

            ws.onmessage = function(ev) {
                term.write(ev.data);
            };

            ws.onclose = function() {
                setStatus('disconnected \u2014 press any key to reconnect', 'disconnected');
                term.write('\r\n\x1b[31m--- Connection closed \u2014 press any key to reconnect ---\x1b[0m\r\n');
                ws = null;
                waitingForReconnect = true;
            };

            ws.onerror = function() {
                /* onclose will fire after this */
            };
        }

        term.onData(function(data) {
            if (waitingForReconnect) {
                waitingForReconnect = false;
                /* On reconnect, redirect to login since token is consumed */
                window.location.href = 'login.php';
                return;
            }
            if (!ws || ws.readyState !== 1) return;

            for (var i = 0; i < data.length; i++) {
                var ch = data.charCodeAt(i);

                if (ch === 13 || ch === 10) {
                    term.write('\r\n');
                    ws.send(inputLine + '\r\n');
                    inputLine = '';
                } else if (ch === 127 || ch === 8) {
                    if (inputLine.length > 0) {
                        inputLine = inputLine.slice(0, -1);
                        term.write('\b \b');
                    }
                } else if (ch === 21) {
                    while (inputLine.length > 0) {
                        inputLine = inputLine.slice(0, -1);
                        term.write('\b \b');
                    }
                } else if (ch >= 32) {
                    inputLine += data[i];
                    term.write(data[i]);
                }
            }
        });

        window.addEventListener('resize', function() {
            fitAddon.fit();
        });

        connect();
    })();
    </script>
</body>
</html>
```

Key differences from the old `index.html`:
- Receives `authToken` from PHP and auto-sends `connect token:<token>` on WebSocket open
- Clears token from JS memory after sending
- On disconnect/reconnect, redirects to login page (token is consumed, can't reuse)

- [ ] **Step 2: Commit**

```bash
git add web/client.php
git commit -m "Add token-aware xterm.js web client, replaces index.html"
```

---

## Chunk 4: C Server-Side Token Validation

### Task 8: Create mariadb_auth module

**Files:**
- Create: `src/db/mariadb_auth.c`
- Create: `src/hdrs/mariadb_auth.h`

- [ ] **Step 1: Create src/hdrs/mariadb_auth.h**

Follow the pattern from `mariadb_lockout.h`:

```c
/* mariadb_auth.h - Token-based authentication via MariaDB
 *
 * Validates one-time login tokens generated by the PHP web layer.
 * Tokens are stored in the auth_tokens table with a 60-second expiry.
 * The PHP layer handles all password operations — netmuse never sees
 * plaintext passwords.
 *
 * Also manages the universe_players mapping (account_id → player_dbref).
 *
 * SAFETY:
 * - All SQL uses mysql_real_escape_string to prevent injection
 * - Connection checked before every operation
 */

#ifndef _MARIADB_AUTH_H_
#define _MARIADB_AUTH_H_

#include "config.h"

#ifdef USE_MARIADB

/* ============================================================================
 * TOKEN VALIDATION
 * ============================================================================ */

/*
 * mariadb_auth_validate_token - Validate and consume a one-time login token
 *
 * Checks the auth_tokens table for a matching, unexpired, unconsumed token.
 * On success, marks the token as consumed and returns the account_id.
 *
 * @param token  The token string (64-char hex)
 * @return account_id on success, 0 on failure (invalid/expired/consumed)
 */
long mariadb_auth_validate_token(const char *token);

/*
 * mariadb_auth_cleanup_expired - Delete expired and consumed tokens
 *
 * Called periodically from timer.c to keep the auth_tokens and
 * email_verify_tokens tables clean.
 */
void mariadb_auth_cleanup_expired(void);

/* ============================================================================
 * ACCOUNT-PLAYER MAPPING
 * ============================================================================ */

/*
 * mariadb_auth_find_player - Find the player dbref for an account in a universe
 *
 * @param account_id  Account ID to look up
 * @param universe_id Universe ID (0 for lobby/default in Phase 1b)
 * @return player dbref on success, NOTHING (-1) if no mapping exists
 */
dbref mariadb_auth_find_player(long account_id, int universe_id);

/*
 * mariadb_auth_link_player - Link an account to a player in a universe
 *
 * Creates a row in universe_players. Used after auto-creating a player
 * on first token login.
 *
 * @param account_id  Account ID
 * @param universe_id Universe ID (0 for Phase 1b)
 * @param player      Player dbref
 * @return 1 on success, 0 on failure
 */
int mariadb_auth_link_player(long account_id, int universe_id, dbref player);

/*
 * mariadb_auth_get_username - Get the username for an account
 *
 * @param account_id  Account ID
 * @param buf         Output buffer for username
 * @param buflen      Size of output buffer
 * @return 1 on success, 0 if account not found
 */
int mariadb_auth_get_username(long account_id, char *buf, size_t buflen);

#else /* !USE_MARIADB */

/* Stub functions — server requires MariaDB so these should never be reached */
static inline long mariadb_auth_validate_token(
    const char *t __attribute__((unused))) { return 0; }
static inline void mariadb_auth_cleanup_expired(void) { }
static inline dbref mariadb_auth_find_player(
    long a __attribute__((unused)),
    int u __attribute__((unused))) { return -1; }
static inline int mariadb_auth_link_player(
    long a __attribute__((unused)),
    int u __attribute__((unused)),
    dbref p __attribute__((unused))) { return 0; }
static inline int mariadb_auth_get_username(
    long a __attribute__((unused)),
    char *b __attribute__((unused)),
    size_t l __attribute__((unused))) { return 0; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_AUTH_H_ */
```

- [ ] **Step 2: Create src/db/mariadb_auth.c**

```c
/* mariadb_auth.c - Token-based authentication via MariaDB
 *
 * Validates one-time login tokens generated by the PHP web layer.
 * Manages the account-to-player mapping in universe_players.
 *
 * SAFETY:
 * - All string inputs escaped with mysql_real_escape_string
 * - Connection validated before every query
 * - Tokens consumed (deleted) on successful validation
 */

#include <stdio.h>
#include <string.h>
#include <mysql/mysql.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "mariadb.h"
#include "mariadb_auth.h"

#ifdef USE_MARIADB

/* ============================================================================
 * TOKEN VALIDATION
 * ============================================================================ */

long mariadb_auth_validate_token(const char *token)
{
    MYSQL *conn;
    MYSQL_RES *result;
    MYSQL_ROW row;
    char escaped_token[129];
    char query[512];
    long account_id = 0;

    if (!token || !*token) {
        return 0;
    }

    conn = (MYSQL *)mariadb_get_connection();
    if (!conn) {
        return 0;
    }

    mysql_real_escape_string(conn, escaped_token, token,
                             (unsigned long)strlen(token));

    /* Find valid, unexpired, unconsumed token */
    snprintf(query, sizeof(query),
             "SELECT account_id FROM auth_tokens "
             "WHERE token = '%s' AND consumed = 0 AND expires_at > NOW()",
             escaped_token);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "MariaDB auth: token query failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    row = mysql_fetch_row(result);
    if (row && row[0]) {
        account_id = atol(row[0]);
    }
    mysql_free_result(result);

    if (account_id > 0) {
        /* Consume the token — delete it so it can't be reused */
        snprintf(query, sizeof(query),
                 "DELETE FROM auth_tokens WHERE token = '%s'",
                 escaped_token);
        mysql_query(conn, query);
    }

    return account_id;
}

void mariadb_auth_cleanup_expired(void)
{
    MYSQL *conn;

    conn = (MYSQL *)mariadb_get_connection();
    if (!conn) {
        return;
    }

    /* Clean up expired/consumed auth tokens */
    mysql_query(conn,
        "DELETE FROM auth_tokens WHERE expires_at < NOW() OR consumed = 1");

    /* Clean up expired email verification tokens */
    mysql_query(conn,
        "DELETE FROM email_verify_tokens WHERE expires_at < NOW()");
}

/* ============================================================================
 * ACCOUNT-PLAYER MAPPING
 * ============================================================================ */

dbref mariadb_auth_find_player(long account_id, int universe_id)
{
    MYSQL *conn;
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    dbref player = NOTHING;

    conn = (MYSQL *)mariadb_get_connection();
    if (!conn) {
        return NOTHING;
    }

    snprintf(query, sizeof(query),
             "SELECT player_dbref FROM universe_players "
             "WHERE account_id = %ld AND universe_id = %d",
             account_id, universe_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "MariaDB auth: find_player query failed: %s\n",
                mysql_error(conn));
        return NOTHING;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return NOTHING;
    }

    row = mysql_fetch_row(result);
    if (row && row[0]) {
        player = atol(row[0]);
    }
    mysql_free_result(result);

    return player;
}

int mariadb_auth_link_player(long account_id, int universe_id, dbref player)
{
    MYSQL *conn;
    char query[256];

    conn = (MYSQL *)mariadb_get_connection();
    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "INSERT INTO universe_players (account_id, universe_id, player_dbref) "
             "VALUES (%ld, %d, %" DBREF_FMT ")",
             account_id, universe_id, player);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "MariaDB auth: link_player failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    return 1;
}

int mariadb_auth_get_username(long account_id, char *buf, size_t buflen)
{
    MYSQL *conn;
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];

    if (!buf || buflen < 1) {
        return 0;
    }
    buf[0] = '\0';

    conn = (MYSQL *)mariadb_get_connection();
    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "SELECT username FROM accounts WHERE account_id = %ld",
             account_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "MariaDB auth: get_username failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    row = mysql_fetch_row(result);
    if (row && row[0]) {
        strncpy(buf, row[0], buflen - 1);
        buf[buflen - 1] = '\0';
        mysql_free_result(result);
        return 1;
    }

    mysql_free_result(result);
    return 0;
}

#endif /* USE_MARIADB */
```

- [ ] **Step 3: Add mariadb_auth.c to the build**

In `src/db/Makefile`, add `mariadb_auth.c` to the SRCS line:

```makefile
SRCS = $(SRCS_BASE) mariadb.c mariadb_mail.c mariadb_board.c \
       mariadb_channel.c mariadb_lockout.c mariadb_help.c mariadb_news.c \
       mariadb_auth.c
```

- [ ] **Step 4: Build and verify no errors**

```bash
cd /home/ken/homeserver/ken/Games/deMUSE/demuse/src && make install 2>&1 | tail -20
```

Expected: clean build with no warnings from `mariadb_auth.c`.

- [ ] **Step 5: Commit**

```bash
git add src/hdrs/mariadb_auth.h src/db/mariadb_auth.c src/db/Makefile
git commit -m "Add mariadb_auth module: token validation, account-player mapping"
```

---

### Task 9: Add account_id to descriptor and integrate token auth in check_connect

**Files:**
- Modify: `src/hdrs/net.h` (line 69, add `account_id` field)
- Modify: `src/io/connection_handler.c` (add token path in `check_connect()`)
- Modify: `src/hdrs/externs.h` (add extern declarations)
- Modify: `src/muse/timer.c` (add periodic cleanup)

- [ ] **Step 1: Add account_id to descriptor_data in net.h**

After the `void *wsi;` line (line 69):

```c
  long account_id; /* Account ID from web auth, 0 if not authenticated via token */
```

- [ ] **Step 2: Add #include and token auth path in connection_handler.c**

Add `#include "mariadb_auth.h"` to the includes section.

In `check_connect()`, after `parse_connect()` is called and before the existing connect/create handling, add the token detection:

```c
    /* Token-based authentication (web login) */
    if (strncmp(user, "token:", 6) == 0) {
        char *token_str = user + 6;
        long acct_id;
        dbref player;
        char username[51];

        acct_id = mariadb_auth_validate_token(token_str);
        if (acct_id <= 0) {
            queue_string(d, "Invalid or expired token.\n");
            return;
        }

        d->account_id = acct_id;

        /* Session takeover: disconnect any existing session for this account */
        {
            struct descriptor_data *sd, *sd_next;
            for (sd = descriptor_list; sd; sd = sd_next) {
                sd_next = sd->next;
                if (sd != d && sd->account_id == acct_id && sd->state == CONNECTED) {
                    queue_string(sd, "Connection superseded by new login.\n");
                    flush_all_output();
                    shutdownsock(sd);
                }
            }
        }

        /* Look up existing player for this account */
        player = mariadb_auth_find_player(acct_id, 0);

        if (player == NOTHING) {
            /* No player yet — auto-create one */
            if (!mariadb_auth_get_username(acct_id, username, sizeof(username))) {
                queue_string(d, "Account error. Please contact an administrator.\n");
                return;
            }

            /* Check if player name is already taken (pre-migration collision) */
            if (lookup_player(username) != NOTHING) {
                queue_string(d, "That player name is already taken.\n");
                queue_string(d, "Please contact an administrator to link your account.\n");
                return;
            }

            player = create_player(username, "!token-auth!", CLASS_VISITOR, player_start);
            if (player == NOTHING) {
                queue_string(d, "Failed to create player. Please contact an administrator.\n");
                return;
            }

            mariadb_auth_link_player(acct_id, 0, player);

            log_io(tprintf("|G+TOKEN CREATE| account %ld created player %s(#%" DBREF_FMT ")",
                           acct_id, db[player].name, player));
        }

        /* Verify the player object is still valid */
        if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
            queue_string(d, "Your player object is invalid. Please contact an administrator.\n");
            return;
        }

        /* Connect */
        d->state = CONNECTED;
        d->connected_at = now;
        d->player = player;

        log_io(tprintf("|G+TOKEN CONNECT| account %ld concid %ld player %s(#%" DBREF_FMT ")",
                       acct_id, d->concid, db[player].name, player));

        com_send_as_hidden("pub_io",
            tprintf("|G+CONNECT| %s - %s",
                    unparse_object_a(player, player),
                    d->addr),
            player);

        send_message_text(d, motd_msg, 0);
        announce_connect(player);
        do_look_around(player);
        return;
    }
```

This block should be inserted early in `check_connect()`, after `parse_connect()` but before the existing guest/connect/create handling. The `return` at the end prevents falling through to the old auth path.

- [ ] **Step 3: Add periodic token cleanup in timer.c**

Add `#include "mariadb_auth.h"` to the includes.

In the `dispatch()` function, near the `check_newday()` call (every 60 ticks), add:

```c
  /* Clean up expired auth tokens every 5 minutes */
  if (!(ticks % 300)) {
    mariadb_auth_cleanup_expired();
  }
```

- [ ] **Step 4: Add extern declarations in externs.h**

Add near the other MariaDB-related declarations:

```c
/* mariadb_auth.c - token authentication */
extern long mariadb_auth_validate_token(const char *token);
extern void mariadb_auth_cleanup_expired(void);
extern dbref mariadb_auth_find_player(long account_id, int universe_id);
extern int mariadb_auth_link_player(long account_id, int universe_id, dbref player);
extern int mariadb_auth_get_username(long account_id, char *buf, size_t buflen);
```

- [ ] **Step 5: Build and verify**

```bash
cd /home/ken/homeserver/ken/Games/deMUSE/demuse/src && make install 2>&1 | tail -20
```

Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add src/hdrs/net.h src/io/connection_handler.c src/muse/timer.c src/hdrs/externs.h
git commit -m "Integrate token auth into check_connect: validate, session takeover, auto-create player"
```

---

## Chunk 5: Apache Setup & Integration Testing

### Task 10: Create Apache vhost template and configure local Apache

**Files:**
- Create: `web/apache-demuse.conf.example`

- [ ] **Step 1: Create the template**

```apache
# Apache vhost for deMUSE web interface
#
# Copy to /etc/apache2/sites-available/demuse.conf and adjust:
#   1. DocumentRoot path
#   2. DEMUSE_HOME path
#   3. Directory path
#
# Required Apache modules: mod_php, mod_rewrite, mod_proxy, mod_proxy_wstunnel
#   sudo a2enmod rewrite proxy proxy_wstunnel proxy_http
#
# Enable with: sudo a2ensite demuse && sudo systemctl reload apache2

<VirtualHost *:80>
    ServerName localhost
    DocumentRoot /path/to/demuse/web

    # Base directory for PHP to find config files
    SetEnv DEMUSE_HOME /path/to/demuse

    <Directory /path/to/demuse/web>
        AllowOverride All
        Require all granted

        # Default to login page
        DirectoryIndex login.php
    </Directory>

    # WebSocket proxy to netmuse
    ProxyPass /ws ws://localhost:4209/ upgrade=websocket
    ProxyPassReverse /ws ws://localhost:4209/
</VirtualHost>
```

- [ ] **Step 2: Install the actual config for this server**

The user needs to run:
```bash
sudo cp web/apache-demuse.conf.example /etc/apache2/sites-available/demuse.conf
```

Then edit `/etc/apache2/sites-available/demuse.conf`:
- Replace all `/path/to/demuse` with `/home/ken/homeserver/ken/Games/deMUSE/demuse`

Then:
```bash
sudo a2enmod rewrite proxy proxy_wstunnel proxy_http
sudo a2ensite demuse
sudo a2dissite 000-default  # if this is the only site
sudo systemctl reload apache2
```

- [ ] **Step 3: Commit template**

```bash
git add web/apache-demuse.conf.example
git commit -m "Add Apache vhost template with DEMUSE_HOME and WebSocket proxy config"
```

---

### Task 11: End-to-end testing

- [ ] **Step 1: Run the account table SQL**

```bash
mysql -h 192.168.203.21 -u demuse -p'nyh6QZM*xem9uhj8hcb' demuse < config/setup_mariadb.sql
```

Verify: `mysql ... -e "SHOW TABLES LIKE 'accounts'"`

- [ ] **Step 2: Build and restart the game server**

```bash
cd /home/ken/homeserver/ken/Games/deMUSE/demuse/src && make install
```

Restart netmuse.

- [ ] **Step 3: Test registration flow**

1. Browse to `http://<server>/register.php`
2. Fill in username, email, password
3. Submit — should see "check your email" message
4. Check email for verification link
5. Click link — should see "account verified" message

- [ ] **Step 4: Test login + browser connect**

1. Browse to `http://<server>/login.php`
2. Enter credentials, select "Browser"
3. Should redirect to `client.php?token=...`
4. xterm.js should open, auto-send `connect token:...`, land in the game

- [ ] **Step 5: Test login + telnet token**

1. Login again, select "MUD Client"
2. Should display `connect token:abc123...`
3. Open telnet client, connect to game port
4. Paste the `connect token:...` string
5. Should authenticate and land in the game

- [ ] **Step 6: Test session takeover**

1. Connect via browser (stay connected)
2. Login again and connect via browser
3. First session should show "Connection superseded" and disconnect
4. Second session should be active

- [ ] **Step 7: Test expired token**

1. Login, get token, wait 60+ seconds
2. Try connecting with the expired token
3. Should see "Invalid or expired token."

- [ ] **Step 8: Commit any fixes from testing**

```bash
git add -A
git commit -m "Fix issues found during end-to-end auth testing"
```

---

### Task 12: Migration support for existing players

**Files:**
- Modify: `src/io/connection_handler.c`

- [ ] **Step 1: Add legacy connect migration path**

In the existing `connect <name> <password>` success path in `check_connect()`, after the player is authenticated via the traditional method, add migration logic:

```c
        /* Legacy connect succeeded — check if this player has an account */
        /* If not, notify them to create one via the web interface */
        if (d->account_id == 0) {
            queue_string(d,
                "\n*** NOTICE: Plaintext password login will be disabled soon. ***\n"
                "*** Please create a web account at the login page.          ***\n\n");
        }
```

This does not auto-create accounts (passwords are stored as crypt hashes in the db[], and we want bcrypt via PHP). It just warns players to migrate.

- [ ] **Step 2: Commit**

```bash
git add src/io/connection_handler.c
git commit -m "Add migration notice for legacy plaintext password logins"
```

---

### Task 13: Update documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README.md Universe Project section**

Update Phase 1b status to reflect account auth implementation. Add:
- Account registration and email verification via PHP
- Token-based login for WebSocket and telnet
- `DEMUSE_HOME` env var for installation portability
- Session takeover on reconnect
- Auto-player-creation on first token login

- [ ] **Step 2: Update MEMORY.md**

Update the "Current State" section with:
- Account auth system status
- New files (mariadb_auth.c/h, PHP files)
- `DEMUSE_HOME` convention
- Phase 1b status

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Update documentation for Phase 1b account authentication"
```

---

## Summary

| Task | What | Files |
|------|------|-------|
| 1 | Database schema | `config/setup_mariadb.sql` |
| 2 | PHP database helper | `web/db.php`, `web/.htaccess` |
| 3 | PHP email helper | `web/mailer.php` |
| 4 | Registration page | `web/register.php` |
| 5 | Email verification | `web/verify.php` |
| 6 | Login page | `web/login.php` |
| 7 | Web client (token-aware) | `web/client.php` |
| 8 | C auth module | `src/db/mariadb_auth.c`, `src/hdrs/mariadb_auth.h`, `src/db/Makefile` |
| 9 | Token validation in game | `src/hdrs/net.h`, `src/io/connection_handler.c`, `src/muse/timer.c`, `src/hdrs/externs.h` |
| 10 | Apache config | `web/apache-demuse.conf.example` |
| 11 | End-to-end testing | (no new files) |
| 12 | Migration notices | `src/io/connection_handler.c` |
| 13 | Documentation | `README.md` |

Build order: Tasks 1-7 (PHP layer) and Tasks 8-9 (C layer) are independent and can be developed in parallel. Task 10 must precede Task 11 (testing). Task 12-13 are cleanup.
