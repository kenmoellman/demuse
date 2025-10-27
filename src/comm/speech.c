/* speech.c - Basic player communication functions
 * Located in comm/ directory
 * 
 * This file now contains ONLY basic communication commands.
 * Emit functions moved to emit.c
 * Page functions moved to page.c
 * Broadcast functions moved to broadcast.c
 * Admin functions moved to muse/admin.c
 * Player utilities moved to muse/player.c
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

/* Function prototypes */
static int check_auditorium_permission(dbref player, dbref loc);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Generate name for object to be used when spoken
 * For spoof protection: player names always use cname (colorized)
 * @param thing Object to get speaking name for
 * @return Colorized name from database
 */
char *spname(dbref thing)
{
    return db[thing].cname;
}

/**
 * Reconstruct a message that was split by '=' in command parsing
 * NOTE: Non-static so it can be shared with other files
 * @param arg1 First part of message
 * @param arg2 Second part of message (after '=')
 * @return Reconstructed full message (allocated)
 */
char *reconstruct_message(char *arg1, char *arg2)
{
    static char buf[BUFFER_LEN];
    
    if (arg2 && *arg2) {
        snprintf(buf, sizeof(buf), "%s = %s", 
                arg1 ? arg1 : "", arg2);
        return stralloc(buf);
    }
    
    return arg1 ? stralloc(arg1) : stralloc("");
}

/**
 * Check if a room has auditorium restrictions
 * @param player Player trying to speak
 * @param loc Location to check
 * @return 1 if allowed, 0 if restricted
 */
static int check_auditorium_permission(dbref player, dbref loc)
{
    if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
        (!could_doit(player, loc, A_SLOCK) ||
         !could_doit(player, db[loc].zone, A_SLOCK))) {
        did_it(player, loc, A_SFAIL, "Shh.", A_OSFAIL, NULL, A_ASFAIL);
        return 0;
    }
    return 1;
}

/* ===================================================================
 * Basic Communication Commands
 * =================================================================== */

/**
 * SAY command - speak in current room
 * @param player Speaker
 * @param arg1 First part of message
 * @param arg2 Second part of message (if split by =)
 */
void do_say(dbref player, char *arg1, char *arg2)
{
    dbref loc;
    char *message;
    char buf[BUFFER_LEN];
    char *bf;
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    message = reconstruct_message(arg1, arg2);
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    
    /* Notify everybody */
    notify(player, tprintf("You say \"%s\"", bf));
    notify_in(loc, player, tprintf("%s says \"%s\"", spname(player), bf));
    
    SAFE_FREE(message);
}

/**
 * WHISPER command - private message to someone in same room
 * @param player Whisperer
 * @param arg1 Target player
 * @param arg2 Message to whisper
 */
void do_whisper(dbref player, char *arg1, char *arg2)
{
    dbref who;
    char buf[BUFFER_LEN];
    char *bf;
    
    pronoun_substitute(buf, player, arg2, player);
    bf = buf + strlen(db[player].name) + 1;
    
    /* Match target */
    init_match(player, arg1, TYPE_PLAYER);
    match_neighbor();
    match_me();
    if (power(player, POW_REMOTE)) {
        match_absolute();
        match_player(NOTHING, NULL);
    }
    
    who = match_result();
    switch (who) {
        case NOTHING:
            notify(player, "Whisper to whom?");
            break;
            
        case AMBIGUOUS:
            notify(player, "I don't know who you mean!");
            break;
            
        default:
            if (*bf == POSE_TOKEN || *bf == NOSP_POSE) {
                const char *possessive = (*bf == NOSP_POSE) ? "'s" : "";
                notify(player,
                      tprintf("You whisper-posed %s with \"%s%s %s\".",
                             db[who].cname, spname(player),
                             possessive, bf + 1));
                notify(who,
                      tprintf("%s whisper-poses: %s%s %s", 
                             spname(player), spname(player), 
                             possessive, bf + 1));
                did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
            } else if (*bf == THINK_TOKEN) {
                notify(player, 
                      tprintf("You whisper-thought %s with \"%s . o O ( %s )\".",
                             db[who].cname, spname(player), bf + 1));
                notify(who, 
                      tprintf("%s whisper-thinks: %s . o O ( %s )", 
                             spname(player), spname(player), bf + 1));
                did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
            } else {
                notify(player,
                      tprintf("You whisper \"%s\" to %s.", bf, db[who].name));
                notify(who,
                      tprintf("%s whispers \"%s\"", spname(player), bf));
                did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
            }
            break;
    }
}

/**
 * POSE command - emote an action
 * @param player Actor
 * @param arg1 First part of pose
 * @param arg2 Second part if split by =
 * @param possessive 1 for possessive pose (:'s), 0 for normal pose (:)
 */
void do_pose(dbref player, char *arg1, char *arg2, int possessive)
{
    dbref loc;
    char lastchar;
    char format[32];  /* Buffer for format string */
    char *message;
    char buf[BUFFER_LEN];
    char *bf;
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    if (possessive) {
        /* Get last character of player's name for proper possessive */
        size_t namelen = strlen(db[player].name);
        lastchar = to_lower(db[player].name[namelen - 1]);
        strcpy(format, (lastchar == 's') ? "%s' %s" : "%s's %s");
    } else {
        strcpy(format, "%s %s");
    }
    
    message = reconstruct_message(arg1, arg2);
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    
    /* Notify everybody */
    notify_in(loc, NOTHING, tprintf(format, spname(player), bf));
    
    SAFE_FREE(message);
}

/**
 * THINK command - show a thought bubble
 * @param player Thinker
 * @param arg1 First part of thought
 * @param arg2 Second part if split by =
 */
void do_think(dbref player, char *arg1, char *arg2)
{
    dbref loc;
    char *message;
    char buf[BUFFER_LEN];
    char *bf;
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    message = reconstruct_message(arg1, arg2);
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    
    /* Notify everybody */
    notify_in(loc, NOTHING, tprintf("%s . o O ( %s )", spname(player), bf));
    
    SAFE_FREE(message);
}

/**
 * TO command - directed speech with [to target] prefix
 * @param player Speaker
 * @param arg1 Target and message combined
 * @param arg2 Additional message part if split by =
 */
void do_to(dbref player, char *arg1, char *arg2)
{
    dbref loc, thing;
    char *s;
    char *message;
    char buf2[BUFFER_LEN-18];
    char buf3[BUFFER_LEN];
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    message = reconstruct_message(arg1, arg2);
    
    /* Find the space separating target from message */
    s = strchr(message, ' ');
    if (!s) {
        notify(player, "No message.");
        SAFE_FREE(message);
        return;
    }
    
    *s++ = '\0';
    if (!*message || strlen(s) < 1) {
        notify(player, "No player mentioned.");
        SAFE_FREE(message);
        return;
    }
    
    /* Look up the target player */
    thing = lookup_player(message);
    if (thing == AMBIGUOUS) {
        thing = NOTHING;
    }
    
    if (thing != NOTHING) {
        snprintf(buf2, sizeof(buf2), "%s", db[thing].cname);
    } else {
        snprintf(buf2, sizeof(buf2), "%s", message);
    }
    
    /* Format the message based on type */
/*    if (*s == POSE_TOKEN) {
        snprintf(buf3, sizeof(buf3), "[to %s] %s %s", 
                buf2, db[player].cname, s + 1);
    } else if (*s == NOSP_POSE) {
        snprintf(buf3, sizeof(buf3), "[to %s] %s's %s", 
                buf2, db[player].cname, s + 1);
    } else if (*s == THINK_TOKEN) {
        snprintf(buf3, sizeof(buf3), "[to %s] %s . o O ( %s )", 
                buf2, db[player].cname, s + 1);
    } else {
        snprintf(buf3, sizeof(buf3), "%s [to %s]: %s", 
                db[player].cname, buf2, s);
    }
    */
    size_t name_len = strlen(db[player].cname);
    size_t buf2_len = strlen(buf2);
    size_t max_s_len;
    if (*s == ':') {
        if (*(s + 1) == '\'') {
            // "[to %s] %s's %s" - fixed chars: "[to ", "] ", "'s " = 9
            max_s_len = sizeof(buf3) - name_len - buf2_len - 9 - 1; // -1 for null terminator
            snprintf(buf3, sizeof(buf3), "[to %s] %s's %.*s",
                    buf2, db[player].cname, (int)max_s_len, s + 1);
        } else {
            // "[to %s] %s %s" - fixed chars: "[to ", "] ", " " = 7
            max_s_len = sizeof(buf3) - name_len - buf2_len - 7 - 1;
            snprintf(buf3, sizeof(buf3), "[to %s] %s %.*s",
                    buf2, db[player].cname, (int)max_s_len, s + 1);
        }
    } else if (*s == '.') {
        // "[to %s] %s . o O ( %s )" - fixed chars: "[to ", "] ", " . o O ( ", " )" = 17
        max_s_len = sizeof(buf3) - name_len - buf2_len - 17 - 1;
        snprintf(buf3, sizeof(buf3), "[to %s] %s . o O ( %.*s )",
                buf2, db[player].cname, (int)max_s_len, s + 1);
    } else {
        // "%s [to %s]: %s" - fixed chars: " [to ", "]: " = 8
        max_s_len = sizeof(buf3) - name_len - buf2_len - 8 - 1;
        snprintf(buf3, sizeof(buf3), "%s [to %s]: %.*s",
                db[player].cname, buf2, (int)max_s_len, s);
    }
    
    /* Notify everybody in the room */
    notify_in(loc, NOTHING, buf3);
    
    SAFE_FREE(message);
}

/* ===================================================================
 * Simple Utility Commands
 * =================================================================== */

/**
 * ECHO command - echo text back to player
 * @param player Player to echo to
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 * @param type 0 for pronoun substitution, non-zero for literal
 */
void do_echo(dbref player, char *arg1, char *arg2, int type)
{
    char *message = reconstruct_message(arg1, arg2);
    char buf[BUFFER_LEN];
    
    if (type == 0) {
        pronoun_substitute(buf, player, message, player);
        notify(player, buf + strlen(db[player].name) + 1);
    } else {
        notify(player, message);
    }
    
    SAFE_FREE(message);
}

/**
 * USE command - use an object
 * @param player User
 * @param arg1 Object to use
 */
void do_use(dbref player, char *arg1)
{
    dbref thing;
    
    thing = match_thing(player, arg1);
    if (thing == NOTHING) {
        return;
    }
    
    did_it(player, thing, A_USE, 
          "You don't know how to use that.", 
          A_OUSE, NULL, A_AUSE);
}
