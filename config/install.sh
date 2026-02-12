#!/bin/bash
# install.sh - Full installer for deMUSE
#
# This script handles the complete setup process:
#   1. Check prerequisites (gcc, make, libmariadb-dev)
#   2. Build the code (cd src && make install)
#   3. Run config/setup_mariadb.sh to create database/user/table
#   4. Seed the database with default config values from defaults.sql
#   5. Print startup instructions
#
# Usage:
#   cd demuse
#   bash config/install.sh
#
# The script is idempotent - safe to run multiple times.

set -e

# ============================================================================
# Find the project root
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "  deMUSE Full Installer"
echo "============================================"
echo ""
echo "Project directory: $PROJECT_DIR"
echo ""

# ============================================================================
# Step 1: Check prerequisites
# ============================================================================
echo "-- Step 1: Checking prerequisites --"
echo ""

MISSING=""

if ! command -v gcc >/dev/null 2>&1; then
    MISSING="$MISSING gcc"
fi

if ! command -v make >/dev/null 2>&1; then
    MISSING="$MISSING make"
fi

# Check for MariaDB development library
if ! command -v mariadb_config >/dev/null 2>&1; then
    if ! pkg-config --exists libmariadb 2>/dev/null; then
        MISSING="$MISSING libmariadb-dev"
    fi
fi

# Check for a MySQL/MariaDB client (needed for seeding)
MYSQL_CLIENT=""
if command -v mariadb >/dev/null 2>&1; then
    MYSQL_CLIENT="mariadb"
elif command -v mysql >/dev/null 2>&1; then
    MYSQL_CLIENT="mysql"
fi

if [ -z "$MYSQL_CLIENT" ]; then
    MISSING="$MISSING mariadb-client"
fi

if [ -n "$MISSING" ]; then
    echo "ERROR: Missing required packages:$MISSING"
    echo ""
    echo "Install with: sudo apt install$MISSING"
    echo "  (or equivalent for your distribution)"
    exit 1
fi

echo "  gcc:              $(gcc --version | head -1)"
echo "  make:             $(make --version | head -1)"
echo "  mariadb_config:   $(mariadb_config --version 2>/dev/null || echo 'via pkg-config')"
echo "  MySQL client:     $MYSQL_CLIENT"
echo ""
echo "All prerequisites satisfied."
echo ""

# ============================================================================
# Step 2: Build the code
# ============================================================================
echo "-- Step 2: Building deMUSE --"
echo ""

cd "$PROJECT_DIR/src"
make clean 2>/dev/null || true
if make install 2>&1 | tail -5; then
    echo ""
    echo "Build successful."
else
    echo ""
    echo "ERROR: Build failed. Check the output above for errors."
    exit 1
fi
echo ""

# ============================================================================
# Step 3: Set up MariaDB (database, user, table)
# ============================================================================
echo "-- Step 3: MariaDB setup --"
echo ""

CONF_FILE="$PROJECT_DIR/run/db/mariadb.conf"

if [ -f "$CONF_FILE" ]; then
    echo "MariaDB config file already exists: $CONF_FILE"
    echo "Skipping database setup (run 'bash config/setup_mariadb.sh' to reconfigure)."
    echo ""
else
    echo "Running MariaDB setup..."
    echo ""
    bash "$PROJECT_DIR/config/setup_mariadb.sh"
    echo ""
fi

# ============================================================================
# Step 4: Seed default config values
# ============================================================================
echo "-- Step 4: Seeding default config values --"
echo ""

if [ ! -f "$CONF_FILE" ]; then
    echo "ERROR: MariaDB config file not found at $CONF_FILE"
    echo "Run 'bash config/setup_mariadb.sh' first."
    exit 1
fi

# Read credentials from the config file
DB_HOST="localhost"
DB_PORT="3306"
DB_USER="demuse"
DB_PASS=""
DB_NAME="demuse"

while IFS='=' read -r key value; do
    # Skip comments and blank lines
    case "$key" in
        '#'*|'') continue ;;
    esac
    # Trim whitespace
    key=$(echo "$key" | xargs)
    value=$(echo "$value" | xargs)
    case "$key" in
        host)     DB_HOST="$value" ;;
        port)     DB_PORT="$value" ;;
        user)     DB_USER="$value" ;;
        password) DB_PASS="$value" ;;
        database) DB_NAME="$value" ;;
    esac
done < "$CONF_FILE"

# Run the defaults SQL
SEED_ARGS="-u $DB_USER -h $DB_HOST -P $DB_PORT"
if [ -n "$DB_PASS" ]; then
    SEED_ARGS="$SEED_ARGS -p$DB_PASS"
fi

echo "Seeding config table with defaults..."
if $MYSQL_CLIENT $SEED_ARGS "$DB_NAME" < "$PROJECT_DIR/config/defaults.sql" 2>&1; then
    echo "Default config values seeded successfully."
else
    echo ""
    echo "ERROR: Failed to seed default config values."
    echo "Check that the database is accessible and the config table exists."
    exit 1
fi
echo ""

# Verify the seed
ROW_COUNT=$($MYSQL_CLIENT $SEED_ARGS "$DB_NAME" -N -e "SELECT COUNT(*) FROM config;" 2>/dev/null || echo "0")
echo "Config table now has $ROW_COUNT entries."
echo ""

# ============================================================================
# Step 5: Done
# ============================================================================
echo "============================================"
echo "  Installation Complete!"
echo "============================================"
echo ""
echo "To start deMUSE:"
echo "  cd $PROJECT_DIR/run"
echo "  ../bin/wd &       (starts watchdog + server)"
echo ""
echo "Or start directly (no watchdog):"
echo "  cd $PROJECT_DIR/run"
echo "  ../bin/netmuse"
echo ""
echo "Once running, connect on port $(grep "inet_port" "$PROJECT_DIR/config/defaults.sql" | head -1 | grep -o "'[0-9]*'" | tr -d "'")"
echo "and log in as root to verify:"
echo "  @config dbstatus   (should show 'Connected')"
echo "  @info config       (shows all config values)"
echo ""
