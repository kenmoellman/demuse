/* player.c - Player utility and helper functions
 * Located in muse/ directory
 * 
 * This file contains non-administrative player functions:
 * - Name and title formatting
 * - Player lookup and matching
 * - Connection status checking
 * - Player attributes and properties
 * - Idle and away status management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "match.h"

#define MAX_PLAYER_MATCHES 10


static int is_connected_raw(dbref who);  /* Forward declaration for raw check */

//extern dbref it;
//extern dbref matcher;
//extern char *match_name;
//extern dbref exact_match;

/* ===================================================================
 * Name and Title Functions (moved from speech.c)
 * =================================================================== */

/**
 * Get title for a player (name with alias if set)
 * @param player Player to get title for
 * @return Formatted title string (allocated)
 */
char *title(dbref player)
{
    char buf[BUFFER_LEN];
    const char *alias;
    
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return stralloc("*INVALID*");
    }
    
    alias = atr_get(player, A_ALIAS);
    
    if (!alias || !*alias) {
        return stralloc(db[player].cname);
    }
    
    snprintf(buf, sizeof(buf), "%s (%s)", db[player].cname, alias);
    return stralloc(buf);
}

/**
 * Get display name for a player with proper formatting
 * @param player Player to get name for
 * @param viewer Who is looking at the name
 * @return Formatted name (allocated)
 */
char *player_name(dbref player, dbref viewer)
{
    if (!GoodObject(player)) {
        return stralloc("*INVALID*");
    }
    
    if (Typeof(player) != TYPE_PLAYER) {
        return stralloc(db[player].name);
    }
    
    /* Use colorized name if viewer supports it */
    if (GoodObject(viewer) && (db[viewer].flags & PLAYER_ANSI)) {
        return stralloc(db[player].cname);
    }
    
    return stralloc(db[player].name);
}

/* ===================================================================
 * Player Lookup and Matching Functions
 * =================================================================== */

/**
 * Find a player by name (exact match)
 * @param name Player name to find
 * @return dbref of player or NOTHING
 */
dbref find_player(const char *name)
{
    dbref player;
    
    if (!name || !*name) {
        return NOTHING;
    }
    
    /* First try exact match via lookup_player */
    player = lookup_player(name);
    if (GoodObject(player)) {
        return player;
    }
    
    return NOTHING;
}

/**
 * Find multiple players matching a pattern
 * @param pattern Name pattern (supports wildcards)
 * @param viewer Who is searching (for permissions)
 * @return Array of dbrefs, first element is count
 */
dbref *find_players_pattern(const char *pattern, dbref viewer)
{
    static dbref matches[MAX_PLAYER_MATCHES + 1];
    int count = 0;
    dbref player;
    
    matches[0] = 0;  /* Count goes in first element */
    
    if (!pattern || !*pattern) {
        return matches;
    }
    
    /* TODO: Implement pattern matching */
    /* For now, just do exact match */
    player = find_player(pattern);
    if (GoodObject(player)) {
        matches[0] = 1;
        matches[1] = player;
    }
    
    return matches;
}

/* ===================================================================
 * Connection Status Functions
 * =================================================================== */

/**
 * Check if a player is connected
 * @param player Player to check
 * @return 1 if connected, 0 if not
 */
/**
 * Check if a player is connected (with optional visibility check)
 * @param viewer The player checking connection status (or NOTHING for raw check)
 * @param target The player to check if connected
 * @return 1 if connected (and visible), 0 otherwise
 *
 * When called as is_connected(NOTHING, player) or is_connected(player, NOTHING):
 *   - Does raw connection check (no hiding)
 * When called as is_connected(viewer, target):
 *   - Checks if viewer can see that target is connected (respects hiding)
 *
 * For backward compatibility with single-arg calls:
 *   is_connected(player) becomes is_connected(NOTHING, player)
 */
int is_connected(dbref viewer, dbref target)
{
    struct descriptor_data *d;
    dbref who;

    /* Handle backward compatibility - single arg passed as first param */
    if (target == NOTHING || target == 0) {
        /* Old style: is_connected(player) */
        who = viewer;
        viewer = NOTHING;  /* No viewer means raw check */
    } else {
        who = target;
    }

    /* Validate target */
    if (!GoodObject(who) || Typeof(who) != TYPE_PLAYER) {
        return 0;
    }

    /* Check CONNECT flag first for efficiency */
    if (!(db[who].flags & CONNECT)) {
        return 0;
    }

    /* Verify descriptor actually exists */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == who) {
            /* Found connected descriptor */

            /* If no viewer specified, just return connected status */
            if (viewer == NOTHING) {
                return 1;  /* Raw connection check */
            }

            /* Check if viewer can see this connected player */
            /* Handle hiding with A_LHIDE attribute */
            if (*atr_get(who, A_LHIDE) && !controls(viewer, who, POW_WHO)) {
                /* Player is hiding - check if viewer can see them */
                return could_doit(viewer, who, A_LHIDE);
            }

            return 1;  /* Connected and visible */
        }
    }

    /* Flag was wrong, clear it */
    db[who].flags &= ~CONNECT;
    return 0;
}

/**
 * Raw connection check - no hiding, just checks if really connected
 * This replaces the old is_connected_raw function
 */
static int is_connected_raw(dbref who)
{
    return is_connected(NOTHING, who);
}


/**
 * Get idle time for a connected player
 * @param player Player to check
 * @return Idle time in seconds, or -1 if not connected
 */
time_t get_idle_time(dbref player)
{
    struct descriptor_data *d;
    
    if (!is_connected(NOTHING, player)) {
        return -1;
    }
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            return now - d->last_time;
        }
    }
    
    return -1;
}

/**
 * Get connection time for a player
 * @param player Player to check
 * @return Connection time in seconds, or -1 if not connected
 */
time_t get_conn_time(dbref player)
{
    struct descriptor_data *d;
    
    if (!is_connected(NOTHING, player)) {
        return -1;
    }
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            return now - d->connected_at;
        }
    }
    
    return -1;
}

/**
 * Get descriptor for a connected player
 * @param player Player to find descriptor for
 * @return Descriptor pointer or NULL
 */
struct descriptor_data *get_descriptor(dbref player)
{
    struct descriptor_data *d;
    
    if (!is_connected(NOTHING, player)) {
        return NULL;
    }
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            return d;
        }
    }
    
    return NULL;
}

/* ===================================================================
 * Player Status Functions
 * =================================================================== */

/**
 * Check if player is idle
 * @param player Player to check
 * @return 1 if idle, 0 if not
 */
int is_idle(dbref player)
{
    time_t idle_time;
    
    if (!is_connected(NOTHING, player)) {
        return 0;
    }
    
    /* Check IDLE flag */
    if (db[player].flags & PLAYER_IDLE) {
        return 1;
    }
    
    /* Check actual idle time */
    idle_time = get_idle_time(player);
    if (idle_time > 0 && idle_time > default_idletime) {
        /* Set idle flag */
        db[player].flags |= PLAYER_IDLE;
        return 1;
    }
    
    return 0;
}

/**
 * Get away message for a player
 * @param player Player to check
 * @return Away message or NULL
 */
const char *get_away_message(dbref player)
{
    const char *away;
    
    if (!GoodObject(player)) {
        return NULL;
    }
    
    away = atr_get(player, A_AWAY);
    if (away && *away) {
        return away;
    }
    
    return NULL;
}

/**
 * Get idle message for a player
 * @param player Player to check
 * @return Idle message or NULL
 */
const char *get_idle_message(dbref player)
{
    const char *idle;
    
    if (!GoodObject(player)) {
        return NULL;
    }
    
    idle = atr_get(player, A_IDLE);
    if (idle && *idle) {
        return idle;
    }
    
    /* Check current idle message */
    idle = atr_get(player, A_IDLE_CUR);
    if (idle && *idle) {
        return idle;
    }
    
    return NULL;
}

/**
 * Set away message for a player
 * @param player Player to set message for
 * @param message Away message (NULL to clear)
 */
void set_away_message(dbref player, const char *message)
{
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return;
    }
    
    if (message && *message) {
        atr_add(player, A_AWAY, message);
        notify(player, tprintf("Away message set to: %s", message));
    } else {
        atr_clr(player, A_AWAY);
        notify(player, "Away message cleared.");
    }
}

/* ===================================================================
 * Player Property Functions
 * =================================================================== */

/**
 * Check if player is a guest
 * @param player Player to check
 * @return 1 if guest, 0 if not
 */
int is_guest(dbref player)
{
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return 0;
    }
    
    return Guest(player);
}

/**
 * Check if player is a robot/puppet
 * @param player Player to check
 * @return 1 if robot, 0 if not
 */
int is_robot(dbref player)
{
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return 0;
    }
    
    return Robot(player);
}

/**
 * Check if player has ANSI color support
 * @param player Player to check
 * @return 1 if ANSI enabled, 0 if not
 */
int has_ansi(dbref player)
{
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return 0;
    }
    
    return (db[player].flags & PLAYER_ANSI) ? 1 : 0;
}

/**
 * Check if player wants beeps suppressed
 * @param player Player to check
 * @return 1 if nobeep, 0 if beeps allowed
 */
int is_nobeep(dbref player)
{
    if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
        return 0;
    }
    
    return (db[player].flags & PLAYER_NOBEEP) ? 1 : 0;
}

/**
 * Check if player is hidden (for pages, who, etc.)
 * @param player Player to check
 * @return 1 if hidden, 0 if visible
 */
int is_hidden(dbref player)
{
    const char *lhide;
    const char *blacklist;
    
    if (!GoodObject(player)) {
        return 0;
    }
    
    /* Check LHIDE attribute */
    lhide = atr_get(player, A_LHIDE);
    if (lhide && *lhide) {
        return 1;
    }
    
    /* Check BLACKLIST attribute */
    blacklist = atr_get(player, A_BLACKLIST);
    if (blacklist && *blacklist) {
        return 1;
    }
    
    /* Check DARK flag (for wizards) */
    if (Dark(player) && Wizard(player)) {
        return 1;
    }
    
    return 0;
}

/* ===================================================================
 * Player Location Functions
 * =================================================================== */

/**
 * Get the room a player is in
 * @param player Player to check
 * @return dbref of room or NOTHING
 */
dbref get_player_room(dbref player)
{
    dbref loc;
    
    if (!GoodObject(player)) {
        return NOTHING;
    }
    
    loc = db[player].location;
    
    /* Follow location chain to find room */
    while (GoodObject(loc) && Typeof(loc) != TYPE_ROOM) {
        loc = db[loc].location;
    }
    
    return loc;
}

/**
 * Check if two players can interact (same room or remote powers)
 * @param player First player
 * @param target Second player
 * @return 1 if can interact, 0 if not
 */
int can_interact(dbref player, dbref target)
{
    dbref ploc, tloc;
    
    if (!GoodObject(player) || !GoodObject(target)) {
        return 0;
    }
    
    /* Remote power allows interaction anywhere */
    if (power(player, POW_REMOTE)) {
        return 1;
    }
    
    /* Check if in same room */
    ploc = get_player_room(player);
    tloc = get_player_room(target);
    
    if (ploc == tloc && ploc != NOTHING) {
        return 1;
    }
    
    return 0;
}

/* ===================================================================
 * Player Matching Functions
 * =================================================================== */

/**
 * Unified player matching function
 * @param player The player doing the matching (or NULL for stateful mode)
 * @param name The name to match (or NULL for stateful mode)
 * @return dbref of matched player or NOTHING (or sets exact_match in stateful mode)
 * 
 * When called with (NULL, NULL), uses global matching state variables
 * When called with (player, name), does direct matching and returns result
 */
dbref match_player(dbref player, const char *name)
{
    dbref match;
    const char *p;
    const char *lookup_name;
    dbref context_player;
    
    /* Check if we're in stateful mode (both args NULL) */
    if (player == NOTHING && name == NULL) {
        /* Use global matching state variables */
        extern dbref it;           /* From match.c globals */
        dbref matcher;      /* The player doing the match */
        extern char *match_name;   /* The name being matched */
        extern dbref exact_match;  /* Where to store the result */
        
        /* First check if 'it' is already a player */
        if (it != NOTHING && Typeof(it) == TYPE_PLAYER) {
            exact_match = it;
            return it;  /* Also return it for consistency */
        }
        
        /* Use globals for matching */
        lookup_name = match_name;
        context_player = matcher;
        
        /* Do the matching with global values */
        if (!lookup_name || !*lookup_name) {
            return NOTHING;
        }
        
        /* Handle lookup token (*name) */
        if (*lookup_name == LOOKUP_TOKEN) {
            for (p = lookup_name + 1; isspace(*p); p++);
            lookup_name = p;
        }
        
        /* Try exact player name match */
        match = lookup_player(lookup_name);
        if (match != NOTHING && Typeof(match) == TYPE_PLAYER) {
            exact_match = match;  /* Set global result */
            return match;
        }
        
        /* No match in stateful mode */
        return NOTHING;
    }
    
    /* Direct mode - use provided arguments */
    if (!name || !*name) {
        return NOTHING;
    }
    
    lookup_name = name;
    context_player = player;
    
    /* Handle lookup token (*name) */
    if (*lookup_name == LOOKUP_TOKEN) {
        for (p = lookup_name + 1; isspace(*p); p++);
        lookup_name = p;
    }
    
    /* Try "me" */
    if (context_player != NOTHING && !strcasecmp(lookup_name, "me")) {
        return context_player;
    }
    
    /* Try exact player name match */
    match = lookup_player(lookup_name);
    if (match != NOTHING && Typeof(match) == TYPE_PLAYER) {
        return match;
    }
    
    /* TODO: Add partial matching if needed */
    /* match = partial_match_player(lookup_name); */
    
    return NOTHING;
}

/* ===================================================================
 * Mass Player Operations
 * =================================================================== */

/**
 * Notify all players matching a criteria
 * @param message Message to send
 * @param except Player to exclude (or NOTHING)
 * @param flags Criteria flags
 */
void notify_players(const char *message, dbref except, int flags)
{
    struct descriptor_data *d;
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state != CONNECTED) {
            continue;
        }
        
        if (d->player == except) {
            continue;
        }
        
        /* Check various flags */
        if ((flags & 1) && (db[d->player].flags & PLAYER_NO_WALLS)) {
            continue;  /* Skip if no-walls and flag set */
        }
        
        notify(d->player, message);
    }
}

/**
 * Count connected players
 * @return Number of connected players
 */
int count_connected(void)
{
    struct descriptor_data *d;
    int count = 0;
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED) {
            count++;
        }
    }
    
    return count;
}

/**
 * Get list of connected players
 * @param buffer Array to fill with dbrefs
 * @param maxsize Maximum size of array
 * @return Number of players added to buffer
 */
int get_connected_players(dbref *buffer, int maxsize)
{
    struct descriptor_data *d;
    int count = 0;
    
    for (d = descriptor_list; d && count < maxsize; d = d->next) {
        if (d->state == CONNECTED) {
            buffer[count++] = d->player;
        }
    }
    
    return count;
}
