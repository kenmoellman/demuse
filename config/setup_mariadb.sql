-- setup_mariadb.sql - Reference SQL for deMUSE MariaDB config table
--
-- RECOMMENDED: Use the interactive setup script instead:
--   bash config/setup_mariadb.sh
--
-- The setup script prompts for all credentials and runs these commands
-- automatically. This file is provided as a reference for manual setup
-- or for environments where the script cannot be used.
--
-- Manual usage:
--   1. Edit the values below (database name, user, password)
--   2. Run:  mysql -u root -p < config/setup_mariadb.sql
--   3. Copy config/mariadb.conf.example to run/db/mariadb.conf
--   4. Edit run/db/mariadb.conf with matching credentials
--   5. Start deMUSE and run @config/seed in-game

-- ============================================================================
-- DATABASE AND USER SETUP
-- ============================================================================
-- Change 'demuse' and 'CHANGE_ME' to your preferred values.

CREATE DATABASE IF NOT EXISTS demuse
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'demuse'@'localhost'
  IDENTIFIED BY 'CHANGE_ME';

GRANT ALL PRIVILEGES ON demuse.* TO 'demuse'@'localhost';
FLUSH PRIVILEGES;

-- ============================================================================
-- CONFIG TABLE
-- ============================================================================

USE demuse;

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

-- After starting deMUSE, run @config/seed in-game to populate
-- all config values from the running server into this table.
