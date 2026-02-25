/* mariadb_mail.h - MariaDB-backed private mail operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the private player-to-player mail system.
 * All queries go directly to MariaDB - no in-memory cache.
 *
 * The MAIL_RESULT struct is shared with mariadb_board.h for board posts.
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * Stubs return failure values when MariaDB is not available.
 */

#ifndef _MARIADB_MAIL_H_
#define _MARIADB_MAIL_H_

#include "config.h"

/* ============================================================================
 * MESSAGE FLAGS
 * ============================================================================
 * Shared between messaging.c, mariadb_mail.c, and mariadb_board.c.
 * Values must match data stored in MariaDB tables and legacy flat-file.
 */

#define MF_DELETED  0x01
#define MF_READ     0x02
#define MF_NEW      0x04
#define MF_BOARD    0x08  /* Legacy: used only during flat-file conversion */

/* ============================================================================
 * SHARED RESULT STRUCT
 * ============================================================================ */

/*
 * MAIL_RESULT - Result struct for mail/board query results
 *
 * Used by both mariadb_mail and mariadb_board functions.
 * The caller must SMART_FREE the message field when done.
 */
typedef struct mail_result {
    long id;              /* DB primary key (mail_id or post_id) */
    dbref sender;         /* from / author */
    dbref recipient;      /* recipient / board_room */
    long sent_date;       /* Unix timestamp */
    int flags;            /* MF_DELETED=1, MF_READ=2, MF_NEW=4 */
    char *message;        /* Caller must SMART_FREE */
} MAIL_RESULT;

/* Callback type for list operations */
typedef void (*mail_list_callback)(const MAIL_RESULT *result, long position,
                                   void *userdata);

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/*
 * mariadb_mail_init - Create the mail table if it doesn't exist
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_init(void);

/*
 * mariadb_mail_send - Insert a new mail message
 *
 * PARAMETERS:
 *   sender    - dbref of sender (NOTHING for system mail)
 *   recipient - dbref of recipient player
 *   message   - Message text
 *   flags     - Initial flags (typically MF_NEW)
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_send(dbref sender, dbref recipient, const char *message,
                      int flags);

/*
 * mariadb_mail_get_by_position - Get a mail message by position number
 *
 * Position is 1-based, ordered by mail_id (oldest first).
 * Only non-deleted messages are counted unless include_deleted is true.
 *
 * PARAMETERS:
 *   recipient       - dbref of mailbox owner
 *   msgnum          - 1-based message position
 *   include_deleted - Whether to include deleted messages in numbering
 *   out             - Output: filled with message data (caller frees message)
 *
 * RETURNS: 1 if found, 0 if not found or error
 */
int mariadb_mail_get_by_position(dbref recipient, long msgnum,
                                 int include_deleted, MAIL_RESULT *out);

/*
 * mariadb_mail_update_flags - Update flags for a specific message by ID
 *
 * PARAMETERS:
 *   mail_id - Primary key of the message
 *   flags   - New flags value
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_update_flags(long mail_id, int flags);

/*
 * mariadb_mail_delete_range - Mark/unmark messages as deleted by position
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *   start     - Start position (1-based)
 *   end       - End position (1-based), or 0 for single message
 *   undelete  - If true, clear MF_DELETED and set MF_READ instead
 *   player    - dbref of player performing the action (for permission check)
 *
 * RETURNS: Number of messages affected
 */
long mariadb_mail_delete_range(dbref recipient, long start, long end,
                               int undelete, dbref player);

/*
 * mariadb_mail_purge - Permanently remove deleted messages
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *   player    - dbref of player performing purge (for permission check)
 *
 * RETURNS: Number of messages purged
 */
long mariadb_mail_purge(dbref recipient, dbref player);

/*
 * mariadb_mail_list - Iterate over messages with callback
 *
 * PARAMETERS:
 *   recipient       - dbref of mailbox owner
 *   include_deleted - Whether to include deleted messages
 *   callback        - Function called for each message
 *   userdata        - Passed through to callback
 *
 * RETURNS: Number of messages iterated
 */
long mariadb_mail_list(dbref recipient, int include_deleted,
                       mail_list_callback callback, void *userdata);

/*
 * mariadb_mail_count - Count messages in mailbox
 *
 * PARAMETERS:
 *   recipient       - dbref of mailbox owner
 *   include_deleted - Whether to count deleted messages
 *
 * RETURNS: Message count, or -1 on error
 */
long mariadb_mail_count(dbref recipient, int include_deleted);

/*
 * mariadb_mail_count_unread - Count unread, non-deleted messages
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *
 * RETURNS: Unread count, or -1 on error
 */
long mariadb_mail_count_unread(dbref recipient);

/*
 * mariadb_mail_count_from - Count messages from a specific sender
 *
 * PARAMETERS:
 *   recipient       - dbref of mailbox owner
 *   sender          - dbref of sender to count
 *   include_deleted - Whether to count deleted messages
 *
 * RETURNS: Message count, or -1 on error
 */
long mariadb_mail_count_from(dbref recipient, dbref sender,
                             int include_deleted);

/*
 * mariadb_mail_count_unread_from - Count unread messages from a specific sender
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *   sender    - dbref of sender
 *
 * RETURNS: Unread count, or -1 on error
 */
long mariadb_mail_count_unread_from(dbref recipient, dbref sender);

/*
 * mariadb_mail_count_new_from - Count new messages from a specific sender
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *   sender    - dbref of sender
 *
 * RETURNS: New count, or -1 on error
 */
long mariadb_mail_count_new_from(dbref recipient, dbref sender);

/*
 * mariadb_mail_count_read_from - Count read messages from a specific sender
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *   sender    - dbref of sender
 *
 * RETURNS: Read count, or -1 on error
 */
long mariadb_mail_count_read_from(dbref recipient, dbref sender);

/*
 * mariadb_mail_size - Get total message storage size for a player
 *
 * PARAMETERS:
 *   recipient - dbref of mailbox owner
 *
 * RETURNS: Total bytes of message text, or 0 on error
 */
long mariadb_mail_size(dbref recipient);

/*
 * mariadb_mail_remove_player - Delete all mail for a player
 *
 * PARAMETERS:
 *   recipient - dbref of player
 *
 * RETURNS: Number of messages removed
 */
long mariadb_mail_remove_player(dbref recipient);

/*
 * mariadb_mail_remove_all - Truncate the entire mail table
 *
 * WARNING: Destroys all private mail!
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_remove_all(void);

/*
 * mariadb_mail_stats - Get aggregate statistics for @info mail
 *
 * PARAMETERS:
 *   total_out     - Output: total message count
 *   deleted_out   - Output: deleted message count
 *   new_out       - Output: new message count
 *   unread_out    - Output: unread message count
 *   read_out      - Output: read message count
 *   text_size_out - Output: total message text bytes
 *   players_out   - Output: distinct recipients with mail
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_mail_stats(long *total_out, long *deleted_out, long *new_out,
                       long *unread_out, long *read_out, long *text_size_out,
                       long *players_out);

#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================ */

static inline int mariadb_mail_init(void) { return 0; }
static inline int mariadb_mail_send(dbref s __attribute__((unused)),
    dbref r __attribute__((unused)), const char *m __attribute__((unused)),
    int f __attribute__((unused))) { return 0; }
static inline int mariadb_mail_get_by_position(dbref r __attribute__((unused)),
    long n __attribute__((unused)), int d __attribute__((unused)),
    MAIL_RESULT *o __attribute__((unused))) { return 0; }
static inline int mariadb_mail_update_flags(long id __attribute__((unused)),
    int f __attribute__((unused))) { return 0; }
static inline long mariadb_mail_delete_range(dbref r __attribute__((unused)),
    long s __attribute__((unused)), long e __attribute__((unused)),
    int u __attribute__((unused)), dbref p __attribute__((unused)))
    { return 0; }
static inline long mariadb_mail_purge(dbref r __attribute__((unused)),
    dbref p __attribute__((unused))) { return 0; }
static inline long mariadb_mail_list(dbref r __attribute__((unused)),
    int d __attribute__((unused)),
    mail_list_callback c __attribute__((unused)),
    void *u __attribute__((unused))) { return 0; }
static inline long mariadb_mail_count(dbref r __attribute__((unused)),
    int d __attribute__((unused))) { return -1; }
static inline long mariadb_mail_count_unread(dbref r __attribute__((unused)))
    { return -1; }
static inline long mariadb_mail_count_from(dbref r __attribute__((unused)),
    dbref s __attribute__((unused)), int d __attribute__((unused)))
    { return -1; }
static inline long mariadb_mail_count_unread_from(
    dbref r __attribute__((unused)),
    dbref s __attribute__((unused))) { return -1; }
static inline long mariadb_mail_count_new_from(dbref r __attribute__((unused)),
    dbref s __attribute__((unused))) { return -1; }
static inline long mariadb_mail_count_read_from(
    dbref r __attribute__((unused)),
    dbref s __attribute__((unused))) { return -1; }
static inline long mariadb_mail_size(dbref r __attribute__((unused)))
    { return 0; }
static inline long mariadb_mail_remove_player(dbref r __attribute__((unused)))
    { return 0; }
static inline int mariadb_mail_remove_all(void) { return 0; }
static inline int mariadb_mail_stats(long *t __attribute__((unused)),
    long *d __attribute__((unused)), long *n __attribute__((unused)),
    long *u __attribute__((unused)), long *r __attribute__((unused)),
    long *s __attribute__((unused)), long *p __attribute__((unused)))
    { return 0; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_MAIL_H_ */
