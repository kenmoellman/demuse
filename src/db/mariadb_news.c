/* mariadb_news.c - MariaDB-backed news article operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the news article system.
 * News articles have author, date, topic, and body.
 * Read tracking is per-player via the news_read table.
 *
 * Commands:
 *   +news             - List unread articles
 *   +news list=all    - List all articles
 *   +news list=new    - List unread articles (same as +news)
 *   +news read=<#>    - Read article and mark as read
 *   +news post=<topic>/<body> - Post new article (admin)
 *   +news remove=<#>  - Delete article (admin)
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
 * @return 1 on success, 0 on failure
 */
int mariadb_news_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        return 0;
    }

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
 * Article Operations
 * =================================================================== */

/**
 * Post a new news article
 * @param author dbref of poster
 * @param topic  Article topic/title
 * @param body   Article body text
 * @return news_id on success, 0 on failure
 */
long mariadb_news_post(dbref author, const char *topic, const char *body)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char *esc_topic, *esc_body;
    char *query;
    size_t topic_len, body_len, query_len;
    long result = 0;

    if (!conn || !topic || !body) {
        return 0;
    }

    topic_len = strlen(topic);
    body_len = strlen(body);

    SAFE_MALLOC(esc_topic, char, topic_len * 2 + 1);
    SAFE_MALLOC(esc_body, char, body_len * 2 + 1);

    mysql_real_escape_string(conn, esc_topic, topic, (unsigned long)topic_len);
    mysql_real_escape_string(conn, esc_body, body, (unsigned long)body_len);

    query_len = topic_len * 2 + body_len * 2 + 256;
    SAFE_MALLOC(query, char, query_len);

    snprintf(query, query_len,
             "INSERT INTO news (topic, body, author) "
             "VALUES ('%s', '%s', %" DBREF_FMT ")",
             esc_topic, esc_body, author);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_post: %s\n", mysql_error(conn));
    } else {
        result = (long)mysql_insert_id(conn);
    }

    SMART_FREE(query);
    SMART_FREE(esc_body);
    SMART_FREE(esc_topic);

    return result;
}

/**
 * Get a news article by ID
 * @param news_id Article ID
 * @param out     Output: filled with article data (caller frees topic and body)
 * @return 1 if found, 0 if not found or error
 */
int mariadb_news_get(long news_id, NEWS_RESULT *out)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[256];

    if (!conn || !out) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "SELECT news_id, topic, body, author, "
             "UNIX_TIMESTAMP(created_at) "
             "FROM news WHERE news_id = %ld",
             news_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_get: %s\n", mysql_error(conn));
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return 0;
    }

    out->news_id = row[0] ? atol(row[0]) : 0;

    if (row[1]) {
        size_t len = strlen(row[1]);
        SAFE_MALLOC(out->topic, char, len + 1);
        strncpy(out->topic, row[1], len);
        out->topic[len] = '\0';
    } else {
        out->topic = NULL;
    }

    if (row[2]) {
        size_t len = strlen(row[2]);
        SAFE_MALLOC(out->body, char, len + 1);
        strncpy(out->body, row[2], len);
        out->body[len] = '\0';
    } else {
        out->body = NULL;
    }

    out->author = row[3] ? atol(row[3]) : -1;
    out->created_at = row[4] ? atol(row[4]) : 0;

    mysql_free_result(res);
    return 1;
}

/**
 * Delete a news article (cascade removes news_read entries)
 * @param news_id Article ID to delete
 * @return 1 on success, 0 on failure
 */
int mariadb_news_delete(long news_id)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[128];

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "DELETE FROM news WHERE news_id = %ld", news_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_delete: %s\n", mysql_error(conn));
        return 0;
    }

    return mysql_affected_rows(conn) > 0 ? 1 : 0;
}

/* ===================================================================
 * Counting
 * =================================================================== */

/**
 * Count total news articles
 * @return Total count, or -1 on error
 */
long mariadb_news_count(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    long result = -1;

    if (!conn) {
        return -1;
    }

    if (mysql_query(conn, "SELECT COUNT(*) FROM news")) {
        fprintf(stderr, "mariadb_news_count: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return -1;
    }

    row = mysql_fetch_row(res);
    if (row && row[0]) {
        result = atol(row[0]);
    }

    mysql_free_result(res);
    return result;
}

/**
 * Count unread news articles for a player
 * @param player dbref of player
 * @return Unread count, or -1 on error
 */
long mariadb_news_count_unread(dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[256];
    long result = -1;

    if (!conn) {
        return -1;
    }

    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM news n "
             "LEFT JOIN news_read nr ON n.news_id = nr.news_id "
             "AND nr.player_dbref = %" DBREF_FMT " "
             "WHERE nr.news_id IS NULL",
             player);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_count_unread: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return -1;
    }

    row = mysql_fetch_row(res);
    if (row && row[0]) {
        result = atol(row[0]);
    }

    mysql_free_result(res);
    return result;
}

/* ===================================================================
 * Listing
 * =================================================================== */

/**
 * List news articles (all or unread only)
 *
 * Builds a formatted list string. Each line contains:
 *   [news_id] [date] [author_name] [topic] [NEW marker if unread]
 *
 * @param player      dbref of player (for read status)
 * @param unread_only If true, only show unread articles
 * @param list_out    Output: formatted list (caller must SMART_FREE)
 * @param count       Output: number of articles listed
 * @return 1 on success, 0 on failure
 */
int mariadb_news_list(dbref player, int unread_only, char **list_out,
                      int *count)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    char *buf;
    size_t buf_size = 16384;
    size_t buf_used = 0;
    int n = 0;

    if (!conn || !list_out || !count) {
        return 0;
    }

    if (unread_only) {
        snprintf(query, sizeof(query),
                 "SELECT n.news_id, n.topic, n.author, "
                 "UNIX_TIMESTAMP(n.created_at), "
                 "(CASE WHEN nr.news_id IS NULL THEN 1 ELSE 0 END) AS is_unread "
                 "FROM news n "
                 "LEFT JOIN news_read nr ON n.news_id = nr.news_id "
                 "AND nr.player_dbref = %" DBREF_FMT " "
                 "WHERE nr.news_id IS NULL "
                 "ORDER BY n.created_at DESC",
                 player);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT n.news_id, n.topic, n.author, "
                 "UNIX_TIMESTAMP(n.created_at), "
                 "(CASE WHEN nr.news_id IS NULL THEN 1 ELSE 0 END) AS is_unread "
                 "FROM news n "
                 "LEFT JOIN news_read nr ON n.news_id = nr.news_id "
                 "AND nr.player_dbref = %" DBREF_FMT " "
                 "ORDER BY n.created_at DESC",
                 player);
    }

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_list: %s\n", mysql_error(conn));
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    SAFE_MALLOC(buf, char, buf_size);
    buf[0] = '\0';

    while ((row = mysql_fetch_row(res))) {
        char line[256];
        long article_id = row[0] ? atol(row[0]) : 0;
        const char *topic = row[1] ? row[1] : "(untitled)";
        dbref author = row[2] ? atol(row[2]) : -1;
        long ts = row[3] ? atol(row[3]) : 0;
        int is_unread = row[4] ? atoi(row[4]) : 0;

        /* Format date */
        char date_str[20];
        time_t t = (time_t)ts;
        struct tm *tm_info = localtime(&t);
        if (tm_info) {
            strftime(date_str, sizeof(date_str), "%m/%d/%Y", tm_info);
        } else {
            strncpy(date_str, "--/--/----", sizeof(date_str) - 1);
            date_str[sizeof(date_str) - 1] = '\0';
        }

        /* Format author name */
        const char *author_name = "Unknown";
        if (GoodObject(author) && Typeof(author) == TYPE_PLAYER) {
            author_name = db[author].name;
        }

        snprintf(line, sizeof(line), "  %3ld) %s %-15s %s%s\n",
                 article_id, date_str, author_name, topic,
                 is_unread ? " |G!+[NEW]|" : "");

        size_t llen = strlen(line);
        if (buf_used + llen >= buf_size - 1) {
            break;
        }

        strncpy(buf + buf_used, line, buf_size - buf_used - 1);
        buf_used += llen;
        buf[buf_used] = '\0';
        n++;
    }

    mysql_free_result(res);

    *list_out = buf;
    *count = n;
    return 1;
}

/* ===================================================================
 * Read Tracking
 * =================================================================== */

/**
 * Mark a news article as read by a player
 * @param player  dbref of player
 * @param news_id Article ID
 * @return 1 on success, 0 on failure
 */
int mariadb_news_mark_read(dbref player, long news_id)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO news_read (player_dbref, news_id) "
             "VALUES (%" DBREF_FMT ", %ld)",
             player, news_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_news_mark_read: %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ===================================================================
 * Legacy Import
 * =================================================================== */

/**
 * Import news topics from legacy flat text file
 *
 * Parses old-format newstext files with & topic_name markers.
 * Each topic is inserted as a news article with author=root_dbref.
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
                if (mariadb_news_post(import_author, topic, body) > 0) {
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
        if (mariadb_news_post(import_author, topic, body) > 0) {
            count++;
        }
    }

    fclose(fp);
    return count;
}

#endif /* USE_MARIADB */
