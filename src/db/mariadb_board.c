/* mariadb_board.c - MariaDB-backed public board operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Implements SQL operations for the public board posting system.
 * All queries go directly to MariaDB - no in-memory cache.
 *
 * Post numbering is positional (ORDER BY post_id) rather than stored,
 * matching the original linked-list traversal behavior.
 *
 * SAFETY:
 * - All SQL uses mysql_real_escape_string to prevent injection
 * - Message text allocated with SAFE_MALLOC, caller must SMART_FREE
 * - Connection checked before every operation
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
#include "mariadb_mail.h"
#include "mariadb_board.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

/*
 * mariadb_board_init - Create the board table if it doesn't exist
 *
 * Called during server startup after mariadb_init().
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        fprintf(stderr, "MariaDB board: No connection available\n");
        return 0;
    }

    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS board ("
        "  post_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  author       BIGINT       NOT NULL,"
        "  board_room   BIGINT       NOT NULL DEFAULT 0,"
        "  posted_date  BIGINT       NOT NULL,"
        "  flags        INT          NOT NULL DEFAULT 0,"
        "  message      TEXT         NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_board_room (board_room),"
        "  INDEX idx_author     (author)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        "  COMMENT='deMUSE public board posts'";

    if (mysql_query(conn, create_sql)) {
        fprintf(stderr, "MariaDB board: Failed to create table: %s\n",
                mysql_error(conn));
        return 0;
    }

    fprintf(stderr, "MariaDB board: Table ready\n");
    return 1;
}

/* ============================================================================
 * CORE OPERATIONS
 * ============================================================================ */

/*
 * mariadb_board_post - Insert a new board post
 */
int mariadb_board_post(dbref author, dbref board_room, const char *message,
                       int flags)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char *escaped_msg;
    char query[4096];
    size_t msg_len;

    if (!conn || !message) {
        return 0;
    }

    msg_len = strlen(message);
    SAFE_MALLOC(escaped_msg, char, msg_len * 2 + 1);
    if (!escaped_msg) {
        return 0;
    }

    mysql_real_escape_string(conn, escaped_msg, message,
                             (unsigned long)msg_len);

    snprintf(query, sizeof(query),
             "INSERT INTO board (author, board_room, posted_date, flags, message) "
             "VALUES (%" DBREF_FMT ", %" DBREF_FMT ", %ld, %d, '%s')",
             author, board_room, (long)now, flags, escaped_msg);

    SMART_FREE(escaped_msg);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: Post failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    return 1;
}

/*
 * mariadb_board_get_by_position - Get a board post by position number
 *
 * Position is 1-based, ordered by post_id (oldest first).
 */
int mariadb_board_get_by_position(dbref board_room, long postnum,
                                  int include_deleted, MAIL_RESULT *out)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[512];

    if (!conn || !out || postnum <= 0) {
        return 0;
    }

    memset(out, 0, sizeof(MAIL_RESULT));

    if (include_deleted) {
        snprintf(query, sizeof(query),
                 "SELECT post_id, author, board_room, posted_date, flags, message "
                 "FROM board WHERE board_room = %" DBREF_FMT " "
                 "ORDER BY post_id LIMIT 1 OFFSET %ld",
                 board_room, postnum - 1);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT post_id, author, board_room, posted_date, flags, message "
                 "FROM board WHERE board_room = %" DBREF_FMT " "
                 "AND NOT (flags & 1) "
                 "ORDER BY post_id LIMIT 1 OFFSET %ld",
                 board_room, postnum - 1);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: get_by_position failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return 0;
    }

    out->id = strtol(row[0], NULL, 10);
    out->sender = strtol(row[1], NULL, 10);      /* author */
    out->recipient = strtol(row[2], NULL, 10);    /* board_room */
    out->sent_date = strtol(row[3], NULL, 10);
    out->flags = (int)strtol(row[4], NULL, 10);

    /* Allocate message copy - caller must SMART_FREE */
    if (row[5]) {
        size_t len = strlen(row[5]) + 1;
        SAFE_MALLOC(out->message, char, len);
        if (out->message) {
            strncpy(out->message, row[5], len);
            out->message[len - 1] = '\0';
        }
    } else {
        out->message = NULL;
    }

    mysql_free_result(result);
    return 1;
}

/*
 * mariadb_board_update_flags - Update flags for a specific post by ID
 */
int mariadb_board_update_flags(long post_id, int flags)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE board SET flags = %d WHERE post_id = %ld",
             flags, post_id);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: update_flags failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    return 1;
}

/*
 * mariadb_board_delete_range - Mark/unmark posts as deleted by position
 *
 * Iterates through posts at positions [start..end] and sets flags.
 * If end == 0, only the post at position start is affected.
 * Permission check: only the author or board admin can delete.
 */
long mariadb_board_delete_range(dbref board_room, long start, long end,
                                int undelete, dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[512];
    long count = 0;
    long pos;
    int target_flag;

    if (!conn) {
        return 0;
    }

    if (start <= 0) {
        return 0;
    }

    /* If end is 0, treat as single post */
    if (end == 0) {
        end = start;
    }

    target_flag = undelete ? MF_READ : MF_DELETED;

    /* Get all posts for this board room, ordered by post_id */
    snprintf(query, sizeof(query),
             "SELECT post_id, author, flags FROM board "
             "WHERE board_room = %" DBREF_FMT " ORDER BY post_id",
             board_room);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: delete_range query failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    pos = 1;
    while ((row = mysql_fetch_row(result)) != NULL) {
        if (pos >= start && pos <= end) {
            long post_id = strtol(row[0], NULL, 10);
            dbref author = strtol(row[1], NULL, 10);

            /* Check permissions: author or board admin */
            if (author == player || power(player, POW_BOARD)) {
                char update[256];
                snprintf(update, sizeof(update),
                         "UPDATE board SET flags = %d WHERE post_id = %ld",
                         target_flag, post_id);
                if (!mysql_query(conn, update)) {
                    count++;
                }
            }
        }
        pos++;
    }

    mysql_free_result(result);
    return count;
}

/*
 * mariadb_board_purge - Permanently remove deleted posts
 *
 * Only removes posts the player has permission to purge:
 * - Posts they authored
 * - All deleted posts if they have POW_BOARD
 */
long mariadb_board_purge(dbref board_room, dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[512];
    long affected;

    if (!conn) {
        return 0;
    }

    if (power(player, POW_BOARD)) {
        /* Board admin: purge all deleted posts */
        snprintf(query, sizeof(query),
                 "DELETE FROM board WHERE board_room = %" DBREF_FMT " "
                 "AND (flags & 1)",
                 board_room);
    } else {
        /* Regular user: purge only own deleted posts */
        snprintf(query, sizeof(query),
                 "DELETE FROM board WHERE board_room = %" DBREF_FMT " "
                 "AND author = %" DBREF_FMT " AND (flags & 1)",
                 board_room, player);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: purge failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    affected = (long)mysql_affected_rows(conn);
    return affected;
}

/*
 * mariadb_board_list - Iterate over posts with callback
 *
 * Calls the callback for each post on the board, passing the
 * 1-based position number and a MAIL_RESULT struct. The callback
 * should NOT free the message pointer - it's freed after the callback.
 */
long mariadb_board_list(dbref board_room, int include_deleted,
                        mail_list_callback callback, void *userdata)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[512];
    long count = 0;
    long pos = 1;

    if (!conn || !callback) {
        return 0;
    }

    if (include_deleted) {
        snprintf(query, sizeof(query),
                 "SELECT post_id, author, board_room, posted_date, flags, message "
                 "FROM board WHERE board_room = %" DBREF_FMT " "
                 "ORDER BY post_id",
                 board_room);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT post_id, author, board_room, posted_date, flags, message "
                 "FROM board WHERE board_room = %" DBREF_FMT " "
                 "AND NOT (flags & 1) "
                 "ORDER BY post_id",
                 board_room);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB board: list query failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    while ((row = mysql_fetch_row(result)) != NULL) {
        MAIL_RESULT mr;
        mr.id = strtol(row[0], NULL, 10);
        mr.sender = strtol(row[1], NULL, 10);     /* author */
        mr.recipient = strtol(row[2], NULL, 10);   /* board_room */
        mr.sent_date = strtol(row[3], NULL, 10);
        mr.flags = (int)strtol(row[4], NULL, 10);
        mr.message = row[5];  /* Temporary - valid until mysql_free_result */

        callback(&mr, pos, userdata);
        pos++;
        count++;
    }

    mysql_free_result(result);
    return count;
}

/* ============================================================================
 * COUNT OPERATIONS
 * ============================================================================ */

/*
 * mariadb_board_count - Count posts on a board
 */
long mariadb_board_count(dbref board_room, int include_deleted)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long count;

    if (!conn) {
        return -1;
    }

    if (include_deleted) {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM board WHERE board_room = %" DBREF_FMT,
                 board_room);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM board WHERE board_room = %" DBREF_FMT " "
                 "AND NOT (flags & 1)",
                 board_room);
    }

    if (mysql_query(conn, query)) {
        return -1;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return -1;
    }

    row = mysql_fetch_row(result);
    count = row ? strtol(row[0], NULL, 10) : -1;
    mysql_free_result(result);
    return count;
}

/* ============================================================================
 * BULK OPERATIONS
 * ============================================================================ */

/*
 * mariadb_board_remove_all - Truncate the entire board table
 */
int mariadb_board_remove_all(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        return 0;
    }

    if (mysql_query(conn, "TRUNCATE TABLE board")) {
        log_error(tprintf("MariaDB board: truncate failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    log_important("MariaDB board: All posts truncated");
    return 1;
}

/*
 * mariadb_board_stats - Get aggregate statistics for @info mail
 */
int mariadb_board_stats(long *total_out, long *deleted_out,
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
        "FROM board";

    if (mysql_query(conn, stats_sql)) {
        log_error(tprintf("MariaDB board: stats query failed: %s",
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

#endif /* USE_MARIADB */
