/* messaging.c - Unified mail and board system
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * This file merges the functionality of mail.c and board.c,
 * modernizes to ANSI C, and fixes security issues.
 *
 * MARIADB MIGRATION (2026-02):
 * All message storage has been migrated from an in-memory mdb[] array
 * with flat-file serialization to MariaDB tables. The public function
 * signatures are unchanged - only the internals have been replaced.
 *
 * Removed: mdb[] array, MDB_ENTRY typedef, mdbref type, free-list
 *          management, get_mailk/set_mailk (A_MAILK attribute),
 *          write_messages/read_messages flat-file I/O.
 *
 * All message operations now query MariaDB directly via the
 * mariadb_mail and mariadb_board modules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#include "externs.h"
#include "db.h"
#include "mariadb_mail.h"
#include "mariadb_board.h"
#include "mariadb_news.h"

/* Message flags are defined in mariadb_mail.h:
 * MF_DELETED  0x01
 * MF_READ     0x02
 * MF_NEW      0x04
 * MF_BOARD    0x08  (legacy, used only during flat-file conversion)
 */

/* msg_dest_type is defined in externs.h */

/* Function prototypes - ANSI C style */

/* Initialization and cleanup */
void init_mail(void);
void free_mail(void);

/* Core message operations */
void post_message(dbref, dbref, const char *, const char *, msg_dest_type, int);
void read_message(dbref, dbref, long);
long delete_messages(dbref, dbref, long, long, int);
void purge_deleted(dbref, dbref);
static void list_mail_messages(dbref, dbref);

/* Utility functions */
long count_messages(dbref, int);
long count_unread(dbref);
long mail_size(dbref);

/* Board-specific functions */
void ban_from_board(dbref, dbref);
void unban_from_board(dbref, dbref);
int is_banned_from_board(dbref);

/* Database I/O wrappers */
void write_mail(FILE *);  /* No-op: messages are in MariaDB */
void read_mail(FILE *);   /* Detects legacy data and auto-converts */

/* Command handlers */
void do_mail(dbref, char *, char *);
void do_board(dbref, char *, char *);

/* External API functions (called from other modules) */
void check_mail(dbref, char *);
long check_mail_internal(dbref, char *);
long dt_mail(dbref);
void info_mail(dbref);

#ifdef SHRINK_DB
void remove_all_mail(void);
#endif

/* ============================================================================
 * RANGE PARSING
 * ============================================================================ */

/*
 * parse_delete_range - Parse a deletion argument into start/end values
 *
 * Handles: single number ("5"), range ("3-7"), or "all".
 * Sets start_out/end_out and returns 1 on success, 0 on parse error.
 * For "all", sets start=1, end=LONG_MAX.
 */
static int parse_delete_range(const char *arg, long *start_out, long *end_out)
{
    char *dash;
    char buf[64];

    if (!arg || !*arg || !start_out || !end_out) {
        return 0;
    }

    /* Handle "all" */
    if (!string_compare((char *)arg, "all")) {
        *start_out = 1;
        *end_out = LONG_MAX;
        return 1;
    }

    /* Copy to scratch buffer for parsing */
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Look for range separator */
    dash = strchr(buf, '-');
    if (dash && dash != buf) {
        /* Range: "3-7" */
        *dash = '\0';
        *start_out = atol(buf);
        *end_out = atol(dash + 1);
        if (*start_out <= 0 || *end_out <= 0 || *end_out < *start_out) {
            return 0;
        }
        return 1;
    }

    /* Single number */
    *start_out = atol(buf);
    *end_out = *start_out;
    if (*start_out <= 0) {
        return 0;
    }
    return 1;
}

/* Safe string operations - prevent buffer overflows */
static void safe_copy(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0) return;

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void safe_append(char *dest, const char *src, size_t dest_size)
{
    size_t dest_len;

    if (!dest || !src || dest_size == 0) return;

    dest_len = strlen(dest);
    if (dest_len >= dest_size - 1) return;

    strncat(dest, src, dest_size - dest_len - 1);
}

/* ===================================================================
 * Initialization and Cleanup
 * =================================================================== */

/* Initialize message system - tables are created during server startup */
void init_mail(void)
{
    /* MariaDB tables are initialized in server_main.c via
     * mariadb_mail_init() and mariadb_board_init().
     * Nothing to do here for the MariaDB backend. */
}

/* Free message system - no-op for MariaDB backend */
void free_mail(void)
{
    /* No in-memory state to free. MariaDB connection is cleaned up
     * by mariadb_cleanup() in server_main.c. */
}

/* ===================================================================
 * Core Message Operations
 * =================================================================== */

/* Calculate mail storage size */
long mail_size(dbref player)
{
    if (player < 0 || player >= db_top) return 0;

    return mariadb_mail_size(player);
}

/* Count messages in mailbox */
long count_messages(dbref mailbox, int include_deleted)
{
    long count;

    if (mailbox < 0 || mailbox >= db_top) return 0;

    /* Check if this is a board room or player mailbox */
    if (mailbox == default_room) {
        count = mariadb_board_count(mailbox, include_deleted);
    } else {
        count = mariadb_mail_count(mailbox, include_deleted);
    }

    return (count >= 0) ? count : 0;
}

/* Count unread messages for a player */
long count_unread(dbref player)
{
    long count;

    if (player < 0 || player >= db_top) return 0;

    count = mariadb_mail_count_unread(player);
    return (count >= 0) ? count : 0;
}

/* Send a message - unified function for mail, board, and news */
void post_message(dbref from, dbref to, const char *subject,
                  const char *message, msg_dest_type type, int flags)
{
    long msgnum;

    if (!message || !*message) return;

    /* Validate sender */
    if (from != NOTHING && (from < 0 || from >= db_top)) return;

    /* For news, to is NOTHING — skip recipient validation and quota */
    if (type == MSG_NEWS) {
        if (!mariadb_news_post_article(from, subject ? subject : "(untitled)",
                                        message)) {
            if (from != NOTHING) {
                notify(from, "+news: Failed to post article.");
            }
        }
        return;
    }

    if (to < 0 || to >= db_top) return;

    /* Check quota for non-board messages */
    if (type != MSG_BOARD) {
        if (db[to].i_flags & I_QUOTAFULL) {
            if (from != NOTHING) {
                notify(from, "That player has insufficient quota.");
            }
            return;
        }
    }

    /* Route to appropriate MariaDB table */
    if (type == MSG_BOARD) {
        if (!mariadb_board_post(from, to, subject, message, flags)) {
            if (from != NOTHING) {
                notify(from, "+board: Failed to post message.");
            }
            return;
        }
    } else {
        if (!mariadb_mail_send(from, to, subject, message, flags)) {
            if (from != NOTHING) {
                notify(from, "+mail: Failed to send message.");
            }
            return;
        }
    }

    /* Notify recipient for private mail */
    if (type == MSG_PRIVATE && from != NOTHING) {
        if (could_doit(from, to, A_LPAGE)) {
            msgnum = mariadb_mail_count(to, 0);
            notify(to, tprintf("+mail: You have new mail from %s (message %ld)",
                              unparse_object(to, from), msgnum));
        }
    }

    recalc_bytes(to);
}

/* Delete messages - unified function */
long delete_messages(dbref player, dbref mailbox, long start,
                     long end, int undelete)
{
    long count;
    int is_board = (mailbox == default_room);

    if (mailbox < 0 || mailbox >= db_top) return 0;
    if (start < 0 || end < 0) return 0;

    /* If start is 0, nothing to do */
    if (start <= 0) return 0;

    if (is_board) {
        count = mariadb_board_delete_range(mailbox, start, end,
                                           undelete, player);
    } else {
        count = mariadb_mail_delete_range(mailbox, start, end,
                                          undelete, player);
    }

    recalc_bytes(mailbox);
    return count;
}

/* Purge deleted messages - unified function */
void purge_deleted(dbref player, dbref mailbox)
{
    int is_board = (mailbox == default_room);

    if (mailbox < 0 || mailbox >= db_top) return;

    if (is_board) {
        mariadb_board_purge(mailbox, player);
    } else {
        mariadb_mail_purge(mailbox, player);
    }

    recalc_bytes(mailbox);
}

/* ===================================================================
 * List Callback Helpers
 * =================================================================== */

/* Context struct for list_messages callback */
typedef struct {
    dbref player;       /* Player viewing the list */
    dbref mailbox;      /* Mailbox/board being listed */
    int is_board;       /* Whether this is a board listing */
    const char *label;  /* Display label ("+board", "+news", etc.) */
} list_context;

/* Callback for list_messages - displays one message line */
static void list_message_callback(const MAIL_RESULT *mr, long position,
                                  void *userdata)
{
    list_context *ctx = (list_context *)userdata;
    char status_char;
    char date_buf[32];
    char *preview;
    char buf[1024];

    /* Determine status character */
    if (mr->flags & MF_DELETED) {
        status_char = 'd';
    } else if (mr->flags & MF_NEW) {
        status_char = '*';
    } else if (mr->flags & MF_READ) {
        status_char = ' ';
    } else {
        status_char = 'u';
    }

    /* Check permissions */
    if (ctx->mailbox != ctx->player &&
        mr->sender != ctx->player &&
        !(ctx->is_board && (status_char != 'd' || power(ctx->player, POW_BOARD)))) {
        return;
    }

    /* Clear MF_NEW flag when owner lists their own mail */
    if (status_char == '*' && ctx->player == ctx->mailbox) {
        int new_flags = mr->flags & ~MF_NEW;
        if (ctx->is_board) {
            mariadb_board_update_flags(mr->id, new_flags);
        } else {
            mariadb_mail_update_flags(mr->id, new_flags);
        }
    }

    /* Format sender name - validate sender still exists, color-aware padding */
    {
        char *padded_name;
        char *padded_date;

        if (GoodObject(mr->sender)) {
            padded_name = ljust(truncate_color(db[mr->sender].cname, 20), 20);
        } else {
            padded_name = ljust("*deleted*", 20);
        }

        /* Format date */
        safe_copy(date_buf, mktm(mr->sent_date, "D", ctx->player),
                 sizeof(date_buf));
        padded_date = ljust(date_buf, 19);

        /* Get subject or message preview */
        if (mr->subject && mr->subject[0]) {
            preview = tprintf("%s", truncate_color(mr->subject, 25));
        } else {
            preview = tprintf("%s", truncate_color(mr->message ? mr->message : "", 25));
        }

        /* Remove newlines from preview */
        {
            char *nl = strchr(preview, '\n');
            if (nl) *nl = '\0';
        }

        snprintf(buf, sizeof(buf), "%5ld) %c %s | %s | %s",
                position, status_char, padded_name, padded_date, preview);
        notify(ctx->player, buf);
    }
}

/* List private mail messages */
static void list_mail_messages(dbref player, dbref mailbox)
{
    char buf[1024];
    list_context ctx;

    if (mailbox < 0 || mailbox >= db_top) return;

    /* Print header */
    snprintf(buf, sizeof(buf),
        "|W!+------>| |B!++mail| |W!+for| %s", db[mailbox].cname);
    if (player != mailbox) {
        safe_append(buf, tprintf(" |W!+from| %s", db[player].cname), sizeof(buf));
    }
    safe_append(buf, " |W!+<------|", sizeof(buf));
    notify(player, buf);

    /* Setup callback context */
    ctx.player = player;
    ctx.mailbox = mailbox;
    ctx.is_board = 0;
    ctx.label = "+mail";

    /* List all messages (including deleted for owner/admin visibility) */
    mariadb_mail_list(mailbox, 1, list_message_callback, &ctx);

    notify(player, "");
}

/* List public messages (board or news) with per-player read tracking */
static void list_public_messages(dbref player, dbref board_room,
                                  int unread_only, const char *label)
{
    list_context ctx;
    long count;

    /* Print header */
    if (unread_only) {
        notify(player, tprintf("|C+%s|   |Y!+Unread Posts|", label));
    } else {
        notify(player, tprintf("|C+%s|   |Y!+All Posts|", label));
    }
    notify(player,
        "------------------------------+---------------------+------------------------");
    notify(player,
        "         |Y!+Author|               | |W!+Time/Date|           | Message");
    notify(player,
        "------------------------------+---------------------+------------------------");

    /* Setup callback context */
    ctx.player = player;
    ctx.mailbox = board_room;
    ctx.is_board = 1;
    ctx.label = label;

    /* Use per-player read tracking — dispatch to news or board */
    if (board_room == NEWS_ROOM) {
        count = mariadb_news_list_for_player(player, 1, unread_only,
                                              list_message_callback, &ctx);
    } else {
        count = mariadb_board_list_for_player(board_room, player, 1, unread_only,
                                              list_message_callback, &ctx);
    }

    if (count == 0 && unread_only) {
        notify(player, tprintf("  No unread posts. Use '%s list=all' to see all posts.",
                              label));
    }

    notify(player,
        "------------------------------+---------------------+------------------------");

    /* Show unread/total summary */
    {
        long total, unread;

        if (board_room == NEWS_ROOM) {
            total = mariadb_news_count(0);
            unread = mariadb_news_count_unread(player);
        } else {
            total = mariadb_board_count(board_room, 0);
            unread = mariadb_board_count_unread(player, board_room);
        }
        if (total < 0) total = 0;

        notify(player, tprintf("  %ld unread, %ld total.%s",
                              unread, total,
                              unread_only && total > count ?
                              tprintf(" Use '%s list=all' to see all.", label) : ""));
    }
}


/* Read a public message (board or news) by position */
static void read_public_message(dbref player, dbref board_room, long msgnum,
                                 const char *label)
{
    MAIL_RESULT mr;
    char buf[512];
    int found;

    if (msgnum <= 0) {
        notify(player, tprintf("%s: Specify a post number to read.", label));
        return;
    }

    /* Get message by position (include deleted for permission checking) */
    if (board_room == NEWS_ROOM) {
        found = mariadb_news_get_by_position(msgnum, 1, &mr);
    } else {
        found = mariadb_board_get_by_position(board_room, msgnum, 1, &mr);
    }

    if (!found) {
        notify(player, tprintf("%s: Invalid post number.", label));
        return;
    }

    /* Check permissions for deleted messages */
    if ((mr.flags & MF_DELETED) &&
        mr.sender != player &&
        !Wizard(player) && !power(player, POW_BOARD)) {
        notify(player, tprintf("%s: Invalid post number.", label));
        if (mr.subject) SMART_FREE(mr.subject);
        if (mr.message) SMART_FREE(mr.message);
        return;
    }

    /* Display message */
    notify(player, tprintf("Message %ld:", msgnum));

    notify(player, tprintf("Subject: %s",
                          (mr.subject && mr.subject[0]) ?
                          mr.subject : "(no subject)"));

    notify(player, tprintf("From: %s",
                          mr.sender == NOTHING ? "The MUSE Server" :
                          unparse_object(player, mr.sender)));

    snprintf(buf, sizeof(buf), "Date: %s", mktm(mr.sent_date, "D", player));
    notify(player, buf);

    /* Show flags if applicable */
    if (mr.flags & MF_DELETED) {
        notify(player, "Flags: deleted");
    }

    notify(player, "");
    notify(player, mr.message ? mr.message : "(empty)");

    /* Mark as read via board_read (per-player tracking) */
    if (board_room == NEWS_ROOM) {
        mariadb_news_mark_read(player, mr.id);
    } else {
        mariadb_board_mark_read(player, mr.id);
    }

    if (mr.subject) SMART_FREE(mr.subject);
    if (mr.message) SMART_FREE(mr.message);
}

/* Read a private mail message by position (mail only) */
void read_message(dbref player, dbref mailbox, long msgnum)
{
    MAIL_RESULT mr;
    char buf[512];
    int found;

    if (mailbox < 0 || mailbox >= db_top) return;
    if (msgnum <= 0) return;

    found = mariadb_mail_get_by_position(mailbox, msgnum, 1, &mr);

    if (!found) {
        notify(player, "+mail: Invalid message number.");
        return;
    }

    /* Check permissions for deleted messages */
    if ((mr.flags & MF_DELETED) &&
        mailbox != player &&
        mr.sender != player) {
        notify(player, "+mail: Invalid message number.");
        if (mr.subject) SMART_FREE(mr.subject);
        if (mr.message) SMART_FREE(mr.message);
        return;
    }

    /* Display message */
    notify(player, tprintf("Message %ld:", msgnum));

    notify(player, tprintf("Subject: %s",
                          (mr.subject && mr.subject[0]) ?
                          mr.subject : "(no subject)"));

    notify(player, tprintf("To: %s", db[mailbox].cname));

    notify(player, tprintf("From: %s",
                          mr.sender == NOTHING ? "The MUSE Server" :
                          unparse_object(player, mr.sender)));

    snprintf(buf, sizeof(buf), "Date: %s", mktm(mr.sent_date, "D", player));
    notify(player, buf);

    /* Show flags if applicable */
    if (mr.flags & (MF_DELETED | MF_READ | MF_NEW)) {
        safe_copy(buf, "Flags:", sizeof(buf));
        if (mr.flags & MF_DELETED) safe_append(buf, " deleted", sizeof(buf));
        if (mr.flags & MF_READ) safe_append(buf, " read", sizeof(buf));
        if (mr.flags & MF_NEW) safe_append(buf, " new", sizeof(buf));
        notify(player, buf);
    }

    notify(player, "");
    notify(player, mr.message ? mr.message : "(empty)");

    /* Mark as read if reading own mail */
    if (mailbox == player) {
        int new_flags = (mr.flags & ~MF_NEW) | MF_READ;
        mariadb_mail_update_flags(mr.id, new_flags);
    }

    if (mr.subject) SMART_FREE(mr.subject);
    if (mr.message) SMART_FREE(mr.message);
}

/* ===================================================================
 * Board Ban System
 * =================================================================== */

/* Board ban checking - uses MariaDB board_bans table */
int is_banned_from_board(dbref player)
{
    return mariadb_board_is_banned(player);
}

/**
 * check_board - Notify player of unread board posts on connect
 */
void check_board(dbref player)
{
    long unread;

    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return;
    }

    unread = mariadb_board_count_unread(player, default_room);
    if (unread > 0) {
        notify(player, tprintf("You have %ld unread board post%s. Type '+board' to view.",
                              unread, unread == 1 ? "" : "s"));
    }
}

/* ===================================================================
 * Database I/O Wrappers
 * =================================================================== */

/*
 * write_mail - No-op for MariaDB backend
 *
 * Messages are stored in MariaDB, not in the flat-file database.
 * This function is kept for API compatibility with db_io.c.
 */
void write_mail(FILE *fp)
{
    (void)fp;  /* Messages are in MariaDB now */
}

/*
 * read_mail - Detect and auto-convert legacy flat-file messages
 *
 * If the file contains legacy '+' message lines after ***END OF DUMP***,
 * fork/exec the external convert_db tool to import them into MariaDB.
 * The tool runs as a separate process with its own MariaDB connection.
 * On subsequent loads (after conversion), there are no '+' lines.
 */
void read_mail(FILE *fp)
{
    char buf[2048];
    long legacy_count = 0;

    if (!fp) return;

    /* Peek ahead to count legacy message lines */
    while (fgets(buf, (int)sizeof(buf), fp) != NULL) {
        if (buf[0] == '+') {
            legacy_count++;
        }
    }

    if (legacy_count == 0) {
        /* No legacy messages - nothing to do */
        return;
    }

    /* Legacy messages found - fork/exec the convert_db tool */
    log_important(tprintf("Found %ld legacy messages in flat-file - "
                         "running convert_db to import to MariaDB",
                         legacy_count));

    {
        pid_t pid;
        int status;

        pid = fork();
        if (pid < 0) {
            log_error("read_mail: fork() failed for convert_db");
            return;
        }

        if (pid == 0) {
            /* Child process - exec convert_db */
            /* Try ../bin/convert_db (from run/ directory) first */
            execl("../bin/convert_db", "convert_db", "--all",
                  def_db_in, (char *)NULL);
            /* If that fails, try bin/convert_db (from project root) */
            execl("bin/convert_db", "convert_db", "--all",
                  def_db_in, (char *)NULL);
            /* If exec fails, exit child with error */
            fprintf(stderr, "read_mail: execl convert_db failed\n");
            _exit(1);
        }

        /* Parent process - wait for child to complete.
         * waitpid may fail with ECHILD if SIGCHLD is SIG_IGN,
         * which is fine - the child still ran. */
        if (waitpid(pid, &status, 0) < 0) {
            /* Child ran but we can't get status - assume success
             * since convert_db logs its own errors */
            log_important("Legacy message conversion dispatched "
                         "(convert_db ran as child process)");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            log_important("Legacy message conversion completed successfully");
        } else {
            log_error(tprintf("convert_db exited with status %d",
                             WIFEXITED(status) ? WEXITSTATUS(status) : -1));
        }
    }
}

/* ===================================================================
 * External API Functions
 * These are called by other parts of the codebase
 * =================================================================== */

/**
 * check_mail - Display formatted mail status to player
 *
 * Shows mail counts with color formatting. Called when players
 * connect or manually check mail status.
 *
 * @param player Player checking mail
 * @param arg2 Optional target player name (check mail sent to them)
 */
void check_mail(dbref player, char *arg2)
{
    dbref target;
    char buf[2048];
    long read_count, new_msg, tot;

    if (!arg2 || !*arg2) {
        target = player;
    } else {
        target = lookup_player(arg2);
        if (target == NOTHING) {
            target = player;
        }
    }

    if (target == player) {
        /* Checking own mail */
        tot = mariadb_mail_count(target, 0);
        if (tot <= 0) return;

        new_msg = mariadb_mail_count_unread(target);
        if (new_msg < 0) new_msg = 0;

        /* Count truly "new" (MF_NEW flag) vs unread */
        {
            long new_count = 0;
            long unread_count = 0;

            /* We need read count to calculate unread */
            read_count = tot - new_msg;  /* Approximate: read = total - unread */

            /* Get actual new message count via stats */
            /* For simplicity, treat all unread as potentially new */
            /* Use the count_from functions for more detail when checking
             * own mail - for now, a simpler approach */
            new_count = new_msg;  /* All unread are reported */
            unread_count = 0;

            /* Try to get a more accurate breakdown */
            {
                long mail_new = 0, mail_read = 0;
                long mail_total = 0, mail_deleted = 0;
                long mail_unread = 0, mail_text = 0, mail_players = 0;

                if (mariadb_mail_stats(&mail_total, &mail_deleted, &mail_new,
                                       &mail_unread, &mail_read, &mail_text,
                                       &mail_players)) {
                    /* These are global stats, not per-player.
                     * For per-player, we use individual queries */
                }
            }

            /* Build status message using simple counts */
            snprintf(buf, sizeof(buf),
                    "|W!++mail:| You have |Y!+%ld| message%s.",
                    tot, (tot == 1) ? "" : "s");

            if (new_msg > 0) {
                safe_append(buf,
                           tprintf(" |G!+%ld| of them %s unread.",
                                  new_msg, (new_msg == 1) ? "is" : "are"),
                           sizeof(buf));
            }
        }
    } else {
        /* Checking mail sent to someone else */
        tot = mariadb_mail_count_from(target, player, 0);
        if (tot <= 0) return;

        new_msg = mariadb_mail_count_new_from(target, player);
        if (new_msg < 0) new_msg = 0;

        read_count = mariadb_mail_count_read_from(target, player);
        if (read_count < 0) read_count = 0;

        snprintf(buf, sizeof(buf),
                "|W!++mail:| %s has |Y!+%ld| message%s from you.",
                db[target].cname, tot, (tot == 1) ? "" : "s");

        if (new_msg > 0) {
            safe_append(buf,
                       tprintf(" |G!+%ld| of them %s new.",
                              new_msg, (new_msg == 1) ? "is" : "are"),
                       sizeof(buf));

            if ((tot - read_count - new_msg) > 0) {
                size_t blen = strlen(buf);
                if (blen > 0 && buf[blen - 1] == '.') {
                    buf[blen - 1] = '\0';
                }
                safe_append(buf,
                           tprintf("; |M!+%ld| other%s unread.",
                                  tot - read_count - new_msg,
                                  (tot - read_count - new_msg == 1) ? " is" : "s are"),
                           sizeof(buf));
            }
        } else if ((tot - read_count) > 0) {
            safe_append(buf,
                       tprintf(" %ld of them %s unread.",
                              tot - read_count,
                              (tot - read_count == 1) ? "is" : "are"),
                       sizeof(buf));
        }
    }

    notify(player, buf);
}

/**
 * check_mail_internal - Get count of unread messages
 *
 * Used by other code that needs to know unread count without
 * displaying it to the player.
 *
 * @param player Player to check
 * @param arg2 Optional target player name
 * @return Number of unread messages, or -1 on error
 */
long check_mail_internal(dbref player, char *arg2)
{
    dbref target;

    if (arg2 && *arg2) {
        target = lookup_player(arg2);
        if (target == NOTHING) {
            log_error(tprintf("+mail error: Invalid target in check_mail_internal! (%s)",
                             arg2));
            return -1;
        }
    } else {
        target = player;
    }

    if (target == player) {
        /* Count own unread mail */
        return mariadb_mail_count_unread(target);
    } else {
        /* Count unread mail sent by player to target */
        return mariadb_mail_count_unread_from(target, player);
    }
}

/**
 * info_mail - Display mail system statistics
 *
 * Shows detailed information about the mail system including
 * message counts for both mail and board tables.
 *
 * @param player Player requesting info (typically admin)
 */
void info_mail(dbref player)
{
    long mail_total = 0, mail_deleted = 0, mail_new = 0;
    long mail_unread = 0, mail_read = 0, mail_text = 0;
    long mail_players = 0;
    long board_total = 0, board_deleted = 0, board_text = 0;
    long news_total = 0, news_deleted = 0, news_text = 0;

    notify(player, "|W!+========================================|");
    notify(player, "|W!+      Mail System Information        |");
    notify(player, "|W!+========================================|");
    notify(player, "");

    /* Get mail statistics */
    mariadb_mail_stats(&mail_total, &mail_deleted, &mail_new,
                       &mail_unread, &mail_read, &mail_text,
                       &mail_players);

    /* Get board statistics (excludes news) */
    mariadb_board_stats(&board_total, &board_deleted, &board_text);

    /* Get news statistics */
    mariadb_news_stats(&news_total, &news_deleted, &news_text);

    notify(player, "|C!+Storage Backend:|  MariaDB");
    notify(player, "");

    notify(player, "|C!+Private Mail Statistics:|");
    notify(player, tprintf("  Total messages:      %ld", mail_total));
    notify(player, tprintf("  Deleted (purgable):  %ld", mail_deleted));
    notify(player, tprintf("  New messages:        %ld", mail_new));
    notify(player, tprintf("  Unread messages:     %ld", mail_unread));
    notify(player, tprintf("  Read messages:       %ld", mail_read));
    notify(player, tprintf("  Message text:        %ld bytes (%.2f KB)",
                          mail_text, mail_text / 1024.0));
    notify(player, tprintf("  Players with mail:   %ld", mail_players));
    if (mail_players > 0) {
        notify(player, tprintf("  Avg msgs/player:     %.1f",
                              (float)mail_total / mail_players));
    }
    notify(player, "");

    notify(player, "|C!+Board Statistics:|");
    notify(player, tprintf("  Total posts:         %ld", board_total));
    notify(player, tprintf("  Deleted (purgable):  %ld", board_deleted));
    notify(player, tprintf("  Post text:           %ld bytes (%.2f KB)",
                          board_text, board_text / 1024.0));
    notify(player, "");

    notify(player, "|C!+News Statistics:|");
    notify(player, tprintf("  Total articles:      %ld", news_total));
    notify(player, tprintf("  Deleted (purgable):  %ld", news_deleted));
    notify(player, tprintf("  Article text:        %ld bytes (%.2f KB)",
                          news_text, news_text / 1024.0));
    notify(player, "");

    notify(player, "|C!+Combined:|");
    notify(player, tprintf("  Total entries:       %ld",
                          mail_total + board_total + news_total));
    notify(player, tprintf("  Total text:          %ld bytes (%.2f KB)",
                          mail_text + board_text + news_text,
                          (mail_text + board_text + news_text) / 1024.0));
    notify(player, "");

    /* Show top mail users if admin */
    if (power(player, POW_SECURITY) || power(player, POW_STATS)) {
        notify(player, "|C!+Top Mail Users:|");

        /* Simple bubble sort to find top 5 */
        struct { dbref player; long count; } top[5] = {{NOTHING, 0},
            {NOTHING, 0}, {NOTHING, 0}, {NOTHING, 0}, {NOTHING, 0}};
        int top_count = 0;
        dbref d;

        for (d = 0; d < db_top; d++) {
            if (Typeof(d) == TYPE_PLAYER) {
                long mcount = mariadb_mail_count(d, 1);
                if (mcount > 0) {
                    /* Check if this player belongs in top 5 */
                    for (int j = 0; j < 5; j++) {
                        if (top[j].player == NOTHING || mcount > top[j].count) {
                            /* Shift down */
                            for (int k = 4; k > j; k--) {
                                top[k] = top[k-1];
                            }
                            top[j].player = d;
                            top[j].count = mcount;
                            if (top_count < 5) top_count++;
                            break;
                        }
                    }
                }
            }
        }

        for (int j = 0; j < top_count; j++) {
            notify(player, tprintf("  %d. %-20s %ld messages",
                                  j + 1,
                                  db[top[j].player].cname,
                                  top[j].count));
        }
        notify(player, "");
    }

    /* Recommendations */
    if (mail_total > 0 && mail_deleted > (long)(mail_total * 0.3)) {
        notify(player, "|Y!+Recommendation:| Consider running mail purge - "
                      "30%+ messages are deleted.");
    }

    notify(player, "|W!+========================================|");
}

/**
 * dt_mail - Count total messages for a player
 * Used for statistics and quota calculations
 *
 * @param who Player to count messages for
 * @return Number of messages (including deleted), or -1 if not a player
 */
long dt_mail(dbref who)
{
    long count;

    if (who < 0 || who >= db_top) return -1;
    if (Typeof(who) != TYPE_PLAYER) return -1;

    count = mariadb_mail_count(who, 1);
    return (count >= 0) ? count : -1;
}

#ifdef SHRINK_DB
/**
 * remove_all_mail - Nuclear option: delete all mail from all players
 *
 * WARNING: This is a dangerous database maintenance function!
 * Only used during database shrinking operations.
 */
void remove_all_mail(void)
{
    log_important("remove_all_mail() called - wiping all player mail!");

    mariadb_mail_remove_all();
    mariadb_board_remove_all();

    log_important("remove_all_mail() completed");
}
#endif /* SHRINK_DB */

/* ===================================================================
 * Command Handlers
 * =================================================================== */

/* Command: +mail */
void do_mail(dbref player, char *arg1, char *arg2)
{
    if (Typeof(player) != TYPE_PLAYER || Guest(player)) {
        notify(player, "Sorry, only real players can use mail.");
        return;
    }

    /* Route to appropriate handler */
    if (!string_compare(arg1, "send") || !string_compare(arg1, "sendcode")) {
        /* +mail send=*<player> — enter paste mode for mail */
        int code = !string_compare(arg1, "sendcode") ? 1 : 0;
        dbref recip;

        if (!arg2 || !*arg2) {
            notify(player, "+mail: Specify a recipient. Usage: +mail send=*<player>");
            return;
        }
        recip = lookup_player(arg2);
        if (recip == NOTHING || Typeof(recip) != TYPE_PLAYER) {
            notify(player, "+mail: Unknown player.");
            return;
        }
        /* Check page lock and blacklist */
        if (!could_doit(player, recip, A_LPAGE)) {
            notify(player, "+mail: That player is page-locked against you.");
            return;
        }
        if (!could_doit(real_owner(player), real_owner(recip), A_BLACKLIST) ||
            !could_doit(real_owner(recip), real_owner(player), A_BLACKLIST)) {
            notify(player, "+mail: There's a blacklist in effect.");
            return;
        }
        start_paste_session(player, PASTE_MAIL, recip, NULL, NULL, code);
    }
    else if (!string_compare(arg1, "delete") || !string_compare(arg1, "undelete")) {
        /* Handle delete/undelete — supports single, range (3-7), or "all" */
        long start, end, del;
        int is_undelete = !string_compare(arg1, "undelete");

        if (!arg2 || !*arg2) {
            notify(player, tprintf("+mail: Specify a message number, range, or 'all'. "
                                  "Usage: +mail %s=<#|#-#|all>",
                                  is_undelete ? "undelete" : "delete"));
            return;
        }
        if (!parse_delete_range(arg2, &start, &end)) {
            notify(player, tprintf("+mail: Invalid range '%s'. "
                                  "Use a number, range (3-7), or 'all'.", arg2));
            return;
        }
        del = delete_messages(player, player, start, end, is_undelete);
        notify(player, tprintf("+mail: %ld message%s %sdeleted.", del,
                              del == 1 ? "" : "s",
                              is_undelete ? "un" : ""));
    }
    else if (!string_compare(arg1, "check")) {
        long unread_count = count_unread(player);
        notify(player, tprintf("+mail: You have %ld unread message%s.",
                              unread_count, unread_count == 1 ? "" : "s"));
    }
    else if (!string_compare(arg1, "read")) {
        if (arg2 && *arg2) {
            read_message(player, player, atol(arg2));
        } else {
            notify(player, "+mail: Specify a message number to read.");
        }
    }
    else if (!string_compare(arg1, "purge")) {
        purge_deleted(player, player);
        notify(player, "+mail: Deleted messages purged.");
    }
    else if (!string_compare(arg1, "list") || (!*arg1 && !*arg2)) {
        list_mail_messages(player, player);
    }
    else {
        notify(player, "+mail: Invalid syntax. See 'help +mail'.");
    }
}

/* Command: +board */
void do_board(dbref player, char *arg1, char *arg2)
{
    if (Typeof(player) != TYPE_PLAYER || Guest(player)) {
        notify(player, "Sorry, only real players can use the board.");
        return;
    }

    if (is_banned_from_board(player) &&
        !power(player, POW_BOARD)) {
        notify(player, "+board: You have been banned from the board.");
        return;
    }

    /* Route to appropriate handler */
    if (!string_compare(arg1, "list") || (!*arg1 && !*arg2)) {
        /* +board or +board list = unread only; +board list=all = all posts */
        int unread_only = 1;
        if (arg2 && !string_compare(arg2, "all")) {
            unread_only = 0;
        }
        list_public_messages(player, default_room, unread_only, "+board");
    }
    else if (!string_compare(arg1, "read")) {
        if (arg2 && *arg2) {
            read_public_message(player, default_room, atol(arg2), "+board");
        } else {
            notify(player, "+board: Specify a post number to read.");
        }
    }
    else if (!string_compare(arg1, "post") || !string_compare(arg1, "postcode")) {
        /* +board post=<subject> — enter paste mode for board post */
        int code = !string_compare(arg1, "postcode") ? 1 : 0;
        if (!arg2 || !*arg2) {
            notify(player, "+board: Specify a subject. Usage: +board post=<subject>");
            return;
        }
        start_paste_session(player, PASTE_BOARD, default_room, arg2, NULL, code);
    }
    else if (!string_compare(arg1, "delete")) {
        /* Delete board posts — supports single, range (3-7), or "all"
         * Permission check is per-post in mariadb_board_delete_range:
         * authors can delete their own posts, admins can delete any. */
        long start, end, del;

        if (!arg2 || !*arg2) {
            notify(player, "+board: Specify a post number, range, or 'all'. "
                          "Usage: +board delete=<#|#-#|all>");
            return;
        }
        if (!parse_delete_range(arg2, &start, &end)) {
            notify(player, tprintf("+board: Invalid range '%s'. "
                                  "Use a number, range (3-7), or 'all'.", arg2));
            return;
        }
        del = delete_messages(player, default_room, start, end, 0);
        notify(player, tprintf("+board: %ld post%s deleted.", del,
                              del == 1 ? "" : "s"));
    }
    else if (!string_compare(arg1, "undelete")) {
        /* Undelete board posts — supports single, range (3-7), or "all" */
        long start, end, undel;

        if (!arg2 || !*arg2) {
            notify(player, "+board: Specify a post number, range, or 'all'. "
                          "Usage: +board undelete=<#|#-#|all>");
            return;
        }
        if (!parse_delete_range(arg2, &start, &end)) {
            notify(player, tprintf("+board: Invalid range '%s'. "
                                  "Use a number, range (3-7), or 'all'.", arg2));
            return;
        }
        undel = delete_messages(player, default_room, start, end, 1);
        notify(player, tprintf("+board: %ld post%s undeleted.", undel,
                              undel == 1 ? "" : "s"));
    }
    else if (!string_compare(arg1, "purge")) {
        /* Purge deleted board posts (admin only) */
        long purged;

        if (!Wizard(player) && !power(player, POW_BOARD)) {
            notify(player, "+board: Permission denied.");
            return;
        }
        purged = mariadb_board_purge(default_room, player);
        notify(player, tprintf("+board: %ld deleted post%s purged.", purged,
                              purged == 1 ? "" : "s"));
    }
    else if (!string_compare(arg1, "ban") && power(player, POW_BOARD)) {
        dbref target;
        if (!arg2 || !*arg2) {
            notify(player, "+board: Usage: +board ban=*<player>");
            return;
        }
        target = lookup_player(arg2);
        if (target == NOTHING || Typeof(target) != TYPE_PLAYER) {
            notify(player, "+board: Unknown player.");
            return;
        }
        mariadb_board_ban(target, player);
        notify(player, tprintf("+board: %s has been banned from posting.", db[target].cname));
    }
    else if (!string_compare(arg1, "unban") && power(player, POW_BOARD)) {
        dbref target;
        if (!arg2 || !*arg2) {
            notify(player, "+board: Usage: +board unban=*<player>");
            return;
        }
        target = lookup_player(arg2);
        if (target == NOTHING || Typeof(target) != TYPE_PLAYER) {
            notify(player, "+board: Unknown player.");
            return;
        }
        mariadb_board_unban(target);
        notify(player, tprintf("+board: %s has been unbanned from posting.", db[target].cname));
    }
    else if (!string_compare(arg1, "check")) {
        long unread = mariadb_board_count_unread(player, default_room);
        notify(player, tprintf("+board: You have %ld unread post%s.",
                              unread, unread == 1 ? "" : "s"));
    }
    else {
        notify(player, "+board: Invalid syntax. See 'help +board'.");
    }
}

/* ===================================================================
 * News Command (+news)
 *
 * News articles are stored in the board table with board_room = NEWS_ROOM.
 * Permission model: only Wizards and POW_ANNOUNCE can post/delete/purge.
 * Guests CAN read news (unlike +board which blocks guests entirely).
 * =================================================================== */

/* Command: +news */
void do_news(dbref player, char *arg1, char *arg2)
{
    if (Typeof(player) != TYPE_PLAYER) {
        notify(player, "Only players can use +news.");
        return;
    }

    /* Default: list unread */
    if (!arg1 || !*arg1 || !string_compare(arg1, "list")) {
        int unread_only = 1;
        if (arg2 && !string_compare(arg2, "all")) {
            unread_only = 0;
        }
        list_public_messages(player, NEWS_ROOM, unread_only, "+news");
        return;
    }

    /* Read article by position */
    if (!string_compare(arg1, "read")) {
        if (!arg2 || !*arg2) {
            notify(player, "+news: Specify an article number. Usage: +news read=<#>");
            return;
        }
        read_public_message(player, NEWS_ROOM, atol(arg2), "+news");
        return;
    }

    /* Post article (admin only): +news post=<topic> — enter paste mode */
    if (!string_compare(arg1, "post") || !string_compare(arg1, "postcode")) {
        int code = !string_compare(arg1, "postcode") ? 1 : 0;

        if (!Wizard(player) && !power(player, POW_ANNOUNCE)) {
            notify(player, "+news: Permission denied.");
            return;
        }

        if (!arg2 || !*arg2) {
            notify(player, "+news: Specify a topic. Usage: +news post=<topic>");
            return;
        }

        start_paste_session(player, PASTE_NEWS, NOTHING, arg2, NULL, code);
        return;
    }

    /* Check unread count */
    if (!string_compare(arg1, "check")) {
        long unread = mariadb_news_count_unread(player);
        notify(player, tprintf("+news: You have %ld unread article%s.",
                              unread, unread == 1 ? "" : "s"));
        return;
    }

    /* Delete article(s) (admin only) — supports single, range (3-7), or "all" */
    if (!string_compare(arg1, "remove") || !string_compare(arg1, "delete")) {
        long start, end, del;

        if (!Wizard(player) && !power(player, POW_ANNOUNCE)) {
            notify(player, "+news: Permission denied.");
            return;
        }

        if (!arg2 || !*arg2) {
            notify(player, "+news: Specify an article number, range, or 'all'. "
                          "Usage: +news delete=<#|#-#|all>");
            return;
        }

        if (!parse_delete_range(arg2, &start, &end)) {
            notify(player, tprintf("+news: Invalid range '%s'. "
                                  "Use a number, range (3-7), or 'all'.", arg2));
            return;
        }

        del = mariadb_news_delete_range(start, end, 0, player);
        if (del > 0) {
            notify(player, tprintf("+news: %ld article%s deleted.", del,
                                  del == 1 ? "" : "s"));
            log_important(tprintf("NEWS: %s(#%" DBREF_FMT ") deleted %ld article%s",
                                 db[player].name, player, del,
                                 del == 1 ? "" : "s"));
        } else {
            notify(player, "+news: No articles found in that range.");
        }
        return;
    }

    /* Undelete article(s) (admin only) */
    if (!string_compare(arg1, "undelete")) {
        long start, end, undel;

        if (!Wizard(player) && !power(player, POW_ANNOUNCE)) {
            notify(player, "+news: Permission denied.");
            return;
        }

        if (!arg2 || !*arg2) {
            notify(player, "+news: Specify an article number, range, or 'all'. "
                          "Usage: +news undelete=<#|#-#|all>");
            return;
        }

        if (!parse_delete_range(arg2, &start, &end)) {
            notify(player, tprintf("+news: Invalid range '%s'. "
                                  "Use a number, range (3-7), or 'all'.", arg2));
            return;
        }

        undel = mariadb_news_delete_range(start, end, 1, player);
        notify(player, tprintf("+news: %ld article%s undeleted.", undel,
                              undel == 1 ? "" : "s"));
        return;
    }

    /* Purge deleted articles (admin only) */
    if (!string_compare(arg1, "purge")) {
        long purged;

        if (!Wizard(player) && !power(player, POW_ANNOUNCE)) {
            notify(player, "+news: Permission denied.");
            return;
        }

        purged = mariadb_news_purge(player);
        notify(player, tprintf("+news: %ld deleted article%s purged.", purged,
                              purged == 1 ? "" : "s"));
        return;
    }

    notify(player, "+news: Unknown subcommand. See 'help +news'.");
}

/* ===================================================================
 * News Connection Check
 * =================================================================== */

/**
 * check_news - Notify player of unread news on connect
 *
 * Called from descriptor_mgmt.c after announce_connect().
 *
 * @param player Player who just connected
 */
void check_news(dbref player)
{
    long unread = mariadb_news_count_unread(player);

    if (unread > 0) {
        notify(player, tprintf("|Y!+You have %ld unread news article%s. Type '+news' to read.|",
                              unread, unread == 1 ? "" : "s"));
    }
}
