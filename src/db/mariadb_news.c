/* mariadb_news.c - News article operations (wrappers around board table)
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * News articles are stored in the board table with board_room = NEWS_ROOM (-2).
 * All functions are thin wrappers around mariadb_board_* functions.
 *
 * This eliminates code duplication between the board and news systems.
 * The only behavioral difference is the posting permission model:
 *   +board = anyone unless banned
 *   +news  = admins only (Wizard or POW_ANNOUNCE)
 *
 * Legacy import (mariadb_news_import_legacy) is kept for one-time migration
 * from old flat-file newstext files.
 */

#ifdef USE_MARIADB

/* Suppress conversion warnings from MariaDB headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <mysql.h>
#pragma GCC diagnostic pop

#include "config.h"
#include "externs.h"
#include "mariadb.h"
#include "mariadb_news.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ===================================================================
 * Table Initialization
 * =================================================================== */

/**
 * Create the news and news_read tables if they don't exist
 *
 * Kept for backward compatibility during migration period.
 * The board table (used by news) is initialized by mariadb_board_init().
 *
 * @return 1 on success, 0 on failure
 */
int mariadb_news_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        return 0;
    }

    /* Legacy tables kept for migration/rollback — CREATE IF NOT EXISTS
     * ensures they exist for the migration SQL to read from */
    const char *sql_news =
        "CREATE TABLE IF NOT EXISTS news ("
        "  news_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  topic        VARCHAR(80)  NOT NULL,"
        "  body         TEXT         NOT NULL,"
        "  author       BIGINT       NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_created (created_at)"
        ") ENGINE=InnoDB"
        "  DEFAULT CHARSET=utf8mb4"
        "  COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, sql_news)) {
        fprintf(stderr, "mariadb_news_init (news): %s\n", mysql_error(conn));
        return 0;
    }

    const char *sql_read =
        "CREATE TABLE IF NOT EXISTS news_read ("
        "  player_dbref BIGINT       NOT NULL,"
        "  news_id      BIGINT       NOT NULL,"
        "  read_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (player_dbref, news_id),"
        "  FOREIGN KEY (news_id) REFERENCES news(news_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB"
        "  DEFAULT CHARSET=utf8mb4"
        "  COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, sql_read)) {
        fprintf(stderr, "mariadb_news_init (news_read): %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ===================================================================
 * Wrapper Functions — delegate to mariadb_board_* with NEWS_ROOM
 * =================================================================== */

/**
 * Post a new news article (stored in board table with NEWS_ROOM)
 */
int mariadb_news_post_article(dbref author, const char *topic, const char *body)
{
    return mariadb_board_post(author, NEWS_ROOM, topic, body, 0);
}

/**
 * Get a news article by position number
 */
int mariadb_news_get_by_position(long postnum, int include_deleted,
                                  MAIL_RESULT *out)
{
    return mariadb_board_get_by_position(NEWS_ROOM, postnum,
                                         include_deleted, out);
}

/**
 * List news articles with per-player read tracking
 */
long mariadb_news_list_for_player(dbref player, int include_deleted,
                                   int unread_only, mail_list_callback callback,
                                   void *userdata)
{
    return mariadb_board_list_for_player(NEWS_ROOM, player, include_deleted,
                                         unread_only, callback, userdata);
}

/**
 * Mark/unmark news articles as deleted by position
 */
long mariadb_news_delete_range(long start, long end, int undelete, dbref player)
{
    return mariadb_board_delete_range(NEWS_ROOM, start, end, undelete, player);
}

/**
 * Permanently remove deleted news articles
 */
long mariadb_news_purge(dbref player)
{
    return mariadb_board_purge(NEWS_ROOM, player);
}

/**
 * Count total news articles
 */
long mariadb_news_count(int include_deleted)
{
    return mariadb_board_count(NEWS_ROOM, include_deleted);
}

/**
 * Count unread news articles for a player
 */
long mariadb_news_count_unread(dbref player)
{
    return mariadb_board_count_unread(player, NEWS_ROOM);
}

/**
 * Mark a news article as read by a player
 */
int mariadb_news_mark_read(dbref player, long post_id)
{
    return mariadb_board_mark_read(player, post_id);
}

/**
 * Get aggregate statistics for news articles
 */
int mariadb_news_stats(long *total_out, long *deleted_out,
                        long *text_size_out)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;

    if (!conn) {
        return 0;
    }

    const char *stats_sql =
        "SELECT "
        "  COUNT(*), "
        "  SUM(CASE WHEN flags & 1 THEN 1 ELSE 0 END), "
        "  COALESCE(SUM(LENGTH(message)), 0) "
        "FROM board WHERE board_room = -2";

    if (mysql_query(conn, stats_sql)) {
        log_error(tprintf("MariaDB news: stats query failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    row = mysql_fetch_row(result);
    if (row) {
        if (total_out) *total_out = strtol(row[0] ? row[0] : "0", NULL, 10);
        if (deleted_out) *deleted_out = strtol(row[1] ? row[1] : "0", NULL, 10);
        if (text_size_out) *text_size_out = strtol(row[2] ? row[2] : "0", NULL, 10);
    }

    mysql_free_result(result);
    return 1;
}

/* ===================================================================
 * Legacy Import
 * =================================================================== */

/**
 * Import news topics from legacy flat text file
 *
 * Parses old-format newstext files with & topic_name markers.
 * Each topic is inserted as a news article in the board table
 * with board_room = NEWS_ROOM.
 *
 * @param filename Path to the legacy newstext file
 * @return Number of articles imported, or -1 on error
 */
int mariadb_news_import_legacy(const char *filename)
{
    FILE *fp;
    char line[1024];
    char topic[80];
    char body[65536];
    int body_len;
    int count = 0;
    int in_topic = 0;
    dbref import_author = 1;  /* Player #1 (root/admin) */

    if (!filename) {
        return -1;
    }

    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "mariadb_news_import_legacy: cannot open %s\n", filename);
        return -1;
    }

    topic[0] = '\0';
    body[0] = '\0';
    body_len = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '&') {
            /* Save previous topic */
            if (in_topic && topic[0] && body_len > 0) {
                while (body_len > 0 && (body[body_len - 1] == '\n' ||
                       body[body_len - 1] == '\r')) {
                    body[--body_len] = '\0';
                }
                if (mariadb_news_post_article(import_author, topic, body)) {
                    count++;
                }
            }

            /* Parse new topic name */
            char *p = &line[1];
            while (*p == ' ' || *p == '\t') p++;

            char *end = p + strlen(p) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end-- = '\0';
            }

            strncpy(topic, p, sizeof(topic) - 1);
            topic[sizeof(topic) - 1] = '\0';
            body[0] = '\0';
            body_len = 0;
            in_topic = 1;
        } else if (in_topic) {
            size_t llen = strlen(line);
            if (body_len + (int)llen < (int)sizeof(body) - 1) {
                strncpy(body + body_len, line, sizeof(body) - (size_t)body_len - 1);
                body_len += (int)llen;
                body[body_len] = '\0';
            }
        }
    }

    /* Save final topic */
    if (in_topic && topic[0] && body_len > 0) {
        while (body_len > 0 && (body[body_len - 1] == '\n' ||
               body[body_len - 1] == '\r')) {
            body[--body_len] = '\0';
        }
        if (mariadb_news_post_article(import_author, topic, body)) {
            count++;
        }
    }

    fclose(fp);
    return count;
}

#endif /* USE_MARIADB */
