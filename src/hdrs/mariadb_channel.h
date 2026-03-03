/* mariadb_channel.h - MariaDB-backed channel system with in-memory cache
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Replaces the legacy TYPE_CHANNEL db[] objects and A_CHANNEL attribute
 * string parsing with a MariaDB-backed system. An in-memory cache provides
 * fast lookups for message delivery (com_send_int loop).
 *
 * Three hash tables provide O(1) access:
 * - channel_name_hash: name -> channel_cache_t*
 * - channel_id_hash: channel_id (as string) -> channel_cache_t*
 * - member_hash: player dbref (as string) -> channel_member_t* linked list
 *
 * All mutations write through to MariaDB first, then update the cache.
 *
 * SAFETY:
 * - All SQL uses mysql_real_escape_string to prevent injection
 * - Cache entries allocated with SAFE_MALLOC, freed with SMART_FREE
 * - Connection checked before every operation
 */

#ifndef _MARIADB_CHANNEL_H_
#define _MARIADB_CHANNEL_H_

#include "config.h"

/* ============================================================================
 * CACHE STRUCTURES
 * ============================================================================ */

/*
 * channel_cache_t - In-memory channel definition
 *
 * Replaces db[channel].* access for channel properties.
 * Allocated with SAFE_MALLOC, strings are SAFE_MALLOC'd copies.
 */
typedef struct channel_cache {
    long   channel_id;
    char  *name;        /* plain name, no prefix (hash key) */
    char  *cname;       /* colored display name */
    dbref  owner;
    long   flags;       /* SEE_OK, QUIET, HAVEN, DARK */
    int    is_system;   /* 1=built-in, cannot be deleted */
    int    min_level;   /* 0=anyone 1=official 2=builder 3=director */
    char  *password;
    char  *topic;
    char  *join_msg;
    char  *leave_msg;
    char  *speak_lock;
    char  *join_lock;
    char  *hide_lock;
} channel_cache_t;

/*
 * channel_member_t - Per-player membership record
 *
 * Replaces A_CHANNEL string parsing for membership checks.
 * Linked list per player (keyed by player dbref in member_hash).
 */
typedef struct channel_member {
    long   channel_id;
    char  *alias;
    char  *color_name;  /* per-player colored name */
    int    muted;
    int    is_default;
    int    is_operator;
    int    is_banned;
    struct channel_member *next;  /* linked list per player */
} channel_member_t;


#ifdef USE_MARIADB

/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

/*
 * mariadb_channel_init - Create channels + channel_members tables
 *
 * Called during server startup after mariadb_init().
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_channel_init(void);

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================ */

/*
 * channel_cache_init - Initialize the three hash tables
 *
 * Must be called before any cache operations. Called from server_main.c
 * during startup.
 */
void channel_cache_init(void);

/*
 * channel_cache_load - Load all channels and members from MariaDB into cache
 *
 * Called after mariadb_channel_init() and after legacy conversion.
 *
 * RETURNS: number of channels loaded, -1 on error
 */
int channel_cache_load(void);

/*
 * channel_cache_clear - Free all cache entries and clear hash tables
 *
 * Called during shutdown or before reload.
 */
void channel_cache_clear(void);

/*
 * channel_cache_lookup - Find channel by name
 *
 * @param name Plain channel name (no prefix)
 * @return channel_cache_t* or NULL if not found
 */
channel_cache_t *channel_cache_lookup(const char *name);

/*
 * channel_cache_lookup_by_id - Find channel by database ID
 *
 * @param channel_id Database primary key
 * @return channel_cache_t* or NULL if not found
 */
channel_cache_t *channel_cache_lookup_by_id(long channel_id);

/*
 * channel_cache_get_member - Get membership record for player on channel
 *
 * @param player Player dbref
 * @param channel_id Channel database ID
 * @return channel_member_t* or NULL if not a member
 */
channel_member_t *channel_cache_get_member(dbref player, long channel_id);

/*
 * channel_cache_get_member_list - Get full membership list for player
 *
 * @param player Player dbref
 * @return Head of linked list, or NULL if no memberships
 */
channel_member_t *channel_cache_get_member_list(dbref player);

/* ============================================================================
 * CHANNEL CRUD OPERATIONS
 * ============================================================================
 * All functions write to MariaDB first, then update the cache.
 */

/*
 * mariadb_channel_create - Create a new channel
 *
 * @param name Plain name (no prefix chars)
 * @param cname ANSI-colored display name
 * @param owner Owner player dbref
 * @param flags Channel flags (SEE_OK, QUIET, etc.)
 * @param min_level Access level (0-3)
 * @param is_system 1 for built-in channels
 * @return channel_id on success, -1 on failure
 */
long mariadb_channel_create(const char *name, const char *cname,
                             dbref owner, long flags, int min_level,
                             int is_system);

/*
 * mariadb_channel_destroy - Delete a channel and all memberships
 *
 * CASCADE deletes all channel_members rows.
 *
 * @param channel_id Channel to destroy
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_destroy(long channel_id);

/*
 * mariadb_channel_update_field - Update a single text field
 *
 * Valid fields: name, cname, password, topic, join_msg, leave_msg,
 *               speak_lock, join_lock, hide_lock
 *
 * @param channel_id Channel to update
 * @param field Column name
 * @param value New value (NULL to clear)
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_update_field(long channel_id, const char *field,
                                  const char *value);

/*
 * mariadb_channel_update_flags - Update channel flags
 *
 * @param channel_id Channel to update
 * @param flags New flags value
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_update_flags(long channel_id, long flags);

/*
 * mariadb_channel_update_owner - Update channel owner
 *
 * @param channel_id Channel to update
 * @param owner New owner dbref
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_update_owner(long channel_id, dbref owner);

/* ============================================================================
 * MEMBERSHIP OPERATIONS
 * ============================================================================ */

/*
 * mariadb_channel_join - Add player to channel
 *
 * @param channel_id Channel to join
 * @param player Player dbref
 * @param alias Channel alias for this player
 * @param color_name Initial colored name (from channel default)
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_join(long channel_id, dbref player,
                          const char *alias, const char *color_name);

/*
 * mariadb_channel_leave - Remove player from channel
 *
 * @param channel_id Channel to leave
 * @param player Player dbref
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_leave(long channel_id, dbref player);

/*
 * mariadb_channel_set_mute - Set mute status
 *
 * @param channel_id Channel
 * @param player Player dbref
 * @param muted 1=muted, 0=unmuted
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_mute(long channel_id, dbref player, int muted);

/*
 * mariadb_channel_set_alias - Change player's channel alias
 *
 * @param channel_id Channel
 * @param player Player dbref
 * @param alias New alias
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_alias(long channel_id, dbref player,
                               const char *alias);

/*
 * mariadb_channel_set_color - Change player's channel color name
 *
 * @param channel_id Channel
 * @param player Player dbref
 * @param color_name New colored name
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_color(long channel_id, dbref player,
                               const char *color_name);

/*
 * mariadb_channel_set_default - Set player's default channel
 *
 * Clears is_default on all other memberships for this player,
 * then sets is_default=1 on the specified channel.
 *
 * @param player Player dbref
 * @param channel_id Channel to make default
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_default(dbref player, long channel_id);

/*
 * mariadb_channel_set_operator - Set/clear operator status
 *
 * @param channel_id Channel
 * @param player Player dbref
 * @param is_op 1=operator, 0=not
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_operator(long channel_id, dbref player, int is_op);

/*
 * mariadb_channel_set_ban - Set/clear ban status
 *
 * @param channel_id Channel
 * @param player Player dbref
 * @param is_banned 1=banned, 0=not
 * @return 1 on success, 0 on failure
 */
int mariadb_channel_set_ban(long channel_id, dbref player, int is_banned);

/*
 * mariadb_channel_remove_player_all - Remove player from all channels
 *
 * Called when a player is destroyed or wiped.
 *
 * @param player Player dbref
 * @return number of memberships removed
 */
int mariadb_channel_remove_player_all(dbref player);

/* ============================================================================
 * LOCK EVALUATION HELPERS
 * ============================================================================ */

/*
 * channel_eval_lock - Evaluate a boolexp lock string without a db[] object
 *
 * Uses the channel owner as the object context for eval_boolexp.
 *
 * @param player Player to check
 * @param chan_owner Channel owner dbref (for evaluation context)
 * @param lock_str Boolexp string, or NULL/empty for "pass"
 * @return 1 if player passes the lock, 0 if not
 */
int channel_eval_lock(dbref player, dbref chan_owner, const char *lock_str);

/*
 * channel_controls - Check if player controls a channel
 *
 * Player controls a channel if they:
 * - Have POW_CHANNEL control over the channel owner
 * - Are an operator on the channel
 *
 * @param player Player to check
 * @param chan Channel cache entry
 * @return 1 if player controls the channel, 0 if not
 */
int channel_controls(dbref player, channel_cache_t *chan);

/*
 * channel_level_prefix - Get the display prefix for a min_level
 *
 * @param min_level 0=none, 1='_', 2='.', 3='*'
 * @return Single character prefix or empty string
 */
const char *channel_level_prefix(int min_level);

/*
 * channel_name_to_level - Extract min_level from prefixed name
 *
 * @param name Channel name possibly starting with *, ., or _
 * @return min_level value (0-3)
 */
int channel_name_to_level(const char *name);

/*
 * channel_strip_prefix - Return name without the level prefix
 *
 * @param name Channel name possibly starting with *, ., or _
 * @return Pointer into name past the prefix (not a copy)
 */
const char *channel_strip_prefix(const char *name);


/* ============================================================================
 * LEGACY CONVERSION
 * ============================================================================ */

/*
 * channel_convert_legacy - Convert TYPE_CHANNEL objects and A_CHANNEL
 *                          attributes to MariaDB-backed storage
 *
 * Called from server_main.c after init_game() loads the flat-file database.
 * Scans for TYPE_CHANNEL objects and player A_CHANNEL attributes, migrating
 * everything to the channels/channel_members MariaDB tables.
 *
 * After conversion:
 * - TYPE_CHANNEL objects are marked TYPE_THING | GOING for recycling
 * - Player A_CHANNEL and A_BANNED attributes are cleared
 * - Channel cache is reloaded from MariaDB
 *
 * Safe to call on databases that have already been converted (no-op).
 *
 * @return Number of channels converted, 0 if none found
 */
int channel_convert_legacy(void);

/* ============================================================================
 * HASH TABLE ITERATION (for channel_search, etc.)
 * ============================================================================ */

/*
 * channel_cache_get_hash - Get the channel name hash table
 *
 * Used by channel_search and other iteration functions.
 *
 * @return hash_table_t* for the channel name hash
 */
void *channel_cache_get_hash(void);


#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================
 * Return failure values so server exits with clear error if MariaDB
 * is not available.
 */

static inline int mariadb_channel_init(void) { return 0; }
static inline void channel_cache_init(void) { }
static inline int channel_cache_load(void) { return -1; }
static inline void channel_cache_clear(void) { }

static inline channel_cache_t *channel_cache_lookup(
    const char *n __attribute__((unused))) { return NULL; }
static inline channel_cache_t *channel_cache_lookup_by_id(
    long id __attribute__((unused))) { return NULL; }
static inline channel_member_t *channel_cache_get_member(
    dbref p __attribute__((unused)),
    long id __attribute__((unused))) { return NULL; }
static inline channel_member_t *channel_cache_get_member_list(
    dbref p __attribute__((unused))) { return NULL; }

static inline long mariadb_channel_create(
    const char *n __attribute__((unused)),
    const char *c __attribute__((unused)),
    dbref o __attribute__((unused)),
    long f __attribute__((unused)),
    int ml __attribute__((unused)),
    int is __attribute__((unused))) { return -1; }
static inline int mariadb_channel_destroy(
    long id __attribute__((unused))) { return 0; }
static inline int mariadb_channel_update_field(
    long id __attribute__((unused)),
    const char *f __attribute__((unused)),
    const char *v __attribute__((unused))) { return 0; }
static inline int mariadb_channel_update_flags(
    long id __attribute__((unused)),
    long f __attribute__((unused))) { return 0; }
static inline int mariadb_channel_update_owner(
    long id __attribute__((unused)),
    dbref o __attribute__((unused))) { return 0; }

static inline int mariadb_channel_join(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    const char *a __attribute__((unused)),
    const char *c __attribute__((unused))) { return 0; }
static inline int mariadb_channel_leave(
    long id __attribute__((unused)),
    dbref p __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_mute(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    int m __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_alias(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    const char *a __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_color(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    const char *c __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_default(
    dbref p __attribute__((unused)),
    long id __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_operator(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    int o __attribute__((unused))) { return 0; }
static inline int mariadb_channel_set_ban(
    long id __attribute__((unused)),
    dbref p __attribute__((unused)),
    int b __attribute__((unused))) { return 0; }
static inline int mariadb_channel_remove_player_all(
    dbref p __attribute__((unused))) { return 0; }

static inline int channel_eval_lock(
    dbref p __attribute__((unused)),
    dbref o __attribute__((unused)),
    const char *l __attribute__((unused))) { return 0; }
static inline int channel_controls(
    dbref p __attribute__((unused)),
    channel_cache_t *c __attribute__((unused))) { return 0; }
static inline const char *channel_level_prefix(
    int l __attribute__((unused))) { return ""; }
static inline int channel_name_to_level(
    const char *n __attribute__((unused))) { return 0; }
static inline const char *channel_strip_prefix(
    const char *n __attribute__((unused))) { return n; }
static inline void *channel_cache_get_hash(void) { return NULL; }
static inline int channel_convert_legacy(void) { return 0; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_CHANNEL_H_ */
