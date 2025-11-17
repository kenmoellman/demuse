/* hash_table.h - Unified Hash Table System with FNV-1a
 * 
 * ============================================================================
 * UNIFIED HASH TABLE SYSTEM (2025)
 * ============================================================================
 * 
 * This provides a single, unified hash table implementation using the FNV-1a
 * hash algorithm. It replaces three separate hash table implementations:
 * 
 * 1. Player name hash (player.c) - for player lookup by name/alias
 * 2. Channel name hash (com.c) - for channel lookup by name
 * 3. Attribute hash (hash.c) - for attribute name lookup
 * 
 * DESIGN GOALS:
 * - Single implementation for all hash needs
 * - FNV-1a hash function for excellent distribution
 * - Support for case-sensitive and case-insensitive keys
 * - Efficient collision handling via chaining
 * - Memory safe with SAFE_MALLOC/SMART_FREE
 * - Extensible for future hash table needs
 * 
 * FNV-1a ALGORITHM:
 * - Fast and simple
 * - Excellent distribution properties
 * - Low collision rate
 * - Industry standard (used in many production systems)
 * 
 * USAGE:
 * ```c
 * hash_table_t *players = hash_create("players", 4096, 0, NULL);
 * hash_insert(players, "PlayerName", (void*)player_dbref);
 * void *result = hash_lookup(players, "PlayerName");
 * hash_destroy(players);
 * ```
 */

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * FNV-1a CONSTANTS
 * ============================================================================ */

/* FNV-1a 32-bit constants */
#define FNV_32_PRIME 0x01000193U      /* 16777619 */
#define FNV_32_OFFSET 0x811C9DC5U     /* 2166136261 */

/* Default hash table sizes (all power of 2 for efficient modulo) */
#define HASH_SIZE_SMALL    256
#define HASH_SIZE_MEDIUM   1024
#define HASH_SIZE_LARGE    4096
#define HASH_SIZE_XLARGE   16384

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/**
 * hash_entry_t - Single entry in hash table
 * 
 * Forms a singly-linked list for collision resolution.
 * Key is always dynamically allocated for consistency.
 * Value is opaque pointer - caller manages memory.
 */
typedef struct hash_entry {
    char *key;                    /* Dynamically allocated key string */
    void *value;                  /* Opaque pointer to value */
    uint32_t hash;                /* Cached hash value (optimization) */
    struct hash_entry *next;      /* Next entry in collision chain */
} hash_entry_t;

/**
 * hash_table_t - Main hash table structure
 * 
 * Represents a complete hash table with metadata.
 * Multiple tables can coexist independently.
 */
typedef struct hash_table {
    char *name;                   /* Table name (for debugging/logging) */
    size_t size;                  /* Number of buckets (power of 2) */
    size_t count;                 /* Current number of entries */
    hash_entry_t **buckets;       /* Array of bucket head pointers */
    int case_sensitive;           /* 0 = ignore case, 1 = case sensitive */
    void (*value_destructor)(void *); /* Optional value cleanup function */
    struct hash_table *next;      /* For global registry (optional) */
} hash_table_t;

/**
 * hash_stats_t - Statistics for hash table analysis
 * 
 * Used for performance monitoring and tuning.
 */
typedef struct hash_stats {
    size_t entries;               /* Total entries */
    size_t buckets_used;          /* Buckets with at least one entry */
    size_t buckets_total;         /* Total buckets */
    size_t max_chain_length;      /* Longest collision chain */
    double avg_chain_length;      /* Average chain length (non-empty) */
    double load_factor;           /* entries / buckets_total */
} hash_stats_t;

/* ============================================================================
 * CORE FUNCTIONS
 * ============================================================================ */

/**
 * hash_create - Create a new hash table
 * 
 * @param name Table name (for debugging, copied internally)
 * @param size Number of buckets (should be power of 2)
 * @param case_sensitive 0 for case-insensitive, 1 for case-sensitive
 * @param value_destructor Optional function to free values on remove/destroy
 * @return New hash table or NULL on error
 * 
 * MEMORY: Caller must call hash_destroy() when done
 * SECURITY: Uses SAFE_MALLOC for all allocations
 */
hash_table_t *hash_create(const char *name, size_t size, int case_sensitive,
                          void (*value_destructor)(void *));

/**
 * hash_destroy - Destroy hash table and free all memory
 * 
 * @param table Table to destroy
 * 
 * MEMORY: Frees all keys. Calls value_destructor on all values if set.
 * SECURITY: NULL-safe, uses SMART_FREE
 */
void hash_destroy(hash_table_t *table);

/**
 * hash_insert - Insert or update key-value pair
 * 
 * @param table Target hash table
 * @param key Key string (will be copied)
 * @param value Value pointer (caller retains ownership unless destructor set)
 * @return 1 on success, 0 on error
 * 
 * BEHAVIOR: If key exists, updates value (calls destructor on old value)
 * MEMORY: Makes internal copy of key
 * SECURITY: Validates inputs, bounds checks
 */
int hash_insert(hash_table_t *table, const char *key, void *value);

/**
 * hash_lookup - Find value by key
 * 
 * @param table Hash table to search
 * @param key Key to find
 * @return Value pointer or NULL if not found
 * 
 * PERFORMANCE: O(1) average case, O(n) worst case where n = chain length
 * SECURITY: NULL-safe, validates inputs
 */
void *hash_lookup(const hash_table_t *table, const char *key);

/**
 * hash_remove - Remove entry by key
 * 
 * @param table Hash table
 * @param key Key to remove
 * @return 1 if removed, 0 if not found
 * 
 * MEMORY: Frees key. Calls value_destructor if set.
 * SECURITY: NULL-safe, validates inputs
 */
int hash_remove(hash_table_t *table, const char *key);

/**
 * hash_exists - Check if key exists
 * 
 * @param table Hash table
 * @param key Key to check
 * @return 1 if exists, 0 if not
 * 
 * PERFORMANCE: Same as hash_lookup but returns boolean
 */
int hash_exists(const hash_table_t *table, const char *key);

/**
 * hash_clear - Remove all entries
 * 
 * @param table Hash table to clear
 * 
 * MEMORY: Frees all keys and calls value_destructor on all values
 * SECURITY: Maintains table structure, just empties it
 */
void hash_clear(hash_table_t *table);

/* ============================================================================
 * ITERATION FUNCTIONS
 * ============================================================================ */

/**
 * hash_iterator_t - Iterator state for traversing hash table
 */
typedef struct hash_iterator {
    const hash_table_t *table;
    size_t bucket_index;
    hash_entry_t *current;
} hash_iterator_t;

/**
 * hash_iterate_init - Initialize iterator
 * 
 * @param table Table to iterate
 * @param iter Iterator structure to initialize
 */
void hash_iterate_init(const hash_table_t *table, hash_iterator_t *iter);

/**
 * hash_iterate_next - Get next entry
 * 
 * @param iter Iterator
 * @param key Pointer to receive key (optional, can be NULL)
 * @param value Pointer to receive value (optional, can be NULL)
 * @return 1 if entry returned, 0 if done
 * 
 * USAGE:
 * ```c
 * hash_iterator_t iter;
 * char *key;
 * void *value;
 * hash_iterate_init(table, &iter);
 * while (hash_iterate_next(&iter, &key, &value)) {
 *     // Use key and value
 * }
 * ```
 */
int hash_iterate_next(hash_iterator_t *iter, char **key, void **value);

/* ============================================================================
 * STATISTICS AND DEBUGGING
 * ============================================================================ */

/**
 * hash_get_stats - Get hash table statistics
 * 
 * @param table Hash table to analyze
 * @param stats Structure to fill with statistics
 * 
 * USAGE: For performance tuning and debugging
 */
void hash_get_stats(const hash_table_t *table, hash_stats_t *stats);

/**
 * hash_dump - Dump hash table contents (for debugging)
 * 
 * @param table Hash table to dump
 * @param player Player to send output to (for notify())
 * 
 * USAGE: Debug command implementation
 */
void hash_dump(const hash_table_t *table, dbref player);

/* ============================================================================
 * HASH FUNCTION (EXPOSED FOR TESTING)
 * ============================================================================ */

/**
 * hash_fnv1a - Compute FNV-1a hash of string
 * 
 * @param str String to hash
 * @param case_sensitive 0 to convert to lowercase, 1 for exact case
 * @return 32-bit FNV-1a hash value
 * 
 * EXPOSED: For testing and special cases
 * SECURITY: NULL-safe
 */
uint32_t hash_fnv1a(const char *str, int case_sensitive);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * hash_suggest_size - Suggest optimal hash table size
 * 
 * @param expected_entries Expected number of entries
 * @return Suggested size (power of 2) for good performance
 * 
 * HEURISTIC: Targets load factor around 0.75
 */
size_t hash_suggest_size(size_t expected_entries);

/**
 * hash_is_power_of_2 - Check if size is power of 2
 * 
 * @param size Size to check
 * @return 1 if power of 2, 0 otherwise
 * 
 * USAGE: Validation in hash_create
 */
int hash_is_power_of_2(size_t size);

#endif /* HASH_TABLE_H */
