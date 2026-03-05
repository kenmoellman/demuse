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

-- ============================================================================
-- MAIL TABLE - Private player-to-player mail
-- ============================================================================

CREATE TABLE IF NOT EXISTS mail (
  mail_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,
  sender       BIGINT       NOT NULL,
  recipient    BIGINT       NOT NULL,
  sent_date    BIGINT       NOT NULL,
  flags        INT          NOT NULL DEFAULT 0,
  message      TEXT         NOT NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_recipient       (recipient),
  INDEX idx_sender          (sender),
  INDEX idx_recipient_flags (recipient, flags)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE private player-to-player mail';

-- ============================================================================
-- BOARD TABLE - Public board posts
-- ============================================================================

CREATE TABLE IF NOT EXISTS board (
  post_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,
  author       BIGINT       NOT NULL,
  board_room   BIGINT       NOT NULL DEFAULT 0,
  posted_date  BIGINT       NOT NULL,
  flags        INT          NOT NULL DEFAULT 0,
  message      TEXT         NOT NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_board_room (board_room),
  INDEX idx_author     (author)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE public board posts';

-- ============================================================================
-- CHANNELS TABLE - Channel definitions
-- ============================================================================

CREATE TABLE IF NOT EXISTS channels (
  channel_id   BIGINT       AUTO_INCREMENT PRIMARY KEY,
  name         VARCHAR(128) NOT NULL COMMENT 'plain name, no prefix chars',
  cname        VARCHAR(256) NOT NULL COMMENT 'ANSI-colored display name',
  owner        BIGINT       NOT NULL COMMENT 'owner player dbref',
  flags        BIGINT       NOT NULL DEFAULT 0,
  is_system    TINYINT      NOT NULL DEFAULT 0 COMMENT '1=built-in, cannot be deleted',
  min_level    TINYINT      NOT NULL DEFAULT 0 COMMENT '0=anyone 1=official 2=builder 3=director',
  password     VARCHAR(128) DEFAULT NULL COMMENT 'crypt password',
  topic        TEXT         DEFAULT NULL,
  join_msg     TEXT         DEFAULT NULL COMMENT 'was A_OENTER',
  leave_msg    TEXT         DEFAULT NULL COMMENT 'was A_OLEAVE',
  speak_lock   TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_SLOCK',
  join_lock    TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_LOCK',
  hide_lock    TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_LHIDE',
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE INDEX idx_name (name),
  INDEX idx_owner (owner)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE channel definitions';

-- ============================================================================
-- CHANNEL_MEMBERS TABLE - Channel membership, bans, operators
-- ============================================================================

CREATE TABLE IF NOT EXISTS channel_members (
  member_id    BIGINT       AUTO_INCREMENT PRIMARY KEY,
  channel_id   BIGINT       NOT NULL,
  player       BIGINT       NOT NULL COMMENT 'player dbref',
  alias        VARCHAR(128) NOT NULL DEFAULT '',
  color_name   VARCHAR(256) NOT NULL COMMENT 'colored display name',
  muted        TINYINT      NOT NULL DEFAULT 0,
  is_default   TINYINT      NOT NULL DEFAULT 0,
  is_operator  TINYINT      NOT NULL DEFAULT 0,
  is_banned    TINYINT      NOT NULL DEFAULT 0,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE INDEX idx_channel_player (channel_id, player),
  INDEX idx_player (player),
  INDEX idx_active (channel_id, is_banned, muted),
  FOREIGN KEY (channel_id) REFERENCES channels(channel_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE channel membership, bans, and operators';

-- ============================================================================
-- LOCKOUTS TABLE - IP, player, and guest-IP bans
-- ============================================================================

CREATE TABLE IF NOT EXISTS lockouts (
  id           BIGINT       AUTO_INCREMENT PRIMARY KEY,
  lockout_type ENUM('player','ip','guestip') NOT NULL,
  target       VARCHAR(256) NOT NULL,
  reason       TEXT         DEFAULT NULL,
  created_by   BIGINT       NOT NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE INDEX idx_type_target (lockout_type, target)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE lockout entries for IP, player, and guest-IP bans';

-- After starting deMUSE, run @config/seed in-game to populate
-- all config values from the running server into this table.
