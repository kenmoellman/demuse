/* mariadb_help.h - MariaDB-backed help topic operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the online help system.
 * Help topics are stored in MariaDB with a two-level hierarchy:
 *   command    - The main command (e.g., "+channel", "look", "@create")
 *   subcommand - Optional subcommand (e.g., "join", "leave") or "" for overview
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * Stubs return failure values when MariaDB is not available.
 */

#ifndef _MARIADB_HELP_H_
#define _MARIADB_HELP_H_

#include "config.h"

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/*
 * mariadb_help_init - Create the help_topics table if it doesn't exist
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_help_init(void);

/*
 * mariadb_help_get - Look up a help topic
 *
 * PARAMETERS:
 *   command    - Main command (e.g., "+channel")
 *   subcommand - Subcommand (e.g., "join") or "" for overview
 *   body_out   - Output: body text (caller must SMART_FREE)
 *
 * RETURNS: 1 if found, 0 if not found or error
 */
int mariadb_help_get(const char *command, const char *subcommand,
                     char **body_out);

/*
 * mariadb_help_set - Insert or update a help topic
 *
 * PARAMETERS:
 *   command    - Main command
 *   subcommand - Subcommand or "" for overview
 *   body       - Help text body
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_help_set(const char *command, const char *subcommand,
                     const char *body);

/*
 * mariadb_help_delete - Remove a help topic
 *
 * PARAMETERS:
 *   command    - Main command
 *   subcommand - Subcommand or "" for overview
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_help_delete(const char *command, const char *subcommand);

/*
 * mariadb_help_list_subcommands - Get list of subcommands for a command
 *
 * Returns a comma-separated string of subcommand names for commands that
 * have entries with non-empty subcommand fields.
 *
 * PARAMETERS:
 *   command  - Main command to list subcommands for
 *   list_out - Output: comma-separated list (caller must SMART_FREE)
 *   count    - Output: number of subcommands found
 *
 * RETURNS: 1 on success, 0 on failure or no subcommands
 */
int mariadb_help_list_subcommands(const char *command, char **list_out,
                                  int *count);

/*
 * mariadb_help_search - Search help topics by pattern
 *
 * Searches both command and subcommand fields for partial matches.
 *
 * PARAMETERS:
 *   pattern  - Search pattern (substring match)
 *   list_out - Output: formatted list of matches (caller must SMART_FREE)
 *   count    - Output: number of matches found
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_help_search(const char *pattern, char **list_out, int *count);

/*
 * mariadb_help_count - Count total help topics
 *
 * RETURNS: Total number of help topics, or -1 on error
 */
long mariadb_help_count(void);

/*
 * mariadb_help_import_legacy - Import help topics from flat text file
 *
 * Parses the old-format helptext file (& topic_name markers) and inserts
 * each topic into the database. All topics go in as command=topic_name,
 * subcommand="". This is a one-time migration function.
 *
 * PARAMETERS:
 *   filename - Path to the legacy helptext file
 *
 * RETURNS: Number of topics imported, or -1 on error
 */
int mariadb_help_import_legacy(const char *filename);

#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================ */

static inline int mariadb_help_init(void) { return 0; }
static inline int mariadb_help_get(const char *c __attribute__((unused)),
    const char *s __attribute__((unused)),
    char **b __attribute__((unused))) { return 0; }
static inline int mariadb_help_set(const char *c __attribute__((unused)),
    const char *s __attribute__((unused)),
    const char *b __attribute__((unused))) { return 0; }
static inline int mariadb_help_delete(const char *c __attribute__((unused)),
    const char *s __attribute__((unused))) { return 0; }
static inline int mariadb_help_list_subcommands(const char *c __attribute__((unused)),
    char **l __attribute__((unused)),
    int *n __attribute__((unused))) { return 0; }
static inline int mariadb_help_search(const char *p __attribute__((unused)),
    char **l __attribute__((unused)),
    int *n __attribute__((unused))) { return 0; }
static inline long mariadb_help_count(void) { return -1; }
static inline int mariadb_help_import_legacy(const char *f __attribute__((unused)))
    { return -1; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_HELP_H_ */
