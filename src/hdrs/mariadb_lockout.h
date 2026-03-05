/* mariadb_lockout.h - MariaDB-backed lockout system with in-memory cache
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Replaces the legacy file-based check_lockout() system, the nologins global,
 * the restrict_connect_class global, and the dead @maintenance command with a
 * unified MariaDB-backed lockout system.
 *
 * Three types of lockout:
 * - LOCKOUT_PLAYER: Ban a specific player by dbref
 * - LOCKOUT_IP: Ban an IP address or CIDR range from all connections
 * - LOCKOUT_GUESTIP: Ban an IP address or CIDR range from guest connections
 *
 * The cache is a simple linked list (the lockout table will be small).
 * CIDR networks are pre-parsed at load time to avoid inet_pton() on every
 * connection attempt.
 *
 * SAFETY:
 * - All SQL uses mysql_real_escape_string to prevent injection
 * - Cache entries allocated with SAFE_MALLOC, freed with SMART_FREE
 * - Connection checked before every operation
 * - All cache updates follow write-to-DB-first pattern
 */

#ifndef _MARIADB_LOCKOUT_H_
#define _MARIADB_LOCKOUT_H_

#include "config.h"
#include <netinet/in.h>
#include <stdint.h>

/* ============================================================================
 * LOCKOUT TYPES AND CACHE STRUCTURE
 * ============================================================================ */

typedef enum {
    LOCKOUT_PLAYER  = 0,  /* Ban a specific player */
    LOCKOUT_IP      = 1,  /* Ban an IP/CIDR from all connections */
    LOCKOUT_GUESTIP = 2   /* Ban an IP/CIDR from guest connections only */
} lockout_type_t;

typedef struct lockout_entry {
    long            id;            /* Database primary key */
    lockout_type_t  type;
    char           *target;        /* dbref-as-string for player, CIDR for IP */
    dbref           target_dbref;  /* Pre-resolved for LOCKOUT_PLAYER */
    struct in_addr  network;       /* Pre-parsed for IP types */
    uint32_t        mask;          /* Pre-computed netmask for IP types */
    char           *reason;
    dbref           created_by;
    time_t          created_at;
    struct lockout_entry *next;
} lockout_entry_t;


#ifdef USE_MARIADB

/* ============================================================================
 * TABLE INITIALIZATION
 * ============================================================================ */

/*
 * mariadb_lockout_init - Create lockouts table if it doesn't exist
 *
 * Called during server startup after mariadb_init().
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_lockout_init(void);

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================ */

/*
 * lockout_cache_load - Load all lockout entries from MariaDB into cache
 *
 * Pre-parses CIDR for IP types, resolves dbrefs for player types.
 * Can be called multiple times (clears existing cache first).
 *
 * RETURNS: number of entries loaded, -1 on error
 */
int lockout_cache_load(void);

/*
 * lockout_cache_clear - Free all cache entries
 *
 * Called during shutdown or before reload.
 */
void lockout_cache_clear(void);

/* ============================================================================
 * LOCKOUT CHECKS
 * ============================================================================ */

/*
 * lockout_check_ip - Check if an IP address is banned from connecting
 *
 * Walks the cache checking LOCKOUT_IP entries against the given address.
 *
 * @param addr  IP address to check (from descriptor's sin_addr)
 * @return Pointer to matching lockout_entry_t, or NULL if not banned
 */
const lockout_entry_t *lockout_check_ip(struct in_addr addr);

/*
 * lockout_check_guestip - Check if an IP address is banned from guest connects
 *
 * Walks the cache checking LOCKOUT_GUESTIP entries against the given address.
 *
 * @param addr  IP address to check
 * @return Pointer to matching lockout_entry_t, or NULL if not banned
 */
const lockout_entry_t *lockout_check_guestip(struct in_addr addr);

/*
 * lockout_check_player - Check if a player is banned from connecting
 *
 * Walks the cache checking LOCKOUT_PLAYER entries against the given dbref.
 *
 * @param player  Player dbref to check
 * @return Pointer to matching lockout_entry_t, or NULL if not banned
 */
const lockout_entry_t *lockout_check_player(dbref player);

/* ============================================================================
 * LOCKOUT MUTATIONS (write-through to DB)
 * ============================================================================ */

/*
 * mariadb_lockout_add - Add a new lockout entry
 *
 * Writes to MariaDB first, then reloads cache.
 *
 * @param type       Lockout type
 * @param target     Target string (dbref for player, CIDR for IP)
 * @param reason     Optional reason text (may be NULL)
 * @param created_by Player who created this lockout
 * @return 1 on success, 0 on failure
 */
int mariadb_lockout_add(lockout_type_t type, const char *target,
                        const char *reason, dbref created_by);

/*
 * mariadb_lockout_remove - Remove a lockout entry
 *
 * Writes to MariaDB first, then reloads cache.
 *
 * @param type    Lockout type
 * @param target  Target string to match
 * @return 1 on success (entry found and removed), 0 on failure/not found
 */
int mariadb_lockout_remove(lockout_type_t type, const char *target);

/* ============================================================================
 * ITERATION AND UTILITY
 * ============================================================================ */

/*
 * lockout_get_list - Get the cache head for iteration
 *
 * Used by @lockout list and @search lockout= commands.
 *
 * @return Head of the lockout entry linked list, or NULL if empty
 */
const lockout_entry_t *lockout_get_list(void);

/*
 * parse_cidr - Parse a CIDR notation string into network and mask
 *
 * Handles both "1.2.3.4/24" and bare "1.2.3.4" (treated as /32).
 * Public for use in input validation.
 *
 * @param cidr_str  CIDR string to parse
 * @param network   Output: network address
 * @param mask      Output: netmask as uint32_t
 * @return 1 on success, 0 on invalid input
 */
int parse_cidr(const char *cidr_str, struct in_addr *network, uint32_t *mask);


#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================
 * Return failure values so server exits with clear error if MariaDB
 * is not available.
 */

static inline int mariadb_lockout_init(void) { return 0; }
static inline int lockout_cache_load(void) { return -1; }
static inline void lockout_cache_clear(void) { }

static inline const lockout_entry_t *lockout_check_ip(
    struct in_addr a __attribute__((unused))) { return NULL; }
static inline const lockout_entry_t *lockout_check_guestip(
    struct in_addr a __attribute__((unused))) { return NULL; }
static inline const lockout_entry_t *lockout_check_player(
    dbref p __attribute__((unused))) { return NULL; }

static inline int mariadb_lockout_add(
    lockout_type_t t __attribute__((unused)),
    const char *tgt __attribute__((unused)),
    const char *r __attribute__((unused)),
    dbref c __attribute__((unused))) { return 0; }
static inline int mariadb_lockout_remove(
    lockout_type_t t __attribute__((unused)),
    const char *tgt __attribute__((unused))) { return 0; }

static inline const lockout_entry_t *lockout_get_list(void) { return NULL; }
static inline int parse_cidr(
    const char *c __attribute__((unused)),
    struct in_addr *n __attribute__((unused)),
    uint32_t *m __attribute__((unused))) { return 0; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_LOCKOUT_H_ */
