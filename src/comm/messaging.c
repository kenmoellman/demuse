/* messaging.c - Unified mail and board system
 * 
 * This file merges the functionality of mail.c and board.c,
 * modernizes to ANSI C, and fixes security issues.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "externs.h"
#include "db.h"

typedef long mdbref;

/* Message flags */
#define MF_DELETED  0x01
#define MF_READ     0x02
#define MF_NEW      0x04
#define MF_BOARD    0x08  /* Message is board post vs private mail */

#define NOMAIL ((mdbref)-1)

/* Message database entry */
typedef struct mdb_entry {
    dbref from;           /* Sender */
    long date;            /* Timestamp */
    int flags;            /* Status flags */
    char *message;        /* Message content (NULL if free slot) */
    mdbref next;          /* Next message or free slot */
} MDB_ENTRY;

/* Message destination types */
typedef enum {
    MSG_PRIVATE,    /* Private player mail */
    MSG_BOARD       /* Public board post */
} msg_dest_type;

/* Function prototypes - ANSI C style */

/* Initialization and cleanup */
void init_mail(void);
void free_mail(void);

/* Core message operations */
void send_message(dbref, dbref, const char *, msg_dest_type, int);
void read_message(dbref, dbref, long);
long delete_messages(dbref, dbref, long, long, int);
void purge_deleted(dbref, dbref);
void list_messages(dbref, dbref, msg_dest_type);

/* Utility functions */
long count_messages(dbref, int);
long count_unread(dbref);
mdbref get_mailk(dbref);
void set_mailk(dbref, mdbref);
long mail_size(dbref);

/* Board-specific functions */
void ban_from_board(dbref, dbref);
void unban_from_board(dbref, dbref);
int is_banned_from_board(dbref);

/* Database I/O wrappers */
void write_mail(FILE *);  /* Write messages to database file */
void read_mail(FILE *);   /* Read messages from database file */

/* Database I/O */
void write_messages(FILE *);  /* Write messages to database file */
void read_messages(FILE *);   /* Read messages from database file */

/* Internal memory management */
mdbref grab_free_mail_slot(void);
void make_free_mail_slot(mdbref slot);

/* Command handlers */
void do_mail(dbref, char *, char *);
void do_board(dbref, char *, char *);

/* External API functions (called from other modules) */
void check_mail(dbref, char *);  /* Display mail status */
long check_mail_internal(dbref, char *);  /* Get unread count */
long dt_mail(dbref);  /* Count total messages for */
void info_mail(dbref);  /* Display mail system statistics */

#ifdef SHRINK_DB
void remove_all_mail(void);  /* Database maintenance - wipe all mail */
#endif

/* Global variables */
extern MDB_ENTRY *mdb;
extern long mdb_top;
extern long mdb_alloc;
extern long mdb_first_free;




/* Global message database */
MDB_ENTRY *mdb = NULL;
long mdb_alloc = 0;
long mdb_top = 0;
long mdb_first_free = NOMAIL;

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

/* Initialize message system */
void init_mail(void)
{
    mdb_top = 0;
    mdb_alloc = 512;
//    mdb = malloc(sizeof(MDB_ENTRY) * mdb_alloc);
    SAFE_MALLOC(mdb, MDB_ENTRY, mdb_alloc);
    
    if (!mdb) {
        panic("Failed to allocate message database");
    }
    
    mdb_first_free = NOMAIL;
}

/* Free message system */
void free_mail(void)
{
    long i;
    
    for (i = 0; i < mdb_top; i++) {
        if (mdb[i].message) {
            SMART_FREE(mdb[i].message);
        }
    }
    
    if (mdb) {
        SMART_FREE(mdb);
        mdb = NULL;
    }
}

/* Get mail key for player */
mdbref get_mailk(dbref player)
{
    char *attr;
    
    if (player < 0 || player >= db_top) return NOMAIL;
    
    attr = atr_get(player, A_MAILK);
    if (!attr || !*attr) return NOMAIL;
    
    return atol(attr);
}

/* Set mail key for player */
void set_mailk(dbref player, mdbref mailk)
{
    char buf[32];
    
    if (player < 0 || player >= db_top) return;
    
    snprintf(buf, sizeof(buf), "%ld", mailk);
    atr_add(player, A_MAILK, buf);
}

/* Calculate mail storage size */
long mail_size(dbref player)
{
    long size = 0;
    mdbref j;
    
    if (player < 0 || player >= db_top) return 0;
    
    for (j = get_mailk(player); j != NOMAIL; j = mdb[j].next) {
        size += sizeof(MDB_ENTRY) + strlen(mdb[j].message) + 1;
    }
    
    return size;
}

/* Grab a free message slot */
mdbref grab_free_mail_slot(void)
{
    mdbref result;
    
    /* Try to use free list first */
    if (mdb_first_free != NOMAIL) {
        if (mdb[mdb_first_free].message != NULL) {
            log_error("+mail's first_free's message isn't null!");
            mdb_first_free = NOMAIL;
        } else {
            result = mdb_first_free;
            mdb_first_free = mdb[mdb_first_free].next;
            return result;
        }
    }
    
    /* Expand database if needed */
    if (++mdb_top >= mdb_alloc) {
        long old_alloc = mdb_alloc;
        MDB_ENTRY *new_mdb;

        mdb_alloc *= 2;
        SAFE_MALLOC(new_mdb, MDB_ENTRY, mdb_alloc);
        if (!new_mdb) {
            panic("Failed to expand message database");
        }

        memcpy(new_mdb, mdb, sizeof(MDB_ENTRY) * old_alloc);
        memset(new_mdb + old_alloc, 0,
               sizeof(MDB_ENTRY) * (mdb_alloc - old_alloc));
        SMART_FREE(mdb);
        mdb = new_mdb;
    }
    
    mdb[mdb_top - 1].message = NULL;
    return mdb_top - 1;
}

/* Return slot to free list */
void make_free_mail_slot(mdbref slot)
{
    if (slot < 0 || slot >= mdb_top) return;
    
    if (mdb[slot].message) {
        SMART_FREE(mdb[slot].message);
    }
    
    mdb[slot].message = NULL;
    mdb[slot].next = mdb_first_free;
    mdb_first_free = slot;
}

/* Count messages in mailbox */
long count_messages(dbref mailbox, int include_deleted)
{
    long count = 0;
    mdbref i;
    
    if (mailbox < 0 || mailbox >= db_top) return 0;
    
    for (i = get_mailk(mailbox); i != NOMAIL; i = mdb[i].next) {
        if (include_deleted || !(mdb[i].flags & MF_DELETED)) {
            count++;
        }
    }
    
    return count;
}

/* Count unread messages for a player */
long count_unread(dbref player)
{
    long count = 0;
    mdbref i;
    
    if (player < 0 || player >= db_top) return 0;
    
    for (i = get_mailk(player); i != NOMAIL; i = mdb[i].next) {
        if (!(mdb[i].flags & MF_READ) && !(mdb[i].flags & MF_DELETED)) {
            count++;
        }
    }
    
    return count;
}

/* Send a message - unified function for both mail and board */
void send_message(dbref from, dbref to, const char *message,
                  msg_dest_type type, int flags)
{
    mdbref i, prev = NOMAIL;
    long msgnum = 1;
    char *msg_copy;
    
    if (!message || !*message) return;
    if (to < 0 || to >= db_top) return;
    
    /* Validate sender */
    if (from != NOTHING && (from < 0 || from >= db_top)) return;
    
    /* Check quota for non-board messages */
    if (type != MSG_BOARD) {
        if (db[to].i_flags & I_QUOTAFULL) {
            if (from != NOTHING) {
                notify(from, "That player has insufficient quota.");
            }
            return;
        }
    }
    
    /* Find insertion point */
    for (i = get_mailk(to); i != NOMAIL; i = mdb[i].next) {
        if (mdb[i].flags & MF_DELETED) {
            break; /* Reuse deleted slot */
        }
        prev = i;
        msgnum++;
    }
    
    /* Get new slot if needed */
    if (i == NOMAIL) {
        if (prev == NOMAIL) {
            i = grab_free_mail_slot();
            set_mailk(to, i);
        } else {
            mdb[prev].next = i = grab_free_mail_slot();
        }
        mdb[i].next = NOMAIL;
    }
    
    /* Copy message safely */
//    msg_copy = malloc(strlen(message) + 1);
    SAFE_MALLOC(msg_copy, char, strlen(message) + 1);
    if (!msg_copy) return;
    strcpy(msg_copy, message);
    
    /* Fill in message data */
    mdb[i].from = from;
    mdb[i].date = now;
    mdb[i].flags = flags | (type == MSG_BOARD ? MF_BOARD : 0);
    mdb[i].message = msg_copy;
    
    /* Notify recipient for private mail */
    if (type == MSG_PRIVATE && from != NOTHING) {
        if (could_doit(from, to, A_LPAGE)) {
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
    mdbref i;
    long num, count = 0;
    int target_flag = undelete ? MF_READ : MF_DELETED;
    
    if (mailbox < 0 || mailbox >= db_top) return 0;
    if (start < 0 || end < 0) return 0;
    
    /* Navigate to start position */
    num = start;
    for (i = get_mailk(mailbox); i != NOMAIL && num > 1; i = mdb[i].next) {
        num--;
    }
    
    /* Delete range */
    for (num = end - start; (num >= 0 || end == 0) && i != NOMAIL; 
         i = mdb[i].next, num--) {
        
        /* Check permissions */
        if (mailbox == player || 
            mdb[i].from == player ||
            power(player, POW_BOARD)) {
            
            mdb[i].flags = target_flag;
            count++;
        }
        
        if (end == 0) break; /* Delete only first if end == 0 */
    }
    
    recalc_bytes(mailbox);
    return count;
}

/* Purge deleted messages - unified function */
void purge_deleted(dbref player, dbref mailbox)
{
    mdbref i, next, prev = NOMAIL;
    int is_board = (mailbox == default_room);
    
    if (mailbox < 0 || mailbox >= db_top) return;
    
    for (i = get_mailk(mailbox); i != NOMAIL; i = next) {
        next = mdb[i].next;
        
        /* Check permissions and deletion status */
        if ((mdb[i].flags & MF_DELETED) &&
            (mailbox == player || 
             mdb[i].from == player ||
             (is_board && power(player, POW_BOARD)))) {
            
            /* Remove from chain */
            if (prev != NOMAIL) {
                mdb[prev].next = mdb[i].next;
            } else {
                set_mailk(mailbox, mdb[i].next);
            }
            
            make_free_mail_slot(i);
        } else {
            prev = i;
        }
    }
    
    recalc_bytes(mailbox);
}

/* List messages - unified function with type awareness */
void list_messages(dbref player, dbref mailbox, msg_dest_type type)
{
    mdbref j;
    long i = 1;
    char buf[1024];
    char status_char;
    int is_board = (type == MSG_BOARD);
    const char *sys_name = is_board ? "+board" : "+mail";
    
    if (mailbox < 0 || mailbox >= db_top) return;
    
    /* Print header */
    if (is_board) {
        notify(player, 
            "|C++board|   |Y!+Author|               | |W!+Time/Date|           | Message");
        notify(player,
            "------------------------------+---------------------+------------------------");
    } else {
        snprintf(buf, sizeof(buf), 
            "|W!+------>| |B!+%s| |W!+for| %s", sys_name, db[mailbox].cname);
        if (player != mailbox) {
            safe_append(buf, tprintf(" |W!+from| %s", db[player].cname), sizeof(buf));
        }
        safe_append(buf, " |W!+<------|", sizeof(buf));
        notify(player, buf);
    }
    
    /* List messages */
    for (j = get_mailk(mailbox); j != NOMAIL; j = mdb[j].next, i++) {
        /* Determine status character */
        if (mdb[j].flags & MF_DELETED) {
            status_char = 'd';
        } else if (mdb[j].flags & MF_NEW) {
            status_char = '*';
            if (player == mailbox) {
                mdb[j].flags &= ~MF_NEW; /* Mark as seen */
            }
        } else if (mdb[j].flags & MF_READ) {
            status_char = ' ';
        } else {
            status_char = 'u';
        }
        
        /* Check permissions */
        if (mailbox == player || 
            mdb[j].from == player ||
            (is_board && (status_char != 'd' || power(player, POW_BOARD)))) {
            
            char name_buf[32];
            char date_buf[32];
            char *preview;
            
            /* Format sender name */
            safe_copy(name_buf, 
                     truncate_color(db[mdb[j].from].cname, 20),
                     sizeof(name_buf));
            
            /* Format date */
            safe_copy(date_buf, mktm(mdb[j].date, "D", player), sizeof(date_buf));
            
            /* Get message preview */
            preview = tprintf("%s", truncate_color(mdb[j].message, 25));
            
            /* Remove newlines from preview */
            char *nl = strchr(preview, '\n');
            if (nl) *nl = '\0';
            
            snprintf(buf, sizeof(buf), "%5ld) %c %-20s | %-19s | %s",
                    i, status_char, name_buf, date_buf, preview);
            notify(player, buf);
        }
    }
    
    notify(player, "");
}

/* Read a message - unified function */
void read_message(dbref player, dbref mailbox, long msgnum)
{
    mdbref j;
    long i;
    char buf[512];
    int is_board = (mailbox == default_room);
    
    if (mailbox < 0 || mailbox >= db_top) return;
    if (msgnum <= 0) return;
    
    /* Find message */
    i = msgnum;
    for (j = get_mailk(mailbox); j != NOMAIL && i > 1; j = mdb[j].next, i--)
        ;
    
    if (j == NOMAIL) {
        notify(player, tprintf("%s: Invalid message number.",
                              is_board ? "+board" : "+mail"));
        return;
    }
    
    /* Check permissions */
    if ((mdb[j].flags & MF_DELETED) &&
        mailbox != player && 
        mdb[j].from != player &&
        !(is_board && power(player, POW_BOARD))) {
        notify(player, tprintf("%s: Invalid message number.",
                              is_board ? "+board" : "+mail"));
        return;
    }
    
    /* Display message */
    notify(player, tprintf("Message %ld:", msgnum));
    
    if (!is_board) {
        notify(player, tprintf("To: %s", db[mailbox].cname));
    }
    
    notify(player, tprintf("From: %s", 
                          mdb[j].from == NOTHING ? "The MUSE Server" :
                          unparse_object(player, mdb[j].from)));
    
    snprintf(buf, sizeof(buf), "Date: %s", mktm(mdb[j].date, "D", player));
    notify(player, buf);
    
    /* Show flags if applicable */
    if (mdb[j].flags & (MF_DELETED | MF_READ | MF_NEW)) {
        safe_copy(buf, "Flags:", sizeof(buf));
        if (mdb[j].flags & MF_DELETED) safe_append(buf, " deleted", sizeof(buf));
        if (mdb[j].flags & MF_READ) safe_append(buf, " read", sizeof(buf));
        if (mdb[j].flags & MF_NEW) safe_append(buf, " new", sizeof(buf));
        notify(player, buf);
    }
    
    notify(player, "");
    notify(player, mdb[j].message);
    
    /* Mark as read if viewing own mail */
    if (mailbox == player) {
        mdb[j].flags &= ~MF_NEW;
        mdb[j].flags |= MF_READ;
    }
}

/* Board ban checking */
int is_banned_from_board(dbref player)
{
    char *blist, *blbegin, *target;
    char buf[1024];
    
    if (!could_doit(player, default_room, A_LPAGE)) {
        snprintf(buf, sizeof(buf), "%s&", atr_get(default_room, A_LPAGE));
        blbegin = blist = stralloc(buf);
        target = tprintf("#%" DBREF_FMT, player);
        
        while (*blist) {
            char *amp = strchr(blist, '&');
            if (amp) *amp = '\0';
            
            if (!strcmp(blist, target)) {
                return 1;
            }
            
            blist += strlen(blist) + 1;
        }
    }
    
    return 0;
}

void write_mail(FILE *fp)
{
	write_messages(fp);
}

/* Write messages to database file */
void write_messages(FILE *fp)
{
    dbref d;
    mdbref i;
    
    if (!fp) return;
    
    for (d = 0; d < db_top; d++) {
        if ((d == 0 || Typeof(d) == TYPE_PLAYER) && 
            (i = get_mailk(d)) != NOMAIL) {
            
            for (; i != NOMAIL; i = mdb[i].next) {
                if (!(mdb[i].flags & MF_DELETED)) {
                    atr_fputs(tprintf("+%ld:%ld:%ld:%d:%s",
                                     mdb[i].from, d, mdb[i].date,
                                     mdb[i].flags, mdb[i].message), fp);
                    fputc('\n', fp);
                }
            }
        }
    }
}

void read_mail(FILE *fp)
{
	read_messages(fp);
}

/* Read messages from database file */
void read_messages(FILE *fp)
{
    char buf[2048];
    dbref to, from;
    time_t date;
    int flags;
    char message[1024];
    char *s;
    
    if (!fp) return;
    
    while (strlen(atr_fgets(buf, sizeof(buf), fp)) && !feof(fp)) {
        if (buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = '\0';
        }
        
        if (*buf == '+') {
            /* Parse message entry */
            s = buf + 1;
            from = atol(s);
            
            if ((s = strchr(s, ':')) && 
                (s++, to = atol(s)) >= 0 &&
                (s = strchr(s, ':')) &&
                (s++, date = atol(s)) &&
                (s = strchr(s, ':')) &&
                (s++, flags = atoi(s)) &&
                (s = strchr(s, ':'))) {
                
                safe_copy(message, s + 1, sizeof(message));
                send_message(from, to, message, 
                           flags & MF_BOARD ? MSG_BOARD : MSG_PRIVATE,
                           flags & ~MF_BOARD);
            }
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
    
    if (!arg2 || !*arg2) {
        target = player;
    } else {
        target = lookup_player(arg2);
        if (target == NOTHING) {
            target = player;
        }
    }
    
    /* Check if target has any mail */
    if (get_mailk(target) == NOMAIL) {
        return;
    }
    
    /* Count messages */
    long read = 0, new_msg = 0, tot = 0;
    mdbref i;
    
    if (target == player) {
        /* Checking own mail */
        for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next) {
            if (mdb[i].flags & MF_READ)
                read++;
            if (mdb[i].flags & MF_NEW)
                new_msg++;
            if (!(mdb[i].flags & MF_DELETED))
                tot++;
        }
        
        /* Build status message */
        snprintf(buf, sizeof(buf), 
                "|W!++mail:| You have |Y!+%ld| message%s.", 
                tot, (tot == 1) ? "" : "s");
        
        if (new_msg > 0) {
            safe_append(buf, 
                       tprintf(" |G!+%ld| of them %s new.", 
                              new_msg, (new_msg == 1) ? "is" : "are"),
                       sizeof(buf));
            
            if ((tot - read - new_msg) > 0) {
                /* Remove last period and add unread count */
                size_t len = strlen(buf);
                if (len > 0 && buf[len-1] == '.') {
                    buf[len-1] = '\0';
                }
                safe_append(buf,
                           tprintf("; |M!+%ld| other%s unread.",
                                  tot - read - new_msg,
                                  (tot - read - new_msg == 1) ? " is" : "s are"),
                           sizeof(buf));
            }
        } else if ((tot - read) > 0) {
            safe_append(buf,
                       tprintf(" %ld of them %s unread.",
                              tot - read,
                              (tot - read == 1) ? "is" : "are"),
                       sizeof(buf));
        }
    } else {
        /* Checking mail sent to someone else */
        for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next) {
            if (mdb[i].from == player) {
                if (mdb[i].flags & MF_READ)
                    read++;
                if (mdb[i].flags & MF_NEW)
                    new_msg++;
                if (!(mdb[i].flags & MF_DELETED))
                    tot++;
            }
        }
        
        snprintf(buf, sizeof(buf),
                "|W!++mail:| %s has |Y!+%ld| message%s from you.",
                db[target].cname, tot, (tot == 1) ? "" : "s");
        
        if (new_msg > 0) {
            safe_append(buf,
                       tprintf(" |G!+%ld| of them %s new.",
                              new_msg, (new_msg == 1) ? "is" : "are"),
                       sizeof(buf));
            
            if ((tot - read - new_msg) > 0) {
                size_t len = strlen(buf);
                if (len > 0 && buf[len-1] == '.') {
                    buf[len-1] = '\0';
                }
                safe_append(buf,
                           tprintf("; |M!+%ld| other%s unread.",
                                  tot - read - new_msg,
                                  (tot - read - new_msg == 1) ? " is" : "s are"),
                           sizeof(buf));
            }
        } else if ((tot - read) > 0) {
            safe_append(buf,
                       tprintf(" %ld of them %s unread.",
                              tot - read,
                              (tot - read == 1) ? "is" : "are"),
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
    long tot = 0;
    mdbref i;
    
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
    
    if (get_mailk(target) == NOMAIL) {
        return 0;
    }
    
    if (target == player) {
        /* Count own unread mail */
        for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next) {
            if (!(mdb[i].flags & MF_READ) && !(mdb[i].flags & MF_DELETED)) {
                tot++;
            }
        }
    } else {
        /* Count unread mail sent by player to target */
        for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next) {
            if (mdb[i].from == player) {
                if (!(mdb[i].flags & MF_READ) && !(mdb[i].flags & MF_DELETED)) {
                    tot++;
                }
            }
        }
    }
    
    return tot;
}

/**
 * info_mail - Display mail system statistics
 * 
 * Shows detailed information about the mail system including
 * database usage, memory allocation, and message counts.
 * 
 * @param player Player requesting info (typically admin)
 */
void info_mail(dbref player)
{
    long total_messages = 0;
    long deleted_messages = 0;
    long board_messages = 0;
    long private_messages = 0;
    long new_messages = 0;
    long unread_messages = 0;
    long players_with_mail = 0;
    size_t total_memory = 0;
    size_t message_text_size = 0;
    mdbref i;
    dbref d;
    
    notify(player, "|W!+========================================|");
    notify(player, "|W!+      Mail System Information        |");
    notify(player, "|W!+========================================|");
    notify(player, "");
    
    /* Calculate statistics */
    for (d = 0; d < db_top; d++) {
        if ((d == 0 || Typeof(d) == TYPE_PLAYER) && get_mailk(d) != NOMAIL) {
            players_with_mail++;
            
            for (i = get_mailk(d); i != NOMAIL; i = mdb[i].next) {
                total_messages++;
                
                if (mdb[i].flags & MF_DELETED) {
                    deleted_messages++;
                }
                
                if (mdb[i].flags & MF_BOARD) {
                    board_messages++;
                } else {
                    private_messages++;
                }
                
                if (mdb[i].flags & MF_NEW) {
                    new_messages++;
                } else if (!(mdb[i].flags & MF_READ)) {
                    unread_messages++;
                }
                
                if (mdb[i].message) {
                    message_text_size += strlen(mdb[i].message) + 1;
                }
            }
        }
    }
    
    /* Calculate memory usage */
    total_memory = sizeof(MDB_ENTRY) * mdb_alloc;
    
    /* Display statistics */
    notify(player, "|C!+Database Status:|");
    notify(player, tprintf("  Allocated slots:     %ld", mdb_alloc));
    notify(player, tprintf("  Used slots:          %ld", mdb_top));
    notify(player, tprintf("  Free slots:          %ld", mdb_alloc - mdb_top));
    notify(player, tprintf("  Utilization:         %.1f%%", 
                          (mdb_top * 100.0) / mdb_alloc));
    notify(player, "");
    
    notify(player, "|C!+Message Statistics:|");
    notify(player, tprintf("  Total messages:      %ld", total_messages));
    notify(player, tprintf("  Private messages:    %ld", private_messages));
    notify(player, tprintf("  Board posts:         %ld", board_messages));
    notify(player, tprintf("  Deleted (purgable):  %ld", deleted_messages));
    notify(player, tprintf("  New messages:        %ld", new_messages));
    notify(player, tprintf("  Unread messages:     %ld", unread_messages));
    notify(player, "");
    
    notify(player, "|C!+User Statistics:|");
    notify(player, tprintf("  Players with mail:   %ld", players_with_mail));
    if (players_with_mail > 0) {
        notify(player, tprintf("  Avg msgs/player:     %.1f", 
                              (float)total_messages / players_with_mail));
    }
    notify(player, "");
    
    notify(player, "|C!+Memory Usage:|");
    notify(player, tprintf("  Structure memory:    %zu bytes (%.2f KB)",
                          total_memory, total_memory / 1024.0));
    notify(player, tprintf("  Message text:        %zu bytes (%.2f KB)",
                          message_text_size, message_text_size / 1024.0));
    notify(player, tprintf("  Total memory:        %zu bytes (%.2f KB)",
                          total_memory + message_text_size,
                          (total_memory + message_text_size) / 1024.0));
    notify(player, "");
    
    /* Show top mail users if admin */
    if (power(player, POW_SECURITY) || power(player, POW_STATS)) {
        notify(player, "|C!+Top Mail Users:|");
        
        /* Simple bubble sort to find top 5 */
        struct { dbref player; long count; } top[5] = {{NOTHING, 0}};
        int top_count = 0;
        
        for (d = 0; d < db_top; d++) {
            if (Typeof(d) == TYPE_PLAYER) {
                long count = dt_mail(d);
                if (count > 0) {
                    /* Check if this player belongs in top 5 */
                    for (int j = 0; j < 5; j++) {
                        if (top[j].player == NOTHING || count > top[j].count) {
                            /* Shift down */
                            for (int k = 4; k > j; k--) {
                                top[k] = top[k-1];
                            }
                            top[j].player = d;
                            top[j].count = count;
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
    if (deleted_messages > total_messages * 0.3) {
        notify(player, "|Y!+Recommendation:| Consider running mail purge - "
                      "30%+ messages are deleted.");
    }
    
    if (mdb_top > mdb_alloc * 0.8) {
        notify(player, "|Y!+Recommendation:| Mail database is 80%+ full - "
                      "expansion may occur soon.");
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
    mdbref i;
    long count = 0;
    
    if (who < 0 || who >= db_top) return -1;
    if (Typeof(who) != TYPE_PLAYER) return -1;
    
    for (i = get_mailk(who); i != NOMAIL; i = mdb[i].next) {
        count++;
    }
    
    return count;
}

#ifdef SHRINK_DB
/**
 * remove_all_mail - Nuclear option: delete all mail from all players
 * 
 * WARNING: This is a dangerous database maintenance function!
 * Only used during database shrinking operations.
 * 
 * NOTE: The hardcoded limit of 3999 is from the original code.
 * If your database has more objects, increase this value or
 * change to iterate through db_top instead.
 */
void remove_all_mail(void)
{
    dbref i;
    char target_buf[32];
    
    log_important("remove_all_mail() called - wiping all player mail!");
    
    /* Original code had hardcoded 3999 limit with warning comment.
     * We'll use db_top instead for safety, but log a warning. */
    if (db_top > 4000) {
        log_error(tprintf("remove_all_mail: Database has %ld objects, "
                         "original code only supported 3999. "
                         "Proceeding with all players anyway.", db_top));
    }
    
    for (i = 0; i < db_top; i++) {
        if (Typeof(i) == TYPE_PLAYER) {
            /* Delete all messages */
            do_mail(i, "delete", "");
            
            /* Purge deleted messages */
            snprintf(target_buf, sizeof(target_buf), "#%" DBREF_FMT, i);
            do_mail(i, "purge", target_buf);
        }
    }
    
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
    if (!string_compare(arg1, "delete") || !string_compare(arg1, "undelete")) {
        /* Handle delete/undelete */
        long del = delete_messages(player, player, 
                                   arg2 && *arg2 ? atol(arg2) : 0,
                                   arg2 && *arg2 ? atol(arg2) : 0,
                                   !string_compare(arg1, "undelete"));
        notify(player, tprintf("+mail: %ld messages %sdeleted.", del,
                              !string_compare(arg1, "delete") ? "" : "un"));
    }
    else if (!string_compare(arg1, "check")) {
        long unread = count_unread(player);
        notify(player, tprintf("+mail: You have %ld unread message%s.",
                              unread, unread == 1 ? "" : "s"));
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
        list_messages(player, player, MSG_PRIVATE);
    }
    else if (*arg1 && *arg2) {
        /* Send mail */
        dbref recip = lookup_player(arg1);
        if (recip == NOTHING || Typeof(recip) != TYPE_PLAYER) {
            notify(player, "+mail: Unknown player.");
            return;
        }
        send_message(player, recip, arg2, MSG_PRIVATE, MF_NEW);
        notify(player, tprintf("+mail: Message sent to %s.", db[recip].cname));
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
        list_messages(player, default_room, MSG_BOARD);
    }
    else if (!string_compare(arg1, "read")) {
        if (arg2 && *arg2) {
            read_message(player, default_room, atol(arg2));
        } else {
            notify(player, "+board: Specify a message number to read.");
        }
    }
    else if (!string_compare(arg1, "write")) {
        if (arg2 && *arg2) {
            send_message(player, default_room, arg2, MSG_BOARD, MF_READ);
            notify(player, "+board: Message posted.");
        } else {
            notify(player, "+board: You must provide a message.");
        }
    }
    else if (!string_compare(arg1, "delete") && power(player, POW_BOARD)) {
        long del = delete_messages(player, default_room,
                                   arg2 && *arg2 ? atol(arg2) : 0,
                                   arg2 && *arg2 ? atol(arg2) : 0, 0);
        notify(player, tprintf("+board: %ld messages deleted.", del));
    }
    else {
        notify(player, "+board: Invalid syntax. See 'help +board'.");
    }
}
