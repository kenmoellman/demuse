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
#include <crypt.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>

#include "copyright.h"
#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "match.h"
#include "credits.h"
#include "player.h"
#include "admin.h"
#include "hash_table.h"

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



/* player.c - Player management and authentication
 * 
 * This file handles player creation, authentication, class/power management,
 * and resource tracking (credits, quota).
 */


/* ===================================================================
 * Constants and Limits
 * =================================================================== */

#define MAX_SAFE_CREDITS (LONG_MAX / 2)
#define MIN_CREDITS 0
#define MAX_BOOT_ITERATIONS 100  /* Reduced from 1000 for DoS prevention */
#define MAX_PLAYERS_LIST 1000
#define MAX_THINGS_LIST 10000

/* Password-related constants */
#define MIN_PASSWORD_LENGTH 4
#define MAX_PASSWORD_LENGTH 128
#define CRYPT_SALT "XX"



/* Temporary const casting - remove when headers updated */
#define CONST_CAST(type, ptr) ((type)(uintptr_t)(ptr))

/* Const-safe version of SET macro */
//#define SET_CONST(astr, bstr) do { \
//    char **__a = &(astr); \
//    const char *__b = (bstr); \
//    if (*__a) free(*__a); \
//    if (!__b || !*__b) { \
//        *__a = NULL; \
//    } else { \
//        *__a = malloc(strlen(__b) + 1); \
//        if (*__a) strcpy(*__a, __b); \
//    } \
//} while(0)

#define SET_CONST(astr, bstr) do { \
    char **__a = &(astr); \
    const char *__b = (bstr); \
    if (*__a) SMART_FREE(*__a); \
    if (!__b || !*__b) { \
        *__a = NULL; \
    } else { \
        SAFE_MALLOC(*__a, char, strlen(__b) + 1); \
        { size_t __len = strlen(__b) + 1; strncpy(*__a, __b, __len - 1); (*__a)[__len - 1] = '\0'; } \
    } \
} while(0)


/* ===================================================================
 * Safe Utility Functions
 * =================================================================== */

/**
 * Safely convert string to long with error checking
 * @param str String to convert
 * @param result Pointer to store result
 * @return 1 on success, 0 on failure
 */
static int safe_atol(const char *str, long *result)
{
    char *endptr;
    long val;
    
    if (!str || !result) {
        return 0;
    }
    
    while (*str && isspace(*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return 0;
    }
    
    errno = 0;
    val = strtol(str, &endptr, 10);
    
    if (errno == ERANGE) {
        return 0;
    }
    
    if (endptr == str) {
        return 0;
    }
    
    while (*endptr && isspace(*endptr)) {
        endptr++;
    }
    
    if (*endptr != '\0') {
        return 0;
    }
    
    *result = val;
    return 1;
}

/**
 * Check if multiplication would overflow
 * @param a First operand
 * @param b Second operand
 * @return 1 if overflow would occur, 0 if safe
 */
static int would_overflow_mul(long a, long b)
{
    if (a == 0 || b == 0) {
        return 0;
    }
    
    if (a > 0) {
        if (b > 0) {
            if (a > LONG_MAX / b) return 1;
        } else {
            if (b < LONG_MIN / a) return 1;
        }
    } else {
        if (b > 0) {
            if (a < LONG_MIN / b) return 1;
        } else {
            if (a < LONG_MAX / b) return 1;
        }
    }
    
    return 0;
}

/**
 * Check if addition would overflow
 * @param a First value
 * @param b Second value
 * @return 1 if overflow would occur, 0 if safe
 */
static int would_overflow_add(long a, long b)
{
    if (b > 0 && a > LONG_MAX - b) {
        return 1;
    }
    
    if (b < 0 && a < LONG_MIN - b) {
        return 1;
    }
    
    return 0;
}

/**
 * Safe string comparison that handles NULL pointers
 * @param s1 First string (may be NULL)
 * @param s2 Second string (may be NULL)
 * @return 0 if equal, non-zero if different
 */
static int safe_strcmp(const char *s1, const char *s2)
{
    if (s1 == NULL && s2 == NULL) {
        return 0;
    }
    
    if (s1 == NULL || s2 == NULL) {
        return 1;
    }
    
    return strcmp(s1, s2);
}

/**
 * Safe string copy with bounds checking
 * @param dest Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 * @return 1 on success, 0 if truncated
 */
static int safe_strcpy(char *dest, const char *src, size_t size)
{
    if (!dest || !src || size == 0) {
        return 0;
    }
    
    size_t src_len = strlen(src);
    
    if (src_len >= size) {
        /* Truncate */
        strncpy(dest, src, size - 1);
        dest[size - 1] = '\0';
        return 0;
    }
    
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
    return 1;
}

/**
 * Validate password meets minimum requirements
 * @param password Password to validate
 * @return 1 if valid, 0 if invalid
 */
static int validate_password(const char *password)
{
    size_t len;
    
    if (!password) {
        return 0;
    }
    
    len = strlen(password);
    
    if (len < MIN_PASSWORD_LENGTH) {
        return 0;
    }
    
    if (len > MAX_PASSWORD_LENGTH) {
        return 0;
    }
    
    /* Could add additional checks here:
     * - Require mixed case
     * - Require numbers
     * - Require special characters
     */
    
    return 1;
}

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static void destroy_player(dbref player);

/* ===================================================================
 * Player Authentication
 * =================================================================== */

/**
 * Authenticate a player connection
 * @param name Player name
 * @param password Password to verify
 * @return Player dbref on success, NOTHING/PASSWORD on failure
 */
dbref connect_player(const char *name, const char *password)
{
    dbref player;
    const char *stored_pass;
    const char *owner_pass;
    
    if (!name || !password || !*password) {
        return NOTHING;
    }
    
    /* Cast away const for old API - temporary */
    player = lookup_player(CONST_CAST(char *, name));
    if (player == NOTHING) {
        return NOTHING;
    }
    
#ifdef USE_INCOMING
    if (Typeof(player) != TYPE_PLAYER && !power(player, POW_INCOMING)) {
        return NOTHING;
    }
#endif
    
    /* Get stored password safely */
    stored_pass = Pass(player);
    if (!stored_pass || !*stored_pass) {
        return PASSWORD;
    }
    
    /* Try direct comparison first */
    if (safe_strcmp(stored_pass, password) == 0) {
        return player;
    }
    
    /* Try encrypted comparison if stored password is encrypted */
    if (strncmp(stored_pass, CRYPT_SALT, 2) != 0) {
        if (safe_strcmp(crypt(password, CRYPT_SALT), stored_pass) == 0) {
            return player;
        }
    }
    
    /* Try owner's password as fallback */
    owner_pass = Pass(db[player].owner);
    if (owner_pass && *owner_pass) {
        /* Direct comparison */
        if (safe_strcmp(owner_pass, password) == 0) {
            return player;
        }
        /* Encrypted comparison */
        if (strncmp(owner_pass, CRYPT_SALT, 2) != 0) {
            if (safe_strcmp(crypt(password, CRYPT_SALT), owner_pass) == 0) {
                return player;
            }
        }
    }
    
    /* All authentication attempts failed */
    return PASSWORD;
}    

/* ===================================================================
 * Player Destruction
 * =================================================================== */

/**
 * Destroy a player and all their belongings
 * @param player Player to destroy
 */
static void destroy_player(dbref player)
{
    dbref loc, thing;
    
    /* Destroy all player-owned objects */
    for (thing = 0; thing < db_top; thing++) {
        if (db[thing].owner == player && thing != player) {
            moveto(thing, NOTHING);
            
            switch (Typeof(thing)) {
            case TYPE_CHANNEL:
            case TYPE_UNIVERSE:
            case TYPE_PLAYER:
                /* Handle puppets/slaves */
                if (db[thing].owner == player && db[player].owner == thing) {
                    db[thing].owner = thing;
                    db[player].owner = player;
                    destroy_player(thing);
                }
                do_empty(thing);
                break;
                
            case TYPE_THING:
                do_empty(thing);
                break;
                
            case TYPE_EXIT:
                loc = find_entrance(thing);
                s_Exits(loc, remove_first(Exits(loc), thing));
                do_empty(thing);
                break;
                
            case TYPE_ROOM:
                do_empty(thing);
                break;
            }
        }
    }
    
    /* Disconnect player */
    boot_off(player);
    
    /* Halt processes */
    do_halt(player, "", "");
    
    /* Move to nowhere */
    moveto(player, NOTHING);
    
    /* Remove from player list */
    delete_player(player);
    
    /* Destroy player object */
    do_empty(player);
}

/* ===================================================================
 * Administrative Player Commands
 * =================================================================== */

/**
 * @PCREATE - Admin command to create new player
 * @param creator Administrator creating the player
 * @param player_name Name for new player
 * @param player_password Password for new player
 */
void do_pcreate(dbref creator, const char *player_name, const char *player_password)
{
    dbref player;
    
    if (!player_name || !player_password) {
        notify(creator, "Usage: @pcreate <name>=<password>");
        return;
    }
    
    if (!power(creator, POW_PCREATE)) {
        log_important(tprintf("%s failed to: @pcreate %s=%s", 
                             unparse_object_a(root, creator),
                             player_name, player_password));
        notify(creator, perm_denied());
        return;
    }
    
    if (lookup_player(CONST_CAST(char *, player_name)) != NOTHING) {
        notify(creator, tprintf("There is already a %s", 
                               unparse_object(creator, 
                                            lookup_player(CONST_CAST(char *, player_name)))));
        return;
    }
    
    if (!ok_player_name(NOTHING, CONST_CAST(char *, player_name), "") || 
        strchr(player_name, ' ')) {
        notify(creator, tprintf("Illegal player name '%s'", player_name));
        return;
    }
    
    if (!validate_password(player_password)) {
        notify(creator, tprintf("Invalid password (must be %d-%d characters)", 
                               MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH));
        return;
    }
    
    player = create_player(player_name, player_password, CLASS_CITIZEN, player_start);
    if (player == NOTHING) {
        notify(creator, tprintf("Failure creating '%s'", player_name));
        return;
    }
    
    notify(creator, tprintf("New player '%s' created with password '%s'",
                           player_name, player_password));
    log_important(tprintf("%s executed: @pcreate %s", 
                         unparse_object_a(root, creator), 
                         unparse_object_a(root, player)));
    log_sensitive(tprintf("%s executed: @pcreate %s=%s", 
                         unparse_object_a(root, creator),
                         unparse_object_a(root, player), 
                         player_password));
}

/**
 * @PASSWORD - Change player password
 * @param player Player changing password
 * @param old Old password
 * @param newobj New password
 */
void do_password(dbref player, const char *old, const char *newobj)
{
    const char *stored_pass;
    
    if (!old || !newobj) {
        notify(player, "Usage: @password <old>=<new>");
        return;
    }
    
    if (!has_pow(player, NOTHING, POW_MEMBER)) {
        notify(player, tprintf("Only registered %s users may change their passwords.",
                              muse_name));
        return;
    }
    
    /* Get stored password safely */
    stored_pass = Pass(player);
    
    /* Validate stored password exists */
    if (!stored_pass || !*stored_pass) {
        notify(player, "Your password is not set. Contact an administrator.");
        return;
    }
    
    /* Verify old password */
    if (safe_strcmp(old, stored_pass) != 0 && 
        safe_strcmp(crypt(old, CRYPT_SALT), stored_pass) != 0) {
        notify(player, "Incorrect password.");
        log_security(tprintf("Failed password change attempt by %s", 
                            unparse_object_a(player, player)));
        return;
    }
    
    /* Validate new password */
    if (!validate_password(newobj)) {
        notify(player, tprintf("Invalid new password (must be %d-%d characters)", 
                              MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH));
        return;
    }
    
    /* Set new password */
    s_Pass(player, crypt(newobj, CRYPT_SALT));
    notify(player, "Password changed successfully.");
    log_security(tprintf("%s changed their password", 
                        unparse_object_a(player, player)));
}

/**
 * @NUKE - Destroy a player completely
 * @param player Administrator nuking
 * @param name Player to nuke
 */
void do_nuke(dbref player, const char *name)
{
    dbref victim;
    int x;

    if (!name || !*name) {
        notify(player, "Usage: @nuke <player>");
        return;
    }

    if (!power(player, POW_NUKE) || Typeof(player) != TYPE_PLAYER) {
        notify(player, "This is a restricted command.");
        return;
    }

    init_match(player, CONST_CAST(char *, name), TYPE_PLAYER);
    match_neighbor();
    match_absolute();
    match_player(NOTHING, NULL);

    victim = noisy_match_result();
    if (victim == NOTHING) {
        return;
    }

    if (Typeof(victim) != TYPE_PLAYER) {
        notify(player, "You can only nuke players!");
        return;
    }

    if (!controls(player, victim, POW_NUKE)) {
        log_important(tprintf("%s failed to: @nuke %s",
                             unparse_object_a(player, player),
                             unparse_object_a(victim, victim)));
        notify(player, perm_denied());
        return;
    }

    if (owns_stuff(victim)) {
        notify(player, "You must @wipeout their belongings first.");
        return;
    }

    /* Boot them off (with DoS protection) */
    for (x = 0; x < MAX_BOOT_ITERATIONS; x++) {
        if (!boot_off(victim)) {
            break;
        }
    }

    /* Clean up */
    do_halt(victim, "", "");
    delete_player(victim);
    db[victim].flags = TYPE_THING;
    db[victim].owner = root;
    destroy_obj(victim, atol(default_doomsday));

    notify(player, tprintf("%s - Nuked.", db[victim].cname));
    log_important(tprintf("%s executed: @nuke %s",
                         unparse_object_a(player, player),
                         unparse_object_a(victim, victim)));
}

/* ===================================================================
 * Power System Functions
 * =================================================================== */

/**
 * Convert power name to number
 * @param nam Power name
 * @return Power number or 0 if not found
 */
ptype name_to_pow(const char *nam)
{
    int k;
    
    if (!nam) {
        return 0;
    }
    
    for (k = 0; k < NUM_POWS; k++) {
        if (!string_compare(powers[k].name, CONST_CAST(char *, nam))) {
            return powers[k].num;
        }
    }
    
    return 0;
}

/**
 * Convert power number to name
 * @param pow Power number
 * @return Power name (allocated)
 */
char *pow_to_name(ptype pow)
{
    int k;
    
    for (k = 0; k < NUM_POWS; k++) {
        if (powers[k].num == pow) {
            return stralloc(powers[k].name);
        }
    }
    
    return stralloc("<unknown power>");
}

/**
 * Get player's class name
 * @param player Player to check
 * @return Class name (allocated)
 */
char *get_class(dbref player)
{
    extern char *type_to_name();
    
    if (Typeof(player) == TYPE_PLAYER) {
        return class_to_name(*db[player].pows);
    } else {
        return type_to_name(Typeof(player));
    }
}

/**
 * @CLASS - Reclassify a player
 * @param player Administrator executing command
 * @param arg1 Target player
 * @param class New class name
 */
void do_class(dbref player, const char *arg1, const char *class)
{
    int i, newlevel;
    dbref who;
    char *current_class;
    
    if (!arg1 || !*arg1) {
        who = player;
    } else {
        init_match(player, CONST_CAST(char *, arg1), TYPE_PLAYER);
        match_me();
        match_player(NOTHING, NULL);
        match_neighbor();
        match_absolute();
        
        who = noisy_match_result();
        if (who == NOTHING) {
            return;
        }
    }
    
    if (Typeof(who) != TYPE_PLAYER) {
        notify(player, "Not a player.");
        return;
    }
    
    if (!class || !*class) {
        /* Display current class */
        current_class = get_class(who);
        notify(player, tprintf("%s is %s %s", db[who].name,
                              (*current_class == 'O' || *current_class == 'A') ? "an" : "a", 
                              current_class));
        return;
    }
    
    /* Find requested class */
    i = name_to_class(CONST_CAST(char *, class));
    if (i == 0) {
        notify(player, tprintf("'%s': no such classification", class));
        return;
    }
    newlevel = i;
    
    /* Check permissions */
    if (!has_pow(player, who, POW_CLASS) ||
        Typeof(player) != TYPE_PLAYER ||
        (newlevel >= db[player].pows[0] && !is_root(player))) {
        
        log_important(tprintf("%s failed to: @class %s=%s", 
                             unparse_object_a(player, player),
                             unparse_object_a(who, who), 
                             class));
        notify(player, perm_denied());
        return;
    }
    
    /* Root must remain director */
    if (who == root && newlevel != CLASS_DIR) {
        notify(player, tprintf("Player #%" DBREF_FMT " cannot resign their position.", root));
        return;
    }
    
    log_important(tprintf("%s executed: @class %s=%s", 
                         unparse_object_a(player, player),
                         unparse_object_a(who, who), 
                         class));
    
    /* Set new class */
    notify(player, tprintf("%s is now reclassified as: %s",
                          db[who].name, class_to_name(newlevel)));
    notify(who, tprintf("You have been reclassified as: %s",
                       class_to_name(newlevel)));
    
    if (!db[who].pows) {
//        db[who].pows = malloc(sizeof(ptype) * 2);
        SAFE_MALLOC(db[who].pows, ptype, 2);
        if (!db[who].pows) {
            notify(player, "Memory allocation error!");
            return;
        }
        db[who].pows[1] = 0;
    }
    
    db[who].pows[0] = newlevel;
    
    /* Set default powers for class */
    for (i = 0; i < NUM_POWS; i++) {
        set_pow(who, powers[i].num, 
               powers[i].init[class_to_list_pos(newlevel)]);
    }
}

/**
 * @EMPOWER - Grant or revoke a specific power
 * @param player Administrator executing command
 * @param whostr Target player
 * @param powstr Power specification (power:value)
 */
void do_empower(dbref player, const char *whostr, const char *powstr)
{
    ptype pow;
    ptype powval;
    dbref who;
    int k;
    char *power_name;
    char *power_value;
    char powstr_copy[256];
    
    if (!whostr || !powstr) {
        notify(player, "Usage: @empower <player>=<power>:<value>");
        return;
    }
    
    if (Typeof(player) != TYPE_PLAYER) {
        notify(player, "You're not a player!");
        return;
    }
    
    /* Parse power string (power:value) */
    if (!safe_strcpy(powstr_copy, powstr, sizeof(powstr_copy))) {
        notify(player, "Power specification too long.");
        return;
    }
    
    power_name = powstr_copy;
    power_value = strchr(powstr_copy, ':');
    
    if (!power_value) {
        notify(player, "Power format: powertype:powerval");
        return;
    }
    
    *power_value++ = '\0';
    
    /* Parse power value */
    if (!string_compare(power_value, "yes")) {
        powval = PW_YES;
    } else if (!string_compare(power_value, "no")) {
        powval = PW_NO;
    } else if (!string_compare(power_value, "yeseq")) {
        powval = PW_YESEQ;
    } else if (!string_compare(power_value, "yeslt")) {
        powval = PW_YESLT;
    } else {
        notify(player, "Power value must be: yes, no, yeseq, or yeslt");
        return;
    }
    
    /* Get power number */
    pow = name_to_pow(power_name);
    if (!pow) {
        notify(player, tprintf("Unknown power: %s", power_name));
        return;
    }
    
    /* Match target */
    who = match_thing(player, whostr);
    if (who == NOTHING) {
        return;
    }
    
    if (Typeof(who) != TYPE_PLAYER) {
        notify(player, "Not a player.");
        return;
    }
    
    /* Check permissions */
    if (!has_pow(player, who, POW_SETPOW)) {
        log_important(tprintf("%s failed to: @empower %s=%s:%s",
                             unparse_object_a(player, player), 
                             unparse_object_a(who, who), 
                             power_name, power_value));
        notify(player, perm_denied());
        return;
    }
    
    if (get_pow(player, pow) < powval && !is_root(player)) {
        notify(player, "You don't have that power yourself!");
        return;
    }
    
    /* Set power */
    for (k = 0; k < NUM_POWS; k++) {
        if (powers[k].num == pow) {
            if (powers[k].max[class_to_list_pos(*db[db[who].owner].pows)] >= powval) {
                set_pow(who, pow, powval);
                log_important(tprintf("%s executed: @empower %s=%s:%s",
                                     unparse_object_a(player, player), 
                                     unparse_object_a(who, who), 
                                     power_name, power_value));
                
                if (powval != PW_NO) {
                    notify(who, tprintf("You have been given the power of %s.", 
                                       pow_to_name(pow)));
                    notify(player, tprintf("%s has been given the power of %s.", 
                                          db[who].name, pow_to_name(pow)));
                    
                    switch (powval) {
                    case PW_YES:
                        notify(who, "You can use it on anyone");
                        break;
                    case PW_YESEQ:
                        notify(who, "You can use it on people your class and under");
                        break;
                    case PW_YESLT:
                        notify(who, "You can use it on people under your class");
                        break;
                    }
                } else {
                    notify(who, tprintf("Your power of %s has been removed.", 
                                       pow_to_name(pow)));
                    notify(player, tprintf("%s's power of %s has been removed.", 
                                          db[who].name, pow_to_name(pow)));
                }
                return;
            } else {
                notify(player, "That exceeds the maximum for that level.");
                return;
            }
        }
    }
    
    notify(player, "Internal error in power system.");
}

/**
 * @POWERS - Display player's powers
 * @param player Viewer
 * @param whostr Target player
 */
void do_powers(dbref player, const char *whostr)
{
    dbref who;
    int k;
    char buf1[128];
    char buf2[2048];
    
    if (!whostr || !*whostr) {
        who = player;
    } else {
        who = match_thing(player, whostr);
        if (who == NOTHING) {
            return;
        }
    }
    
    if (Typeof(who) != TYPE_PLAYER) {
        notify(player, "Not a player.");
        return;
    }
    
    if (!controls(player, who, POW_EXAMINE) && player != who) {
        notify(player, perm_denied());
        return;
    }
    
    notify(player, tprintf("%s's powers:", db[who].name));
    
    for (k = 0; k < NUM_POWS; k++) {
        ptype m;
        const char *l = "";
        
        m = get_pow(who, powers[k].num);
        
        switch (m) {
        case PW_YES:
            l = "|R!+ALL|";
            break;
        case PW_YESLT:
            l = "|M!+LESS|";
            break;
        case PW_YESEQ:
            l = "|Y!+EQUAL|";
            break;
        case PW_NO:
            continue;
        }
        
        if (l && *l) {
            snprintf(buf1, sizeof(buf1), "|C!+[||B!+%s||C!+:|%s|C!+]|", 
                    powers[k].name, l);
            snprintf(buf2, sizeof(buf2), " ");
            strncat(buf2, "                 ", 
                   20 - strlen(strip_color(buf1)));
            notify(player, tprintf("%s%s|G+%s|", buf1, buf2, 
                                  powers[k].description));
        }
    }
}
    
/* ===================================================================
 * Resource Management (Credits & Quota)
 * =================================================================== */

/**
 * @MONEY - Display player's financial status
 * @param player Viewer
 * @param arg1 Target player (optional)
 * @param arg2 Unused
 */
void do_money(dbref player, const char *arg1, const char *arg2)
{
    int amt, assets;
    dbref who;
    const char *credits_str;
    char credits_buf[32];
    dbref total;
    dbref obj[NUM_OBJ_TYPES];
    dbref pla[NUM_CLASSES];
    long asset_calc;
    
    if (!arg1 || !*arg1) {
        who = player;
    } else {
        init_match(player, arg1, TYPE_PLAYER);
        match_me();
        match_player(NOTHING, NULL);
        match_neighbor();
        match_absolute();
        
        who = noisy_match_result();
        if (who == NOTHING) {
            return;
        }
    }
    
    if (!power(player, POW_EXAMINE)) {
        if (arg2 && *arg2) {
            notify(player, "You don't have the authority to do that.");
            return;
        }
        
        if (player != who) {
            notify(player, "You need a search warrant to do that.");
            return;
        }
    }
    
    /* Calculate assets with overflow protection */
    calc_stats(who, &total, obj, pla);
    
    asset_calc = 0;
    
    /* Calculate each asset type with overflow checking */
    if (obj[TYPE_EXIT] > 0) {
        if (would_overflow_mul(obj[TYPE_EXIT], exit_cost)) {
            notify(player, "Asset calculation overflow (exits).");
            return;
        }
        asset_calc += obj[TYPE_EXIT] * exit_cost;
    }
    
    if (obj[TYPE_THING] > 0) {
        if (would_overflow_mul(obj[TYPE_THING], thing_cost)) {
            notify(player, "Asset calculation overflow (things).");
            return;
        }
        if (would_overflow_add(asset_calc, obj[TYPE_THING] * thing_cost)) {
            notify(player, "Asset calculation overflow (total).");
            return;
        }
        asset_calc += obj[TYPE_THING] * thing_cost;
    }
    
    if (obj[TYPE_ROOM] > 0) {
        if (would_overflow_mul(obj[TYPE_ROOM], room_cost)) {
            notify(player, "Asset calculation overflow (rooms).");
            return;
        }
        if (would_overflow_add(asset_calc, obj[TYPE_ROOM] * room_cost)) {
            notify(player, "Asset calculation overflow (total).");
            return;
        }
        asset_calc += obj[TYPE_ROOM] * room_cost;
    }
    
    if (obj[TYPE_PLAYER] > 1) {
        long robots = obj[TYPE_PLAYER] - 1;
        if (would_overflow_mul(robots, robot_cost)) {
            notify(player, "Asset calculation overflow (robots).");
            return;
        }
        if (would_overflow_add(asset_calc, robots * robot_cost)) {
            notify(player, "Asset calculation overflow (total).");
            return;
        }
        asset_calc += robots * robot_cost;
    }
    
    assets = (int)asset_calc;
    
    /* Get credit amount */
    if (inf_mon(who)) {
        amt = 0;
        credits_str = "UNLIMITED";
    } else {
        amt = Pennies(who);
        snprintf(credits_buf, sizeof(credits_buf), "%d credits.", amt);
        credits_str = credits_buf;
    }
    
    notify(player, tprintf("Cash...........: %s", credits_str));
    notify(player, tprintf("Material Assets: %d credits.", assets));
    notify(player, tprintf("Total Net Worth: %d credits.", assets + amt));
    notify(player, " ");
    notify(player, "Note: material assets calculation is only an approximation.");
}

/**
 * @QUOTA - Display or set player's quota
 * @param player Viewer/setter
 * @param arg1 Target player (optional)
 * @param arg2 New quota value (optional)
 */
void do_quota(dbref player, const char *arg1, const char *arg2)
{
    dbref who;
    char buf[32];
    long owned, limit;
    long new_limit;
    const char *quota_str;
    const char *rquota_str;
    
    /* Check if setting quota */
    if (arg2 && *arg2) {
        if (!power(player, POW_SETQUOTA)) {
            notify(player, "You don't have the authority to change quotas.");
            return;
        }
    }
    
    if (!arg1 || !*arg1) {
        who = player;
    } else {
        who = lookup_player(arg1);
        if (who == NOTHING || Typeof(who) != TYPE_PLAYER) {
            notify(player, "Who?");
            return;
        }
    }
    
    if (Robot(who)) {
        notify(player, "Robots don't have quotas!");
        return;
    }
    
    /* Check authority */
    if (!controls(player, who, POW_SETQUOTA)) {
        notify(player, tprintf("You can't %s that player's quota.",
                              (arg2 && *arg2) ? "change" : "examine"));
        return;
    }
    
    /* Calculate current usage */
    quota_str = atr_get(who, A_QUOTA);
    rquota_str = atr_get(who, A_RQUOTA);
    
    if (!quota_str || !*quota_str) {
        quota_str = "0";
    }
    if (!rquota_str || !*rquota_str) {
        rquota_str = "0";
    }
    
    if (!safe_atol(quota_str, &owned)) {
        notify(player, "Error reading quota value.");
        return;
    }
    
    long rquota_val;
    if (!safe_atol(rquota_str, &rquota_val)) {
        notify(player, "Error reading remaining quota.");
        return;
    }
    
    owned = owned - rquota_val;
    
    /* Handle infinite quota */
    if (inf_quota(who)) {
        notify(player, tprintf("Objects: %ld   Limit: UNLIMITED", owned));
        return;
    }
    
    /* Set or display quota */
    if (!arg2 || !*arg2) {
        limit = owned + rquota_val;
        notify(player, tprintf("Objects: %ld   Limit: %ld", owned, limit));
    } else {
        /* Set new quota */
        if (!safe_atol(arg2, &new_limit)) {
            notify(player, "Invalid quota value.");
            return;
        }
        
        if (new_limit < 0) {
            notify(player, "Quota must be non-negative.");
            return;
        }
        
        /* Calculate remaining quota */
        long new_remaining = new_limit - owned;
        
        /* Store as relative values */
        snprintf(buf, sizeof(buf), "%ld", new_remaining);
        atr_add(who, A_RQUOTA, buf);
        snprintf(buf, sizeof(buf), "%ld", new_limit);
        atr_add(who, A_QUOTA, buf);
        
        notify(player, tprintf("Objects: %ld   Limit: %ld", owned, new_limit));
    }
}

/* ===================================================================
 * Player/Thing Lookup Functions
 * =================================================================== */

/**
 * Match a space-separated list of things
 * @param player Viewer
 * @param list Space-separated list of thing names
 * @return Array of dbrefs (first element is count)
 */
dbref *match_things(dbref player, const char *list)
{
    char *x;
    static dbref npl[MAX_THINGS_LIST];
    char in[1024];
    char *inp;
    
    if (!list || !*list) {
        notify(player, "You must give a list of things.");
        npl[0] = 0;
        return npl;
    }
    
    if (!safe_strcpy(in, list, sizeof(in))) {
        notify(player, "List too long.");
        npl[0] = 0;
        return npl;
    }
    
    npl[0] = 0;
    inp = in;
    
    while (inp && npl[0] < MAX_THINGS_LIST - 1) {
        x = parse_up(&inp, ' ');
        if (!x) {
            inp = x;
        } else {
            /* Handle {bracketed} names */
            if (*x == '{' && x[strlen(x) - 1] == '}') {
                x++;
                x[strlen(x) - 1] = '\0';
            }
            
            dbref thing = match_thing(player, x);
            if (thing != NOTHING) {
                npl[++npl[0]] = thing;
            }
        }
    }
    
    return npl;
}

/**
 * Look up a space-separated list of players
 * @param player Viewer
 * @param list Space-separated list of player names
 * @return Array of dbrefs (first element is count)
 */
dbref *lookup_players(dbref player, const char *list)
{
    char *x;
    static dbref npl[MAX_PLAYERS_LIST];
    char in[1024];
    char *inp;
    
    if (!list || !*list) {
        notify(player, "You must give a list of players.");
        npl[0] = 0;
        return npl;
    }
    
    if (!safe_strcpy(in, list, sizeof(in))) {
        notify(player, "List too long.");
        npl[0] = 0;
        return npl;
    }
    
    npl[0] = 0;
    inp = in;
    
    while (inp && npl[0] < MAX_PLAYERS_LIST - 1) {
        x = parse_up(&inp, ' ');
        if (!x) {
            inp = x;
        } else {
            dbref ply = lookup_player(x);
            if (ply == NOTHING) {
                notify(player, tprintf("I don't know who %s is.", x));
            } else {
                npl[++npl[0]] = ply;
            }
        }
    }
    
    return npl;
}

/* ===================================================================
 * Miscellaneous Player Functions
 * =================================================================== */

/**
 * Convert old class number to new system
 * @param lev Old level number
 * @return New class number
 */
int old_to_new_class(int lev)
{
    switch (lev) {
    case 0x8:
        return CLASS_GUEST;
    case 0x9:
        return CLASS_VISITOR;
    case 0xA:
        return CLASS_CITIZEN;
    case 0xB:
        return CLASS_JUNOFF;
    case 0xC:
        return CLASS_OFFICIAL;
    case 0xD:
        return CLASS_BUILDER;
    case 0xE:
        return CLASS_ADMIN;
    case 0xF:
        return CLASS_DIR;
    default:
        return CLASS_VISITOR;
    }
}

/**
 * Stub for @NOPOW_CLASS command
 * @param player Player executing command
 * @param arg1 Target player
 * @param class Class name
 */
void do_nopow_class(dbref player, const char *arg1, const char *class)
{
    /* Implementation similar to do_class but doesn't set powers */
    /* Not fully implemented in original - kept for compatibility */
    notify(player, "This command is not fully implemented.");
}

/**
 * Stub for @MISC command
 * @param player Player executing command
 * @param arg1 First argument
 * @param arg2 Second argument
 */
void do_misc(dbref player, const char *arg1, const char *arg2)
{
    /* Placeholder - not implemented */
    notify(player, "This command is not implemented.");
}

/* ===================================================================
 * Player Creation
 * =================================================================== */

/**
 * Create a guest player
 * @param name Guest name
 * @param alias Guest alias
 * @param password Guest password (usually fixed)
 * @return New player dbref or NOTHING on failure
 */
dbref create_guest(const char *name, const char *alias, const char *password)
{
    dbref player;
    char key[32];
    
    if (!name || !alias) {
        return NOTHING;
    }
    
    /* Clean up old guest if exists */
    player = lookup_player(CONST_CAST(char *, name));
    if (player != NOTHING) {
        if (db[player].pows && Guest(player)) {
            destroy_player(player);
        } else {
            return NOTHING;
        }
    }
    
    /* Create new player object */
    player = new_object();
    
    /* Initialize player - use SET_CONST for const strings */
    SET_CONST(db[player].name, name);
    SET_CONST(db[player].cname, name);
    db[player].location = guest_start;
    db[player].link = guest_start;
    db[player].owner = player;
    db[player].flags = TYPE_PLAYER;
//    db[player].pows = malloc(sizeof(ptype) * 2);
    SAFE_MALLOC(db[player].pows, ptype, 2);
    
    if (!db[player].pows) {
        log_error("Out of memory creating guest!");
        return NOTHING;
    }
    
    db[player].pows[0] = CLASS_GUEST;
    db[player].pows[1] = 0;
    
    /* Set password */
    s_Pass(player, crypt(password ? password : GUEST_PASSWORD, CRYPT_SALT));
    
    /* Set initial resources */
    giveto(player, initial_credits);
    atr_add(player, A_RQUOTA, start_quota);
    atr_add(player, A_QUOTA, start_quota);
    
    /* Place in start location */
    PUSH(player, db[guest_start].contents);
    
    /* Add to player list */
    add_player(player);
    
    /* Auto-join public channel */
    do_force(root, tprintf("#%" DBREF_FMT, player), "+channel +public");
    
    /* Set self-lock */
    snprintf(key, sizeof(key), "#%" DBREF_FMT, player);
    atr_add(player, A_LOCK, key);
    
    /* Set description */
    if (guest_description && *guest_description) {
        atr_add(player, A_DESC, CONST_CAST(char *, guest_description));
    }
    
    /* Set alias */
    if (alias && *alias) {
        delete_player(player);
        atr_add(player, A_ALIAS, CONST_CAST(char *, alias));
        add_player(player);
    }
    
    /* Zero quota for guests */
    atr_add(player, A_RQUOTA, "0");
    atr_add(player, A_QUOTA, "0");
    
    return player;
}

/**
 * Destroy a guest player (safety check included)
 * @param guest Guest player to destroy
 */
void destroy_guest(dbref guest)
{
    if (!Guest(guest)) {
        return;
    }
    
    destroy_player(guest);
}

/**
 * Create a new player
 * @param name Player name
 * @param password Player password
 * @param class Player class
 * @param start Starting location
 * @return New player dbref or NOTHING on failure
 */

dbref create_player(const char *name, const char *password, int class, dbref start)
{
    dbref player;

    if (!name || !password) {
        log_error("create_player: NULL name or password");
        report();
        return NOTHING;
    }

    if (!ok_player_name(NOTHING, CONST_CAST(char *, name), "")) {
        log_error("create_player: failed name check");
        report();
        return NOTHING;
    }

    if (class != CLASS_GUEST && !ok_password(CONST_CAST(char *, password))) {
        log_error("create_player: failed password check");
        report();
        return NOTHING;
    }

    if (strchr(name, ' ')) {
        log_error("create_player: name contains space");
        report();
        return NOTHING;
    }

    /* Create player object */
    player = new_object();

    /* Initialize - use SET_CONST for const strings */
    SET_CONST(db[player].name, name);
    SET_CONST(db[player].cname, name);
    db[player].location = start;
    db[player].link = start;
    db[player].owner = player;
    db[player].flags = TYPE_PLAYER;
//    db[player].pows = malloc(sizeof(ptype) * 2);
    SAFE_MALLOC(db[player].pows, ptype, 2);

    if (!db[player].pows) {
        log_error("Out of memory creating player!");
        return NOTHING;
    }

    db[player].pows[0] = CLASS_GUEST;  /* Will be re-classed */
    db[player].pows[1] = 0;

    /* Set password */
    s_Pass(player, crypt(password, CRYPT_SALT));

    /* Set initial resources */
    giveto(player, initial_credits);
    atr_add(player, A_RQUOTA, start_quota);
    atr_add(player, A_QUOTA, start_quota);

    /* Place in start location */
    PUSH(player, db[start].contents);

    /* Add to player list */
    add_player(player);

    /* Setup for non-guests */
    if (class != CLASS_GUEST) {
        do_force(root, tprintf("#%" DBREF_FMT, player), "+channel +public");
        do_class(root, tprintf("#%" DBREF_FMT, player), class_to_name(class));
    }

    return player;
}

/* ===================================================================
 * Player Hash Table - Modern Implementation
 * =================================================================== */

/* Global player hash table (replaces player_list array) */
static hash_table_t *player_hash = NULL;

/**
 * init_player_hash - Initialize player hash table
 * Call this at startup instead of init_hft()
 */
void init_player_hash(void)
{
    if (!player_hash) {
        player_hash = hash_create("players", HASH_SIZE_LARGE, 0, NULL);
        if (!player_hash) {
            log_error("Failed to create player hash table!");
            exit_nicely(1);
        }
    }
}

/**
 * clear_players - Clear all player entries
 * COMPATIBLE: Same signature as original
 */
void clear_players(void)
{
    if (!player_hash) {
        init_player_hash();
    }
    hash_clear(player_hash);
}

/**
 * add_player - Add player to hash table
 * COMPATIBLE: Same signature as original
 * 
 * Adds both name and alias if present
 */
void add_player(dbref player)
{
    const char *name;
    const char *alias;
    
    if (!GoodObject(player)) {
        log_error("add_player: Invalid player object");
        return;
    }
    
    if (!player_hash) {
        init_player_hash();
    }
    
    /* Add by name (skip if has spaces) */
    name = db[player].name;
    if (name && !strchr(name, ' ')) {
        hash_insert(player_hash, name, (void*)(intptr_t)player);
    }
    
    /* Add by alias if present */
    alias = atr_get(player, A_ALIAS);
    if (alias && *alias) {
        hash_insert(player_hash, alias, (void*)(intptr_t)player);
    }
}

/**
 * lookup_player - Find player by name or alias
 * COMPATIBLE: Same signature as original
 * 
 * Returns player dbref or NOTHING
 */
dbref lookup_player(const char *name)
{
    void *result;
    dbref player;
    
    if (!name) {
        return NOTHING;
    }
    
    if (!player_hash) {
        init_player_hash();
    }
    
    /* Strip leading * characters (for channel names) */
    while (*name == '*') {
        name++;
    }
    
    if (!*name) {
        return NOTHING;
    }
    
    /* Look up in hash table */
    result = hash_lookup(player_hash, name);
    if (result) {
        return (dbref)(intptr_t)result;
    }
    
    /* Handle #dbref format */
    if (name[0] == '#' && name[1]) {
        player = (dbref)atol(name + 1);
        if (player >= 0 && player < db_top) {
            return player;
        }
    }
    
    return NOTHING;
}

/**
 * delete_player - Remove player from hash table
 * COMPATIBLE: Same signature as original
 * 
 * Removes both name and alias entries
 */
void delete_player(dbref player)
{
    const char *name;
    const char *alias;
    
    if (!GoodObject(player)) {
        log_error("delete_player: Invalid player object");
        return;
    }
    
    if (!player_hash) {
        return;
    }
    
    /* Remove by name (if no spaces) */
    name = db[player].name;
    if (name && !strchr(name, ' ')) {
        hash_remove(player_hash, name);
    }
    
    /* Remove by alias if present */
    alias = atr_get(player, A_ALIAS);
    if (alias && *alias) {
        hash_remove(player_hash, alias);
    }
}


//#ifdef DEBUG
///**
// * player_list_stats - Print hash table statistics
// */
//void player_list_stats(void)
//{
//    size_t count, size, collisions;
//
//    if (!player_hash) {
//        log_important("Player hash table not initialized.");
//        return;
//    }
//
//    ht_stats(player_hash, &count, &size, &collisions);
//
//    log_important("Player hash table statistics:");
//    log_important(tprintf("  Total entries: %lu", (unsigned long)count));
//    log_important(tprintf("  Table size: %lu", (unsigned long)size));
//    log_important(tprintf("  Collisions: %lu", (unsigned long)collisions));
//
//    if (size > 0) {
//        float load = (float)count / size;
//        log_important(tprintf("  Load factor: %.2f%%", load * 100));
//    }
//}
//#endif

/* End of player hash implementation */
