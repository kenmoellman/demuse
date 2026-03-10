/* mariadb_mail.c - MariaDB-backed private mail operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Implements SQL operations for the private player-to-player mail system.
 * All queries go directly to MariaDB - no in-memory cache.
 *
 * Message numbering is positional (ORDER BY mail_id) rather than stored,
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

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

/*
 * mariadb_mail_init - Create the mail table if it doesn't exist
 *
 * Called during server startup after mariadb_init().
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        fprintf(stderr, "MariaDB mail: No connection available\n");
        return 0;
    }

    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS mail ("
        "  mail_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  sender       BIGINT       NOT NULL,"
        "  recipient    BIGINT       NOT NULL,"
        "  sent_date    BIGINT       NOT NULL,"
        "  flags        INT          NOT NULL DEFAULT 0,"
        "  message      TEXT         NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_recipient       (recipient),"
        "  INDEX idx_sender          (sender),"
        "  INDEX idx_recipient_flags (recipient, flags)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        "  COMMENT='deMUSE private player-to-player mail'";

    if (mysql_query(conn, create_sql)) {
        fprintf(stderr, "MariaDB mail: Failed to create table: %s\n",
                mysql_error(conn));
        return 0;
    }

    fprintf(stderr, "MariaDB mail: Table ready\n");
    return 1;
}

/* ============================================================================
 * CORE OPERATIONS
 * ============================================================================ */

/*
 * mariadb_mail_send - Insert a new mail message
 */
int mariadb_mail_send(dbref sender, dbref recipient, const char *subject,
                      const char *message, int flags)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char *escaped_msg, *escaped_sub;
    char *query;
    size_t msg_len, sub_len, query_len;
    const char *safe_subject = (subject && *subject) ? subject : "";

    if (!conn || !message) {
        return 0;
    }

    msg_len = strlen(message);
    sub_len = strlen(safe_subject);

    SAFE_MALLOC(escaped_msg, char, msg_len * 2 + 1);
    if (!escaped_msg) return 0;

    SAFE_MALLOC(escaped_sub, char, sub_len * 2 + 1);
    if (!escaped_sub) { SMART_FREE(escaped_msg); return 0; }

    mysql_real_escape_string(conn, escaped_msg, message,
                             (unsigned long)msg_len);
    mysql_real_escape_string(conn, escaped_sub, safe_subject,
                             (unsigned long)sub_len);

    query_len = msg_len * 2 + sub_len * 2 + 256;
    SAFE_MALLOC(query, char, query_len);
    if (!query) { SMART_FREE(escaped_sub); SMART_FREE(escaped_msg); return 0; }

    snprintf(query, query_len,
             "INSERT INTO mail (sender, recipient, sent_date, flags, subject, message) "
             "VALUES (%" DBREF_FMT ", %" DBREF_FMT ", %ld, %d, '%s', '%s')",
             sender, recipient, (long)now, flags, escaped_sub, escaped_msg);

    SMART_FREE(escaped_sub);
    SMART_FREE(escaped_msg);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: Send failed: %s",
                          mysql_error(conn)));
        SMART_FREE(query);
        return 0;
    }

    SMART_FREE(query);
    return 1;
}

/*
 * mariadb_mail_get_by_position - Get a mail message by position number
 *
 * Position is 1-based, ordered by mail_id (oldest first).
 */
int mariadb_mail_get_by_position(dbref recipient, long msgnum,
                                 int include_deleted, MAIL_RESULT *out)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[512];

    if (!conn || !out || msgnum <= 0) {
        return 0;
    }

    memset(out, 0, sizeof(MAIL_RESULT));

    if (include_deleted) {
        snprintf(query, sizeof(query),
                 "SELECT mail_id, sender, recipient, sent_date, flags, "
                 "message, subject "
                 "FROM mail WHERE recipient = %" DBREF_FMT " "
                 "ORDER BY mail_id LIMIT 1 OFFSET %ld",
                 recipient, msgnum - 1);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT mail_id, sender, recipient, sent_date, flags, "
                 "message, subject "
                 "FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND NOT (flags & 1) "
                 "ORDER BY mail_id LIMIT 1 OFFSET %ld",
                 recipient, msgnum - 1);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: get_by_position failed: %s",
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
    out->sender = strtol(row[1], NULL, 10);
    out->recipient = strtol(row[2], NULL, 10);
    out->sent_date = strtol(row[3], NULL, 10);
    out->flags = (int)strtol(row[4], NULL, 10);

    /* Allocate message copy - caller must SMART_FREE */
    if (row[5]) {
        size_t len = strlen(row[5]) + 1;
        SAFE_MALLOC(out->message, char, len);
        if (out->message) {
            memcpy(out->message, row[5], len);
        }
    } else {
        out->message = NULL;
    }

    /* Allocate subject copy - caller must SMART_FREE */
    if (row[6] && row[6][0]) {
        size_t len = strlen(row[6]) + 1;
        SAFE_MALLOC(out->subject, char, len);
        if (out->subject) {
            memcpy(out->subject, row[6], len);
        }
    } else {
        out->subject = NULL;
    }

    mysql_free_result(result);
    return 1;
}

/*
 * mariadb_mail_update_flags - Update flags for a specific message by ID
 */
int mariadb_mail_update_flags(long mail_id, int flags)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE mail SET flags = %d WHERE mail_id = %ld",
             flags, mail_id);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: update_flags failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    return 1;
}

/*
 * mariadb_mail_delete_range - Mark/unmark messages as deleted by position
 *
 * Iterates through messages at positions [start..end] and sets flags.
 * If end == 0, only the message at position start is affected.
 * Permission check: only the mailbox owner or the message sender can delete.
 */
long mariadb_mail_delete_range(dbref recipient, long start, long end,
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

    /* If end is 0, treat as single message */
    if (end == 0) {
        end = start;
    }

    target_flag = undelete ? MF_READ : MF_DELETED;

    /* Get all messages for this recipient, ordered by mail_id */
    snprintf(query, sizeof(query),
             "SELECT mail_id, sender, flags FROM mail "
             "WHERE recipient = %" DBREF_FMT " ORDER BY mail_id",
             recipient);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: delete_range query failed: %s",
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
            long mail_id = strtol(row[0], NULL, 10);
            dbref sender = strtol(row[1], NULL, 10);

            /* Check permissions: mailbox owner or sender */
            if (recipient == player || sender == player ||
                power(player, POW_BOARD)) {
                char update[256];
                snprintf(update, sizeof(update),
                         "UPDATE mail SET flags = %d WHERE mail_id = %ld",
                         target_flag, mail_id);
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
 * mariadb_mail_purge - Permanently remove deleted messages
 *
 * Only removes messages the player has permission to purge:
 * - Messages in their own mailbox
 * - Messages they sent
 */
long mariadb_mail_purge(dbref recipient, dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[512];
    long affected;

    if (!conn) {
        return 0;
    }

    if (recipient == player) {
        /* Purge own mailbox - delete all flagged as deleted */
        snprintf(query, sizeof(query),
                 "DELETE FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND (flags & 1)",
                 recipient);
    } else {
        /* Purge only messages sent by this player that are deleted */
        snprintf(query, sizeof(query),
                 "DELETE FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND sender = %" DBREF_FMT " AND (flags & 1)",
                 recipient, player);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: purge failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    affected = (long)mysql_affected_rows(conn);
    return affected;
}

/*
 * mariadb_mail_list - Iterate over messages with callback
 *
 * Calls the callback for each message in the mailbox, passing the
 * 1-based position number and a MAIL_RESULT struct. The callback
 * should NOT free the message pointer - it's freed after the callback.
 */
long mariadb_mail_list(dbref recipient, int include_deleted,
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
                 "SELECT mail_id, sender, recipient, sent_date, flags, "
                 "message, subject "
                 "FROM mail WHERE recipient = %" DBREF_FMT " "
                 "ORDER BY mail_id",
                 recipient);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT mail_id, sender, recipient, sent_date, flags, "
                 "message, subject "
                 "FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND NOT (flags & 1) "
                 "ORDER BY mail_id",
                 recipient);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: list query failed: %s",
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
        mr.sender = strtol(row[1], NULL, 10);
        mr.recipient = strtol(row[2], NULL, 10);
        mr.sent_date = strtol(row[3], NULL, 10);
        mr.flags = (int)strtol(row[4], NULL, 10);
        mr.message = row[5];  /* Temporary - valid until mysql_free_result */
        mr.subject = (row[6] && row[6][0]) ? row[6] : NULL;  /* Temporary */

        callback(&mr, pos, userdata);
        pos++;
        count++;
    }

    mysql_free_result(result);
    return count;
}

/* ============================================================================
 * COUNT AND SIZE OPERATIONS
 * ============================================================================ */

/*
 * mariadb_mail_count - Count messages in mailbox
 */
long mariadb_mail_count(dbref recipient, int include_deleted)
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
                 "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT,
                 recipient);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND NOT (flags & 1)",
                 recipient);
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

/*
 * mariadb_mail_count_unread - Count unread, non-deleted messages
 */
long mariadb_mail_count_unread(dbref recipient)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long count;

    if (!conn) {
        return -1;
    }

    /* Unread = not MF_READ (2) and not MF_DELETED (1) */
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
             "AND NOT (flags & 2) AND NOT (flags & 1)",
             recipient);

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

/*
 * mariadb_mail_count_from - Count messages from a specific sender
 */
long mariadb_mail_count_from(dbref recipient, dbref sender,
                             int include_deleted)
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
                 "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND sender = %" DBREF_FMT,
                 recipient, sender);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
                 "AND sender = %" DBREF_FMT " AND NOT (flags & 1)",
                 recipient, sender);
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

/*
 * mariadb_mail_count_unread_from - Count unread messages from a specific sender
 */
long mariadb_mail_count_unread_from(dbref recipient, dbref sender)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long count;

    if (!conn) {
        return -1;
    }

    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
             "AND sender = %" DBREF_FMT " "
             "AND NOT (flags & 2) AND NOT (flags & 1)",
             recipient, sender);

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

/*
 * mariadb_mail_count_new_from - Count new messages from a specific sender
 */
long mariadb_mail_count_new_from(dbref recipient, dbref sender)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long count;

    if (!conn) {
        return -1;
    }

    /* New = MF_NEW (4) set and not MF_DELETED (1) */
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
             "AND sender = %" DBREF_FMT " "
             "AND (flags & 4) AND NOT (flags & 1)",
             recipient, sender);

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

/*
 * mariadb_mail_count_read_from - Count read messages from a specific sender
 */
long mariadb_mail_count_read_from(dbref recipient, dbref sender)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long count;

    if (!conn) {
        return -1;
    }

    /* Read = MF_READ (2) set and not MF_DELETED (1) */
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM mail WHERE recipient = %" DBREF_FMT " "
             "AND sender = %" DBREF_FMT " "
             "AND (flags & 2) AND NOT (flags & 1)",
             recipient, sender);

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

/*
 * mariadb_mail_size - Get total message storage size for a player
 */
long mariadb_mail_size(dbref recipient)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[256];
    long size;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "SELECT COALESCE(SUM(LENGTH(message)), 0) FROM mail "
             "WHERE recipient = %" DBREF_FMT,
             recipient);

    if (mysql_query(conn, query)) {
        return 0;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return 0;
    }

    row = mysql_fetch_row(result);
    size = row ? strtol(row[0], NULL, 10) : 0;
    mysql_free_result(result);
    return size;
}

/* ============================================================================
 * BULK OPERATIONS
 * ============================================================================ */

/*
 * mariadb_mail_remove_player - Delete all mail for a player
 */
long mariadb_mail_remove_player(dbref recipient)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    long affected;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "DELETE FROM mail WHERE recipient = %" DBREF_FMT,
             recipient);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB mail: remove_player failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    affected = (long)mysql_affected_rows(conn);
    return affected;
}

/*
 * mariadb_mail_remove_all - Truncate the entire mail table
 */
int mariadb_mail_remove_all(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        return 0;
    }

    if (mysql_query(conn, "TRUNCATE TABLE mail")) {
        log_error(tprintf("MariaDB mail: truncate failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    log_important("MariaDB mail: All mail truncated");
    return 1;
}

/*
 * mariadb_mail_stats - Get aggregate statistics for @info mail
 */
int mariadb_mail_stats(long *total_out, long *deleted_out, long *new_out,
                       long *unread_out, long *read_out, long *text_size_out,
                       long *players_out)
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
        "  SUM(CASE WHEN (flags & 4) AND NOT (flags & 1) THEN 1 ELSE 0 END), "
        "  SUM(CASE WHEN NOT (flags & 2) AND NOT (flags & 1) THEN 1 ELSE 0 END), "
        "  SUM(CASE WHEN (flags & 2) AND NOT (flags & 1) THEN 1 ELSE 0 END), "
        "  COALESCE(SUM(LENGTH(message)), 0), "
        "  COUNT(DISTINCT recipient) "
        "FROM mail";

    if (mysql_query(conn, stats_sql)) {
        log_error(tprintf("MariaDB mail: stats query failed: %s",
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
        if (new_out) *new_out = strtol(row[2] ? row[2] : "0", NULL, 10);
        if (unread_out) *unread_out = strtol(row[3] ? row[3] : "0", NULL, 10);
        if (read_out) *read_out = strtol(row[4] ? row[4] : "0", NULL, 10);
        if (text_size_out) *text_size_out = strtol(row[5] ? row[5] : "0", NULL, 10);
        if (players_out) *players_out = strtol(row[6] ? row[6] : "0", NULL, 10);
    }

    mysql_free_result(result);
    return 1;
}

#endif /* USE_MARIADB */
