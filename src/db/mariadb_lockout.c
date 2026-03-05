/* mariadb_lockout.c - MariaDB-backed lockout system with in-memory cache
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Implements SQL CRUD operations for the lockout system and maintains an
 * in-memory cache (simple linked list) for fast connection-time checks.
 *
 * Three lockout types:
 * - LOCKOUT_PLAYER: Ban by player dbref
 * - LOCKOUT_IP: Ban by IP/CIDR for all connections
 * - LOCKOUT_GUESTIP: Ban by IP/CIDR for guest connections only
 *
 * CIDR matching is pre-computed at cache load time: inet_pton() converts
 * the network address and the prefix length is converted to a bitmask.
 * Connection-time checks are then a simple bitwise AND comparison.
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
#include "mariadb_lockout.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

/* ============================================================================
 * CACHE
 * ============================================================================ */

static lockout_entry_t *lockout_list = NULL;

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
 * cache_free_entry - Free a lockout_entry_t and all its strings
 */
static void cache_free_entry(lockout_entry_t *entry)
{
    if (!entry) {
        return;
    }
    SMART_FREE(entry->target);
    SMART_FREE(entry->reason);
    SMART_FREE(entry);
}

/* ============================================================================
 * CIDR PARSING
 * ============================================================================ */

/*
 * parse_cidr - Parse a CIDR notation string into network and mask
 *
 * Handles "1.2.3.4/24" and bare "1.2.3.4" (treated as /32).
 *
 * @param cidr_str  CIDR string to parse
 * @param network   Output: network address
 * @param mask      Output: netmask as uint32_t (network byte order)
 * @return 1 on success, 0 on invalid input
 */
int parse_cidr(const char *cidr_str, struct in_addr *network, uint32_t *mask)
{
    char buf[64];
    char *slash;
    int prefix_len;

    if (!cidr_str || !network || !mask) {
        return 0;
    }

    strncpy(buf, cidr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        prefix_len = atoi(slash + 1);
        if (prefix_len < 0 || prefix_len > 32) {
            return 0;
        }
    } else {
        prefix_len = 32;  /* bare IP = single host */
    }

    if (inet_pton(AF_INET, buf, network) != 1) {
        return 0;
    }

    /* Compute netmask in network byte order */
    if (prefix_len == 0) {
        *mask = 0;
    } else {
        *mask = htonl(~((uint32_t)0) << (32 - prefix_len));
    }

    /* Normalize: mask the network address */
    network->s_addr &= *mask;

    return 1;
}

/* ============================================================================
 * LOCKOUT TYPE STRING CONVERSION
 * ============================================================================ */

static const char *type_to_str(lockout_type_t type)
{
    switch (type) {
    case LOCKOUT_PLAYER:  return "player";
    case LOCKOUT_IP:      return "ip";
    case LOCKOUT_GUESTIP: return "guestip";
    default:              return "unknown";
    }
}

static lockout_type_t str_to_type(const char *s)
{
    if (!s) return LOCKOUT_IP;
    if (strcmp(s, "player") == 0)  return LOCKOUT_PLAYER;
    if (strcmp(s, "ip") == 0)      return LOCKOUT_IP;
    if (strcmp(s, "guestip") == 0) return LOCKOUT_GUESTIP;
    return LOCKOUT_IP;
}

/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

int mariadb_lockout_init(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS lockouts ("
        "  id           BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  lockout_type ENUM('player','ip','guestip') NOT NULL,"
        "  target       VARCHAR(256) NOT NULL,"
        "  reason       TEXT DEFAULT NULL,"
        "  created_by   BIGINT NOT NULL,"
        "  created_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_type_target (lockout_type, target)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!conn) {
        fprintf(stderr, "mariadb_lockout_init: no database connection\n");
        return 0;
    }

    if (mysql_query(conn, create_sql) != 0) {
        fprintf(stderr, "mariadb_lockout_init: CREATE TABLE failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================ */

void lockout_cache_clear(void)
{
    lockout_entry_t *entry, *next;

    for (entry = lockout_list; entry; entry = next) {
        next = entry->next;
        cache_free_entry(entry);
    }
    lockout_list = NULL;
}

int lockout_cache_load(void)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    MYSQL_RES *result;
    MYSQL_ROW row;
    int count = 0;

    if (!conn) {
        return -1;
    }

    /* Clear existing cache */
    lockout_cache_clear();

    if (mysql_query(conn,
            "SELECT id, lockout_type, target, reason, created_by, "
            "UNIX_TIMESTAMP(created_at) FROM lockouts ORDER BY id") != 0) {
        fprintf(stderr, "lockout_cache_load: query failed: %s\n",
                mysql_error(conn));
        return -1;
    }

    result = mysql_store_result(conn);
    if (!result) {
        fprintf(stderr, "lockout_cache_load: store_result failed: %s\n",
                mysql_error(conn));
        return -1;
    }

    while ((row = mysql_fetch_row(result)) != NULL) {
        lockout_entry_t *entry;

        SAFE_MALLOC(entry, lockout_entry_t, 1);
        if (!entry) {
            mysql_free_result(result);
            return -1;
        }
        memset(entry, 0, sizeof(lockout_entry_t));

        entry->id         = atol(row[0]);
        entry->type       = str_to_type(row[1]);
        entry->target     = safe_strdup(row[2]);
        entry->reason     = safe_strdup(row[3]);
        entry->created_by = atol(row[4]);
        entry->created_at = row[5] ? (time_t)atol(row[5]) : 0;

        /* Pre-parse based on type */
        switch (entry->type) {
        case LOCKOUT_PLAYER:
            entry->target_dbref = atol(row[2]);
            break;
        case LOCKOUT_IP:
        case LOCKOUT_GUESTIP:
            if (!parse_cidr(row[2], &entry->network, &entry->mask)) {
                fprintf(stderr,
                        "lockout_cache_load: invalid CIDR '%s' (id=%ld), "
                        "skipping\n", row[2], entry->id);
                cache_free_entry(entry);
                continue;
            }
            break;
        }

        /* Prepend to list */
        entry->next = lockout_list;
        lockout_list = entry;
        count++;
    }

    mysql_free_result(result);
    return count;
}

/* ============================================================================
 * LOCKOUT CHECKS
 * ============================================================================ */

const lockout_entry_t *lockout_check_ip(struct in_addr addr)
{
    const lockout_entry_t *entry;

    for (entry = lockout_list; entry; entry = entry->next) {
        if (entry->type == LOCKOUT_IP) {
            if ((addr.s_addr & entry->mask) ==
                (entry->network.s_addr & entry->mask)) {
                return entry;
            }
        }
    }
    return NULL;
}

const lockout_entry_t *lockout_check_guestip(struct in_addr addr)
{
    const lockout_entry_t *entry;

    for (entry = lockout_list; entry; entry = entry->next) {
        if (entry->type == LOCKOUT_GUESTIP) {
            if ((addr.s_addr & entry->mask) ==
                (entry->network.s_addr & entry->mask)) {
                return entry;
            }
        }
    }
    return NULL;
}

const lockout_entry_t *lockout_check_player(dbref player)
{
    const lockout_entry_t *entry;

    for (entry = lockout_list; entry; entry = entry->next) {
        if (entry->type == LOCKOUT_PLAYER &&
            entry->target_dbref == player) {
            return entry;
        }
    }
    return NULL;
}

/* ============================================================================
 * LOCKOUT MUTATIONS
 * ============================================================================ */

int mariadb_lockout_add(lockout_type_t type, const char *target,
                        const char *reason, dbref created_by)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[1024];
    char escaped_target[513];
    char escaped_reason[513];

    if (!conn || !target) {
        return 0;
    }

    mysql_real_escape_string(conn, escaped_target, target,
                             (unsigned long)strlen(target));

    if (reason && *reason) {
        size_t rlen = strlen(reason);
        if (rlen > 255) rlen = 255;
        mysql_real_escape_string(conn, escaped_reason, reason,
                                 (unsigned long)rlen);
    } else {
        escaped_reason[0] = '\0';
    }

    if (reason && *reason) {
        snprintf(query, sizeof(query),
                 "INSERT INTO lockouts (lockout_type, target, reason, "
                 "created_by) VALUES ('%s', '%s', '%s', %ld)",
                 type_to_str(type), escaped_target, escaped_reason,
                 created_by);
    } else {
        snprintf(query, sizeof(query),
                 "INSERT INTO lockouts (lockout_type, target, reason, "
                 "created_by) VALUES ('%s', '%s', NULL, %ld)",
                 type_to_str(type), escaped_target, created_by);
    }

    if (mysql_query(conn, query) != 0) {
        /* Check for duplicate key (entry already exists) */
        if (mysql_errno(conn) == 1062) {
            return 0;  /* Already exists */
        }
        fprintf(stderr, "mariadb_lockout_add: INSERT failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    /* Reload cache to pick up the new entry */
    lockout_cache_load();
    return 1;
}

int mariadb_lockout_remove(lockout_type_t type, const char *target)
{
    MYSQL *conn = (MYSQL *)mariadb_get_connection();
    char query[1024];
    char escaped_target[513];
    my_ulonglong affected;

    if (!conn || !target) {
        return 0;
    }

    mysql_real_escape_string(conn, escaped_target, target,
                             (unsigned long)strlen(target));

    snprintf(query, sizeof(query),
             "DELETE FROM lockouts WHERE lockout_type='%s' AND target='%s'",
             type_to_str(type), escaped_target);

    if (mysql_query(conn, query) != 0) {
        fprintf(stderr, "mariadb_lockout_remove: DELETE failed: %s\n",
                mysql_error(conn));
        return 0;
    }

    affected = mysql_affected_rows(conn);
    if (affected == 0) {
        return 0;  /* Not found */
    }

    /* Reload cache to reflect removal */
    lockout_cache_load();
    return 1;
}

/* ============================================================================
 * ITERATION
 * ============================================================================ */

const lockout_entry_t *lockout_get_list(void)
{
    return lockout_list;
}

#endif /* USE_MARIADB */
