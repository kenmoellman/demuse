#!/bin/bash
# setup_mariadb.sh - Interactive setup for deMUSE MariaDB configuration
#
# This script handles three scenarios:
#   A) Full setup: create database, user, table, and config file
#   B) Existing database: just create the config table and config file
#   C) Config file only: database is fully set up, just write the config
#
# Usage:
#   cd demuse
#   bash config/setup_mariadb.sh
#
# Requirements:
#   - mariadb or mysql command-line client (for database operations)
#   - Not required if only writing the config file

set -e

# ============================================================================
# Find the project root (script is in config/)
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONF_FILE="$PROJECT_DIR/run/db/mariadb.conf"

echo "============================================"
echo "  deMUSE MariaDB Setup"
echo "============================================"
echo ""

# ============================================================================
# Find a usable MySQL/MariaDB client
# ============================================================================
MYSQL_CLIENT=""
if command -v mariadb >/dev/null 2>&1; then
    MYSQL_CLIENT="mariadb"
elif command -v mysql >/dev/null 2>&1; then
    MYSQL_CLIENT="mysql"
fi

if [ -n "$MYSQL_CLIENT" ]; then
    echo "Using client: $MYSQL_CLIENT"
else
    echo "Note: No mariadb/mysql client found. Database operations will be skipped."
fi
echo ""

# ============================================================================
# Check for existing config
# ============================================================================
if [ -f "$CONF_FILE" ]; then
    echo "WARNING: $CONF_FILE already exists."
    read -p "Overwrite? (y/N): " OVERWRITE
    if [ "$OVERWRITE" != "y" ] && [ "$OVERWRITE" != "Y" ]; then
        echo "Aborting. Existing config preserved."
        exit 0
    fi
    echo ""
fi

# ============================================================================
# Prompt for deMUSE database settings
# ============================================================================
echo "-- deMUSE Database Settings --"
echo "These are the credentials deMUSE will use to connect."
echo ""

read -p "Database host [localhost]: " DB_HOST
DB_HOST="${DB_HOST:-localhost}"

read -p "Database port [3306]: " DB_PORT
DB_PORT="${DB_PORT:-3306}"

read -p "Database name [demuse]: " DB_NAME
DB_NAME="${DB_NAME:-demuse}"

read -p "Database user [demuse]: " DB_USER
DB_USER="${DB_USER:-demuse}"

# Password prompt (hidden input)
while true; do
    read -s -p "Database password: " DB_PASS
    echo ""
    if [ -z "$DB_PASS" ]; then
        echo "Password cannot be empty."
        continue
    fi
    read -s -p "Confirm password: " DB_PASS2
    echo ""
    if [ "$DB_PASS" != "$DB_PASS2" ]; then
        echo "Passwords do not match. Try again."
        continue
    fi
    break
done

echo ""
echo "-- Summary --"
echo "  Host:     $DB_HOST"
echo "  Port:     $DB_PORT"
echo "  Database: $DB_NAME"
echo "  User:     $DB_USER"
echo "  Password: ********"
echo ""

# ============================================================================
# Determine what database work is needed
# ============================================================================
NEED_CREATE_DB=0
NEED_CREATE_TABLE=0
SKIP_DB_SETUP=0

if [ -z "$MYSQL_CLIENT" ]; then
    echo "No database client available - skipping all database operations."
    echo "You will need to create the database, user, and config table manually."
    echo "See config/setup_mariadb.sql for reference SQL."
    echo ""
    SKIP_DB_SETUP=1
fi

if [ "$SKIP_DB_SETUP" -eq 0 ]; then
    # Try connecting as the deMUSE user first to see what already exists
    echo "Checking existing database setup..."

    DEMUSE_ARGS="-u $DB_USER -p$DB_PASS -h $DB_HOST -P $DB_PORT"

    if $MYSQL_CLIENT $DEMUSE_ARGS "$DB_NAME" -e "SELECT 1 FROM config LIMIT 1;" >/dev/null 2>&1; then
        # Database, user, and table all exist
        echo "  Database '$DB_NAME' exists:  YES"
        echo "  User '$DB_USER' can connect: YES"
        echo "  Config table exists:         YES"
        echo ""
        echo "Database is fully set up. Only the config file needs to be written."
        SKIP_DB_SETUP=1

    elif $MYSQL_CLIENT $DEMUSE_ARGS "$DB_NAME" -e "SELECT 1;" >/dev/null 2>&1; then
        # Database and user exist, but config table doesn't
        echo "  Database '$DB_NAME' exists:  YES"
        echo "  User '$DB_USER' can connect: YES"
        echo "  Config table exists:         NO"
        echo ""
        NEED_CREATE_TABLE=1

    elif $MYSQL_CLIENT $DEMUSE_ARGS -e "SELECT 1;" >/dev/null 2>&1; then
        # User can connect to server but database doesn't exist
        echo "  Database '$DB_NAME' exists:  NO"
        echo "  User '$DB_USER' can connect: YES (to server)"
        echo ""
        NEED_CREATE_DB=1
        NEED_CREATE_TABLE=1

    else
        # Can't connect as deMUSE user at all - need full setup
        echo "  Cannot connect as '$DB_USER' - full setup needed."
        echo ""
        NEED_CREATE_DB=1
        NEED_CREATE_TABLE=1
    fi
fi

# ============================================================================
# Database creation (if needed)
# ============================================================================
if [ "$SKIP_DB_SETUP" -eq 0 ] && [ "$NEED_CREATE_DB" -eq 1 ]; then
    echo "-- MariaDB Admin Credentials --"
    echo "An admin account is needed to create the database and/or user."
    echo "This is typically 'root' or another account with CREATE/GRANT privileges."
    echo ""

    read -p "Admin user [root]: " ADMIN_USER
    ADMIN_USER="${ADMIN_USER:-root}"

    read -s -p "Admin password (leave blank if using unix_socket auth): " ADMIN_PASS
    echo ""
    echo ""

    # Build the admin connection arguments
    ADMIN_ARGS="-u $ADMIN_USER"
    if [ -n "$ADMIN_PASS" ]; then
        ADMIN_ARGS="$ADMIN_ARGS -p$ADMIN_PASS"
    fi
    ADMIN_ARGS="$ADMIN_ARGS -h $DB_HOST -P $DB_PORT"

    # Test admin connection
    echo "Testing admin connection..."
    if ! $MYSQL_CLIENT $ADMIN_ARGS -e "SELECT 1;" >/dev/null 2>&1; then
        echo "ERROR: Cannot connect to MariaDB with the admin credentials provided."
        echo "Check that the MariaDB server is running and credentials are correct."
        echo ""
        echo "If the database was already created outside this script, re-run"
        echo "this script and enter the existing deMUSE user credentials."
        exit 1
    fi
    echo "Admin connection successful."
    echo ""

    # Escape single quotes in password for SQL
    DB_PASS_SQL="${DB_PASS//\'/\'\'}"

    # Determine the host portion for GRANT (localhost vs remote)
    if [ "$DB_HOST" = "localhost" ] || [ "$DB_HOST" = "127.0.0.1" ]; then
        GRANT_HOST="localhost"
    else
        GRANT_HOST="%"
    fi

    echo "Creating database and user..."

    $MYSQL_CLIENT $ADMIN_ARGS <<EOF
-- Create database
CREATE DATABASE IF NOT EXISTS \`$DB_NAME\`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- Create user (or update password if exists)
CREATE USER IF NOT EXISTS '$DB_USER'@'$GRANT_HOST'
  IDENTIFIED BY '$DB_PASS_SQL';

ALTER USER '$DB_USER'@'$GRANT_HOST'
  IDENTIFIED BY '$DB_PASS_SQL';

-- Grant privileges
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'$GRANT_HOST';
FLUSH PRIVILEGES;
EOF

    echo "Database and user created."
    echo ""
fi

# ============================================================================
# Config table creation (if needed)
# ============================================================================
if [ "$SKIP_DB_SETUP" -eq 0 ] && [ "$NEED_CREATE_TABLE" -eq 1 ]; then
    echo "Creating config table..."

    # Prefer connecting as the deMUSE user for table creation
    TABLE_ARGS="-u $DB_USER -p$DB_PASS -h $DB_HOST -P $DB_PORT"

    # If we just created the user, there's a small chance the grant
    # hasn't propagated yet; fall back to admin if available
    if ! $MYSQL_CLIENT $TABLE_ARGS "$DB_NAME" -e "SELECT 1;" >/dev/null 2>&1; then
        if [ -n "$ADMIN_ARGS" ]; then
            TABLE_ARGS="$ADMIN_ARGS"
        else
            echo "ERROR: Cannot connect as '$DB_USER' to create the config table."
            echo "The config file will still be written, but you may need to create"
            echo "the table manually. See config/setup_mariadb.sql for the SQL."
            NEED_CREATE_TABLE=0
        fi
    fi

    if [ "$NEED_CREATE_TABLE" -eq 1 ]; then
        $MYSQL_CLIENT $TABLE_ARGS "$DB_NAME" <<'EOF'
CREATE TABLE IF NOT EXISTS config (
  config_key   VARCHAR(64)  NOT NULL PRIMARY KEY,
  config_value TEXT         NOT NULL,
  config_type  ENUM('STR', 'NUM', 'REF', 'LNG') NOT NULL DEFAULT 'STR',
  description  TEXT         DEFAULT NULL,
  updated_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
                            ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE runtime configuration values';
EOF

        echo "Config table created."
        echo ""
    fi
fi

# ============================================================================
# Final verification (if we have a client)
# ============================================================================
if [ -n "$MYSQL_CLIENT" ] && [ "$SKIP_DB_SETUP" -eq 0 ]; then
    echo "Verifying deMUSE user connection..."
    VERIFY_ARGS="-u $DB_USER -p$DB_PASS -h $DB_HOST -P $DB_PORT $DB_NAME"
    if ! $MYSQL_CLIENT $VERIFY_ARGS -e "SELECT COUNT(*) AS config_rows FROM config;" 2>/dev/null; then
        echo "WARNING: Verification query failed. Check credentials and try again."
    else
        echo "Verification successful."
    fi
    echo ""
fi

# ============================================================================
# Write config file
# ============================================================================
echo "Writing config file: $CONF_FILE"

# Ensure directory exists
mkdir -p "$(dirname "$CONF_FILE")"

cat > "$CONF_FILE" <<CONF
# MariaDB Configuration for deMUSE
# Generated by setup_mariadb.sh on $(date)
#
# DO NOT commit this file to version control!

# Database host
host=$DB_HOST

# Database port
port=$DB_PORT

# Database user
user=$DB_USER

# Database password
password=$DB_PASS

# Database name
database=$DB_NAME
CONF

# Restrict permissions - only owner can read
chmod 600 "$CONF_FILE"

echo "Config file written with mode 600."
echo ""

# ============================================================================
# Done
# ============================================================================
echo "============================================"
echo "  Setup Complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Rebuild deMUSE:  cd src && make install"
echo "  2. Start deMUSE"
echo "  3. Log in as root and run:  @config/dbstatus"
echo "     (should show 'Connected')"
echo "  4. Seed the config table:   @config/seed"
echo "     (writes all current values to the database)"
echo ""
echo "To reconfigure later, run this script again."
