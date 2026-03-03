/* mariadb_channel.c - MariaDB-backed channel system with in-memory cache
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Implements SQL CRUD operations for the channel system and maintains an
 * in-memory cache for fast message delivery. The cache is populated at
 * startup from MariaDB and updated write-through on every mutation.
 *
 * Three hash tables provide O(1) lookups:
 * - channel_name_hash: channel name -> channel_cache_t*
 * - channel_id_hash: channel_id string -> channel_cache_t*
 * - member_hash: player dbref string -> channel_member_t* linked list
 *
 * SAFETY:
 * - All SQL uses mysql_real_escape_string to prevent injection
 * - Cache entries allocated with SAFE_MALLOC, freed with SMART_FREE
 * - Connection checked before every operation
 * - All cache updates follow write-to-DB-first pattern
 */

#ifdef USE_MARIADB

/* Suppress conversion warnings from MariaDB headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <mysql.h>
#pragma GCC diagnostic pop

#include "config.h"
#include "db.h"
#include "externs.h"
#include "mariadb.h"
#include "mariadb_channel.h"
#include "hash_table.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * CACHE HASH TABLES
 * ============================================================================ */

static hash_table_t *channel_name_hash = NULL;  /* name -> channel_cache_t* */
static hash_table_t *channel_id_hash = NULL;     /* id string -> channel_cache_t* */
static hash_table_t *member_hash = NULL;         /* dbref string -> channel_member_t* list */

/* ============================================================================
 * INTERNAL HELPERS - STRING DUPLICATION
 * ============================================================================ */

/*
 * safe_strdup - Duplicate a string using SAFE_MALLOC
 *
 * Returns NULL if input is NULL.
 */
static char *safe_strdup(const char *s)
{
    char *copy;
    size_t len;

    if (!s) {
        return NULL;
    }

    len = strlen(s) + 1;
    SAFE_MALLOC(copy, char, len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

/* ============================================================================
 * INTERNAL HELPERS - CACHE ENTRY MANAGEMENT
 * ============================================================================ */

/*
 * cache_free_channel - Free a channel_cache_t and all its strings
 */
static void cache_free_channel(channel_cache_t *chan)
{
    if (!chan) {
        return;
    }
    SMART_FREE(chan->name);
    SMART_FREE(chan->cname);
    SMART_FREE(chan->password);
    SMART_FREE(chan->topic);
    SMART_FREE(chan->join_msg);
    SMART_FREE(chan->leave_msg);
    SMART_FREE(chan->speak_lock);
    SMART_FREE(chan->join_lock);
    SMART_FREE(chan->hide_lock);
    SMART_FREE(chan);
}

/*
 * cache_free_member - Free a single channel_member_t and its strings
 */
static void cache_free_member(channel_member_t *m)
{
    if (!m) {
        return;
    }
    SMART_FREE(m->alias);
    SMART_FREE(m->color_name);
    SMART_FREE(m);
}

/*
 * cache_free_member_list - Free an entire linked list of channel_member_t
 */
static void cache_free_member_list(channel_member_t *head)
{
    channel_member_t *next;

    while (head) {
        next = head->next;
        cache_free_member(head);
        head = next;
    }
}

/*
 * channel_value_destructor - Hash table value destructor for channel entries
 */
static void channel_value_destructor(void *value)
{
    cache_free_channel((channel_cache_t *)value);
}

/*
 * member_value_destructor - Hash table value destructor for member lists
 */
static void member_value_destructor(void *value)
{
    cache_free_member_list((channel_member_t *)value);
}

/*
 * cache_make_id_key - Format a channel_id as a hash key string
 */
static const char *cache_make_id_key(long channel_id)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%ld", channel_id);
    return buf;
}

/*
 * cache_make_player_key - Format a player dbref as a hash key string
 */
static const char *cache_make_player_key(dbref player)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%" DBREF_FMT, player);
    return buf;
}

/* ============================================================================
 * INTERNAL HELPERS - CACHE ADD/REMOVE
 * ============================================================================ */

/*
 * cache_add_channel - Add a channel to both name and id hash tables
 */
static void cache_add_channel(channel_cache_t *chan)
{
    if (!chan || !chan->name) {
        return;
    }

    hash_insert(channel_name_hash, chan->name, chan);
    hash_insert(channel_id_hash, cache_make_id_key(chan->channel_id), chan);
}

/*
 * cache_remove_channel - Remove a channel from both hash tables and free it
 */
static void cache_remove_channel(channel_cache_t *chan)
{
    if (!chan) {
        return;
    }

    /* Remove from name hash (don't free yet - id hash also references it) */
    /* We need to remove from both hashes before freeing */
    hash_table_t *saved_name_hash = channel_name_hash;
    hash_table_t *saved_id_hash = channel_id_hash;

    /* Temporarily disable destructors to prevent double-free */
    saved_name_hash->value_destructor = NULL;
    saved_id_hash->value_destructor = NULL;

    hash_remove(channel_name_hash, chan->name);
    hash_remove(channel_id_hash, cache_make_id_key(chan->channel_id));

    /* Restore destructors */
    saved_name_hash->value_destructor = channel_value_destructor;
    saved_id_hash->value_destructor = channel_value_destructor;

    /* Now free the channel */
    cache_free_channel(chan);
}

/*
 * cache_add_member - Add a member entry to the member hash
 *
 * Prepends to the linked list for the player.
 */
static void cache_add_member(dbref player, channel_member_t *m)
{
    const char *key;
    channel_member_t *existing;

    if (!m) {
        return;
    }

    key = cache_make_player_key(player);

    /* Temporarily disable destructor for lookup */
    member_hash->value_destructor = NULL;

    existing = (channel_member_t *)hash_lookup(member_hash, key);
    m->next = existing;

    hash_insert(member_hash, key, m);

    /* Restore destructor */
    member_hash->value_destructor = member_value_destructor;
}

/*
 * cache_remove_member - Remove a specific member entry from the member hash
 *
 * Removes the entry for the given channel_id from the player's linked list.
 * Frees the removed entry.
 *
 * @return 1 if found and removed, 0 if not found
 */
static int cache_remove_member(dbref player, long channel_id)
{
    const char *key;
    channel_member_t *curr, *prev;

    key = cache_make_player_key(player);

    /* Temporarily disable destructor */
    member_hash->value_destructor = NULL;

    curr = (channel_member_t *)hash_lookup(member_hash, key);
    prev = NULL;

    while (curr) {
        if (curr->channel_id == channel_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                /* Removing head of list */
                if (curr->next) {
                    hash_insert(member_hash, key, curr->next);
                } else {
                    hash_remove(member_hash, key);
                }
            }
            curr->next = NULL;
            cache_free_member(curr);

            /* Restore destructor */
            member_hash->value_destructor = member_value_destructor;
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }

    /* Restore destructor */
    member_hash->value_destructor = member_value_destructor;
    return 0;
}

/*
 * cache_remove_all_members_for_channel - Remove all members of a channel
 *
 * Iterates all players and removes their membership for the given channel.
 * Called when a channel is destroyed.
 */
static void cache_remove_all_members_for_channel(long channel_id)
{
    hash_iterator_t iter;
    char *key;
    void *value;

    if (!member_hash) {
        return;
    }

    /* Temporarily disable destructor */
    member_hash->value_destructor = NULL;

    /* Collect keys that need updating (can't modify during iteration) */
    /* Instead, iterate and mark, then clean up */
    hash_iterate_init(member_hash, &iter);
    while (hash_iterate_next(&iter, &key, &value)) {
        channel_member_t *head = (channel_member_t *)value;
        channel_member_t *curr = head;
        channel_member_t *prev = NULL;
        channel_member_t *next_m;
        int changed = 0;

        while (curr) {
            next_m = curr->next;
            if (curr->channel_id == channel_id) {
                if (prev) {
                    prev->next = next_m;
                } else {
                    head = next_m;
                }
                curr->next = NULL;
                cache_free_member(curr);
                changed = 1;
            } else {
                prev = curr;
            }
            curr = next_m;
        }

        if (changed) {
            if (head) {
                hash_insert(member_hash, key, head);
            } else {
                hash_remove(member_hash, key);
            }
        }
    }

    /* Restore destructor */
    member_hash->value_destructor = member_value_destructor;
}


/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

int mariadb_channel_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();

    if (!conn) {
        fprintf(stderr, "MariaDB channel: No connection available\n");
        return 0;
    }

    const char *create_channels =
        "CREATE TABLE IF NOT EXISTS channels ("
        "  channel_id   BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  name         VARCHAR(128) NOT NULL COMMENT 'plain name, no prefix chars',"
        "  cname        VARCHAR(256) NOT NULL COMMENT 'ANSI-colored display name',"
        "  owner        BIGINT       NOT NULL COMMENT 'owner player dbref',"
        "  flags        BIGINT       NOT NULL DEFAULT 0,"
        "  is_system    TINYINT      NOT NULL DEFAULT 0 COMMENT '1=built-in, cannot be deleted',"
        "  min_level    TINYINT      NOT NULL DEFAULT 0 COMMENT '0=anyone 1=official 2=builder 3=director',"
        "  password     VARCHAR(128) DEFAULT NULL COMMENT 'crypt password',"
        "  topic        TEXT         DEFAULT NULL,"
        "  join_msg     TEXT         DEFAULT NULL COMMENT 'was A_OENTER',"
        "  leave_msg    TEXT         DEFAULT NULL COMMENT 'was A_OLEAVE',"
        "  speak_lock   TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_SLOCK',"
        "  join_lock    TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_LOCK',"
        "  hide_lock    TEXT         DEFAULT NULL COMMENT 'boolexp string, was A_LHIDE',"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_name (name),"
        "  INDEX idx_owner (owner)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, create_channels)) {
        fprintf(stderr, "MariaDB channel: Failed to create channels table: %s\n",
                mysql_error(conn));
        return 0;
    }

    const char *create_members =
        "CREATE TABLE IF NOT EXISTS channel_members ("
        "  member_id    BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  channel_id   BIGINT       NOT NULL,"
        "  player       BIGINT       NOT NULL COMMENT 'player dbref',"
        "  alias        VARCHAR(128) NOT NULL DEFAULT '',"
        "  color_name   VARCHAR(256) NOT NULL COMMENT 'colored display name',"
        "  muted        TINYINT      NOT NULL DEFAULT 0,"
        "  is_default   TINYINT      NOT NULL DEFAULT 0,"
        "  is_operator  TINYINT      NOT NULL DEFAULT 0,"
        "  is_banned    TINYINT      NOT NULL DEFAULT 0,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_channel_player (channel_id, player),"
        "  INDEX idx_player (player),"
        "  INDEX idx_active (channel_id, is_banned, muted),"
        "  FOREIGN KEY (channel_id) REFERENCES channels(channel_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, create_members)) {
        fprintf(stderr, "MariaDB channel: Failed to create channel_members table: %s\n",
                mysql_error(conn));
        return 0;
    }

    fprintf(stderr, "MariaDB channel: Tables ready\n");
    return 1;
}

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================ */

void channel_cache_init(void)
{
    if (!channel_name_hash) {
        channel_name_hash = hash_create("channel_names", HASH_SIZE_MEDIUM,
                                         0, channel_value_destructor);
    }
    if (!channel_id_hash) {
        channel_id_hash = hash_create("channel_ids", HASH_SIZE_MEDIUM,
                                       1, NULL);  /* No destructor - shares pointers with name hash */
    }
    if (!member_hash) {
        member_hash = hash_create("channel_members", HASH_SIZE_LARGE,
                                   1, member_value_destructor);
    }
}

int channel_cache_load(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    int channel_count = 0;
    int member_count = 0;

    if (!conn) {
        return -1;
    }

    /* Clear existing cache */
    if (channel_name_hash) {
        hash_clear(channel_name_hash);
    }
    if (channel_id_hash) {
        hash_clear(channel_id_hash);
    }
    if (member_hash) {
        hash_clear(member_hash);
    }

    /* Ensure hash tables exist */
    channel_cache_init();

    /* Load all channels */
    const char *chan_sql =
        "SELECT channel_id, name, cname, owner, flags, is_system, min_level, "
        "       password, topic, join_msg, leave_msg, "
        "       speak_lock, join_lock, hide_lock "
        "FROM channels ORDER BY channel_id";

    if (mysql_query(conn, chan_sql)) {
        log_error(tprintf("MariaDB channel: Failed to load channels: %s",
                          mysql_error(conn)));
        return -1;
    }

    result = mysql_store_result(conn);
    if (!result) {
        return -1;
    }

    while ((row = mysql_fetch_row(result)) != NULL) {
        channel_cache_t *chan;
        SAFE_MALLOC(chan, channel_cache_t, 1);
        if (!chan) {
            continue;
        }
        memset(chan, 0, sizeof(channel_cache_t));

        chan->channel_id = strtol(row[0], NULL, 10);
        chan->name       = safe_strdup(row[1]);
        chan->cname      = safe_strdup(row[2]);
        chan->owner      = (dbref)strtol(row[3], NULL, 10);
        chan->flags      = strtol(row[4], NULL, 10);
        chan->is_system  = (int)strtol(row[5], NULL, 10);
        chan->min_level  = (int)strtol(row[6], NULL, 10);
        chan->password   = safe_strdup(row[7]);
        chan->topic      = safe_strdup(row[8]);
        chan->join_msg   = safe_strdup(row[9]);
        chan->leave_msg  = safe_strdup(row[10]);
        chan->speak_lock = safe_strdup(row[11]);
        chan->join_lock  = safe_strdup(row[12]);
        chan->hide_lock  = safe_strdup(row[13]);

        cache_add_channel(chan);
        channel_count++;
    }

    mysql_free_result(result);

    /* Load all memberships */
    const char *mem_sql =
        "SELECT channel_id, player, alias, color_name, muted, "
        "       is_default, is_operator, is_banned "
        "FROM channel_members ORDER BY channel_id, member_id";

    if (mysql_query(conn, mem_sql)) {
        log_error(tprintf("MariaDB channel: Failed to load members: %s",
                          mysql_error(conn)));
        return channel_count;  /* Channels loaded, members failed */
    }

    result = mysql_store_result(conn);
    if (!result) {
        return channel_count;
    }

    while ((row = mysql_fetch_row(result)) != NULL) {
        channel_member_t *m;
        dbref player;

        SAFE_MALLOC(m, channel_member_t, 1);
        if (!m) {
            continue;
        }
        memset(m, 0, sizeof(channel_member_t));

        m->channel_id  = strtol(row[0], NULL, 10);
        player         = (dbref)strtol(row[1], NULL, 10);
        m->alias       = safe_strdup(row[2]);
        m->color_name  = safe_strdup(row[3]);
        m->muted       = (int)strtol(row[4], NULL, 10);
        m->is_default  = (int)strtol(row[5], NULL, 10);
        m->is_operator = (int)strtol(row[6], NULL, 10);
        m->is_banned   = (int)strtol(row[7], NULL, 10);
        m->next        = NULL;

        cache_add_member(player, m);
        member_count++;
    }

    mysql_free_result(result);

    fprintf(stderr, "MariaDB channel: Loaded %d channels, %d memberships\n",
            channel_count, member_count);
    return channel_count;
}

void channel_cache_clear(void)
{
    if (channel_id_hash) {
        /* Clear id hash first (no destructor, shares pointers) */
        hash_clear(channel_id_hash);
    }
    if (channel_name_hash) {
        /* This frees all channel_cache_t entries */
        hash_clear(channel_name_hash);
    }
    if (member_hash) {
        hash_clear(member_hash);
    }
}

/* ============================================================================
 * CACHE LOOKUP FUNCTIONS
 * ============================================================================ */

channel_cache_t *channel_cache_lookup(const char *name)
{
    if (!name || !*name || !channel_name_hash) {
        return NULL;
    }
    return (channel_cache_t *)hash_lookup(channel_name_hash, name);
}

channel_cache_t *channel_cache_lookup_by_id(long channel_id)
{
    if (!channel_id_hash) {
        return NULL;
    }
    return (channel_cache_t *)hash_lookup(channel_id_hash,
                                           cache_make_id_key(channel_id));
}

channel_member_t *channel_cache_get_member(dbref player, long channel_id)
{
    channel_member_t *m;

    if (!member_hash) {
        return NULL;
    }

    m = (channel_member_t *)hash_lookup(member_hash,
                                         cache_make_player_key(player));
    while (m) {
        if (m->channel_id == channel_id) {
            return m;
        }
        m = m->next;
    }

    return NULL;
}

channel_member_t *channel_cache_get_member_list(dbref player)
{
    if (!member_hash) {
        return NULL;
    }
    return (channel_member_t *)hash_lookup(member_hash,
                                            cache_make_player_key(player));
}

void *channel_cache_get_hash(void)
{
    return (void *)channel_name_hash;
}

/* ============================================================================
 * CHANNEL CRUD OPERATIONS
 * ============================================================================ */

long mariadb_channel_create(const char *name, const char *cname,
                             dbref owner, long flags, int min_level,
                             int is_system)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char esc_name[257];
    char esc_cname[513];
    char query[2048];
    long channel_id;
    channel_cache_t *chan;

    if (!conn || !name || !*name) {
        return -1;
    }

    mysql_real_escape_string(conn, esc_name, name,
                              (unsigned long)strlen(name));
    mysql_real_escape_string(conn, esc_cname,
                              cname ? cname : name,
                              (unsigned long)strlen(cname ? cname : name));

    snprintf(query, sizeof(query),
             "INSERT INTO channels (name, cname, owner, flags, min_level, is_system) "
             "VALUES ('%s', '%s', %" DBREF_FMT ", %ld, %d, %d)",
             esc_name, esc_cname, owner, flags, min_level, is_system);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: create failed: %s",
                          mysql_error(conn)));
        return -1;
    }

    channel_id = (long)mysql_insert_id(conn);

    /* Add to cache */
    SAFE_MALLOC(chan, channel_cache_t, 1);
    if (!chan) {
        return channel_id;  /* DB succeeded, cache failed */
    }
    memset(chan, 0, sizeof(channel_cache_t));

    chan->channel_id = channel_id;
    chan->name       = safe_strdup(name);
    chan->cname      = safe_strdup(cname ? cname : name);
    chan->owner      = owner;
    chan->flags      = flags;
    chan->is_system  = is_system;
    chan->min_level  = min_level;

    cache_add_channel(chan);

    return channel_id;
}

int mariadb_channel_destroy(long channel_id)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_cache_t *chan;

    if (!conn) {
        return 0;
    }

    /* Look up before deleting (need name for cache removal) */
    chan = channel_cache_lookup_by_id(channel_id);

    snprintf(query, sizeof(query),
             "DELETE FROM channels WHERE channel_id = %ld",
             channel_id);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: destroy failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Remove from cache */
    if (chan) {
        cache_remove_all_members_for_channel(channel_id);
        cache_remove_channel(chan);
    }

    return 1;
}

int mariadb_channel_update_field(long channel_id, const char *field,
                                  const char *value)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char esc_value[2048];
    char query[4096];
    channel_cache_t *chan;

    if (!conn || !field) {
        return 0;
    }

    /* Validate field name to prevent SQL injection */
    if (strcmp(field, "name") && strcmp(field, "cname") &&
        strcmp(field, "password") && strcmp(field, "topic") &&
        strcmp(field, "join_msg") && strcmp(field, "leave_msg") &&
        strcmp(field, "speak_lock") && strcmp(field, "join_lock") &&
        strcmp(field, "hide_lock")) {
        log_error(tprintf("MariaDB channel: Invalid field name: %s", field));
        return 0;
    }

    if (value) {
        mysql_real_escape_string(conn, esc_value, value,
                                  (unsigned long)strlen(value));
        snprintf(query, sizeof(query),
                 "UPDATE channels SET %s = '%s' WHERE channel_id = %ld",
                 field, esc_value, channel_id);
    } else {
        snprintf(query, sizeof(query),
                 "UPDATE channels SET %s = NULL WHERE channel_id = %ld",
                 field, channel_id);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: update_field failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Update cache */
    chan = channel_cache_lookup_by_id(channel_id);
    if (chan) {
        char **target = NULL;

        if (!strcmp(field, "name")) {
            /* Name change requires re-hashing */
            hash_table_t *saved = channel_name_hash;
            saved->value_destructor = NULL;
            hash_remove(channel_name_hash, chan->name);
            saved->value_destructor = channel_value_destructor;

            SMART_FREE(chan->name);
            chan->name = safe_strdup(value);
            hash_insert(channel_name_hash, chan->name, chan);
            return 1;
        }

        if (!strcmp(field, "cname"))      target = &chan->cname;
        else if (!strcmp(field, "password"))   target = &chan->password;
        else if (!strcmp(field, "topic"))      target = &chan->topic;
        else if (!strcmp(field, "join_msg"))   target = &chan->join_msg;
        else if (!strcmp(field, "leave_msg"))  target = &chan->leave_msg;
        else if (!strcmp(field, "speak_lock")) target = &chan->speak_lock;
        else if (!strcmp(field, "join_lock"))  target = &chan->join_lock;
        else if (!strcmp(field, "hide_lock"))  target = &chan->hide_lock;

        if (target) {
            SMART_FREE(*target);
            *target = safe_strdup(value);
        }
    }

    return 1;
}

int mariadb_channel_update_flags(long channel_id, long flags)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_cache_t *chan;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE channels SET flags = %ld WHERE channel_id = %ld",
             flags, channel_id);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: update_flags failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    chan = channel_cache_lookup_by_id(channel_id);
    if (chan) {
        chan->flags = flags;
    }

    return 1;
}

int mariadb_channel_update_owner(long channel_id, dbref owner)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_cache_t *chan;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE channels SET owner = %" DBREF_FMT " WHERE channel_id = %ld",
             owner, channel_id);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: update_owner failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    chan = channel_cache_lookup_by_id(channel_id);
    if (chan) {
        chan->owner = owner;
    }

    return 1;
}

/* ============================================================================
 * MEMBERSHIP OPERATIONS
 * ============================================================================ */

int mariadb_channel_join(long channel_id, dbref player,
                          const char *alias, const char *color_name)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char esc_alias[257];
    char esc_color[513];
    char query[2048];
    channel_member_t *m;

    if (!conn) {
        return 0;
    }

    mysql_real_escape_string(conn, esc_alias,
                              alias ? alias : "",
                              (unsigned long)strlen(alias ? alias : ""));
    mysql_real_escape_string(conn, esc_color,
                              color_name ? color_name : "",
                              (unsigned long)strlen(color_name ? color_name : ""));

    snprintf(query, sizeof(query),
             "INSERT INTO channel_members (channel_id, player, alias, color_name) "
             "VALUES (%ld, %" DBREF_FMT ", '%s', '%s') "
             "ON DUPLICATE KEY UPDATE alias='%s', color_name='%s', is_banned=0",
             channel_id, player, esc_alias, esc_color, esc_alias, esc_color);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: join failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Update cache - remove old entry if exists, then add new */
    cache_remove_member(player, channel_id);

    SAFE_MALLOC(m, channel_member_t, 1);
    if (!m) {
        return 1;  /* DB succeeded */
    }
    memset(m, 0, sizeof(channel_member_t));

    m->channel_id  = channel_id;
    m->alias       = safe_strdup(alias ? alias : "");
    m->color_name  = safe_strdup(color_name ? color_name : "");
    m->muted       = 0;
    m->is_default  = 0;
    m->is_operator = 0;
    m->is_banned   = 0;
    m->next        = NULL;

    cache_add_member(player, m);

    return 1;
}

int mariadb_channel_leave(long channel_id, dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "DELETE FROM channel_members "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: leave failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    cache_remove_member(player, channel_id);

    return 1;
}

int mariadb_channel_set_mute(long channel_id, dbref player, int muted)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_member_t *m;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE channel_members SET muted = %d "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             muted, channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_mute failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    m = channel_cache_get_member(player, channel_id);
    if (m) {
        m->muted = muted;
    }

    return 1;
}

int mariadb_channel_set_alias(long channel_id, dbref player,
                               const char *alias)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char esc_alias[257];
    char query[512];
    channel_member_t *m;

    if (!conn || !alias) {
        return 0;
    }

    mysql_real_escape_string(conn, esc_alias, alias,
                              (unsigned long)strlen(alias));

    snprintf(query, sizeof(query),
             "UPDATE channel_members SET alias = '%s' "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             esc_alias, channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_alias failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    m = channel_cache_get_member(player, channel_id);
    if (m) {
        SMART_FREE(m->alias);
        m->alias = safe_strdup(alias);
    }

    return 1;
}

int mariadb_channel_set_color(long channel_id, dbref player,
                               const char *color_name)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char esc_color[513];
    char query[1024];
    channel_member_t *m;

    if (!conn || !color_name) {
        return 0;
    }

    mysql_real_escape_string(conn, esc_color, color_name,
                              (unsigned long)strlen(color_name));

    snprintf(query, sizeof(query),
             "UPDATE channel_members SET color_name = '%s' "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             esc_color, channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_color failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    m = channel_cache_get_member(player, channel_id);
    if (m) {
        SMART_FREE(m->color_name);
        m->color_name = safe_strdup(color_name);
    }

    return 1;
}

int mariadb_channel_set_default(dbref player, long channel_id)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_member_t *m;

    if (!conn) {
        return 0;
    }

    /* Clear all defaults for this player */
    snprintf(query, sizeof(query),
             "UPDATE channel_members SET is_default = 0 "
             "WHERE player = %" DBREF_FMT,
             player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_default clear failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Set new default */
    snprintf(query, sizeof(query),
             "UPDATE channel_members SET is_default = 1 "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_default set failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Update cache */
    m = channel_cache_get_member_list(player);
    while (m) {
        m->is_default = (m->channel_id == channel_id) ? 1 : 0;
        m = m->next;
    }

    return 1;
}

int mariadb_channel_set_operator(long channel_id, dbref player, int is_op)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    channel_member_t *m;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "UPDATE channel_members SET is_operator = %d "
             "WHERE channel_id = %ld AND player = %" DBREF_FMT,
             is_op, channel_id, player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_operator failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    m = channel_cache_get_member(player, channel_id);
    if (m) {
        m->is_operator = is_op;
    }

    return 1;
}

int mariadb_channel_set_ban(long channel_id, dbref player, int is_banned)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[1024];
    channel_member_t *m;

    if (!conn) {
        return 0;
    }

    /* If banning, use INSERT ON DUPLICATE KEY UPDATE to create membership
     * record if it doesn't exist */
    if (is_banned) {
        channel_cache_t *chan = channel_cache_lookup_by_id(channel_id);
        const char *cname = chan ? chan->cname : "";
        char esc_cname[513];

        mysql_real_escape_string(conn, esc_cname, cname,
                                  (unsigned long)strlen(cname));

        snprintf(query, sizeof(query),
                 "INSERT INTO channel_members (channel_id, player, alias, "
                 "color_name, is_banned) "
                 "VALUES (%ld, %" DBREF_FMT ", '', '%s', 1) "
                 "ON DUPLICATE KEY UPDATE is_banned = 1",
                 channel_id, player, esc_cname);
    } else {
        snprintf(query, sizeof(query),
                 "UPDATE channel_members SET is_banned = 0 "
                 "WHERE channel_id = %ld AND player = %" DBREF_FMT,
                 channel_id, player);
    }

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: set_ban failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    /* Update or create cache entry */
    m = channel_cache_get_member(player, channel_id);
    if (m) {
        m->is_banned = is_banned;
    } else if (is_banned) {
        /* Create new banned member entry */
        channel_cache_t *chan = channel_cache_lookup_by_id(channel_id);
        SAFE_MALLOC(m, channel_member_t, 1);
        if (m) {
            memset(m, 0, sizeof(channel_member_t));
            m->channel_id  = channel_id;
            m->alias       = safe_strdup("");
            m->color_name  = safe_strdup(chan ? chan->cname : "");
            m->is_banned   = 1;
            m->next        = NULL;
            cache_add_member(player, m);
        }
    }

    return 1;
}

int mariadb_channel_remove_player_all(dbref player)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[256];
    long affected;
    const char *key;

    if (!conn) {
        return 0;
    }

    snprintf(query, sizeof(query),
             "DELETE FROM channel_members WHERE player = %" DBREF_FMT,
             player);

    if (mysql_query(conn, query)) {
        log_error(tprintf("MariaDB channel: remove_player_all failed: %s",
                          mysql_error(conn)));
        return 0;
    }

    affected = (long)mysql_affected_rows(conn);

    /* Remove from cache */
    key = cache_make_player_key(player);
    if (member_hash && hash_exists(member_hash, key)) {
        hash_remove(member_hash, key);
    }

    return (int)affected;
}

/* ============================================================================
 * LOCK EVALUATION HELPERS
 * ============================================================================ */

int channel_eval_lock(dbref player, dbref chan_owner, const char *lock_str)
{
    if (!lock_str || !*lock_str) {
        return 1;  /* empty lock = pass */
    }
    if (!GoodObject(player) || !GoodObject(chan_owner)) {
        return 0;
    }
    return eval_boolexp(player, chan_owner, (char *)lock_str,
                        get_zone_first(player));
}

int channel_controls(dbref player, channel_cache_t *chan)
{
    channel_member_t *m;

    if (!chan || !GoodObject(player)) {
        return 0;
    }

    if (controls(player, chan->owner, POW_CHANNEL)) {
        return 1;
    }

    m = channel_cache_get_member(player, chan->channel_id);
    return (m && m->is_operator);
}

/* ============================================================================
 * LEVEL/PREFIX HELPERS
 * ============================================================================ */

const char *channel_level_prefix(int min_level)
{
    switch (min_level) {
        case 1:  return "_";
        case 2:  return ".";
        case 3:  return "*";
        default: return "";
    }
}

int channel_name_to_level(const char *name)
{
    if (!name || !*name) {
        return 0;
    }
    switch (*name) {
        case '*': return 3;
        case '.': return 2;
        case '_': return 1;
        default:  return 0;
    }
}

const char *channel_strip_prefix(const char *name)
{
    if (!name || !*name) {
        return name;
    }
    if (*name == '*' || *name == '.' || *name == '_') {
        return name + 1;
    }
    return name;
}

/* ============================================================================
 * LEGACY CONVERSION
 * ============================================================================
 * Converts TYPE_CHANNEL objects and A_CHANNEL/A_BANNED/A_USERS attributes
 * to MariaDB-backed storage. Called once during startup when legacy channel
 * objects are detected in the flat-file database.
 *
 * Conversion steps:
 * 1. Iterate db[] for TYPE_CHANNEL objects → INSERT into channels table
 * 2. Iterate db[] for TYPE_PLAYER with A_CHANNEL → INSERT into channel_members
 * 3. Handle A_BANNED on players → mark is_banned in channel_members
 * 4. Handle A_USERS on channels → mark is_operator in channel_members
 * 5. Clear converted attributes, mark channel objects for recycling
 * 6. Reload cache from MariaDB
 */

/*
 * convert_parse_channel_attr - Parse a player's A_CHANNEL attribute
 *
 * Format: "channelname:alias:onoff channelname2:alias2:onoff2 ..."
 * Also handles bare channel names (no colons) as active with empty alias.
 *
 * @param conn MariaDB connection
 * @param player Player dbref
 * @param attr_str The A_CHANNEL attribute value
 * @return Number of memberships created
 */
static int convert_parse_channel_attr(MYSQL *conn, dbref player,
                                        const char *attr_str)
{
    char buf[8192];
    char *tok, *saveptr;
    int count = 0;

    if (!attr_str || !*attr_str) {
        return 0;
    }

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        char *colon1, *colon2;
        const char *chan_name;
        const char *alias = "";
        int onoff = 1;
        const char *plain_name;
        channel_cache_t *chan;

        /* Parse "name:alias:onoff" or bare "name" */
        colon1 = strchr(tok, ':');
        if (colon1) {
            *colon1 = '\0';
            alias = colon1 + 1;
            colon2 = strchr(alias, ':');
            if (colon2) {
                *colon2 = '\0';
                onoff = atoi(colon2 + 1);
            }
        }

        chan_name = tok;
        plain_name = channel_strip_prefix(chan_name);

        /* Look up channel in cache (already loaded from phase 1 conversion) */
        chan = channel_cache_lookup(plain_name);
        if (chan) {
            char esc_alias[257];
            char esc_cname[513];
            char query[2048];
            const char *cname = chan->cname ? chan->cname : plain_name;

            mysql_real_escape_string(conn, esc_alias, alias,
                                      (unsigned long)strlen(alias));
            mysql_real_escape_string(conn, esc_cname, cname,
                                      (unsigned long)strlen(cname));

            snprintf(query, sizeof(query),
                     "INSERT INTO channel_members "
                     "(channel_id, player, alias, color_name, muted) "
                     "VALUES (%ld, %" DBREF_FMT ", '%s', '%s', %d) "
                     "ON DUPLICATE KEY UPDATE "
                     "alias = VALUES(alias), muted = VALUES(muted)",
                     chan->channel_id, player, esc_alias, esc_cname,
                     onoff ? 0 : 1);

            if (!mysql_query(conn, query)) {
                count++;
            } else {
                log_error(tprintf("Channel convert: membership insert "
                                  "failed for player %" DBREF_FMT
                                  " channel %s: %s",
                                  player, plain_name, mysql_error(conn)));
            }
        } else {
            log_error(tprintf("Channel convert: channel '%s' not found "
                              "for player %" DBREF_FMT " membership",
                              plain_name, player));
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }

    return count;
}

/*
 * convert_parse_banned_attr - Parse a player's A_BANNED attribute
 *
 * Format: "channelname1 channelname2 ..."
 * Space-separated list of channel names the player is banned from.
 *
 * @param conn MariaDB connection
 * @param player Player dbref
 * @param attr_str The A_BANNED attribute value
 * @return Number of bans set
 */
static int convert_parse_banned_attr(MYSQL *conn, dbref player,
                                       const char *attr_str)
{
    char buf[4096];
    char *tok, *saveptr;
    int count = 0;

    if (!attr_str || !*attr_str) {
        return 0;
    }

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        const char *plain_name = channel_strip_prefix(tok);
        channel_cache_t *chan = channel_cache_lookup(plain_name);

        if (chan) {
            char esc_cname[513];
            char query[1024];
            const char *cname = chan->cname ? chan->cname : plain_name;

            mysql_real_escape_string(conn, esc_cname, cname,
                                      (unsigned long)strlen(cname));

            /* Use INSERT ON DUPLICATE KEY UPDATE to handle players
             * who are both members and banned */
            snprintf(query, sizeof(query),
                     "INSERT INTO channel_members "
                     "(channel_id, player, alias, color_name, is_banned) "
                     "VALUES (%ld, %" DBREF_FMT ", '', '%s', 1) "
                     "ON DUPLICATE KEY UPDATE is_banned = 1",
                     chan->channel_id, player, esc_cname);

            if (!mysql_query(conn, query)) {
                count++;
            }
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }

    return count;
}

/*
 * convert_parse_users_attr - Parse a channel's A_USERS attribute (operators)
 *
 * Format: "#dbref1 #dbref2 ..."
 * Space-separated list of dbrefs who are operators on this channel.
 *
 * @param conn MariaDB connection
 * @param channel_id Channel's database ID
 * @param attr_str The A_USERS attribute value
 * @return Number of operators set
 */
static int convert_parse_users_attr(MYSQL *conn, long channel_id,
                                      const char *attr_str)
{
    char buf[4096];
    char *tok, *saveptr;
    int count = 0;

    if (!attr_str || !*attr_str) {
        return 0;
    }

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        dbref op_player;
        char query[256];

        /* Parse #dbref format */
        if (*tok == '#') {
            tok++;
        }
        op_player = (dbref)strtol(tok, NULL, 10);

        if (GoodObject(op_player) && Typeof(op_player) == TYPE_PLAYER) {
            snprintf(query, sizeof(query),
                     "UPDATE channel_members SET is_operator = 1 "
                     "WHERE channel_id = %ld AND player = %" DBREF_FMT,
                     channel_id, op_player);

            if (!mysql_query(conn, query)) {
                if (mysql_affected_rows(conn) > 0) {
                    count++;
                }
            }
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }

    return count;
}

int channel_convert_legacy(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    dbref i;
    int channels_converted = 0;
    int members_converted = 0;
    int bans_converted = 0;
    int operators_converted = 0;
    int legacy_found = 0;

    if (!conn) {
        log_error("Channel convert: No MariaDB connection.");
        return 0;
    }

    /* ================================================================
     * Pass 1: Check for legacy TYPE_CHANNEL objects
     * ================================================================ */
    for (i = 0; i < db_top; i++) {
        if (Typeof(i) == TYPE_CHANNEL) {
            legacy_found = 1;
            break;
        }
    }

    if (!legacy_found) {
        return 0;  /* No legacy channels — already converted or fresh install */
    }

    log_important("Legacy TYPE_CHANNEL objects found. Beginning conversion...");

    /* ================================================================
     * Pass 2: Convert TYPE_CHANNEL objects to channels table
     * ================================================================ */
    for (i = 0; i < db_top; i++) {
        if (Typeof(i) != TYPE_CHANNEL) {
            continue;
        }

        const char *obj_name = db[i].name;
        const char *obj_cname = db[i].cname;
        dbref owner = db[i].owner;
        long flags = db[i].flags & ~TYPE_MASK;  /* Strip type bits */
        const char *plain_name;
        int min_level;
        char *topic, *speak_lock, *join_lock, *hide_lock;
        char *join_msg, *leave_msg, *password;
        char esc_name[257], esc_cname[513], esc_topic[2049];
        char esc_slock[1025], esc_jlock[1025], esc_hlock[1025];
        char esc_jmsg[2049], esc_lmsg[2049], esc_pass[257];
        char query[8192];

        if (!obj_name || !*obj_name) {
            continue;
        }

        /* Determine min_level from name prefix */
        min_level = channel_name_to_level(obj_name);
        plain_name = channel_strip_prefix(obj_name);

        /* Skip if this channel already exists in MariaDB */
        if (channel_cache_lookup(plain_name)) {
            log_important(tprintf("Channel convert: '%s' already exists in "
                                  "MariaDB, skipping.", plain_name));
            /* Still mark the object for recycling */
            db[i].flags = (db[i].flags & ~TYPE_MASK) | TYPE_THING;
            db[i].flags |= GOING;
            continue;
        }

        /* Use colored name or fall back to plain name */
        if (!obj_cname || !*obj_cname) {
            obj_cname = plain_name;
        }

        /* Validate owner */
        if (!GoodObject(owner) || Typeof(owner) != TYPE_PLAYER) {
            owner = root;  /* Fall back to root */
        }

        /* Get attributes from the channel object */
        topic = atr_get(i, A_DESC);
        speak_lock = atr_get(i, A_SLOCK);
        join_lock = atr_get(i, A_LOCK);
        hide_lock = atr_get(i, A_LHIDE);
        join_msg = atr_get(i, A_OENTER);
        leave_msg = atr_get(i, A_OLEAVE);
        password = atr_get(i, A_PASS);

        /* Escape all strings for SQL */
        mysql_real_escape_string(conn, esc_name, plain_name,
                                  (unsigned long)strlen(plain_name));
        mysql_real_escape_string(conn, esc_cname, obj_cname,
                                  (unsigned long)strlen(obj_cname));
        mysql_real_escape_string(conn, esc_topic, topic ? topic : "",
                                  (unsigned long)strlen(topic ? topic : ""));
        mysql_real_escape_string(conn, esc_slock,
                                  speak_lock ? speak_lock : "",
                                  (unsigned long)strlen(
                                      speak_lock ? speak_lock : ""));
        mysql_real_escape_string(conn, esc_jlock,
                                  join_lock ? join_lock : "",
                                  (unsigned long)strlen(
                                      join_lock ? join_lock : ""));
        mysql_real_escape_string(conn, esc_hlock,
                                  hide_lock ? hide_lock : "",
                                  (unsigned long)strlen(
                                      hide_lock ? hide_lock : ""));
        mysql_real_escape_string(conn, esc_jmsg,
                                  join_msg ? join_msg : "",
                                  (unsigned long)strlen(
                                      join_msg ? join_msg : ""));
        mysql_real_escape_string(conn, esc_lmsg,
                                  leave_msg ? leave_msg : "",
                                  (unsigned long)strlen(
                                      leave_msg ? leave_msg : ""));
        mysql_real_escape_string(conn, esc_pass,
                                  password ? password : "",
                                  (unsigned long)strlen(
                                      password ? password : ""));

        /* INSERT into channels table */
        snprintf(query, sizeof(query),
                 "INSERT INTO channels (name, cname, owner, flags, "
                 "is_system, min_level, password, topic, join_msg, "
                 "leave_msg, speak_lock, join_lock, hide_lock) "
                 "VALUES ('%s', '%s', %" DBREF_FMT ", %ld, 0, %d, "
                 "%s, %s, %s, %s, %s, %s, %s)",
                 esc_name, esc_cname, owner, flags, min_level,
                 (*esc_pass ? tprintf("'%s'", esc_pass) : "NULL"),
                 (*esc_topic ? tprintf("'%s'", esc_topic) : "NULL"),
                 (*esc_jmsg ? tprintf("'%s'", esc_jmsg) : "NULL"),
                 (*esc_lmsg ? tprintf("'%s'", esc_lmsg) : "NULL"),
                 (*esc_slock ? tprintf("'%s'", esc_slock) : "NULL"),
                 (*esc_jlock ? tprintf("'%s'", esc_jlock) : "NULL"),
                 (*esc_hlock ? tprintf("'%s'", esc_hlock) : "NULL"));

        if (mysql_query(conn, query)) {
            log_error(tprintf("Channel convert: INSERT failed for '%s': %s",
                              plain_name, mysql_error(conn)));
        } else {
            long new_id = (long)mysql_insert_id(conn);

            /* Add to cache immediately so membership lookups work */
            channel_cache_t *chan;
            SAFE_MALLOC(chan, channel_cache_t, 1);
            if (chan) {
                memset(chan, 0, sizeof(channel_cache_t));
                chan->channel_id = new_id;
                chan->name = safe_strdup(plain_name);
                chan->cname = safe_strdup(obj_cname);
                chan->owner = owner;
                chan->flags = flags;
                chan->is_system = 0;
                chan->min_level = min_level;
                chan->password = safe_strdup(
                    (password && *password) ? password : NULL);
                chan->topic = safe_strdup(
                    (topic && *topic) ? topic : NULL);
                chan->join_msg = safe_strdup(
                    (join_msg && *join_msg) ? join_msg : NULL);
                chan->leave_msg = safe_strdup(
                    (leave_msg && *leave_msg) ? leave_msg : NULL);
                chan->speak_lock = safe_strdup(
                    (speak_lock && *speak_lock) ? speak_lock : NULL);
                chan->join_lock = safe_strdup(
                    (join_lock && *join_lock) ? join_lock : NULL);
                chan->hide_lock = safe_strdup(
                    (hide_lock && *hide_lock) ? hide_lock : NULL);
                cache_add_channel(chan);
            }

            /* Convert A_USERS (operators) for this channel */
            {
                char *users_str = atr_get(i, A_USERS);
                if (users_str && *users_str) {
                    operators_converted += convert_parse_users_attr(
                        conn, new_id, users_str);
                }
            }

            channels_converted++;

            log_important(tprintf("Channel convert: '%s%s' -> channel_id %ld "
                                  "(min_level=%d, owner=#%" DBREF_FMT ")",
                                  channel_level_prefix(min_level),
                                  plain_name, new_id, min_level, owner));
        }

        /* Mark the db[] object for garbage collection */
        db[i].flags = (db[i].flags & ~TYPE_MASK) | TYPE_THING;
        db[i].flags |= GOING;
    }

    /* ================================================================
     * Pass 3: Convert player A_CHANNEL and A_BANNED attributes
     * ================================================================ */
    for (i = 0; i < db_top; i++) {
        if (Typeof(i) != TYPE_PLAYER) {
            continue;
        }

        /* Convert A_CHANNEL (memberships) */
        {
            char *chan_attr = atr_get(i, A_CHANNEL);
            if (chan_attr && *chan_attr) {
                members_converted += convert_parse_channel_attr(
                    conn, i, chan_attr);
                atr_add(i, A_CHANNEL, "");
            }
        }

        /* Convert A_BANNED (bans) */
        {
            char *ban_attr = atr_get(i, A_BANNED);
            if (ban_attr && *ban_attr) {
                bans_converted += convert_parse_banned_attr(
                    conn, i, ban_attr);
                atr_add(i, A_BANNED, "");
            }
        }
    }

    /* ================================================================
     * Pass 4: Reload cache from MariaDB to pick up all memberships
     * ================================================================ */
    channel_cache_clear();
    channel_cache_init();
    channel_cache_load();

    log_important(tprintf("Channel conversion complete: "
                          "%d channels, %d memberships, %d bans, "
                          "%d operators converted.",
                          channels_converted, members_converted,
                          bans_converted, operators_converted));

    return channels_converted;
}

#endif /* USE_MARIADB */
