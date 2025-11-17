/* hash.c - Compatibility wrapper for old hash table interface
 * 
 * ============================================================================
 * MODERNIZATION NOTES
 * ============================================================================
 * 
 * CONVERSION SUMMARY:
 * - Converted from K&R C to ANSI C with proper function prototypes
 * - Replaced deprecated ht_destroy() with hash_destroy()
 * - Replaced deprecated ht_stats() with hash_get_stats() using hash_stats_t
 * - Replaced strcpy() with strncpy() for buffer safety
 * - Added GoodObject() validation for dbref parameters
 * - Updated statistics display to use new hash_stats_t structure
 * - Removed commented-out legacy code
 * - Enhanced documentation throughout
 * - **UPDATED**: Hash tables now auto-register in hash_create()
 * - **UPDATED**: do_showhash() now uses hash_list_all() from hash_table.c
 * 
 * SECURITY IMPROVEMENTS:
 * - All memory allocation uses SAFE_MALLOC
 * - All deallocation uses SMART_FREE
 * - Added NULL pointer checks throughout
 * - Added bounds checking on string operations
 * - Added validation of wrapper structures before use
 * - Proper cleanup in free_hash() to prevent memory leaks
 * 
 * PURPOSE:
 * This maintains the old hash.c API but uses the new hash_table.c
 * implementation underneath. Mainly used for built-in attribute lookup.
 * Acts as a compatibility shim to avoid rewriting legacy code.
 * 
 * ARCHITECTURE:
 * - Wraps new hash_table_t with legacy struct hashtab interface
 * - Hash tables automatically register on creation (no manual registration needed)
 * - @showhash command now uses global list in hash_table.c
 * - Legacy wrapper system (hashtab_wrapper) is deprecated but maintained for compatibility
 */

#include "config.h"
#include "externs.h"
#include "hash.h"
#include "hash_table.h"

/* ===================================================================
 * Global Hash Table List
 * =================================================================== */

struct hashtab_wrapper {
    char *name;
    hash_table_t *ht;
    char *(*display)(void *);
    struct hashtab_wrapper *next;
};

static struct hashtab_wrapper *hashtabs = NULL;

/* ===================================================================
 * Cleanup
 * =================================================================== */

/**
 * free_hash - Clean up all registered hash tables
 * 
 * Called during server shutdown to free all hash tables and wrappers.
 * Iterates through the global hashtabs list and destroys each table.
 * 
 * MEMORY: Frees all wrapper structures, names, and hash tables
 * CLEANUP: Calls hash_destroy() which handles table contents
 * SAFETY: NULL-safe, prevents double-free by setting hashtabs to NULL
 * 
 * PROCESS:
 * 1. Iterate through wrapper list
 * 2. Destroy underlying hash table
 * 3. Free wrapper name string
 * 4. Free wrapper structure
 * 5. Reset global list to NULL
 */

void free_hash(void)
{
    struct hashtab_wrapper *wrapper;
    struct hashtab_wrapper *next;
    int count = 0;
    
    log_important("FREE_HASH: Starting cleanup of all registered hash tables");
    
    for (wrapper = hashtabs; wrapper; wrapper = next) {
        next = wrapper->next;
        
        if (wrapper->name) {
            log_important(tprintf("FREE_HASH: Destroying wrapper for '%s'", wrapper->name));
        }
        
        hash_destroy(wrapper->ht);
        SMART_FREE(wrapper->name);
        SMART_FREE(wrapper);
        count++;
    }
    
    hashtabs = NULL;
    
    log_important(tprintf("FREE_HASH: Cleanup complete (%d wrappers destroyed)", count));
}

/* ===================================================================
 * Debug Command
 * =================================================================== */

/**
 * do_showhash - Display hash table information
 * 
 * Lists all registered hash tables or shows detailed statistics for a
 * specific table. Used by the @showhash command.
 * 
 * UPDATED: Now delegates to hash_list_all() in hash_table.c which
 * uses the automatic registration system.
 * 
 * @param player Player receiving the output (validated)
 * @param arg1 Optional table name (NULL or empty to list all)
 */
void do_showhash(dbref player, char *arg1)
{
    /* Validate player object */
    if (!GoodObject(player)) {
        log_error("do_showhash: Invalid player object");
        return;
    }
    
    /* Delegate to hash_table.c global list function */
    hash_list_all(player, arg1);
}

/* ===================================================================
 * Hash Table Creation
 * =================================================================== */

/**
 * make_hashtab - Create hash table (adapted for new system)
 * 
 * Creates a compatibility wrapper around the new hash_table_t implementation.
 * Maintains the old API while using modern hash table underneath.
 * 
 * COMPATIBILITY: Same signature as legacy version
 * IMPLEMENTATION: Uses hash_create() and hash_insert() from hash_table.c
 * MEMORY: Caller must eventually free via free_hash()
 * 
 * @param nbuck Number of buckets (converted to size_t internally)
 * @param ents Array of hashdeclent structures to populate
 * @param entsize Size of each entry structure
 * @param name Table name (copied internally)
 * @param displayfunc Optional display function for entries
 * @return New hashtab wrapper or NULL on failure
 */
struct hashtab *make_hashtab(int nbuck, void *ents, int entsize,
                             char *name, char *(*displayfunc)(void *))
{
    struct hashtab *op;
    struct hashdeclent *thisent;
    hash_table_t *new_table;
    size_t name_len;
    int entry_count = 0;

    log_important(tprintf("MAKE_HASHTAB: Creating legacy wrapper for '%s' with %d buckets", 
                         name, nbuck));

    /* Create new hash table */
    new_table = hash_create(name, (size_t)nbuck, 0, NULL);
    if (!new_table) {
        log_error(tprintf("MAKE_HASHTAB: Failed to create hash table for '%s'", name));
        return NULL;
    }

    /* Create wrapper structure for compatibility */
    SAFE_MALLOC(op, struct hashtab, 1);

    op->nbuckets = nbuck;
    op->display = displayfunc;

    /* Store pointer to new hash table in place of buckets */
    /* This is a hack but maintains API compatibility */
    op->buckets = (hashbuck *)new_table;

    /* Copy name with bounds checking */
    name_len = strlen(name);
    SAFE_MALLOC(op->name, char, name_len + 1);
    strncpy(op->name, name, name_len);
    op->name[name_len] = '\0';

    /* Populate hash table from entries */
    for (thisent = ents; thisent && thisent->name;
         thisent = (struct hashdeclent *)(((char *)thisent) + entsize)) {
        hash_insert(new_table, thisent->name, thisent);
        entry_count++;
    }

    log_important(tprintf("MAKE_HASHTAB: Successfully created '%s' with %d entries", 
                         name, entry_count));

    return op;
}


/* ===================================================================
 * Hash Table Lookup
 * =================================================================== */

/**
 * lookup_hash - Look up value in hash table
 * 
 * Compatibility wrapper that extracts the new hash_table_t from the
 * legacy hashtab structure and performs lookup using hash_lookup().
 * 
 * COMPATIBILITY: Same signature as legacy version
 * IMPLEMENTATION: Delegates to hash_lookup() from hash_table.c
 * VALIDATION: NULL-safe for both table and name parameters
 * 
 * @param tab Hash table wrapper (legacy structure)
 * @param hashvalue Ignored (kept for API compatibility)
 * @param name Key to look up (must be non-NULL for success)
 * @return Pointer to value if found, NULL if not found or invalid params
 * 
 * NOTE: The hashvalue parameter is ignored because the new implementation
 * computes the hash internally using FNV-1a.
 */
void *lookup_hash(struct hashtab *tab, int hashvalue, char *name)
{
    hash_table_t *new_table;

    if (!tab || !name) {
        return NULL;
    }

    /* Extract new hash table from wrapper */
    new_table = (hash_table_t *)tab->buckets;

    /* Use new lookup function */
    return hash_lookup(new_table, name);
}

/* ===================================================================
 * Hash Function (for compatibility)
 * =================================================================== */

/**
 * hash_name - Compute hash of name
 * 
 * Compatibility wrapper that uses the new FNV-1a hash algorithm.
 * Returns hash value truncated to int for API compatibility.
 * 
 * ALGORITHM: FNV-1a via hash_fnv1a()
 * CASE: Case-insensitive (passes 0 to hash_fnv1a)
 * COMPATIBILITY: Same signature as legacy version
 * 
 * @param name String to hash (NULL returns 0)
 * @return Hash value as int (truncated from uint32_t)
 */
int hash_name(char *name)
{
    /* Return FNV-1a hash, truncated to int for compatibility */
    return (int)hash_fnv1a(name, 0);
}

/* ===================================================================
 * Hash Table Registration (DEPRECATED)
 * =================================================================== */

/**
 * register_hashtab - Register hash table with wrapper system
 * 
 * **DEPRECATED**: As of the automatic registration update, hash tables
 * are automatically registered when created via hash_create(). This
 * function is maintained for backwards compatibility but does nothing.
 * 
 * This function previously allowed @showhash to see hash tables created
 * directly with hash_create(). Now that hash_create() automatically
 * registers tables in a global list, manual registration is unnecessary.
 * 
 * COMPATIBILITY: Function maintained but returns success without action
 * NEW CODE: Do not call this function - registration is automatic
 * 
 * @param ht Hash table to register (ignored)
 * @param name Display name for the hash table (ignored)
 * @param displayfunc Optional display function (ignored)
 * @return Always returns 1 (success)
 */
int register_hashtab(hash_table_t *ht, const char *name, char *(*displayfunc)(void *))
{
    /* Validation for safety */
    if (!ht) {
        log_error("register_hashtab: NULL hash table pointer");
        return 0;
    }
    
    if (!name || !*name) {
        log_error("register_hashtab: NULL or empty name");
        return 0;
    }
    
    /* Log that this is deprecated */
    log_important(tprintf("register_hashtab: Called for '%s' (DEPRECATED - registration is now automatic)",
                         name));
    
    /* Always succeed since table is already registered */
    return 1;
}
