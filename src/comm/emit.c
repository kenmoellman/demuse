/* emit.c - Emit and spoof commands
 * Located in comm/ directory
 * 
 * This file contains all emit-related commands extracted from speech.c.
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
#include "player.h"  /* For player utilities */

/* Function prototypes */
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
    char mybuf[BUFFER_LEN];
    char *s;
    dbref thing, save_loc;
    
    /* Skip leading spaces */
    while (*msg == ' ') {
        msg++;
    }
    
    /* Copy first word */
    snprintf(mybuf, sizeof(mybuf), "%s", msg);
    
    /* Find end of first word */
    s = mybuf;
    while (*s && (*s != ' ')) {
        s++;
    }
    if (*s) {
        *s = '\0';
    }
    
    /* Check if first word is a player name they can't spoof */
    thing = lookup_player(mybuf);
    if (thing != NOTHING && !string_compare(db[thing].name, mybuf)) {
        if (!controls(player, thing, POW_REMOTE)) {
            return 0;  /* Can't spoof this player */
        }
    }
    
    /* Check for possessive form (name's) */
    if ((s - mybuf) > 2 && !strcmp(s - 2, "'s")) {
        *(s - 2) = '\0';
        
        thing = lookup_player(mybuf);
        if (thing != NOTHING && !string_compare(db[thing].name, mybuf)) {
            if (!controls(player, thing, POW_REMOTE)) {
                return 0;  /* Can't spoof this player's possessive */
            }
        }
    }
    
    /* Check if it matches an object in the location */
    save_loc = db[player].location;
    db[player].location = loc;
    init_match(player, mybuf, NOTYPE);
    match_perfect();
    db[player].location = save_loc;
    thing = match_result();
    
    /* Can't spoof objects in the room */
    if (thing != NOTHING) {
        return 0;
    }
    
    return 1;
}

/**
 * Notify all objects in a zone (recursive)
 * @param zone Zone to notify
 * @param msg Message to send
 */
static void notify_in_zone(dbref zone, const char *msg)
{
    dbref thing;
    static int depth = 0;
    char msgcpy[strlen(msg)+1];
    
    /* Prevent infinite recursion */
    if (depth > 10) {
        return;
    }
    
    depth++;
    
    /* Notify everything in this zone */
    for (thing = 0; thing < db_top; thing++) {
        if (db[thing].zone == zone) {
            /* Recursively notify sub-zones */
            notify_in_zone(thing, msg);
            /* Notify objects in this zone */
	    strcpy(msgcpy, msg);
            notify_in(thing, NOTHING, msgcpy);
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
    char *message;
    char buf[BUFFER_LEN];
    char *bf = buf;
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (!check_auditorium_permission(player, loc)) {
        return;
    }
    
    /* Reconstruct message */
    message = reconstruct_message(arg1, arg2);
    
    /* Handle pronoun substitution based on type */
    if (type == 0) {
        pronoun_substitute(buf, player, message, player);
        bf = buf + strlen(db[player].name) + 1;
    } else if (type == 1) {
        snprintf(buf, sizeof(buf), "%s", message);
        bf = buf;
    }
    
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
    char buf[BUFFER_LEN];
    char *bf = buf;
    
    /* Handle message formatting based on emit type */
    if (emittype != 4) {
        pronoun_substitute(buf, player, arg2, player);
        bf = buf + strlen(db[player].name) + 1;
    } else {
        /* Special handling for type 4 - find the = separator */
        bf = arg2;
        while (*bf && !(bf[0] == '=')) {
            bf++;
        }
        if (*bf) {
            bf++;
        }
        emittype = 0;  /* Treat as pemit after special parsing */
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
            if (can_emit_msg(player, db[who].location, bf) ||
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
                ((db[player].location == who) && can_emit_msg(player, who, bf))) {
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
            if (can_emit_msg(player, db[who].location, bf)) {
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
                can_emit_msg(player, (dbref)-1, bf)) {
                
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
    char buf[BUFFER_LEN];
    char *bf;
    
    /* Check permission */
    if (!power(player, POW_REMOTE)) {
        notify(player, "You don't have the authority to do that.");
        return;
    }
    
    /* Parse connection ID */
    if (!isdigit(*arg1)) {
        notify(player, "That's not a number.");
        return;
    }
    
    target = atol(arg1);
    
    /* Find the descriptor */
    for (d = descriptor_list; d && d->concid != target; d = d->next)
        ;
    
    if (!d) {
        notify(player, "Unable to find specified connection ID.");
        return;
    }
    
    /* Process message */
    pronoun_substitute(buf, player, arg2, player);
    bf = buf + strlen(db[player].name) + 1;
    
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
    char *message;
    char buf[BUFFER_LEN];
    char *bf;
    
    /* Check permission */
    if (!power(player, POW_BROADCAST)) {
        notify(player, "Permission denied.");
        return;
    }
    
    /* Process message */
    message = reconstruct_message(arg1, arg2);
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
    
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
