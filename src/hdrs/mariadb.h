/* mariadb.h - MariaDB integration for deMUSE configuration persistence
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides persistent storage of runtime configuration values in MariaDB.
 * MariaDB is REQUIRED for server operation. All config values (including
 * defaults) are stored in the database. The server will not start without
 * a populated config table.
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * When MariaDB is not available at compile time, stub functions cause
 * the server to fail at startup with a clear error message.
 *
 * CREDENTIALS:
 * - Read from run/db/mariadb.conf (simple key=value format)
 * - File must NOT be committed to version control
 *
 * CONFIG TABLE SCHEMA:
 * - config_key VARCHAR(64) PRIMARY KEY
 * - config_value TEXT
 * - config_type ENUM('STR','NUM','REF','LNG')
 * - description TEXT
 * - updated_at TIMESTAMP
 *
 * ARRAY VALUES:
 * - Stored as numbered keys: prefix-1, prefix-2, etc.
 * - Loaded via LIKE 'prefix-%' ORDER BY config_key
 * - Used for perm_messages, potentially other arrays
 */

#ifndef _MARIADB_H_
#define _MARIADB_H_

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/*
 * mariadb_init - Initialize MariaDB connection
 *
 * Reads credentials from run/db/mariadb.conf and connects to the database.
 * Failure is FATAL — the server cannot start without MariaDB.
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_init(void);

/*
 * mariadb_config_load - Load all config values from database
 *
 * Reads the config table and sets the corresponding global variables.
 * Also loads array config values (perm_messages).
 *
 * RETURNS: Number of config values loaded, or -1 on error
 */
int mariadb_config_load(void);

/*
 * mariadb_config_save - Save a single config key/value to database
 *
 * Uses INSERT ... ON DUPLICATE KEY UPDATE for upsert behavior.
 *
 * PARAMETERS:
 *   key   - Config key name (e.g. "muse_name")
 *   value - Config value as string
 *   type  - One of "STR", "NUM", "REF", "LNG"
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_config_save(const char *key, const char *value, const char *type);

/*
 * mariadb_config_save_all - Save all current config values to database
 *
 * Iterates all config entries using conf.h macros and writes each to
 * the database. Also saves array values (perm_messages).
 * Used by @config/seed.
 *
 * RETURNS: Number of config values saved, or -1 on error
 */
int mariadb_config_save_all(void);

/*
 * mariadb_config_load_array - Load an array of config values by prefix
 *
 * Queries rows matching "prefix-%" and builds a dynamically allocated
 * char** array. Each element and the array are allocated via SAFE_MALLOC.
 *
 * PARAMETERS:
 *   prefix     - Key prefix to match (e.g. "perm_messages")
 *   out_array  - Output: pointer to char** array (caller must free)
 *   out_count  - Output: number of elements loaded
 *
 * RETURNS: Number of elements loaded, or -1 on error
 */
int mariadb_config_load_array(const char *prefix, char ***out_array,
                               int *out_count);

/*
 * mariadb_config_save_array - Save an array of config values with numbered keys
 *
 * Saves each element as "prefix-N" (1-based). Removes stale entries.
 *
 * PARAMETERS:
 *   prefix - Key prefix (e.g. "perm_messages")
 *   array  - Array of strings to save
 *   count  - Number of elements in array
 *
 * RETURNS: Number of elements saved, or -1 on error
 */
int mariadb_config_save_array(const char *prefix, char **array, int count);

/*
 * mariadb_is_connected - Check if MariaDB connection is active
 *
 * RETURNS: 1 if connected, 0 if not
 */
int mariadb_is_connected(void);

/*
 * mariadb_cleanup - Close MariaDB connection and free resources
 *
 * Called during server shutdown. Safe to call even if not connected.
 */
void mariadb_cleanup(void);

#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS (MariaDB not available at compile time)
 * ============================================================================
 * MariaDB is REQUIRED for server operation. These stubs return failure
 * values that will cause the server to exit at startup with a clear
 * error message from server_main.c.
 */

static inline int mariadb_init(void) { return 0; }
static inline int mariadb_config_load(void) { return -1; }
static inline int mariadb_config_save(const char *key __attribute__((unused)),
                                       const char *value __attribute__((unused)),
                                       const char *type __attribute__((unused)))
{ return 0; }
static inline int mariadb_config_save_all(void) { return -1; }
static inline int mariadb_config_load_array(const char *prefix __attribute__((unused)),
                                             char ***out_array __attribute__((unused)),
                                             int *out_count __attribute__((unused)))
{ return -1; }
static inline int mariadb_config_save_array(const char *prefix __attribute__((unused)),
                                             char **array __attribute__((unused)),
                                             int count __attribute__((unused)))
{ return -1; }
static inline int mariadb_is_connected(void) { return 0; }
static inline void mariadb_cleanup(void) { }

#endif /* USE_MARIADB */

#endif /* _MARIADB_H_ */
