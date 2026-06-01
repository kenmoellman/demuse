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
    $stmt = $pdo->prepare('SELECT config_value FROM config WHERE config_key = :key LIMIT 1');
    $stmt->execute(['key' => $key]);
    $row = $stmt->fetch();
    return $row ? $row['config_value'] : null;
}

/**
 * Get the canonical base URL used in outgoing emails.
 *
 * Returns the trimmed `web_url_base` config value (e.g.
 * "https://demuse.example.com") with any trailing slash removed, or null
 * if it has not been configured. Callers MUST NOT fall back to
 * $_SERVER['HTTP_HOST'] — that value is attacker-controlled and was the
 * cause of a host-header injection finding in the email-bearing flows.
 *
 * @return string|null
 */
function get_web_url_base(): ?string
{
    $base = trim(get_config('web_url_base') ?? '');
    return $base === '' ? null : rtrim($base, '/');
}

/**
 * Start the PHP session with hardened cookie attributes.
 *
 * Idempotent — safe to call from every page entry point and from the CSRF
 * helpers. Sets:
 *   - HttpOnly: the cookie is not readable from JavaScript, so an XSS
 *     cannot exfiltrate the session that carries the CSRF token.
 *   - SameSite=Lax: the cookie is not sent on cross-site requests,
 *     blunting CSRF as defense-in-depth alongside the token check.
 *   - Secure: set only when the request arrived over HTTPS, so a plain-HTTP
 *     dev/test deployment is not locked out. Behind a TLS-terminating proxy
 *     that does not pass HTTPS through, harden this at the proxy instead.
 *
 * MUST be called before any output (it sends a Set-Cookie header). Call it
 * at the top of each page so the session is reliably started before HTML is
 * emitted — calling session_start() only later (e.g. from csrf_token() in
 * the form markup) would fail once output has begun.
 *
 * @return void
 */
function start_session(): void
{
    if (session_status() === PHP_SESSION_ACTIVE) {
        return;
    }
    $secure = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || (($_SERVER['SERVER_PORT'] ?? null) == 443);
    session_set_cookie_params([
        'httponly' => true,
        'secure'   => $secure,
        'samesite' => 'Lax',
    ]);
    session_start();
}

/**
 * Get/lazy-create the per-session CSRF token.
 *
 * The token is bound to the session, so an attacker hosting a hostile
 * form on another origin cannot fabricate a valid value. Pages embed it
 * in a hidden input; csrf_check() validates the POST against the
 * session value with a constant-time comparison.
 *
 * @return string
 */
function csrf_token(): string
{
    start_session();
    if (empty($_SESSION['csrf_token'])) {
        $_SESSION['csrf_token'] = bin2hex(random_bytes(32));
    }
    return $_SESSION['csrf_token'];
}

/**
 * Validate the CSRF token on a POST request.
 *
 * Returns false if the token is missing, mismatched, or the session has
 * no token. Uses hash_equals to defeat timing oracles. Callers should
 * treat false as a refusal to proceed and re-render the form.
 *
 * @return bool
 */
function csrf_check(): bool
{
    start_session();
    $sent = $_POST['csrf_token'] ?? '';
    $expected = $_SESSION['csrf_token'] ?? '';
    if (!is_string($sent) || $sent === '' || $expected === '') {
        return false;
    }
    return hash_equals($expected, $sent);
}

/**
 * Get the client IP as seen by PHP.
 *
 * This trusts $_SERVER['REMOTE_ADDR'], which is set by the web server
 * (Apache/nginx). If deMUSE is behind another proxy in front of that
 * web server, configure mod_remoteip / set_real_ip_from there — do
 * not honor X-Forwarded-For inside PHP, which would inherit the same
 * spoofing problem the C-side WebSocket layer guards against.
 *
 * @return string
 */
function client_ip(): string
{
    return $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0';
}

/**
 * Sliding-window rate-limit check.
 *
 * Returns true if the action is permitted (and increments the counter),
 * false if the caller has exceeded $max attempts within $window_sec
 * seconds for this (action, identifier) pair.
 *
 * Each call atomically reads, evaluates, and writes the counter under a
 * row lock so concurrent requests cannot race past the limit. On DB
 * error the function fails open (returns true) and logs — a stuck DB
 * should not lock everyone out, but the error_log entry surfaces it.
 *
 * @param string $action       Short action label, e.g. 'login', 'register'.
 * @param string $identifier   Per-actor identity, e.g. an IP or email.
 * @param int    $max          Permitted attempts within the window.
 * @param int    $window_sec   Sliding window length, in seconds.
 * @return bool
 */
function rate_limit_check(string $action, string $identifier, int $max, int $window_sec): bool
{
    /* Cap to the rate_key column width in the schema (VARCHAR(128)). */
    $key = substr($action . ':' . $identifier, 0, 128);
    $pdo = get_pdo();
    $started = false;

    try {
        $pdo->beginTransaction();
        $started = true;

        $stmt = $pdo->prepare(
            'SELECT counter, UNIX_TIMESTAMP(window_start) AS ws
             FROM rate_limit WHERE rate_key = :k FOR UPDATE'
        );
        $stmt->execute(['k' => $key]);
        $row = $stmt->fetch();

        $now = time();

        if (!$row) {
            $stmt = $pdo->prepare(
                'INSERT INTO rate_limit (rate_key, counter, window_start)
                 VALUES (:k, 1, NOW())'
            );
            $stmt->execute(['k' => $key]);
            $pdo->commit();
            return true;
        }

        if (($now - (int)$row['ws']) >= $window_sec) {
            /* Window expired — reset the counter. */
            $stmt = $pdo->prepare(
                'UPDATE rate_limit SET counter = 1, window_start = NOW()
                 WHERE rate_key = :k'
            );
            $stmt->execute(['k' => $key]);
            $pdo->commit();
            return true;
        }

        if ((int)$row['counter'] >= $max) {
            $pdo->commit();
            return false;
        }

        $stmt = $pdo->prepare(
            'UPDATE rate_limit SET counter = counter + 1 WHERE rate_key = :k'
        );
        $stmt->execute(['k' => $key]);
        $pdo->commit();
        return true;
    } catch (Throwable $e) {
        if ($started && $pdo->inTransaction()) {
            $pdo->rollBack();
        }
        error_log('deMUSE rate_limit: ' . $e->getMessage());
        /* Fail open on DB error so a stuck DB does not lock users out. */
        return true;
    }
}
