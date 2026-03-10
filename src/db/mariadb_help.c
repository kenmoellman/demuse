/* mariadb_help.c - MariaDB-backed help topic operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the online help system.
 * Help topics are stored with a two-level hierarchy (command/subcommand).
 *
 * Lookup behavior:
 *   "help +channel"       -> command="+channel", subcommand=""
 *   "help +channel join"  -> command="+channel", subcommand="join"
 *   "help"                -> command="help", subcommand=""
 *
 * Legacy import parses old-format helptext files (& topic markers) into
 * flat command="" entries for one-time migration.
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
#include "mariadb_help.h"

#include <stdio.h>
#include <string.h>

/* ===================================================================
 * Table Initialization
 * =================================================================== */

/**
 * Create the help_topics table if it doesn't exist
 * @return 1 on success, 0 on failure
 */
int mariadb_help_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        return 0;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS help_topics ("
        "  help_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  command      VARCHAR(30)  NOT NULL,"
        "  subcommand   VARCHAR(30)  NOT NULL DEFAULT '',"
        "  body         TEXT         NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP"
        "                            ON UPDATE CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_cmd_sub (command, subcommand)"
        ") ENGINE=InnoDB"
        "  DEFAULT CHARSET=utf8mb4"
        "  COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "mariadb_help_init: %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ===================================================================
 * Topic Lookup
 * =================================================================== */

/**
 * Look up a help topic by command and subcommand
 * @param command    Main command name
 * @param subcommand Subcommand name or "" for overview
 * @param body_out   Output: body text (caller must SMART_FREE)
 * @return 1 if found, 0 if not found or error
 */
int mariadb_help_get(const char *command, const char *subcommand,
                     char **body_out)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    char esc_cmd[64];
    char esc_sub[64];

    if (!conn || !command || !body_out) {
        return 0;
    }

    if (!subcommand) {
        subcommand = "";
    }

    mysql_real_escape_string(conn, esc_cmd, command, (unsigned long)strlen(command));
    mysql_real_escape_string(conn, esc_sub, subcommand, (unsigned long)strlen(subcommand));

    snprintf(query, sizeof(query),
             "SELECT body FROM help_topics "
             "WHERE command = '%s' AND subcommand = '%s'",
             esc_cmd, esc_sub);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_help_get: %s\n", mysql_error(conn));
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        mysql_free_result(res);
        return 0;
    }

    size_t len = strlen(row[0]);
    SAFE_MALLOC(*body_out, char, len + 1);
    strncpy(*body_out, row[0], len);
    (*body_out)[len] = '\0';

    mysql_free_result(res);
    return 1;
}

/* ===================================================================
 * Topic Modification
 * =================================================================== */

/**
 * Insert or update a help topic
 * @param command    Main command name
 * @param subcommand Subcommand name or "" for overview
 * @param body       Help text body
 * @return 1 on success, 0 on failure
 */
int mariadb_help_set(const char *command, const char *subcommand,
                     const char *body)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char *esc_cmd, *esc_sub, *esc_body;
    char *query;
    size_t cmd_len, sub_len, body_len, query_len;
    int result;

    if (!conn || !command || !body) {
        return 0;
    }

    if (!subcommand) {
        subcommand = "";
    }

    cmd_len = strlen(command);
    sub_len = strlen(subcommand);
    body_len = strlen(body);

    SAFE_MALLOC(esc_cmd, char, cmd_len * 2 + 1);
    SAFE_MALLOC(esc_sub, char, sub_len * 2 + 1);
    SAFE_MALLOC(esc_body, char, body_len * 2 + 1);

    mysql_real_escape_string(conn, esc_cmd, command, (unsigned long)cmd_len);
    mysql_real_escape_string(conn, esc_sub, subcommand, (unsigned long)sub_len);
    mysql_real_escape_string(conn, esc_body, body, (unsigned long)body_len);

    query_len = body_len * 2 + 256;
    SAFE_MALLOC(query, char, query_len);

    snprintf(query, query_len,
             "INSERT INTO help_topics (command, subcommand, body) "
             "VALUES ('%s', '%s', '%s') "
             "ON DUPLICATE KEY UPDATE body = VALUES(body)",
             esc_cmd, esc_sub, esc_body);

    result = mysql_query(conn, query) == 0 ? 1 : 0;

    if (!result) {
        fprintf(stderr, "mariadb_help_set: %s\n", mysql_error(conn));
    }

    SMART_FREE(query);
    SMART_FREE(esc_body);
    SMART_FREE(esc_sub);
    SMART_FREE(esc_cmd);

    return result;
}

/**
 * Remove a help topic
 * @param command    Main command name
 * @param subcommand Subcommand name or "" for overview
 * @return 1 on success, 0 on failure
 */
int mariadb_help_delete(const char *command, const char *subcommand)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[512];
    char esc_cmd[64];
    char esc_sub[64];

    if (!conn || !command) {
        return 0;
    }

    if (!subcommand) {
        subcommand = "";
    }

    mysql_real_escape_string(conn, esc_cmd, command, (unsigned long)strlen(command));
    mysql_real_escape_string(conn, esc_sub, subcommand, (unsigned long)strlen(subcommand));

    snprintf(query, sizeof(query),
             "DELETE FROM help_topics "
             "WHERE command = '%s' AND subcommand = '%s'",
             esc_cmd, esc_sub);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_help_delete: %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ===================================================================
 * Subcommand Listing
 * =================================================================== */

/**
 * Get list of subcommands for a command
 * @param command  Main command name
 * @param list_out Output: comma-separated list (caller must SMART_FREE)
 * @param count    Output: number of subcommands found
 * @return 1 on success, 0 on failure or no subcommands
 */
int mariadb_help_list_subcommands(const char *command, char **list_out,
                                  int *count)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    char esc_cmd[64];
    char buf[4096];
    char *bp = buf;
    int n = 0;
    int first = 1;

    if (!conn || !command || !list_out || !count) {
        return 0;
    }

    mysql_real_escape_string(conn, esc_cmd, command, (unsigned long)strlen(command));

    snprintf(query, sizeof(query),
             "SELECT subcommand FROM help_topics "
             "WHERE command = '%s' AND subcommand != '' "
             "ORDER BY subcommand",
             esc_cmd);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_help_list_subcommands: %s\n", mysql_error(conn));
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    buf[0] = '\0';

    while ((row = mysql_fetch_row(res))) {
        if (!row[0]) continue;

        size_t needed = strlen(row[0]) + (first ? 0 : 2);
        if ((size_t)(bp - buf) + needed >= sizeof(buf) - 1) {
            break;  /* Buffer full */
        }

        if (!first) {
            *bp++ = ',';
            *bp++ = ' ';
        } else {
            first = 0;
        }

        size_t slen = strlen(row[0]);
        strncpy(bp, row[0], sizeof(buf) - (size_t)(bp - buf) - 1);
        bp += slen;
        n++;
    }

    *bp = '\0';
    mysql_free_result(res);

    *count = n;

    if (n == 0) {
        return 0;
    }

    size_t len = strlen(buf);
    SAFE_MALLOC(*list_out, char, len + 1);
    memcpy(*list_out, buf, len);
    (*list_out)[len] = '\0';

    return 1;
}

/* ===================================================================
 * Topic Search
 * =================================================================== */

/**
 * Search help topics by pattern (substring match)
 * @param pattern  Search pattern
 * @param list_out Output: formatted list of matches (caller must SMART_FREE)
 * @param count    Output: number of matches found
 * @return 1 on success, 0 on failure
 */
int mariadb_help_search(const char *pattern, char **list_out, int *count)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    char esc_pat[128];
    char buf[8192];
    char *bp = buf;
    int n = 0;

    if (!conn || !pattern || !list_out || !count) {
        return 0;
    }

    mysql_real_escape_string(conn, esc_pat, pattern, (unsigned long)strlen(pattern));

    snprintf(query, sizeof(query),
             "SELECT command, subcommand FROM help_topics "
             "WHERE command LIKE '%%%s%%' OR subcommand LIKE '%%%s%%' "
             "ORDER BY command, subcommand",
             esc_pat, esc_pat);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "mariadb_help_search: %s\n", mysql_error(conn));
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    buf[0] = '\0';

    while ((row = mysql_fetch_row(res))) {
        char line[128];

        if (row[1] && row[1][0]) {
            snprintf(line, sizeof(line), "  %s %s\n", row[0], row[1]);
        } else {
            snprintf(line, sizeof(line), "  %s\n", row[0]);
        }

        size_t llen = strlen(line);
        if ((size_t)(bp - buf) + llen >= sizeof(buf) - 1) {
            break;
        }

        strncpy(bp, line, sizeof(buf) - (size_t)(bp - buf) - 1);
        bp += llen;
        n++;
    }

    *bp = '\0';
    mysql_free_result(res);

    *count = n;

    size_t len = strlen(buf);
    SAFE_MALLOC(*list_out, char, len + 1);
    memcpy(*list_out, buf, len);
    (*list_out)[len] = '\0';

    return 1;
}

/**
 * Count total help topics
 * @return Total count, or -1 on error
 */
long mariadb_help_count(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *res;
    MYSQL_ROW row;
    long result = -1;

    if (!conn) {
        return -1;
    }

    if (mysql_query(conn, "SELECT COUNT(*) FROM help_topics")) {
        fprintf(stderr, "mariadb_help_count: %s\n", mysql_error(conn));
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
 * Legacy Import
 * =================================================================== */

/**
 * Import help topics from legacy flat text file
 *
 * Parses old-format helptext files with & topic_name markers.
 * Each topic is inserted as command=topic_name, subcommand="".
 *
 * @param filename Path to the legacy helptext file
 * @return Number of topics imported, or -1 on error
 */
int mariadb_help_import_legacy(const char *filename)
{
    FILE *fp;
    char line[1024];
    char topic[64];
    char body[65536];
    int body_len;
    int count = 0;
    int in_topic = 0;

    if (!filename) {
        return -1;
    }

    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "mariadb_help_import_legacy: cannot open %s\n", filename);
        return -1;
    }

    topic[0] = '\0';
    body[0] = '\0';
    body_len = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '&') {
            /* Save previous topic if we have one */
            if (in_topic && topic[0] && body_len > 0) {
                /* Trim trailing newlines */
                while (body_len > 0 && (body[body_len - 1] == '\n' ||
                       body[body_len - 1] == '\r')) {
                    body[--body_len] = '\0';
                }
                if (mariadb_help_set(topic, "", body)) {
                    count++;
                }
            }

            /* Parse new topic name */
            char *p = &line[1];
            while (*p == ' ' || *p == '\t') p++;

            /* Remove trailing newline/whitespace */
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
            /* Append to body */
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
        if (mariadb_help_set(topic, "", body)) {
            count++;
        }
    }

    fclose(fp);
    return count;
}

#endif /* USE_MARIADB */
