/* hash_table.c - Unified Hash Table Implementation with FNV-1a
 * 
 * ============================================================================
 * IMPLEMENTATION NOTES
 * ============================================================================
 * 
 * HASH ALGORITHM: FNV-1a (Fowler-Noll-Vo)
 * - 32-bit version used for efficiency
 * - Excellent distribution with low collision rate
 * - Fast computation (XOR and multiply per byte)
 * - Well-tested in production systems
 * 
 * COLLISION RESOLUTION: Separate Chaining
 * - Each bucket is a linked list
 * - Simple and efficient for typical load factors
 * - Handles variable load well
 * - Easy to implement correctly
 * 
 * MEMORY MANAGEMENT:
 * - All allocations use SAFE_MALLOC
 * - All frees use SMART_FREE
 * - Keys are always copied internally
 * - Values are opaque - caller manages unless destructor provided
 * 
 * CASE SENSITIVITY:
 * - Configurable per table
 * - Case-insensitive mode converts to lowercase before hashing
 * - No locale dependencies - simple ASCII lowercase
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Insert: O(1) average, O(n) worst case (n = chain length)
 * - Lookup: O(1) average, O(n) worst case
 * - Remove: O(1) average, O(n) worst case
 * - Iterate: O(size + count)
 * - Space: O(size + count)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "hash_table.h"

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/* Global list of all hash tables for debugging/introspection */
static hash_table_t *all_tables = NULL;

/**
 * register_table - Add table to global list
 * 
 * Automatically called by hash_create() to register table for
 * debugging commands like @showhash.
 * 
 * @param table Table to register
 */
static void register_table(hash_table_t *table)
{
    if (!table) {
        return;
    }
    
    /* Add to head of global list */
    table->next = all_tables;
    all_tables = table;
    
    log_important(tprintf("HASH REGISTER: '%s' added to global list", table->name));
}

/**
 * unregister_table - Remove table from global list
 * 
 * Automatically called by hash_destroy() to unregister table.
 * 
 * @param table Table to unregister
 */
static void unregister_table(hash_table_t *table)
{
    hash_table_t *curr, *prev;
    
    if (!table) {
        return;
    }
    
    /* Find and remove from list */
    prev = NULL;
    for (curr = all_tables; curr; curr = curr->next) {
        if (curr == table) {
            if (prev) {
                prev->next = curr->next;
            } else {
                all_tables = curr->next;
            }
            log_important(tprintf("HASH UNREGISTER: '%s' removed from global list",
                                 table->name));
            return;
        }
        prev = curr;
    }
    
    log_error(tprintf("HASH UNREGISTER: '%s' not found in global list!",
                     table->name));
}

/**
 * to_lowercase - Convert string to lowercase (internal copy)
 * 
 * @param str Source string
 * @return Newly allocated lowercase string or NULL
 * 
 * MEMORY: Caller must free returned string
 * SECURITY: NULL-safe, uses SAFE_MALLOC
 */
static char *to_lowercase(const char *str)
{
    char *result;
    char *p;
    size_t len;
    
    if (!str) {
        return NULL;
    }
    
    len = strlen(str);
    SAFE_MALLOC(result, char, len + 1);
    
    p = result;
    while (*str) {
        *p++ = to_lower((unsigned char)*str);
        str++;
    }
    *p = '\0';
    
    return result;
}

/**
 * string_equal - Compare strings respecting case sensitivity setting
 * 
 * @param s1 First string
 * @param s2 Second string
 * @param case_sensitive Comparison mode
 * @return 1 if equal, 0 if different
 * 
 * SECURITY: NULL-safe
 */
static int string_equal(const char *s1, const char *s2, int case_sensitive)
{
    if (!s1 || !s2) {
        return (s1 == s2);
    }
    
    if (case_sensitive) {
        return strcmp(s1, s2) == 0;
    } else {
        return string_compare(s1, s2) == 0;
    }
}

/* ============================================================================
 * HASH FUNCTION - FNV-1a
 * ============================================================================ */

/**
 * hash_fnv1a - Compute FNV-1a 32-bit hash
 * 
 * ALGORITHM:
 * 1. Initialize hash to FNV offset basis (2166136261)
 * 2. For each byte:
 *    a. XOR hash with byte value
 *    b. Multiply hash by FNV prime (16777619)
 * 3. Return final hash
 * 
 * PROPERTIES:
 * - Fast: Only XOR and multiply per byte
 * - Good distribution: Even for similar strings
 * - Low collision rate: Typically <1% for reasonable loads
 * - Avalanche effect: Single bit change affects entire hash
 * 
 * @param str String to hash (NULL returns offset basis)
 * @param case_sensitive 0 to ignore case, 1 for exact
 * @return 32-bit hash value
 */
uint32_t hash_fnv1a(const char *str, int case_sensitive)
{
    uint32_t hash = FNV_32_OFFSET;
    const unsigned char *p;
    
    if (!str) {
        return hash;
    }
    
    p = (const unsigned char *)str;
    
    if (case_sensitive) {
        /* Case-sensitive: hash bytes as-is */
        while (*p) {
            hash ^= *p++;
            hash *= FNV_32_PRIME;
        }
    } else {
        /* Case-insensitive: convert to lowercase first */
        while (*p) {
            hash ^= to_lower(*p);
            p++;
            hash *= FNV_32_PRIME;
        }
    }
    
    return hash;
}

/* ============================================================================
 * CORE FUNCTIONS
 * ============================================================================ */

/**
 * hash_create - Create new hash table
 * 
 * MEMORY: Allocates table structure and bucket array
 * VALIDATION: Checks size is reasonable and power of 2
 * 
 * @param name Table name (copied internally)
 * @param size Number of buckets (should be power of 2)
 * @param case_sensitive 0 for case-insensitive, 1 for case-sensitive
 * @param value_destructor Optional function to free values
 * @return New table or NULL on error
 */
hash_table_t *hash_create(const char *name, size_t size, int case_sensitive,
                          void (*value_destructor)(void *))
{
    hash_table_t *table;
    size_t i;
    
    /* Validate inputs */
    if (!name || size == 0) {
        log_error("hash_create: Invalid parameters");
        return NULL;
    }
    
    /* Warn if size is not power of 2 (but allow it) */
    if (!hash_is_power_of_2(size)) {
        log_important(tprintf("hash_create: Size %zu not power of 2 (performance warning)",
                             size));
    }
    
    /* Limit maximum size to prevent memory issues */
    if (size > (1 << 24)) {  /* 16 million buckets */
        log_error(tprintf("hash_create: Size %zu exceeds maximum", size));
        return NULL;
    }
    
    /* Allocate table structure */
    SAFE_MALLOC(table, hash_table_t, 1);
    
    /* Allocate and copy name */
    {
        size_t name_len = strlen(name) + 1;
        SAFE_MALLOC(table->name, char, name_len);
        memcpy(table->name, name, name_len - 1);
        table->name[name_len - 1] = '\0';
    }
    
    /* Allocate buckets array (initially all NULL) */
    SAFE_MALLOC(table->buckets, hash_entry_t *, size);
    for (i = 0; i < size; i++) {
        table->buckets[i] = NULL;
    }
    
    /* Initialize fields */
    table->size = size;
    table->count = 0;
    table->case_sensitive = case_sensitive;
    table->value_destructor = value_destructor;
    table->next = NULL;
    
    log_important(tprintf("HASH CREATE: '%s' created with %zu buckets (case_sensitive=%d, destructor=%s)",
                         name, size, case_sensitive,
                         (value_destructor != NULL) ? "yes" : "no"));
    
    /* Automatically register for @showhash */
    register_table(table);
    
    return table;
}

/**
 * hash_destroy - Destroy hash table and free all memory
 * 
 * MEMORY: Frees all entries, keys, buckets, name, and table structure
 * BEHAVIOR: Calls value_destructor on all values if set
 * 
 * @param table Table to destroy (NULL-safe)
 */
void hash_destroy(hash_table_t *table)
{
    size_t i;
    hash_entry_t *entry;
    hash_entry_t *next;
    
    if (!table) {
        return;
    }
    
    /* Unregister from global list first */
    unregister_table(table);
    
    /* Free all entries */
    for (i = 0; i < table->size; i++) {
        entry = table->buckets[i];
        while (entry) {
            next = entry->next;
            
            /* Free key */
            if (entry->key) {
                SMART_FREE(entry->key);
            }
            
            /* Call destructor on value if provided */
            if (table->value_destructor && entry->value) {
                table->value_destructor(entry->value);
            }
            
            /* Free entry structure */
            SMART_FREE(entry);
            
            entry = next;
        }
    }
    
    /* Free buckets array */
    if (table->buckets) {
        SMART_FREE(table->buckets);
    }
    
    /* Free name */
    if (table->name) {
        log_important(tprintf("HASH DESTROY: '%s' destroyed (%zu entries freed)",
                             table->name, table->count));
        SMART_FREE(table->name);
    }
    
    /* Free table structure */
    SMART_FREE(table);
}

/**
 * hash_insert - Insert or update key-value pair
 * 
 * BEHAVIOR: If key exists, replaces value (destroying old if destructor set)
 * MEMORY: Makes internal copy of key
 * 
 * @param table Target table
 * @param key Key string (copied)
 * @param value Value pointer
 * @return 1 on success, 0 on error
 */
int hash_insert(hash_table_t *table, const char *key, void *value)
{
    uint32_t hash;
    size_t bucket;
    hash_entry_t *entry;
    hash_entry_t *new_entry;
    
    /* Validate inputs */
    if (!table || !key) {
        log_error("hash_insert: Invalid parameters");
        return 0;
    }
    
    /* Compute hash and bucket */
    hash = hash_fnv1a(key, table->case_sensitive);
    bucket = hash % table->size;
    
    /* Check if key already exists */
    entry = table->buckets[bucket];
    while (entry) {
        if (entry->hash == hash && string_equal(entry->key, key, table->case_sensitive)) {
            /* Key exists - update value */
            if (table->value_destructor && entry->value) {
                table->value_destructor(entry->value);
            }
            entry->value = value;
            log_important(tprintf("HASH UPDATE: '%s' key='%s' (updated existing entry, count=%zu)",
                                 table->name, key, table->count));
            return 1;
        }
        entry = entry->next;
    }
    
    /* Key doesn't exist - create new entry */
    SAFE_MALLOC(new_entry, hash_entry_t, 1);
    
    /* Copy key */
    {
        size_t key_len = strlen(key) + 1;
        SAFE_MALLOC(new_entry->key, char, key_len);
        memcpy(new_entry->key, key, key_len - 1);
        new_entry->key[key_len - 1] = '\0';
    }
    
    /* Set fields */
    new_entry->value = value;
    new_entry->hash = hash;
    
    /* Insert at head of bucket list */
    new_entry->next = table->buckets[bucket];
    table->buckets[bucket] = new_entry;
    
    table->count++;
    
    log_important(tprintf("HASH INSERT: '%s' key='%s' (new entry, count=%zu, bucket=%zu)",
                         table->name, key, table->count, bucket));
    
    return 1;
}

/**
 * hash_lookup - Find value by key
 * 
 * PERFORMANCE: O(1) average case with good hash function
 * 
 * @param table Hash table to search
 * @param key Key to find
 * @return Value pointer or NULL if not found
 */
void *hash_lookup(const hash_table_t *table, const char *key)
{
    uint32_t hash;
    size_t bucket;
    hash_entry_t *entry;
    
    /* Validate inputs */
    if (!table || !key) {
        return NULL;
    }
    
    /* Compute hash and bucket */
    hash = hash_fnv1a(key, table->case_sensitive);
    bucket = hash % table->size;
    
    /* Search bucket chain */
    entry = table->buckets[bucket];
    while (entry) {
        if (entry->hash == hash && string_equal(entry->key, key, table->case_sensitive)) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * hash_remove - Remove entry by key
 * 
 * MEMORY: Frees key and entry, calls destructor on value if set
 * 
 * @param table Hash table
 * @param key Key to remove
 * @return 1 if removed, 0 if not found
 */
int hash_remove(hash_table_t *table, const char *key)
{
    uint32_t hash;
    size_t bucket;
    hash_entry_t *entry;
    hash_entry_t *prev;
    
    /* Validate inputs */
    if (!table || !key) {
        return 0;
    }
    
    /* Compute hash and bucket */
    hash = hash_fnv1a(key, table->case_sensitive);
    bucket = hash % table->size;
    
    /* Search bucket chain */
    prev = NULL;
    entry = table->buckets[bucket];
    
    while (entry) {
        if (entry->hash == hash && string_equal(entry->key, key, table->case_sensitive)) {
            /* Found it - remove from chain */
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[bucket] = entry->next;
            }
            
            /* Free key */
            if (entry->key) {
                SMART_FREE(entry->key);
            }
            
            /* Call destructor on value if provided */
            if (table->value_destructor && entry->value) {
                table->value_destructor(entry->value);
            }
            
            /* Free entry */
            SMART_FREE(entry);
            
            table->count--;
            log_important(tprintf("HASH REMOVE: '%s' key='%s' (removed, count=%zu)",
                                 table->name, key, table->count));
            return 1;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    return 0;
}

/**
 * hash_exists - Check if key exists in table
 * 
 * @param table Hash table
 * @param key Key to check
 * @return 1 if exists, 0 if not
 */
int hash_exists(const hash_table_t *table, const char *key)
{
    return hash_lookup(table, key) != NULL;
}

/**
 * hash_clear - Remove all entries from table
 * 
 * MEMORY: Frees all entries and keys, calls destructor on all values
 * STRUCTURE: Preserves table structure and size
 * 
 * @param table Table to clear
 */
void hash_clear(hash_table_t *table)
{
    size_t i;
    hash_entry_t *entry;
    hash_entry_t *next;
    
    if (!table) {
        return;
    }
    
    /* Free all entries */
    for (i = 0; i < table->size; i++) {
        entry = table->buckets[i];
        while (entry) {
            next = entry->next;
            
            /* Free key */
            if (entry->key) {
                SMART_FREE(entry->key);
            }
            
            /* Call destructor on value if provided */
            if (table->value_destructor && entry->value) {
                table->value_destructor(entry->value);
            }
            
            /* Free entry */
            SMART_FREE(entry);
            
            entry = next;
        }
        
        table->buckets[i] = NULL;
    }
    
    table->count = 0;
    
    log_important(tprintf("HASH CLEAR: '%s' cleared (all entries removed)", table->name));
}

/* ============================================================================
 * ITERATION FUNCTIONS
 * ============================================================================ */

/**
 * hash_iterate_init - Initialize iterator for traversing table
 * 
 * @param table Table to iterate
 * @param iter Iterator structure to initialize
 */
void hash_iterate_init(const hash_table_t *table, hash_iterator_t *iter)
{
    if (!table || !iter) {
        return;
    }
    
    iter->table = table;
    iter->bucket_index = 0;
    iter->current = NULL;
}

/**
 * hash_iterate_next - Get next entry in iteration
 *
 * @param iter Iterator
 * @param key Pointer to receive key (optional)
 * @param value Pointer to receive value (optional)
 * @return 1 if entry returned, 0 if done
 *
 * BUG FIX (2025-11-17): Fixed infinite loop where bucket_index was not
 * incremented after processing entries in a bucket, causing the same
 * bucket to be processed repeatedly.
 */
int hash_iterate_next(hash_iterator_t *iter, char **key, void **value)
{
    if (!iter || !iter->table) {
        return 0;
    }

    /* If we have a current entry, move to next in chain */
    if (iter->current) {
        iter->current = iter->current->next;
    }

    /* Find next non-empty bucket if needed */
    while (!iter->current && iter->bucket_index < iter->table->size) {
        iter->current = iter->table->buckets[iter->bucket_index];
        iter->bucket_index++;  /* ALWAYS increment - moved outside the if */
    }

    /* Return current entry if we have one */
    if (iter->current) {
        if (key) {
            *key = iter->current->key;
        }
        if (value) {
            *value = iter->current->value;
        }
        return 1;
    }

    return 0;
}

/* ============================================================================
 * STATISTICS AND DEBUGGING
 * ============================================================================ */

/**
 * hash_get_stats - Compute hash table statistics
 * 
 * USAGE: Performance analysis and tuning
 * 
 * @param table Table to analyze
 * @param stats Structure to fill
 */
void hash_get_stats(const hash_table_t *table, hash_stats_t *stats)
{
    size_t i;
    hash_entry_t *entry;
    size_t chain_length;
    size_t total_chain_length;
    
    if (!table || !stats) {
        return;
    }
    
    /* Initialize stats */
    stats->entries = table->count;
    stats->buckets_total = table->size;
    stats->buckets_used = 0;
    stats->max_chain_length = 0;
    total_chain_length = 0;
    
    /* Analyze each bucket */
    for (i = 0; i < table->size; i++) {
        if (table->buckets[i]) {
            stats->buckets_used++;
            
            /* Count chain length */
            chain_length = 0;
            entry = table->buckets[i];
            while (entry) {
                chain_length++;
                entry = entry->next;
            }
            
            total_chain_length += chain_length;
            
            if (chain_length > stats->max_chain_length) {
                stats->max_chain_length = chain_length;
            }
        }
    }
    
    /* Compute averages */
    if (stats->buckets_used > 0) {
        stats->avg_chain_length = (double)total_chain_length / stats->buckets_used;
    } else {
        stats->avg_chain_length = 0.0;
    }
    
    stats->load_factor = (double)stats->entries / stats->buckets_total;
}

/**
 * hash_dump - Dump hash table contents for debugging
 * 
 * @param table Table to dump
 * @param player Player to send output to
 */
void hash_dump(const hash_table_t *table, dbref player)
{
    hash_stats_t stats;
    hash_iterator_t iter;
    char *key;
    void *value;
    int count = 0;
    
    if (!table) {
        notify(player, "NULL hash table");
        return;
    }
    
    /* Display header */
    notify(player, tprintf("Hash Table: %s", table->name));
    notify(player, tprintf("  Size: %zu buckets", table->size));
    notify(player, tprintf("  Entries: %zu", table->count));
    notify(player, tprintf("  Case Sensitive: %s", table->case_sensitive ? "Yes" : "No"));
    
    /* Get statistics */
    hash_get_stats(table, &stats);
    notify(player, tprintf("  Buckets Used: %zu (%.1f%%)",
                          stats.buckets_used,
                          100.0 * stats.buckets_used / stats.buckets_total));
    notify(player, tprintf("  Load Factor: %.2f", stats.load_factor));
    notify(player, tprintf("  Max Chain: %zu", stats.max_chain_length));
    notify(player, tprintf("  Avg Chain: %.2f", stats.avg_chain_length));
    
    /* List entries (limited to first 100) */
    notify(player, "\nEntries (first 100):");
    hash_iterate_init(table, &iter);
    while (hash_iterate_next(&iter, &key, &value) && count < 100) {
        notify(player, tprintf("  [%d] %s -> %p", count, key, value));
        count++;
    }
    
    if (table->count > 100) {
        notify(player, tprintf("  ... and %zu more entries", table->count - 100));
    }
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * hash_suggest_size - Suggest optimal size for expected entry count
 * 
 * HEURISTIC: Targets load factor of 0.75 for good performance
 * RETURNS: Next power of 2 that gives load factor <= 0.75
 * 
 * @param expected_entries Expected number of entries
 * @return Suggested size (power of 2)
 */
size_t hash_suggest_size(size_t expected_entries)
{
    size_t size = 16;  /* Minimum reasonable size */
    size_t target;
    
    if (expected_entries == 0) {
        return size;
    }
    
    /* Calculate target size for 0.75 load factor */
    target = (expected_entries * 4) / 3;  /* expected / 0.75 */
    
    /* Round up to next power of 2 */
    while (size < target && size < (1 << 24)) {
        size <<= 1;
    }
    
    return size;
}

/**
 * hash_is_power_of_2 - Check if number is power of 2
 * 
 * ALGORITHM: n is power of 2 if (n & (n-1)) == 0 and n != 0
 * 
 * @param n Number to check
 * @return 1 if power of 2, 0 otherwise
 */
int hash_is_power_of_2(size_t n)
{
    return (n != 0) && ((n & (n - 1)) == 0);
}

/* ============================================================================
 * GLOBAL TABLE LIST ACCESS
 * ============================================================================ */

/**
 * hash_list_all - List all registered hash tables
 * 
 * Used by @showhash command to display all tables or show statistics.
 * 
 * @param player Player to send output to
 * @param table_name Optional specific table name (NULL to list all)
 */
void hash_list_all(dbref player, const char *table_name)
{
    hash_table_t *table;
    hash_stats_t stats;
    int count = 0;
    
    /* Validate player */
    if (!GoodObject(player)) {
        log_error("hash_list_all: Invalid player object");
        return;
    }
    
    /* If no specific table requested, list all */
    if (!table_name || !*table_name) {
        notify(player, "Hash tables:");
        for (table = all_tables; table; table = table->next) {
            if (!table->name) {
                notify(player, tprintf("  [ERROR: NULL name in table %p]",
                                      (void *)table));
                continue;
            }
            notify(player, tprintf("  %s", table->name));
            count++;
        }
        if (count == 0) {
            notify(player, "  (none registered)");
        }
        notify(player, "Done.");
        return;
    }
    
    /* Find specific table and show stats */
    for (table = all_tables; table; table = table->next) {
        if (!table->name) {
            continue;
        }
        
        if (!string_compare(table_name, table->name)) {
            /* Found it - get statistics */
            hash_get_stats(table, &stats);
            
            notify(player, tprintf("%s: %zu entries, %zu buckets, %zu used (%.1f%%)",
                                  table->name,
                                  stats.entries,
                                  stats.buckets_total,
                                  stats.buckets_used,
                                  (stats.buckets_total > 0)
                                      ? (100.0 * (double)stats.buckets_used / (double)stats.buckets_total)
                                      : 0.0));
            
            notify(player, tprintf("Load factor: %.2f, Max chain: %zu, Avg chain: %.2f",
                                  stats.load_factor,
                                  stats.max_chain_length,
                                  stats.avg_chain_length));
            
            return;
        }
    }
    
    notify(player, "Couldn't find that hash table.");
}

/**
 * hash_find_by_name - Find hash table by name
 * 
 * @param name Table name to search for
 * @return Pointer to table if found, NULL otherwise
 */
hash_table_t *hash_find_by_name(const char *name)
{
    hash_table_t *table;
    
    if (!name || !*name) {
        return NULL;
    }
    
    for (table = all_tables; table; table = table->next) {
        if (!table->name) {
            continue;
        }
        
        if (!string_compare(name, table->name)) {
            return table;
        }
    }
    
    return NULL;
}

/**
 * hash_count_tables - Count registered hash tables
 * 
 * @return Number of tables in global list
 */
int hash_count_tables(void)
{
    hash_table_t *table;
    int count = 0;
    
    for (table = all_tables; table; table = table->next) {
        count++;
    }
    
    return count;
}

/* ============================================================================
 * END OF hash_table.c
 * ============================================================================ */
