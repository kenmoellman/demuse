/* mariadb.c - MariaDB integration for deMUSE configuration persistence
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * This file implements persistent storage of runtime configuration values
 * in a MariaDB database. All code is wrapped in #ifdef USE_MARIADB.
 *
 * MariaDB is REQUIRED for server operation. All config values (including
 * defaults) are stored in the database. The server will not start without
 * a populated config table.
 *
 * FEATURES:
 * - Credential parsing from run/db/mariadb.conf
 * - Connection management with error handling
 * - Config table CRUD operations (load, save, save_all)
 * - Array config support (e.g. perm_messages-1, perm_messages-2, ...)
 * - Uses the same conf.h macro trick as conf.c for iterating config entries
 *
 * SAFETY:
 * - All SQL uses proper escaping to prevent injection
 * - String config values use SET() macro for proper memory management
 * - Array elements individually allocated with SAFE_MALLOC
 *
 * DEPENDENCIES:
 * - libmariadb-dev (MariaDB Connector/C)
 * - config.h, externs.h, mariadb.h
 */

#ifdef USE_MARIADB

/* Suppress conversion warnings from MariaDB headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <mysql.h>
#pragma GCC diagnostic pop

#include "config.h"
#include "externs.h"
#include "mariadb.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/* MariaDB connection handle */
static MYSQL *mariadb_conn = NULL;

/* Credential storage */
#define MARIADB_CRED_MAXLEN 256
static char mariadb_host[MARIADB_CRED_MAXLEN];
static char mariadb_user[MARIADB_CRED_MAXLEN];
static char mariadb_pass[MARIADB_CRED_MAXLEN];
static char mariadb_dbname[MARIADB_CRED_MAXLEN];
static unsigned int mariadb_port = 3306;

/* Config file path (relative to run directory) */
#define MARIADB_CONF_FILE "db/mariadb.conf"

/* ============================================================================
 * CREDENTIAL PARSING
 * ============================================================================ */

/*
 * trim_whitespace - Remove leading/trailing whitespace from a string in place
 *
 * PARAMETERS:
 *   str - String to trim (modified in place)
 *
 * RETURNS: Pointer to trimmed string (same buffer, possibly offset)
 */
static char *trim_whitespace(char *str)
{
    char *end;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    /* Trim trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';

    return str;
}

/*
 * parse_credentials - Read MariaDB credentials from config file
 *
 * File format (simple key=value, one per line):
 *   host=localhost
 *   port=3306
 *   user=demuse
 *   password=secret
 *   database=demuse
 *
 * Lines starting with # are comments. Blank lines are ignored.
 *
 * RETURNS: 1 on success, 0 on failure
 */
static int parse_credentials(void)
{
    FILE *fp;
    char line[512];
    char *key;
    char *value;
    char *eq;

    /* Set defaults */
    strncpy(mariadb_host, "localhost", MARIADB_CRED_MAXLEN);
    mariadb_host[MARIADB_CRED_MAXLEN - 1] = '\0';
    strncpy(mariadb_user, "demuse", MARIADB_CRED_MAXLEN);
    mariadb_user[MARIADB_CRED_MAXLEN - 1] = '\0';
    mariadb_pass[0] = '\0';
    strncpy(mariadb_dbname, "demuse", MARIADB_CRED_MAXLEN);
    mariadb_dbname[MARIADB_CRED_MAXLEN - 1] = '\0';
    mariadb_port = 3306;

    fp = fopen(MARIADB_CONF_FILE, "r");
    if (!fp) {
        fprintf(stderr, "MariaDB: Cannot open credential file db/mariadb.conf\n");
        return 0;
    }

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }

        /* Skip comments and blank lines */
        key = trim_whitespace(line);
        if (*key == '#' || *key == '\0') {
            continue;
        }

        /* Find = separator */
        eq = strchr(key, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        value = trim_whitespace(eq + 1);
        key = trim_whitespace(key);

        if (strcmp(key, "host") == 0) {
            strncpy(mariadb_host, value, MARIADB_CRED_MAXLEN);
            mariadb_host[MARIADB_CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "port") == 0) {
            mariadb_port = (unsigned int)strtoul(value, NULL, 10);
        } else if (strcmp(key, "user") == 0) {
            strncpy(mariadb_user, value, MARIADB_CRED_MAXLEN);
            mariadb_user[MARIADB_CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "password") == 0) {
            strncpy(mariadb_pass, value, MARIADB_CRED_MAXLEN);
            mariadb_pass[MARIADB_CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "database") == 0) {
            strncpy(mariadb_dbname, value, MARIADB_CRED_MAXLEN);
            mariadb_dbname[MARIADB_CRED_MAXLEN - 1] = '\0';
        }
    }

    fclose(fp);
    return 1;
}

/* ============================================================================
 * CONNECTION MANAGEMENT
 * ============================================================================ */

/*
 * mariadb_init - Initialize MariaDB connection
 *
 * Reads credentials, initializes the MySQL library, and connects.
 * MariaDB is REQUIRED - failure to connect is fatal for server startup.
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_init(void)
{
    /* Parse credentials file */
    if (!parse_credentials()) {
        fprintf(stderr, "MariaDB: No credentials file - cannot start server\n");
        fprintf(stderr, "Run: bash config/setup_mariadb.sh\n");
        return 0;
    }

    /* Initialize MySQL library */
    mariadb_conn = mysql_init(NULL);
    if (!mariadb_conn) {
        fprintf(stderr, "MariaDB: mysql_init() failed - out of memory\n");
        return 0;
    }

    /* Set connection timeout (5 seconds) */
    unsigned int timeout = 5;
    mysql_options(mariadb_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    /* Attempt connection */
    if (!mysql_real_connect(mariadb_conn, mariadb_host, mariadb_user,
                            mariadb_pass, mariadb_dbname, mariadb_port,
                            NULL, 0)) {
        fprintf(stderr, "MariaDB: Connection failed: %s\n",
                mysql_error(mariadb_conn));
        mysql_close(mariadb_conn);
        mariadb_conn = NULL;
        return 0;
    }

    /* Set character set to UTF-8 */
    mysql_set_character_set(mariadb_conn, "utf8mb4");

    fprintf(stderr, "MariaDB: Connected to %s@%s:%u/%s\n",
            mariadb_user, mariadb_host, mariadb_port, mariadb_dbname);
    return 1;
}

/*
 * mariadb_is_connected - Check if MariaDB connection is active
 *
 * Also performs a ping to verify the connection is still alive.
 *
 * RETURNS: 1 if connected, 0 if not
 */
int mariadb_is_connected(void)
{
    if (!mariadb_conn) {
        return 0;
    }

    /* Ping to check connection is still alive */
    if (mysql_ping(mariadb_conn) != 0) {
        fprintf(stderr, "MariaDB: Connection lost: %s\n",
                mysql_error(mariadb_conn));
        mysql_close(mariadb_conn);
        mariadb_conn = NULL;
        return 0;
    }

    return 1;
}

/*
 * mariadb_cleanup - Close MariaDB connection and free resources
 *
 * Safe to call even if not connected.
 */
void mariadb_cleanup(void)
{
    if (mariadb_conn) {
        mysql_close(mariadb_conn);
        mariadb_conn = NULL;
        log_important("MariaDB: Connection closed");
    }
}

/* ============================================================================
 * ARRAY CONFIG OPERATIONS
 * ============================================================================ */

/*
 * mariadb_config_load_array - Load an array of config values by prefix
 *
 * Queries rows matching "prefix-%" pattern, ordered by key, and builds
 * a dynamically allocated char** array. Each element is allocated via
 * SAFE_MALLOC. The array itself is allocated via SAFE_MALLOC.
 *
 * Used for perm_messages (perm_messages-1, perm_messages-2, ...).
 *
 * PARAMETERS:
 *   prefix     - Key prefix to match (e.g. "perm_messages")
 *   out_array  - Output: pointer to char** array (caller must free)
 *   out_count  - Output: number of elements loaded
 *
 * RETURNS: Number of elements loaded, or -1 on error
 */
int mariadb_config_load_array(const char *prefix, char ***out_array,
                               int *out_count)
{
    MYSQL_RES *result;
    MYSQL_ROW row;
    char query[512];
    char escaped_prefix[129];
    int count;
    int i;
    char **array;

    if (!out_array || !out_count) {
        return -1;
    }
    *out_array = NULL;
    *out_count = 0;

    if (!mariadb_is_connected()) {
        return -1;
    }

    /* Escape prefix for SQL LIKE pattern */
    mysql_real_escape_string(mariadb_conn, escaped_prefix, prefix,
                             (unsigned long)strlen(prefix));

    snprintf(query, sizeof(query),
             "SELECT config_value FROM config "
             "WHERE config_key LIKE '%s-%%' "
             "ORDER BY config_key",
             escaped_prefix);

    if (mysql_query(mariadb_conn, query)) {
        fprintf(stderr, "MariaDB: array load query failed for '%s': %s\n",
                prefix, mysql_error(mariadb_conn));
        return -1;
    }

    result = mysql_store_result(mariadb_conn);
    if (!result) {
        fprintf(stderr, "MariaDB: array load store_result failed: %s\n",
                mysql_error(mariadb_conn));
        return -1;
    }

    count = (int)mysql_num_rows(result);
    if (count <= 0) {
        mysql_free_result(result);
        return 0;
    }

    /* Allocate array of char pointers */
    SAFE_MALLOC(array, char *, (size_t)count);

    i = 0;
    while ((row = mysql_fetch_row(result)) != NULL && i < count) {
        if (row[0]) {
            size_t len = strlen(row[0]) + 1;
            char *str;
            SAFE_MALLOC(str, char, len);
            strncpy(str, row[0], len);
            str[len - 1] = '\0';
            array[i] = str;
        } else {
            array[i] = NULL;
        }
        i++;
    }

    mysql_free_result(result);

    *out_array = array;
    *out_count = i;

    return i;
}

/*
 * mariadb_config_save_array - Save an array of config values with numbered keys
 *
 * Saves each array element as "prefix-N" where N starts at 1.
 * Also removes any old entries beyond the current array size.
 *
 * PARAMETERS:
 *   prefix - Key prefix (e.g. "perm_messages")
 *   array  - Array of strings to save
 *   count  - Number of elements in array
 *
 * RETURNS: Number of elements saved, or -1 on error
 */
int mariadb_config_save_array(const char *prefix, char **array, int count)
{
    int i;
    int saved = 0;
    char keybuf[64];

    if (!mariadb_is_connected()) {
        return -1;
    }

    if (!prefix || !array) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (array[i]) {
            snprintf(keybuf, sizeof(keybuf), "%s-%d", prefix, i + 1);
            if (mariadb_config_save(keybuf, array[i], "STR")) {
                saved++;
            }
        }
    }

    /* Remove any stale entries beyond current count */
    {
        char query[512];
        char escaped_prefix[129];

        mysql_real_escape_string(mariadb_conn, escaped_prefix, prefix,
                                 (unsigned long)strlen(prefix));

        /* Delete entries where the number suffix > count.
         * We can't do arithmetic in LIKE, so delete all matching prefix
         * then re-insert. Instead, just delete specific extras. */
        for (i = count + 1; i <= count + 50; i++) {
            snprintf(query, sizeof(query),
                     "DELETE FROM config WHERE config_key = '%s-%d'",
                     escaped_prefix, i);
            mysql_query(mariadb_conn, query);
        }
    }

    return saved;
}

/* ============================================================================
 * CONFIG TABLE OPERATIONS
 * ============================================================================ */

/*
 * mariadb_config_load - Load all config values from database
 *
 * Queries the config table and sets corresponding global variables.
 * Also loads array config values (perm_messages).
 *
 * RETURNS: Number of config values loaded, or -1 on error
 */
int mariadb_config_load(void)
{
    MYSQL_RES *result;
    MYSQL_ROW row;
    int count = 0;

    if (!mariadb_is_connected()) {
        return -1;
    }

    /* Query all config entries (excluding array entries which use prefix-N) */
    if (mysql_query(mariadb_conn,
                    "SELECT config_key, config_value, config_type FROM config "
                    "WHERE config_key NOT LIKE '%%-%%'")) {
        fprintf(stderr, "MariaDB: config load query failed: %s\n",
                mysql_error(mariadb_conn));
        return -1;
    }

    result = mysql_store_result(mariadb_conn);
    if (!result) {
        fprintf(stderr, "MariaDB: config load store_result failed: %s\n",
                mysql_error(mariadb_conn));
        return -1;
    }

    while ((row = mysql_fetch_row(result)) != NULL) {
        const char *db_key = row[0];
        const char *db_val = row[1];
        const char *db_type = row[2];

        if (!db_key || !db_val || !db_type) {
            continue;
        }

        /*
         * Match each database row against known config entries.
         * Uses the same conf.h macro trick as conf.c.
         *
         * For STR types: use SET() to properly free old and allocate new
         * For NUM types: use strtol and cast to int
         * For REF types: use strtol (dbref is long)
         * For LNG types: use strtol
         */

#define DO_STR(str, var) \
        if (strcmp(db_type, "STR") == 0 && !string_compare(db_key, str)) { \
            SET(var, (char *)db_val); \
            count++; \
            continue; \
        }

#define DO_NUM(str, var) \
        if (strcmp(db_type, "NUM") == 0 && !string_compare(db_key, str)) { \
            var = (int)strtol(db_val, NULL, 10); \
            count++; \
            continue; \
        }

#define DO_REF(str, var) \
        if (strcmp(db_type, "REF") == 0 && !string_compare(db_key, str)) { \
            var = strtol(db_val, NULL, 10); \
            count++; \
            continue; \
        }

#define DO_LNG(str, var) \
        if (strcmp(db_type, "LNG") == 0 && !string_compare(db_key, str)) { \
            var = strtol(db_val, NULL, 10); \
            count++; \
            continue; \
        }

#include "conf.h"

#undef DO_STR
#undef DO_NUM
#undef DO_REF
#undef DO_LNG
    }

    mysql_free_result(result);

    /* Load array config values: perm_messages */
    {
        char **pm_array = NULL;
        int pm_count = 0;
        int loaded;

        loaded = mariadb_config_load_array("perm_messages", &pm_array,
                                            &pm_count);
        if (loaded > 0 && pm_array) {
            /* Free any existing perm_messages array */
            if (perm_messages) {
                int i;
                for (i = 0; i < perm_messages_count; i++) {
                    if (perm_messages[i]) {
                        SAFE_FREE(perm_messages[i]);
                    }
                }
                SAFE_FREE(perm_messages);
            }
            perm_messages = pm_array;
            perm_messages_count = pm_count;
            count += pm_count;
        }
    }

    fprintf(stderr, "MariaDB: Loaded %d config values from database\n",
            count);
    return count;
}

/*
 * mariadb_config_save - Save a single config key/value to database
 *
 * Uses INSERT ... ON DUPLICATE KEY UPDATE for upsert behavior.
 * The config_key is the primary key; if it already exists, the value
 * and updated_at timestamp are updated.
 *
 * PARAMETERS:
 *   key   - Config key name (e.g. "muse_name")
 *   value - Config value as string
 *   type  - One of "STR", "NUM", "REF", "LNG"
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_config_save(const char *key, const char *value, const char *type)
{
    char query[2048];
    char escaped_key[129];
    char escaped_value[513];
    char escaped_type[9];

    if (!mariadb_is_connected()) {
        return 0;
    }

    if (!key || !value || !type) {
        return 0;
    }

    /* Escape input values to prevent SQL injection */
    mysql_real_escape_string(mariadb_conn, escaped_key, key,
                             (unsigned long)strlen(key));
    mysql_real_escape_string(mariadb_conn, escaped_value, value,
                             (unsigned long)strlen(value));
    mysql_real_escape_string(mariadb_conn, escaped_type, type,
                             (unsigned long)strlen(type));

    snprintf(query, sizeof(query),
             "INSERT INTO config (config_key, config_value, config_type, updated_at) "
             "VALUES ('%s', '%s', '%s', NOW()) "
             "ON DUPLICATE KEY UPDATE config_value='%s', config_type='%s', updated_at=NOW()",
             escaped_key, escaped_value, escaped_type,
             escaped_value, escaped_type);

    if (mysql_query(mariadb_conn, query)) {
        log_error(tprintf("MariaDB: config save failed for '%s': %s",
                          key, mysql_error(mariadb_conn)));
        return 0;
    }

    return 1;
}

/*
 * mariadb_config_save_all - Save all current config values to database
 *
 * Iterates all config entries using the conf.h macro trick and saves
 * each one. Also saves array config values (perm_messages).
 * Used by @config/seed to populate the database.
 *
 * RETURNS: Number of config values saved, or -1 on error
 */
int mariadb_config_save_all(void)
{
    int count = 0;
    char numbuf[32];

    if (!mariadb_is_connected()) {
        return -1;
    }

    /*
     * Iterate all config entries and save each one.
     * For numeric types, convert to string first.
     */

#define DO_STR(str, var) \
    if (var) { \
        if (mariadb_config_save(str, var, "STR")) count++; \
    }

#define DO_NUM(str, var) \
    do { \
        snprintf(numbuf, sizeof(numbuf), "%d", var); \
        if (mariadb_config_save(str, numbuf, "NUM")) count++; \
    } while(0);

#define DO_REF(str, var) \
    do { \
        snprintf(numbuf, sizeof(numbuf), "%" DBREF_FMT, var); \
        if (mariadb_config_save(str, numbuf, "REF")) count++; \
    } while(0);

#define DO_LNG(str, var) \
    do { \
        snprintf(numbuf, sizeof(numbuf), "%ld", var); \
        if (mariadb_config_save(str, numbuf, "LNG")) count++; \
    } while(0);

#include "conf.h"

#undef DO_STR
#undef DO_NUM
#undef DO_REF
#undef DO_LNG

    /* Save array config values: perm_messages */
    if (perm_messages && perm_messages_count > 0) {
        int saved = mariadb_config_save_array("perm_messages", perm_messages,
                                               perm_messages_count);
        if (saved > 0) {
            count += saved;
        }
    }

    log_important(tprintf("MariaDB: Saved %d config values to database",
                          count));
    return count;
}

#endif /* USE_MARIADB */
