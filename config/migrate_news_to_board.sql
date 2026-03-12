-- migrate_news_to_board.sql - Migrate news articles into the board table
--
-- News articles are stored in the board table with board_room = -2 (NEWS_ROOM).
-- This allows news to share all board infrastructure: soft delete, undelete,
-- purge, per-player read tracking via board_read.
--
-- Usage:
--   mysql -u demuse -p demuse < config/migrate_news_to_board.sql
--
-- Safe to run multiple times — uses INSERT IGNORE to skip duplicates.
-- Does NOT drop old tables (rollback safety).

USE demuse;

-- ============================================================================
-- MIGRATE NEWS ARTICLES → BOARD TABLE
-- ============================================================================
-- board_room = -2 identifies these as news articles.
-- posted_date = UNIX_TIMESTAMP(created_at) to match board's date format.
-- flags = 0 (active, not deleted).

INSERT IGNORE INTO board (author, board_room, posted_date, flags, subject, message, created_at)
SELECT author, -2, UNIX_TIMESTAMP(created_at), 0, topic, body, created_at
FROM news
ORDER BY news_id;

-- ============================================================================
-- MIGRATE NEWS READ TRACKING → BOARD_READ TABLE
-- ============================================================================
-- Join on topic + author + created_at to find the matching board post_id.

INSERT IGNORE INTO board_read (player_dbref, post_id, read_at)
SELECT nr.player_dbref, b.post_id, nr.read_at
FROM news_read nr
JOIN news n ON nr.news_id = n.news_id
JOIN board b ON b.board_room = -2
  AND b.subject = n.topic
  AND b.author = n.author
  AND b.created_at = n.created_at;

-- ============================================================================
-- DEPRECATION NOTICE
-- ============================================================================
-- The news and news_read tables are no longer used by the application.
-- They are kept for rollback safety. After verifying the migration:
--
--   DROP TABLE IF EXISTS news_read;
--   DROP TABLE IF EXISTS news;
--
-- The setup_mariadb.sql file retains the CREATE TABLE statements
-- marked as deprecated for reference.
