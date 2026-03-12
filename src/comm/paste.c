/* paste.c - Multi-line text pasting system
 *
 * Originally from MAZE pre-release code (c) 1998 itsme
 * Modernized and secured for deMUSE 2.0
 *
 * This module provides multi-line text input for:
 * - Object attributes (@paste <object>/<attr>)
 * - Room descriptions (@paste, @paste <room>)
 * - Mail messages (+mail send=*<player>)
 * - Board posts (+board post=<subject>)
 * - News articles (+news post=<topic>)
 * - Channel paste (+channel paste=<channel>)
 *
 * Usage:
 *   @paste <target>[/<attribute>]   - Paste to attribute
 *   @pastecode <target>[/<attr>]    - Paste preserving whitespace
 *   <enter lines of text>
 *   .  (period on line by itself to end)
 *   @pasteabort (to cancel)
 *
 * The start_paste_session() API is used by +mail, +board, +news,
 * and +channel to enter paste mode from their command handlers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "interface.h"
#include "mariadb_channel.h"
#include "mariadb_mail.h"
#include "mariadb_board.h"

void do_paste_int(dbref, char *, char *, int);

/* ===================================================================
 * Data Structures
 * =================================================================== */

/* Single line in a paste buffer */
typedef struct pastey {
    char *str;
    struct pastey *next;
} PASTEY;

/* Paste session state
 *
 * paste_type_t enum is defined in externs.h:
 *   PASTE_ATTR, PASTE_ROOM, PASTE_MAIL, PASTE_BOARD,
 *   PASTE_NEWS, PASTE_CHANNEL
 */
typedef struct paste_struct {
    dbref player;           /* Player doing the pasting */
    dbref target;           /* Target object/player/board_room (NOTHING for channels/news) */
    paste_type_t type;      /* What kind of paste session */
    char *channel_name;     /* Channel name for PASTE_CHANNEL (NULL otherwise) */
    char *subject;          /* Subject/topic for mail/board/news (NULL if needs_subject) */
    int needs_subject;      /* If 1, first line entered becomes subject */
    int code;               /* 0=strip leading spaces, 1=preserve (code mode) */
    PASTEY *paste;          /* Linked list of pasted lines */
    ATTR *attr;             /* Target attribute for PASTE_ATTR (NULL otherwise) */
    struct paste_struct *next;
} PASTE;

/* Global paste stack - external linkage for compatibility */
PASTE *paste_stack = NULL;

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static PASTE *find_paste(dbref player);
static void add_to_stack(dbref player, dbref target, paste_type_t type,
                         ATTR *attr, int code);
static void do_end_paste(dbref player);
static char *strip_leading_spaces(const char *input);
static void free_paste_lines(PASTEY *lines);
static void assemble_paste_buffer(PASTE *p, char *buffer, size_t bufsize);
static const char *paste_type_name(paste_type_t type);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Find paste session for a player
 */
static PASTE *find_paste(dbref player)
{
    PASTE *p;

    for (p = paste_stack; p; p = p->next) {
        if (p->player == player) {
            return p;
        }
    }

    return NULL;
}

/**
 * Strip leading whitespace from a string
 */
static char *strip_leading_spaces(const char *input)
{
    static char buffer[MAX_COMMAND_LEN];
    const char *ptr = input;

    if (!input) {
        return "";
    }

    /* Skip leading spaces */
    while (*ptr && *ptr == ' ') {
        ptr++;
    }

    /* Copy to static buffer */
    strncpy(buffer, ptr, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    return buffer;
}

/**
 * Free all lines in a paste buffer
 */
static void free_paste_lines(PASTEY *lines)
{
    PASTEY *current = lines;
    PASTEY *next;

    while (current) {
        next = current->next;
        if (current->str) {
            SMART_FREE(current->str);
        }
        SMART_FREE(current);
        current = next;
    }
}

/**
 * Assemble paste lines into a single buffer with newline separators
 */
static void assemble_paste_buffer(PASTE *p, char *buffer, size_t bufsize)
{
    PASTEY *line;
    char *text;

    buffer[0] = '\0';

    for (line = p->paste; line; line = line->next) {
        text = (p->code == 1) ? line->str : strip_leading_spaces(line->str);
        if (text && *text) {
            strncat(buffer, text, bufsize - strlen(buffer) - 2);
            if (line->next) {
                strncat(buffer, "\n", bufsize - strlen(buffer) - 1);
            }
        }
    }
}

/**
 * Return human-readable name for a paste type
 */
static const char *paste_type_name(paste_type_t type)
{
    switch (type) {
        case PASTE_ATTR:    return "ATTR";
        case PASTE_ROOM:    return "ROOM";
        case PASTE_MAIL:    return "MAIL";
        case PASTE_BOARD:   return "BOARD";
        case PASTE_NEWS:    return "NEWS";
        case PASTE_CHANNEL: return "CHANNEL";
        default:            return "UNKNOWN";
    }
}

/* ===================================================================
 * Paste Session Management
 * =================================================================== */

/**
 * Check if a player is currently pasting
 */
char is_pasting(dbref player)
{
    return (find_paste(player) != NULL);
}

/**
 * Create a new paste session on the stack
 */
static void add_to_stack(dbref player, dbref target, paste_type_t type,
                         ATTR *attr, int code)
{
    PASTE *new_paste;

    SAFE_MALLOC(new_paste, PASTE, 1);
    if (!new_paste) {
        log_error("Out of memory in add_to_stack!");
        notify(player, "System error: out of memory.");
        return;
    }

    new_paste->player = player;
    new_paste->target = target;
    new_paste->type = type;
    new_paste->channel_name = NULL;
    new_paste->subject = NULL;
    new_paste->needs_subject = 0;
    new_paste->attr = attr;
    new_paste->code = code;
    new_paste->paste = NULL;
    new_paste->next = paste_stack;

    paste_stack = new_paste;
}

/**
 * Remove and free a paste session
 */
void remove_paste(dbref player)
{
    PASTE *p, *prev = NULL;

    for (p = paste_stack; p; prev = p, p = p->next) {
        if (p->player == player) {
            /* Unlink from stack */
            if (prev) {
                prev->next = p->next;
            } else {
                paste_stack = p->next;
            }

            /* Free all lines */
            free_paste_lines(p->paste);

            /* Free channel name and subject if set */
            SMART_FREE(p->channel_name);
            SMART_FREE(p->subject);

            /* Free the paste structure */
            SMART_FREE(p);
            return;
        }
    }
}

/* ===================================================================
 * Public Paste Session API
 * =================================================================== */

/**
 * start_paste_session - Entry point for +mail, +board, +news, +channel
 *
 * Creates a paste session and prompts the user for input.
 * Called from command handlers after they have validated permissions
 * and resolved targets.
 *
 * PARAMETERS:
 *   player       - Player entering paste mode
 *   type         - PASTE_MAIL, PASTE_BOARD, PASTE_NEWS, or PASTE_CHANNEL
 *   target       - Recipient dbref (mail) or board_room (board), NOTHING otherwise
 *   subject      - Subject/topic string, or NULL to prompt for it
 *   channel_name - Channel name for PASTE_CHANNEL, NULL otherwise
 *   code         - 0=normal, 1=code mode (preserve whitespace)
 *
 * RETURNS: 1 on success, 0 on failure
 */
int start_paste_session(dbref player, paste_type_t type, dbref target,
                        const char *subject, const char *channel_name,
                        int code)
{
    PASTE *p;

    /* Clear any existing paste session */
    if (is_pasting(player)) {
        notify(player, "Clearing old paste, starting fresh.");
        remove_paste(player);
    }

    /* Create the session */
    add_to_stack(player, target, type, NULL, code);
    p = find_paste(player);
    if (!p) {
        return 0;
    }

    /* Store subject if provided */
    if (subject && *subject) {
        size_t slen = strlen(subject) + 1;
        SAFE_MALLOC(p->subject, char, slen);
        if (p->subject) {
            strncpy(p->subject, subject, slen);
            p->subject[slen - 1] = '\0';
        }
    }

    /* Store channel name if provided */
    if (channel_name && *channel_name) {
        size_t clen = strlen(channel_name) + 1;
        SAFE_MALLOC(p->channel_name, char, clen);
        if (p->channel_name) {
            strncpy(p->channel_name, channel_name, clen);
            p->channel_name[clen - 1] = '\0';
        }
    }

    /* For mail with no subject, prompt for it on first line */
    if (type == PASTE_MAIL && (!subject || !*subject)) {
        p->needs_subject = 1;
        notify(player, "Subject:");
    } else {
        notify(player, "Enter text. End with '.' on a blank line, or '@pasteabort' to cancel.");
    }

    return 1;
}

/* ===================================================================
 * @paste Commands (attribute and room paste only)
 * =================================================================== */

/**
 * Start a paste session (normal mode)
 */
void do_paste(dbref player, char *arg1, char *arg2)
{
    do_paste_int(player, arg1, arg2, 0);
}

/**
 * Start a paste session (code mode - preserves formatting)
 */
void do_pastecode(dbref player, char *arg1, char *arg2)
{
    do_paste_int(player, arg1, arg2, 1);
}

/**
 * Internal paste initialization for @paste/@pastecode
 *
 * Handles attribute paste and room paste only.
 * Mail, board, news, and channel paste use start_paste_session() instead.
 */
void do_paste_int(dbref player, char *arg1, char *arg2, int code)
{
    dbref target = NOTHING;
    ATTR *attr = NULL;
    paste_type_t type = PASTE_ROOM;
    char *slash_pos;

    (void)arg2;  /* No longer used — mail/channel handled by + commands */

    /* Check if already pasting */
    if (is_pasting(player)) {
        notify(player, "Clearing old paste, starting fresh.");
        remove_paste(player);
    }

    /* Default to current location */
    if (!*arg1) {
        target = db[player].location;
    }
    /* Handle attribute/object paste */
    else {
        /* Check for attribute specification */
        slash_pos = strchr(arg1, '/');
        if (slash_pos) {
            *slash_pos++ = '\0';
        }

        /* Match the target object */
        init_match(player, arg1, NOTYPE);
        match_me();
        match_here();
        match_neighbor();
        match_possession();
        match_exit();
        match_absolute();
        match_player(NOTHING, NULL);

        target = noisy_match_result();
        if (target == NOTHING) {
            return;
        }

        /* If attribute specified, look it up */
        if (slash_pos) {
            attr = atr_str(player, target, slash_pos);
            if (!attr) {
                notify(player, "No such attribute.");
                return;
            }
            type = PASTE_ATTR;
        }
    }

    /* Validate target exists */
    if (!GoodObject(target)) {
        notify(player, "Invalid paste target.");
        return;
    }

    /* For attribute pastes, check modify permission */
    if (attr && !controls(player, target, POW_MODIFY)) {
        notify(player, perm_denied());
        return;
    }

    /* Create paste session */
    add_to_stack(player, target, type, attr, code);
    notify(player, "Enter lines to be pasted. End with '.' or type '@pasteabort'.");
}

/* ===================================================================
 * Paste Line Input
 * =================================================================== */

/**
 * Add a line to the current paste
 *
 * Called from game.c process_command() when is_pasting() is true.
 */
void add_more_paste(dbref player, char *str)
{
    PASTE *p;
    PASTEY *new_line, *current, *prev = NULL;
    size_t len;

    /* Check for end-of-paste marker */
    if (!strcmp(str, ".")) {
        do_end_paste(player);
        remove_paste(player);
        return;
    }

    /* Check for abort */
    if (!string_compare("@pasteabort", str)) {
        remove_paste(player);
        notify(player, "Aborted.");
        return;
    }

    /* Find paste session */
    p = find_paste(player);
    if (!p) {
        return;
    }

    /* Handle subject prompt — first line becomes the subject */
    if (p->needs_subject) {
        if (!str || !*str) {
            /* Empty subject line — use default */
            size_t dlen = strlen("(no subject)") + 1;
            SAFE_MALLOC(p->subject, char, dlen);
            if (p->subject) {
                strncpy(p->subject, "(no subject)", dlen);
                p->subject[dlen - 1] = '\0';
            }
        } else {
            size_t slen = strlen(str) + 1;
            SAFE_MALLOC(p->subject, char, slen);
            if (p->subject) {
                strncpy(p->subject, str, slen);
                p->subject[slen - 1] = '\0';
            }
        }
        p->needs_subject = 0;
        notify(player, "Enter text. End with '.' on a blank line, or '@pasteabort' to cancel.");
        return;
    }

    /* For attribute pastes, concatenate to single string */
    if (p->attr) {
        if (!p->paste) {
            SAFE_MALLOC(p->paste, PASTEY, 1);
            if (!p->paste) {
                log_error("Out of memory in add_more_paste!");
                notify(player, "System error: out of memory.");
                return;
            }
            SAFE_MALLOC(p->paste->str, char, 1);
            if (!p->paste->str) {
                log_error("Out of memory in add_more_paste!");
                notify(player, "System error: out of memory.");
                SMART_FREE(p->paste);
                p->paste = NULL;
                return;
            }
            p->paste->str[0] = '\0';
            p->paste->next = NULL;
        }

        /* Calculate new length */
        len = strlen(p->paste->str) + strlen(str) + 1;

        /* Allocate new buffer, copy old + new, free old */
        char *new_str;
        SAFE_MALLOC(new_str, char, len);
        if (!new_str) {
            log_error("Out of memory in add_more_paste!");
            notify(player, "System error: out of memory.");
            return;
        }

        snprintf(new_str, len, "%s%s", p->paste->str, str);
        SMART_FREE(p->paste->str);
        p->paste->str = new_str;
        return;
    }

    /* For other pastes, add as separate line */
    SAFE_MALLOC(new_line, PASTEY, 1);
    if (!new_line) {
        log_error("Out of memory in add_more_paste!");
        notify(player, "System error: out of memory.");
        return;
    }

    len = strlen(str) + 1;
    SAFE_MALLOC(new_line->str, char, len);
    if (!new_line->str) {
        SMART_FREE(new_line);
        log_error("Out of memory in add_more_paste!");
        notify(player, "System error: out of memory.");
        return;
    }
    strncpy(new_line->str, str, len);
    new_line->str[len - 1] = '\0';

    new_line->next = NULL;

    /* Add to end of list */
    if (!p->paste) {
        p->paste = new_line;
    } else {
        for (current = p->paste; current; prev = current, current = current->next)
            ;
        prev->next = new_line;
    }
}

/* ===================================================================
 * Paste Completion
 * =================================================================== */

/**
 * Complete and send the paste
 *
 * Dispatches to the appropriate handler based on paste type.
 */
static void do_end_paste(dbref player)
{
    PASTE *p;
    PASTEY *line;
    char *text;
    char header[256];
    char footer[256];
    char buffer[65536];

    p = find_paste(player);
    if (!p) {
        return;
    }

    /* Handle attribute paste */
    if (p->type == PASTE_ATTR) {
        if (!GoodObject(p->target)) {
            notify(player, "Paste target no longer exists.");
            return;
        }
        if (!p->paste || !p->paste->str || !*p->paste->str) {
            atr_clr(p->target, p->attr);
            notify(player, tprintf("%s - Cleared.", db[p->target].cname));
        } else {
            atr_add(p->target, p->attr, p->paste->str);
            notify(player, tprintf("%s - Set.", db[p->target].cname));
        }
        return;
    }

    /* Handle mail paste */
    if (p->type == PASTE_MAIL) {
        if (!GoodObject(p->target) || Typeof(p->target) != TYPE_PLAYER) {
            notify(player, "+mail: Recipient no longer valid.");
            return;
        }
        assemble_paste_buffer(p, buffer, sizeof(buffer));
        if (!*buffer) {
            notify(player, "+mail: Empty message not sent.");
            return;
        }
        post_message(player, p->target,
                     p->subject ? p->subject : "(no subject)",
                     buffer, MSG_PRIVATE, MF_NEW);
        notify(player, tprintf("+mail: Message sent to %s.", db[p->target].cname));
        return;
    }

    /* Handle board paste */
    if (p->type == PASTE_BOARD) {
        assemble_paste_buffer(p, buffer, sizeof(buffer));
        if (!*buffer) {
            notify(player, "+board: Empty post not submitted.");
            return;
        }
        post_message(player, p->target,
                     p->subject ? p->subject : "(no subject)",
                     buffer, MSG_BOARD, MF_READ);
        notify(player, "+board: Message posted.");
        return;
    }

    /* Handle news paste — uses post_message with MSG_NEWS */
    if (p->type == PASTE_NEWS) {
        assemble_paste_buffer(p, buffer, sizeof(buffer));
        if (!*buffer) {
            notify(player, "+news: Empty article not posted.");
            return;
        }
        post_message(player, NOTHING,
                     p->subject ? p->subject : "(untitled)",
                     buffer, MSG_NEWS, 0);
        notify(player, tprintf("+news: Article posted: %s",
                              p->subject ? p->subject : "(untitled)"));
        log_important(tprintf("NEWS: %s(#%" DBREF_FMT ") posted article: %s",
                             db[player].name, player,
                             p->subject ? p->subject : "(untitled)"));
        return;
    }

    /* Handle channel paste */
    if (p->type == PASTE_CHANNEL && p->channel_name) {
        snprintf(header, sizeof(header),
                 "|W+----- ||C!+Begin paste from |%s |W+-----|",
                 db[player].cname);
        snprintf(footer, sizeof(footer),
                 "|W+----- ||C!+End paste from |%s |W+-----|",
                 db[player].cname);

        com_send_as(p->channel_name, header, player);
        for (line = p->paste; line; line = line->next) {
            text = (p->code == 1) ? line->str : strip_leading_spaces(line->str);
            if (text && *text) {
                com_send_as(p->channel_name, text, player);
            }
        }
        com_send_as(p->channel_name, footer, player);
        return;
    }

    /* Handle room/player paste (PASTE_ROOM — legacy @paste behavior) */
    if (p->type == PASTE_ROOM && GoodObject(p->target)) {
        snprintf(header, sizeof(header),
                 "|W+----- ||C!+Begin @paste text from |%s |W+-----|",
                 db[player].cname);
        snprintf(footer, sizeof(footer),
                 "|W+----- ||C!+End @paste text from |%s |W+-----|",
                 db[player].cname);

        if (Typeof(p->target) == TYPE_ROOM) {
            notify(player, header);
            notify_in(p->target, player, header);
        } else if (Typeof(p->target) == TYPE_PLAYER) {
            notify(p->target, header);
        }

        for (line = p->paste; line; line = line->next) {
            text = (p->code == 1) ? line->str : strip_leading_spaces(line->str);
            if (text && *text) {
                if (Typeof(p->target) == TYPE_ROOM) {
                    notify(player, text);
                    notify_in(p->target, player, text);
                } else if (Typeof(p->target) == TYPE_PLAYER) {
                    notify(p->target, text);
                }
            }
        }

        if (Typeof(p->target) == TYPE_ROOM) {
            notify(player, footer);
            notify_in(p->target, player, footer);
        } else if (Typeof(p->target) == TYPE_PLAYER) {
            notify(p->target, footer);
            notify(player, tprintf("@paste text sent to %s.",
                                  unparse_object(player, p->target)));
        }
        return;
    }
}

/* ===================================================================
 * Administrative Commands
 * =================================================================== */

/**
 * Show paste statistics (admin command)
 */
void do_pastestats(dbref player, char *arg)
{
    PASTE *p;
    PASTEY *line;
    int paste_num = 0;
    int total_pastes = 0;
    size_t size;
    int requested;
    char target_desc[256];

    /* Check permission */
    if (!power(player, POW_REMOTE)) {
        notify(player, perm_denied());
        return;
    }

    /* Count total pastes */
    for (p = paste_stack; p; p = p->next) {
        total_pastes++;
    }

    if (total_pastes == 0) {
        notify(player, "There are no paste sessions active.");
        return;
    }

    /* List all pastes if no argument */
    if (!*arg) {
        for (p = paste_stack; p; p = p->next) {
            paste_num++;

            /* Calculate size */
            size = 0;
            for (line = p->paste; line; line = line->next) {
                size += strlen(line->str) + 1;
            }

            /* Build target description */
            if (p->channel_name) {
                snprintf(target_desc, sizeof(target_desc),
                         "%s %s", paste_type_name(p->type), p->channel_name);
            } else if (p->target == NOTHING) {
                snprintf(target_desc, sizeof(target_desc),
                         "%s", paste_type_name(p->type));
            } else if (GoodObject(p->target)) {
                strncpy(target_desc, db[p->target].cname, sizeof(target_desc) - 1);
                target_desc[sizeof(target_desc) - 1] = '\0';
                if (p->attr) {
                    strncat(target_desc, "/", sizeof(target_desc) - strlen(target_desc) - 1);
                    strncat(target_desc, p->attr->name, sizeof(target_desc) - strlen(target_desc) - 1);
                }
            } else {
                snprintf(target_desc, sizeof(target_desc),
                         "%s (invalid)", paste_type_name(p->type));
            }

            notify(player, tprintf("%d: %s -> %s [%s]: %zu bytes",
                                  paste_num, db[p->player].cname, target_desc,
                                  paste_type_name(p->type), size));
        }
        return;
    }

    /* Show specific paste */
    requested = (int)strtol(arg, NULL, 10);
    if (requested < 1 || requested > total_pastes) {
        notify(player, tprintf("Valid pastes: 1 - %d", total_pastes));
        return;
    }

    /* Find requested paste */
    paste_num = 0;
    for (p = paste_stack; p; p = p->next) {
        paste_num++;
        if (paste_num == requested) {
            break;
        }
    }

    if (!p) {
        return;
    }

    /* Build target description */
    if (p->channel_name) {
        snprintf(target_desc, sizeof(target_desc),
                 "%s %s", paste_type_name(p->type), p->channel_name);
    } else if (p->target == NOTHING) {
        snprintf(target_desc, sizeof(target_desc),
                 "%s", paste_type_name(p->type));
    } else if (GoodObject(p->target)) {
        strncpy(target_desc, db[p->target].cname, sizeof(target_desc) - 1);
        target_desc[sizeof(target_desc) - 1] = '\0';
        if (p->attr) {
            strncat(target_desc, "/", sizeof(target_desc) - strlen(target_desc) - 1);
            strncat(target_desc, p->attr->name, sizeof(target_desc) - strlen(target_desc) - 1);
        }
    } else {
        snprintf(target_desc, sizeof(target_desc),
                 "%s (invalid)", paste_type_name(p->type));
    }

    /* Display paste contents */
    notify(player, tprintf("[%s] %s", paste_type_name(p->type), target_desc));
    if (p->subject) {
        notify(player, tprintf("Subject: %s", p->subject));
    }
    notify(player, "|B+------ ||W+BEGIN ||B+------|");

    for (line = p->paste; line; line = line->next) {
        notify(player, line->str);
    }

    notify(player, "|B+------  ||W+END  ||B+------|");
}
