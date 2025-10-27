/* broadcast.c - Global announcement and broadcast functions
 * Located in comm/ directory
 * 
 * This file contains functions for system-wide announcements.
 * Includes both player announcements (with cost) and admin broadcasts.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "player.h"

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Get announcement name for a player (with or without announce power)
 * @param player Player to get name for
 * @return Formatted name string (allocated)
 */
static char *announce_name(dbref player)
{
    if (!GoodObject(player)) {
        return stralloc("*INVALID*");
    }

    if (power(player, POW_ANNOUNCE)) {
        return stralloc(db[player].cname);
    } else {
        return stralloc(unparse_object(player, player));
    }
}

/* ===================================================================
 * Announcement Functions
 * =================================================================== */

/**
 * ANNOUNCE command - global announcement to all players
 * Costs credits unless player has announce power
 * @param player Announcer
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 */
void do_announce(dbref player, char *arg1, char *arg2)
{
    char *message;
    char buf[BUFFER_LEN];
    char *ann_name;
    
    /* Validate player */
    if (!GoodObject(player)) {
        return;
    }
    
    /* Check permissions */
    if (Guest(player) || (Typeof(player) != TYPE_PLAYER)) {
        notify(player, "You can't do that.");
        return;
    }
    
    /* Reconstruct message from arguments */
    message = reconstruct_message(arg1, arg2);
    if (!message || !*message) {
        notify(player, "Announce what?");
        if (message) {
            SAFE_FREE(message);
        }
        return;
    }
    
    /* Check payment (free for those with announce power) */
    if (!power(player, POW_ANNOUNCE) && !payfor(player, announce_cost)) {
        notify(player, "Sorry, you don't have enough credits.");
        SAFE_FREE(message);
        return;
    }
    
    /* Get announcement name */
    ann_name = announce_name(player);
    
    /* Format announcement based on message type */
    if (*message == POSE_TOKEN) {
        /* Pose format: ":waves" */
        snprintf(buf, sizeof(buf), "%s announce-poses: %s %s", 
                ann_name, db[player].cname, message + 1);
    } else if (*message == NOSP_POSE) {
        /* Possessive pose: ";'s great" */
        snprintf(buf, sizeof(buf), "%s announce-poses: %s's %s", 
                ann_name, db[player].cname, message + 1);
    } else if (*message == THINK_TOKEN) {
        /* Think bubble: ".thinks quietly" */
        snprintf(buf, sizeof(buf), "%s announce-thinks: %s . o O ( %s )", 
                ann_name, db[player].cname, message + 1);
    } else {
        /* Regular say format */
        snprintf(buf, sizeof(buf), "%s announces \"%s\"", 
                ann_name, message);
    }
    
    /* Log the announcement */
    log_io(tprintf("%s [owner=%s] executes: @announce %s", 
                  unparse_object_a(player, player),
                  unparse_object_a(db[player].owner, db[player].owner), 
                  message));
    
    /* Send to monitoring channel */
    com_send_as_hidden("pub_io", 
                      tprintf("%s [owner=%s] executes: @announce %s", 
                             unparse_object_a(player, player),
                             unparse_object_a(db[player].owner, db[player].owner), 
                             message), 
                      player);
    
    /* Send to all players (respects PLAYER_NO_WALLS flag) */
    notify_all(buf, NOTHING, 1);
    
    /* Clean up */
    SAFE_FREE(ann_name);
    SAFE_FREE(message);
}

/**
 * BROADCAST command - official system-wide message
 * Requires broadcast power, goes to everyone
 * @param player Broadcaster
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 */
void do_broadcast(dbref player, char *arg1, char *arg2)
{
    char *message;
    char buf[BUFFER_LEN];
    
    /* Validate player */
    if (!GoodObject(player)) {
        return;
    }
    
    /* Check permission */
    if (!power(player, POW_BROADCAST)) {
        notify(player, "You don't have the authority to do that.");
        return;
    }
    
    /* Reconstruct message */
    message = reconstruct_message(arg1, arg2);
    if (!message || !*message) {
        notify(player, "Broadcast what?");
        if (message) {
            SAFE_FREE(message);
        }
        return;
    }
    
    /* Format broadcast */
    snprintf(buf, sizeof(buf), "Official broadcast from %s: \"%s\"",
            db[player].cname, message);
    
    /* Log the broadcast */
    log_important(tprintf("%s executes: @broadcast %s",
                         unparse_object_a(player, player), message));
    
    /* Send to all players (ignores PLAYER_NO_WALLS) */
    notify_all(buf, NOTHING, 0);
    
    /* Clean up */
    SAFE_FREE(message);
}

/* ===================================================================
 * Wall/Shout Functions (similar to announce but different format)
 * =================================================================== */

/**
 * WALL command - send message to all players in same zone
 * @param player Sender
 * @param message Message to send
 */
void do_wall(dbref player, const char *message)
{
    dbref zone;
    struct descriptor_data *d;
    char buf[BUFFER_LEN];
    
    /* Validate inputs */
    if (!GoodObject(player)) {
        return;
    }
    
    if (!message || !*message) {
        notify(player, "Wall what?");
        return;
    }
    
    /* Get player's zone */
    zone = db[player].zone;
    if (zone == NOTHING) {
        notify(player, "You're not in a zone.");
        return;
    }
    
    /* Check permission to wall in this zone */
    if (!could_doit(player, zone, A_SLOCK)) {
        notify(player, "You can't wall in this zone.");
        return;
    }
    
    /* Format message */
    snprintf(buf, sizeof(buf), "[%s walls]: %s",
            db[player].cname, message);
    
    /* Send to all players in same zone */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && 
            GoodObject(d->player) &&
            db[d->player].zone == zone &&
            !(db[d->player].flags & PLAYER_NO_WALLS)) {
            notify(d->player, buf);
        }
    }
}

/**
 * SHOUT command - loud message to current room and adjacent rooms
 * @param player Shouter
 * @param message Message to shout
 */
void do_shout(dbref player, const char *message)
{
    dbref loc, exit;
    char buf[BUFFER_LEN];
    
    /* Validate inputs */
    if (!GoodObject(player)) {
        return;
    }
    
    if (!message || !*message) {
        notify(player, "Shout what?");
        return;
    }
    
    loc = db[player].location;
    if (!GoodObject(loc)) {
        return;
    }
    
    /* Check auditorium restrictions */
    if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
        (!could_doit(player, loc, A_SLOCK) ||
         !could_doit(player, db[loc].zone, A_SLOCK))) {
        notify(player, "Shhh! This is an auditorium.");
        return;
    }
    
    /* Format shout message */
    snprintf(buf, sizeof(buf), "%s shouts, \"%s\"",
            db[player].cname, message);
    
    /* Notify current room */
    notify_in(loc, NOTHING, buf);
    
    /* Notify adjacent rooms */
    DOLIST(exit, db[loc].exits) {
        dbref dest;
        
        if (!GoodObject(exit)) {
            continue;
        }
        
        dest = db[exit].link;
        if (GoodObject(dest) && Typeof(dest) == TYPE_ROOM) {
            char distant_buf[BUFFER_LEN];
            snprintf(distant_buf, sizeof(distant_buf),
                    "From a distance you hear %s shout, \"%s\"",
                    db[player].cname, message);
            notify_in(dest, NOTHING, distant_buf);
        }
    }
}

/* ===================================================================
 * System Message Functions
 * =================================================================== */

/**
 * Send a system message to all players
 * @param message Message to send
 * @param except Player to exclude (or NOTHING)
 * @param obey_walls 1 to respect PLAYER_NO_WALLS, 0 to ignore
 */
void system_announce(const char *message, dbref except, int obey_walls)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN];
    
    if (!message) {
        return;
    }
    
    /* Prefix with system identifier */
    snprintf(buf, sizeof(buf), "GAME: %s", message);
    
    /* Send to all connected players */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state != CONNECTED) {
            continue;
        }
        
        if (!GoodObject(d->player)) {
            continue;
        }
        
        if (d->player == except) {
            continue;
        }
        
        if (obey_walls && (db[d->player].flags & PLAYER_NO_WALLS)) {
            continue;
        }
        
        notify(d->player, buf);
    }
}

/**
 * Notify all players about a connection event
 * @param player Player who connected/disconnected
 * @param connected 1 for connection, 0 for disconnection
 */
void announce_connection(dbref player, int connected)
{
    char buf[BUFFER_LEN];
    const char *name;
    
    /* Validate player */
    if (!GoodObject(player)) {
        return;
    }
    
    /* Don't announce for dark players */
    if (Dark(player)) {
        return;
    }
    
    /* Don't announce for guests unless configured */
    if (Guest(player) && !announce_guests) {
        return;
    }
    
    /* Get appropriate name */
    name = db[player].cname;
    if (!name || !*name) {
        name = "Someone";
    }
    
    /* Format message */
    if (connected) {
        snprintf(buf, sizeof(buf), "%s has connected.", name);
    } else {
        snprintf(buf, sizeof(buf), "%s has disconnected.", name);
    }
    
    /* Send to monitoring channel */
    com_send("connect", buf);
    
    /* Optionally send to all players */
    if (announce_connects) {
        system_announce(buf, player, 1);
    }
}

/* ===================================================================
 * Emergency Broadcast System
 * =================================================================== */

/**
 * Send an emergency message that bypasses all filters
 * Used for shutdown warnings, critical errors, etc.
 * @param message Emergency message
 */
void emergency_broadcast(const char *message)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN + 100];
    
    if (!message) {
        return;
    }
    
    /* Format with attention-getting prefix */
    snprintf(buf, sizeof(buf), 
            "\n*** EMERGENCY BROADCAST ***\n%s\n*** END BROADCAST ***\n",
            message);
    
    /* Send to ALL descriptors, even non-connected ones */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && GoodObject(d->player)) {
            /* Connected player - use notify */
            notify(d->player, buf);
        } else {
            /* Not connected - send raw */
            queue_string(d, buf);
        }
        
        /* Force output to be sent immediately */
        process_output(d);
    }
    
    /* Log the emergency */
    log_important(tprintf("EMERGENCY BROADCAST: %s", message));
}

/* ===================================================================
 * Helper Functions for Message Distribution
 * =================================================================== */

/**
 * Send message to all players in a specific location
 * @param loc Location to send to
 * @param except Player to exclude (or NOTHING)
 * @param message Message to send
 */
void notify_location(dbref loc, dbref except, const char *message)
{
    dbref thing;
    
    if (!GoodObject(loc) || !message) {
        return;
    }
    
    /* Notify all contents of location */
    DOLIST(thing, db[loc].contents) {
        if (!GoodObject(thing) || thing == except) {
            continue;
        }
        
        /* Notify players and puppets */
        if (Typeof(thing) == TYPE_PLAYER) {
            notify(thing, message);
        } else if ((Typeof(thing) == TYPE_THING) && 
                   (db[thing].flags & PUPPET) &&
                   GoodObject(db[thing].owner)) {
            notify(db[thing].owner, message);
        }
    }
}

/**
 * Send message to all players who can hear in a location
 * Respects listening objects and auditorium mode
 * @param loc Location to send to
 * @param speaker Speaker (for auditorium checks)
 * @param message Message to send
 */
void notify_audible(dbref loc, dbref speaker, const char *message)
{
    dbref thing;
    int is_auditorium;
    
    if (!GoodObject(loc) || !message) {
        return;
    }
    
    is_auditorium = IS(loc, TYPE_ROOM, ROOM_AUDITORIUM);
    
    DOLIST(thing, db[loc].contents) {
        if (!GoodObject(thing)) {
            continue;
        }
        
        /* In auditorium, only speaker and room controllers can be heard */
        if (is_auditorium && 
            GoodObject(speaker) &&
            thing != speaker &&
            !controls(thing, loc, 0)) {
            continue;
        }
        
        /* Notify based on object type */
        if (Typeof(thing) == TYPE_PLAYER) {
            notify(thing, message);
        } else if (Listener(thing) && GoodObject(db[thing].owner)) {
            /* Listening objects relay to their owners */
            char relay_buf[BUFFER_LEN];
            snprintf(relay_buf, sizeof(relay_buf), 
                    "[%s] %s", db[thing].cname, message);
            notify(db[thing].owner, relay_buf);
        }
    }
}

/**
 * Count how many players would receive a broadcast
 * Useful for logging and statistics
 * @param obey_walls 1 to respect PLAYER_NO_WALLS
 * @return Number of recipients
 */
int count_broadcast_recipients(int obey_walls)
{
    struct descriptor_data *d;
    int count = 0;
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state != CONNECTED) {
            continue;
        }
        
        if (!GoodObject(d->player)) {
            continue;
        }
        
        if (obey_walls && (db[d->player].flags & PLAYER_NO_WALLS)) {
            continue;
        }
        
        count++;
    }
    
    return count;
}

/**
 * Send a timed broadcast (for scheduled announcements)
 * @param message Message to send
 * @param delay_seconds Seconds to wait before sending
 */
void schedule_broadcast(const char *message, int delay_seconds)
{
    /* TODO: Implement when queue system is available */
    /* For now, just send immediately */
    if (delay_seconds <= 0) {
        system_announce(message, NOTHING, 1);
    } else {
        /* Would need to use the command queue system */
        log_error("schedule_broadcast: Delayed broadcasts not yet implemented");
    }
}
