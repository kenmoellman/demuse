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
  subject      VARCHAR(80)  NOT NULL DEFAULT '',
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
  subject      VARCHAR(80)  NOT NULL DEFAULT '',
  message      TEXT         NOT NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_board_room (board_room),
  INDEX idx_author     (author)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE public board posts';

-- ============================================================================
-- BOARD_BANS TABLE - Players banned from posting to the board
-- ============================================================================

CREATE TABLE IF NOT EXISTS board_bans (
  player       BIGINT       NOT NULL COMMENT 'banned player dbref',
  banned_by    BIGINT       NOT NULL COMMENT 'admin who issued ban',
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE INDEX idx_player (player)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE board posting bans';

-- ============================================================================
-- BOARD_READ TABLE - Tracks which board posts each player has read
-- ============================================================================

CREATE TABLE IF NOT EXISTS board_read (
  player_dbref BIGINT       NOT NULL,
  post_id      BIGINT       NOT NULL,
  read_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (player_dbref, post_id),
  FOREIGN KEY (post_id) REFERENCES board(post_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE board read tracking per player';

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

-- ============================================================================
-- HELP_TOPICS TABLE - Online help system
-- ============================================================================

CREATE TABLE IF NOT EXISTS help_topics (
  help_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,
  command      VARCHAR(30)  NOT NULL COMMENT 'command name (e.g. +channel, look, @create)',
  subcommand   VARCHAR(30)  NOT NULL DEFAULT '' COMMENT 'subcommand (e.g. join, leave) or empty for overview',
  body         TEXT         NOT NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
                            ON UPDATE CURRENT_TIMESTAMP,
  UNIQUE INDEX idx_cmd_sub (command, subcommand)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='deMUSE online help topics';

-- ============================================================================
-- NEWS TABLE - DEPRECATED: News articles now stored in board table
-- News uses board_room = -2 (NEWS_ROOM). Read tracking uses board_read.
-- These tables are kept for migration rollback safety.
-- Run config/migrate_news_to_board.sql to migrate existing data.
-- ============================================================================

CREATE TABLE IF NOT EXISTS news (
  news_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,
  topic        VARCHAR(80)  NOT NULL,
  body         TEXT         NOT NULL,
  author       BIGINT       NOT NULL COMMENT 'dbref of poster',
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_created (created_at)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='DEPRECATED - deMUSE news articles (migrated to board table)';

-- ============================================================================
-- NEWS_READ TABLE - DEPRECATED: Now uses board_read with NEWS_ROOM posts
-- ============================================================================

CREATE TABLE IF NOT EXISTS news_read (
  player_dbref BIGINT       NOT NULL,
  news_id      BIGINT       NOT NULL,
  read_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (player_dbref, news_id),
  FOREIGN KEY (news_id) REFERENCES news(news_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='DEPRECATED - deMUSE news read tracking (migrated to board_read)';

-- ============================================================================
-- ACCOUNT AUTHENTICATION TABLES
-- ============================================================================

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

CREATE TABLE IF NOT EXISTS password_reset_tokens (
  token_id    BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  account_id  BIGINT UNSIGNED NOT NULL,
  token       VARCHAR(64)  NOT NULL UNIQUE,
  expires_at  DATETIME     NOT NULL COMMENT '1-hour expiry',
  created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='Password reset tokens requested via web interface';

-- ============================================================================
-- SEED DATA
-- ============================================================================
-- After creating tables, load default data:
--   mysql -u demuse -p demuse < config/defaults.sql
--   mysql -u demuse -p demuse < config/help_seed.sql
--
-- Or use @help/import and @news/import in-game to import from legacy files.
