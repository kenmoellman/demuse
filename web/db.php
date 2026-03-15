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
