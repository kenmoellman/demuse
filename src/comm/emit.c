/* emit.c - Emit and spoof commands
 * Located in comm/ directory
 * 
 * This file contains all emit-related commands.
 * Emits allow players to create arbitrary messages in rooms.
 * Anti-spoofing checks prevent impersonation.
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

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_ZONE_DEPTH 10  /* Maximum recursion depth for zone operations */

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static int can_emit_msg(dbref player, dbref loc, const char *msg);
static void notify_in_zone(dbref zone, const char *msg);
static int check_auditorium_permission(dbref player, dbref loc);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Check if a room has auditorium restrictions
 * @param player Player trying to speak/emit
 * @param loc Location to check
 * @return 1 if allowed, 0 if restricted
 */
static int check_auditorium_permission(dbref player, dbref loc)
{
    if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
        !controls(player, loc, POW_REMOTE) &&
        (!could_doit(player, loc, A_SLOCK) ||
         !could_doit(player, db[loc].zone, A_SLOCK))) {
        did_it(player, loc, A_SFAIL, "Shh.", A_OSFAIL, NULL, A_ASFAIL);
        return 0;
    }
    return 1;
}

/**
 * Check if player can emit a message that might appear to come from someone
 * This is the anti-spoofing protection
 * @param player Player trying to emit
 * @param loc Location where emit will appear
 * @param msg Message to check for spoofing
 * @return 1 if allowed, 0 if not
 */
static int can_emit_msg(dbref player, dbref loc, const char *msg)
{
    char first_word[BUFFER_LEN];
    const char *p;
    size_t word_len;
    dbref thing;
    dbref save_loc;
    
    /* Skip leading spaces */
    while (*msg && isspace((unsigned char)*msg)) {
        msg++;
    }
    
    if (!*msg) {
        return 1;  /* Empty message is fine */
    }
    
    /* Extract first word */
    p = msg;
    word_len = 0;
    while (*p && !isspace((unsigned char)*p) && word_len < sizeof(first_word) - 1) {
        first_word[word_len++] = *p++;
    }
    first_word[word_len] = '\0';
    
    if (word_len == 0) {
        return 1;  /* No word to check */
    }
    
    /* Check if first word is a player name they can't spoof */
    thing = lookup_player(first_word);
    if (thing != NOTHING && !string_compare(db[thing].name, first_word)) {
        if (!controls(player, thing, POW_REMOTE)) {
            return 0;  /* Can't spoof this player */
        }
    }
    
    /* Check for possessive form (name's) */
    if (word_len >= 2 && first_word[word_len - 2] == '\'' && 
        first_word[word_len - 1] == 's') {
        /* Remove 's and check again */
        first_word[word_len - 2] = '\0';
        
        thing = lookup_player(first_word);
        if (thing != NOTHING && !string_compare(db[thing].name, first_word)) {
            if (!controls(player, thing, POW_REMOTE)) {
                return 0;  /* Can't spoof this player's possessive */
            }
        }
    }
    
    /* Check if it matches an object in the location */
    if (loc != NOTHING && loc >= 0 && loc < db_top) {
        save_loc = db[player].location;
        db[player].location = loc;
        init_match(player, first_word, NOTYPE);
        match_perfect();
        thing = match_result();
        db[player].location = save_loc;
        
        /* Can't spoof objects in the room */
        if (thing != NOTHING && thing != AMBIGUOUS) {
            return 0;
        }
    }
    
    return 1;
}

/**
 * Notify all objects in a zone (recursive with depth limit)
 * @param zone Zone to notify
 * @param msg Message to send
 */
static void notify_in_zone(dbref zone, const char *msg)
{
    dbref thing;
    static int depth = 0;
    
    /* Prevent infinite recursion */
    if (depth >= MAX_ZONE_DEPTH) {
        return;
    }
    
    /* Prevent stack overflow from huge messages */
    if (!msg || strlen(msg) > BUFFER_LEN) {
        return;
    }
    
    depth++;
    
    /* Notify everything in this zone */
    for (thing = 0; thing < db_top; thing++) {
        if (db[thing].zone == zone) {
            /* Recursively notify sub-zones */
            notify_in_zone(thing, msg);
            
            /* Notify objects in this zone - make a safe copy */
            if (Typeof(thing) == TYPE_ROOM) {
                char msg_copy[BUFFER_LEN];
                strncpy(msg_copy, msg, sizeof(msg_copy) - 1);
                msg_copy[sizeof(msg_copy) - 1] = '\0';
                notify_in(thing, NOTHING, msg_copy);
            }
        }
    }
    
    depth--;
}

/* ===================================================================
 * Basic Emit Commands
 * =================================================================== */

/**
 * EMIT command - emit text to current room
 * @param player Emitter
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 * @param type 0 for pronoun substitution, 1 for literal
 */
void do_emit(dbref player, char *arg1, char *arg2, int type)
{
    dbref loc;
    char *message = NULL;
    char buf[BUFFER_LEN];
    char *bf;
    
    loc = getloc(player);
    if (loc == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    /* Reconstruct message */
    message = reconstruct_message(arg1, arg2);
    if (!message) {
        notify(player, "Invalid message.");
        return;
    }
    
    /* Handle pronoun substitution based on type */
    if (type == 0) {
        pronoun_substitute(buf, player, message, player);
        bf = buf + strlen(db[player].name) + 1;
    } else {
        strncpy(buf, message, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        bf = buf;
    }
    
    /* Free allocated message */
    free(message);
    
    /* Wizards can always emit */
    if (power(player, POW_REMOTE)) {
        notify_in(loc, NOTHING, bf);
        return;
    }
    
    /* Check anti-spoofing */
    if (can_emit_msg(player, db[player].location, bf)) {
        notify_in(loc, NOTHING, bf);
    } else {
        notify(player, "Permission denied - that would be spoofing.");
    }
}

/* ===================================================================
 * Targeted Emit Commands
 * =================================================================== */

/**
 * General emit command handler for pemit, remit, oemit, zemit
 * @param player Emitter
 * @param arg1 Target specification
 * @param arg2 Message to emit
 * @param emittype 0=pemit, 1=remit, 2=oemit, 3=zemit, 4=special
 */
void do_general_emit(dbref player, char *arg1, char *arg2, int emittype)
{
    dbref who;
    char *message = NULL;
    char buf[BUFFER_LEN];
    char *bf;
    int needs_spoof_check = 1;
    
    /* Reconstruct and process message */
    if (emittype == 4) {
        /* Special handling - find the = separator */
        char *eq = strchr(arg2, '=');
        if (eq) {
            bf = eq + 1;
        } else {
            bf = arg2;
        }
        emittype = 0;  /* Treat as pemit after special parsing */
        needs_spoof_check = 0;  /* Already in arg2, no reconstruction needed */
    } else {
        message = reconstruct_message(arg1, arg2);
        if (!message) {
            notify(player, "Invalid message.");
            return;
        }
        
        pronoun_substitute(buf, player, message, player);
        bf = buf + strlen(db[player].name) + 1;
        free(message);
        message = NULL;
    }
    
    /* Match the target */
    init_match(player, arg1, TYPE_PLAYER);
    match_absolute();
    match_player(NOTHING, NULL);
    match_neighbor();
    match_possession();
    match_me();
    match_here();
    who = noisy_match_result();
    
    if (who == NOTHING) {
        return;
    }
    
    /* Check permissions for remote emit */
    if (get_room(who) != get_room(player) && 
        !controls(player, get_room(who), POW_REMOTE) && 
        !controls_a_zone(player, who, POW_REMOTE)) {
        notify(player, "Permission denied - you can't emit there.");
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, db[who].location)) {
        return;
    }
    
    /* Handle different emit types */
    switch (emittype) {
        case 0: /* pemit - to a specific player/object */
            if ((needs_spoof_check && can_emit_msg(player, db[who].location, bf)) ||
                controls(player, who, POW_REMOTE)) {
                notify(who, bf);
                did_it(player, who, NULL, 0, NULL, 0, A_APEMIT);
                if (!(db[player].flags & QUIET)) {
                    notify(player, tprintf("%s just saw \"%s\".",
                                         unparse_object(player, who), bf));
                }
            } else {
                notify(player, "Permission denied - that would be spoofing.");
            }
            break;
            
        case 1: /* remit - to a room */
            if (controls(player, who, POW_REMOTE) ||
                ((db[player].location == who) && 
                 (needs_spoof_check && can_emit_msg(player, who, bf)))) {
                notify_in(who, NOTHING, bf);
                if (!(db[player].flags & QUIET)) {
                    notify(player, tprintf("Everything in %s saw \"%s\".",
                                         unparse_object(player, who), bf));
                }
            } else {
                notify(player, "Permission denied.");
            }
            break;
            
        case 2: /* oemit - to everyone except target */
            if (needs_spoof_check && can_emit_msg(player, db[who].location, bf)) {
                notify_in(db[who].location, who, bf);
                if (!(db[player].flags & QUIET)) {
                    notify(player, tprintf("Everyone except %s saw \"%s\".",
                                         unparse_object(player, who), bf));
                }
            } else {
                notify(player, "Permission denied.");
            }
            break;
            
        case 3: /* zemit - to entire zone */
            if (controls(player, who, POW_REMOTE) &&
                controls(player, who, POW_MODIFY) &&
                (needs_spoof_check && can_emit_msg(player, (dbref)-1, bf))) {
                
                if (db[who].zone == NOTHING && !(db[player].flags & QUIET)) {
                    notify(player, tprintf("%s might not be a zone... but I'll do it anyway.",
                                         unparse_object(player, who)));
                }
                
                notify_in_zone(who, bf);
                
                if (!(db[player].flags & QUIET)) {
                    notify(player, tprintf("Everything in zone %s saw \"%s\".",
                                         unparse_object(player, who), bf));
                }
            } else {
                notify(player, "Permission denied.");
            }
            break;
    }
}

/* ===================================================================
 * Special Emit Commands
 * =================================================================== */

/**
 * CEMIT command - emit to a specific connection ID
 * @param player Emitter
 * @param arg1 Connection ID
 * @param arg2 Message to send
 */
void do_cemit(dbref player, char *arg1, char *arg2)
{
    struct descriptor_data *d;
    long target;
    char *message = NULL;
    char buf[BUFFER_LEN];
    char *bf;
    char *endptr;
    
    /* Check permission */
    if (!power(player, POW_REMOTE)) {
        notify(player, "You don't have the authority to do that.");
        return;
    }
    
    /* Validate and parse connection ID */
    if (!arg1 || !*arg1 || !isdigit((unsigned char)*arg1)) {
        notify(player, "That's not a valid connection number.");
        return;
    }
    
    target = strtol(arg1, &endptr, 10);
    if (*endptr != '\0' || target < 0) {
        notify(player, "That's not a valid connection number.");
        return;
    }
    
    /* Find the descriptor */
    for (d = descriptor_list; d && d->concid != target; d = d->next)
        ;
    
    if (!d) {
        notify(player, "Unable to find specified connection ID.");
        return;
    }
    
    /* Process message */
    message = reconstruct_message(arg1, arg2);
    if (!message) {
        notify(player, "Invalid message.");
        return;
    }
    
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    free(message);
    
    /* Send feedback to emitter */
    if (!(db[player].flags & QUIET)) {
        notify(player, tprintf("Connection %ld just saw \"%s\".", target, bf));
    }
    
    /* Send to target */
    if (d->state == CONNECTED) {
        notify(d->player, bf);
    } else {
        /* Not connected - send raw with newline */
        char raw_buf[BUFFER_LEN + 2];
        snprintf(raw_buf, sizeof(raw_buf), "%s\n", bf);
        queue_string(d, raw_buf);
    }
}

/**
 * WEMIT command - emit to all connected players (world emit)
 * @param player Emitter
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 */
void do_wemit(dbref player, char *arg1, char *arg2)
{
    struct descriptor_data *d;
    char *message = NULL;
    char buf[BUFFER_LEN];
    char *bf;
    
    /* Check permission */
    if (!power(player, POW_BROADCAST)) {
        notify(player, "Permission denied.");
        return;
    }
    
    /* Process message */
    message = reconstruct_message(arg1, arg2);
    if (!message) {
        notify(player, "Invalid message.");
        return;
    }
    
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    free(message);
    
    /* Send to all connected players */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED) {
            notify(d->player, bf);
        }
    }
    
    /* Feedback to emitter */
    if (!(db[player].flags & QUIET)) {
        notify(player, tprintf("World emit sent: \"%s\"", bf));
    }
}
