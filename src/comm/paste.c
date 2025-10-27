/* paste.c - Multi-line text pasting system
 * 
 * Originally from MAZE pre-release code (c) 1998 itsme
 * Modernized and secured for deMUSE 2.0
 * 
 * This module allows users to paste multi-line text to:
 * - Object attributes
 * - Communication channels
 * - Mail messages
 * - Room descriptions
 * 
 * Usage:
 *   @paste <target>[/<attribute>]
 *   @paste channel <channel-name>
 *   @paste mail <player>
 *   <enter lines of text>
 *   .  (period on line by itself to end)
 *   @pasteabort (to cancel)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "interface.h"

void do_paste_int(dbref, char *, char *, int);

/* ===================================================================
 * Data Structures
 * =================================================================== */

/* Single line in a paste buffer */
typedef struct pastey {
    char *str;
    struct pastey *next;
} PASTEY;

/* Paste session state */
typedef struct paste_struct {
    dbref player;           /* Player doing the pasting */
    dbref target;           /* Target object/channel/player */
    int flag;               /* 0=normal, 1=mail */
    int code;               /* 0=strip leading spaces, 1=preserve */
    PASTEY *paste;          /* Linked list of pasted lines */
    ATTR *attr;             /* Target attribute (if any) */
    struct paste_struct *next;
} PASTE;

/* Global paste stack - external linkage for compatibility */
PASTE *paste_stack = NULL;

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static PASTE *find_paste(dbref player);
static void add_to_stack(dbref player, dbref target, ATTR *attr, int code, int flag);
static void do_end_paste(dbref player);
static char *strip_leading_spaces(const char *input);
static void free_paste_lines(PASTEY *lines);
static int validate_paste_target(dbref player, dbref target, ATTR *attr, int flag);

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
            SAFE_FREE(current->str);
        }
        SAFE_FREE(current);
        current = next;
    }
}

/**
 * Validate that a paste target is accessible
 */
static int validate_paste_target(dbref player, dbref target, ATTR *attr, int flag)
{
    /* Check target exists */
    if (!GoodObject(target)) {
        notify(player, "Invalid paste target.");
        return 0;
    }
    
    /* For attribute pastes, check modify permission */
    if (attr && !controls(player, target, POW_MODIFY)) {
        notify(player, perm_denied());
        return 0;
    }
    
    /* For mail, check page lock */
    if (flag == 1) {
        if (!could_doit(player, target, A_LPAGE)) {
            notify(player, "That player is page-locked against you.");
            return 0;
        }
        
#ifdef USE_BLACKLIST
        /* Check blacklist */
        if (!could_doit(real_owner(player), real_owner(target), A_BLACKLIST) ||
            !could_doit(real_owner(target), real_owner(player), A_BLACKLIST)) {
            notify(player, "There's a blacklist in effect.");
            return 0;
        }
#endif
    }
    
    return 1;
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
 * Create a new paste session
 */
static void add_to_stack(dbref player, dbref target, ATTR *attr, int code, int flag)
{
    PASTE *new_paste;
    
//    new_paste = (PASTE *)malloc(sizeof(PASTE));
    SAFE_MALLOC(new_paste, PASTE, 1);
    if (!new_paste) {
        log_error("Out of memory in add_to_stack!");
        notify(player, "System error: out of memory.");
        return;
    }
    
    new_paste->player = player;
    new_paste->target = target;
    new_paste->attr = attr;
    new_paste->code = code;
    new_paste->flag = flag;
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
            
            /* Free the paste structure */
            SAFE_FREE(p);
            return;
        }
    }
}

/* ===================================================================
 * Paste Commands
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
 * Internal paste initialization
 */
void do_paste_int(dbref player, char *arg1, char *arg2, int code)
{
    dbref target = NOTHING;
    ATTR *attr = NULL;
    int flag = 0;
    char *slash_pos;
    
    /* Check if already pasting */
    if (is_pasting(player)) {
        notify(player, "Clearing old paste, starting fresh.");
        remove_paste(player);
    }
    
    /* Default to current location */
    if (!*arg1) {
        target = db[player].location;
    }
    /* Handle channel paste */
    else if (*arg2 && string_prefix("channel", arg1)) {
        target = lookup_channel(arg2);
        if (target == NOTHING) {
            notify(player, "@paste channel: Channel doesn't exist.");
            return;
        }
        if (is_on_channel(player, db[target].name) < 0) {
            notify(player, "@paste channel: You're not on that channel.");
            return;
        }
    }
    /* Handle mail paste */
    else if (*arg2 && string_prefix("mail", arg1)) {
        init_match(player, arg2, NOTYPE);
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
        
        flag = 1;  /* Mail flag */
    }
    /* Handle attribute paste */
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
        }
    }
    
    /* Validate target */
    if (!validate_paste_target(player, target, attr, flag)) {
        return;
    }
    
    /* Create paste session */
    add_to_stack(player, target, attr, code, flag);
    
    /* Notify user */
    if (flag == 1) {
        notify(player, "Enter mail message. End with '.' or type '@pasteabort'.");
    } else {
        notify(player, "Enter lines to be pasted. End with '.' or type '@pasteabort'.");
    }
}

/**
 * Add a line to the current paste
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
        notify(player, "@paste aborted.");
        return;
    }
    
    /* Find paste session */
    p = find_paste(player);
    if (!p) {
        return;
    }
    
    /* For attribute pastes, concatenate to single string */
    if (p->attr) {
        if (!p->paste) {
//            p->paste = (PASTEY *)malloc(sizeof(PASTEY));
            SAFE_MALLOC(p->paste, PASTEY, 1);
            if (!p->paste) {
                log_error("Out of memory in add_more_paste!");
                notify(player, "System error: out of memory.");
                return;
            }
            p->paste->str = stralloc("");
            p->paste->next = NULL;
        }
        
        /* Calculate new length */
        len = strlen(p->paste->str) + strlen(str) + 1;
        
        /* Reallocate and append */
        p->paste->str = (char *)realloc(p->paste->str, len);
        if (!p->paste->str) {
            log_error("Out of memory in add_more_paste!");
            notify(player, "System error: out of memory.");
            return;
        }
        
        strcat(p->paste->str, str);
        return;
    }
    
    /* For other pastes, add as separate line */
//    new_line = (PASTEY *)malloc(sizeof(PASTEY));
    SAFE_MALLOC(new_line,PASTEY,1);
    if (!new_line) {
        log_error("Out of memory in add_more_paste!");
        notify(player, "System error: out of memory.");
        return;
    }
    
    new_line->str = stralloc(str);
    if (!new_line->str) {
        SAFE_FREE(new_line);
        log_error("Out of memory in add_more_paste!");
        notify(player, "System error: out of memory.");
        return;
    }
    
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

/**
 * Complete and send the paste
 */
static void do_end_paste(dbref player)
{
    PASTE *p;
    PASTEY *line;
    char *text;
    char header[256];
    char footer[256];
    char mail_buffer[65536];
    
    p = find_paste(player);
    if (!p) {
        return;
    }
    
    /* Handle attribute paste */
    if (p->attr) {
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
    if (p->flag == 1) {
        mail_buffer[0] = '\0';
        
        for (line = p->paste; line; line = line->next) {
            text = (p->code == 1) ? line->str : strip_leading_spaces(line->str);
            
            if (text && *text) {
                strncat(mail_buffer, text, sizeof(mail_buffer) - strlen(mail_buffer) - 2);
                if (line->next) {
                    strcat(mail_buffer, "\n");
                }
            }
        }
        
        do_mail(player, tprintf("#%d", p->target), mail_buffer);
        return;
    }
    
    /* Handle channel/room paste */
    snprintf(header, sizeof(header),
             "|W+----- ||C!+Begin @paste text from |%s |W+-----|",
             db[player].cname);
    snprintf(footer, sizeof(footer),
             "|W+----- ||C!+End @paste text from |%s |W+-----|",
             db[player].cname);
    
    /* Send header */
    if (Typeof(p->target) == TYPE_CHANNEL) {
        com_send_as(db[p->target].name, header, player);
    } else if (Typeof(p->target) == TYPE_ROOM) {
        notify(player, header);
        notify_in(p->target, player, header);
    } else if (Typeof(p->target) == TYPE_PLAYER) {
        notify(p->target, header);
    }
    
    /* Send lines */
    for (line = p->paste; line; line = line->next) {
        text = (p->code == 1) ? line->str : strip_leading_spaces(line->str);
        
        if (text && *text) {
            if (Typeof(p->target) == TYPE_CHANNEL) {
                com_send_as(db[p->target].name, text, player);
            } else if (Typeof(p->target) == TYPE_ROOM) {
                notify(player, text);
                notify_in(p->target, player, text);
            } else if (Typeof(p->target) == TYPE_PLAYER) {
                notify(p->target, text);
            }
        }
    }
    
    /* Send footer */
    if (Typeof(p->target) == TYPE_CHANNEL) {
        com_send_as(db[p->target].name, footer, player);
    } else if (Typeof(p->target) == TYPE_ROOM) {
        notify(player, footer);
        notify_in(p->target, player, footer);
    } else if (Typeof(p->target) == TYPE_PLAYER) {
        notify(p->target, footer);
        notify(player, tprintf("@paste text sent to %s.",
                              unparse_object(player, p->target)));
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
    int size;
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
        notify(player, "There are no @paste texts being created.");
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
            if (p->target == NOTHING) {
                strcpy(target_desc, "NOTHING");
            } else {
                strcpy(target_desc, db[p->target].cname);
                if (p->attr) {
                    strncat(target_desc, "/", sizeof(target_desc) - strlen(target_desc) - 1);
                    strncat(target_desc, p->attr->name, sizeof(target_desc) - strlen(target_desc) - 1);
                }
                if (Typeof(p->target) == TYPE_CHANNEL) {
                    snprintf(target_desc, sizeof(target_desc), "CHANNEL %s", db[p->target].cname);
                }
            }
            
            notify(player, tprintf("%d: %s -> %s: %d bytes",
                                  paste_num, db[p->player].cname, target_desc, size));
        }
        return;
    }
    
    /* Show specific paste */
    requested = atoi(arg);
    if (requested < 1 || requested > total_pastes) {
        notify(player, tprintf("Valid @pastes: 1 - %d", total_pastes));
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
    if (p->target == NOTHING) {
        strcpy(target_desc, "NOTHING");
    } else {
        strcpy(target_desc, db[p->target].cname);
        if (p->attr) {
            strncat(target_desc, "/", sizeof(target_desc) - strlen(target_desc) - 1);
            strncat(target_desc, p->attr->name, sizeof(target_desc) - strlen(target_desc) - 1);
        }
        if (Typeof(p->target) == TYPE_CHANNEL) {
            snprintf(target_desc, sizeof(target_desc), "CHANNEL %s", db[p->target].cname);
        }
    }
    
    /* Display paste contents */
    notify(player, target_desc);
    notify(player, "|B+------ ||W+BEGIN ||B+------|");
    
    for (line = p->paste; line; line = line->next) {
        notify(player, line->str);
    }
    
    notify(player, "|B+------  ||W+END  ||B+------|");
}
