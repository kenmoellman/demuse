/* parser.h - Multi-Parser Command Dispatch System
 *
 * ============================================================================
 * PARSER AND UNIVERSE SYSTEM (2025)
 * ============================================================================
 *
 * This system provides a flexible command dispatch architecture that separates:
 *
 * 1. PARSER - The command interpretation system (syntax, commands, functions)
 *    Examples: deMUSE, TinyMUSH3, TinyMUD, TinyMAZE
 *    A parser defines WHAT commands exist and HOW they're interpreted
 *
 * 2. UNIVERSE - A world instance using a specific parser
 *    Examples: "Fantasy Realm" (uses deMUSE), "Cyberpunk City" (uses deMUSE)
 *             "Classic MUD" (uses TinyMUD)
 *    A universe defines WHERE players are and WHAT rules apply
 *
 * RELATIONSHIP:
 * - One parser can be used by many universes (many-to-one)
 * - Each universe uses exactly one parser
 * - Multiple universes can share the same parser but have different rules
 *
 * ARCHITECTURE:
 * - Parsers use hash tables for O(1) command lookup
 * - Commands call standardized wrapper functions
 * - Wrappers call existing do_*() core implementation functions
 * - No changes to existing function signatures
 *
 * USAGE:
 * ```c
 * // At startup:
 * init_parsers();      // Create parser definitions
 * init_universes();    // Create universe instances
 *
 * // At runtime:
 * universe_t *u = get_universe(player);
 * parser_t *p = u->parser;
 * cmd = hash_lookup(p->commands, cmd_word);
 * if (cmd) cmd->handler(player, arg1, arg2);
 * ```
 */

#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "db.h"
#include "hash_table.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define MAX_PARSERS 16      /* Maximum number of parser types */
#define MAX_UNIVERSES 64    /* Maximum number of universe instances */

/* Default parser IDs */
#define PARSER_DEMUSE 0     /* Standard deMUSE parser */

/* Default universe IDs */
#define UNIVERSE_DEFAULT 0  /* Default universe (standard deMUSE) */

/* ============================================================================
 * COMMAND HANDLER TYPE
 * ============================================================================ */

/**
 * cmd_handler_t - Function signature for command handlers
 *
 * All command wrappers must match this signature for the dispatch system.
 * Wrappers adapt this standardized signature to call existing do_*() functions.
 *
 * @param player The player executing the command
 * @param arg1   First argument (before =, evaluated)
 * @param arg2   Second argument (after =, evaluated)
 */
typedef void (*cmd_handler_t)(dbref player, char *arg1, char *arg2);

/* ============================================================================
 * COMMAND ENTRY STRUCTURE
 * ============================================================================ */

/**
 * command_entry_t - Single command definition
 *
 * Describes one command: name, handler, restrictions, abbreviation rules.
 * These are stored in parser command hash tables.
 */
typedef struct command_entry {
    const char *name;           /* Command name (e.g., "look", "@create") */
    cmd_handler_t handler;      /* Function to call when command executed */
    int min_length;             /* Minimum abbreviation length (0 = exact only) */

    /* Permission flags */
    unsigned int requires_direct:1;   /* Must be directly executed (not @forced) */
    unsigned int requires_wizard:1;   /* Requires wizard power */
    unsigned int slave_allowed:1;     /* Can slave players use this */
    unsigned int zone_restricted:1;   /* Check zone restrictions */

} command_entry_t;

/* ============================================================================
 * PARSER STRUCTURE
 * ============================================================================ */

/**
 * parser_t - Command interpretation system definition
 *
 * A parser defines a complete command syntax and behavior model.
 * Multiple universes can share the same parser.
 */
typedef struct parser {
    const char *name;           /* Parser name (e.g., "deMUSE", "TinyMUSH3") */
    const char *version;        /* Parser version (e.g., "2025", "3.0") */
    const char *description;    /* Human-readable description */

    /* Command system */
    hash_table_t *commands;     /* Command dispatch hash table */
    int command_count;          /* Number of registered commands */

    /* Function system (optional - for future use) */
    hash_table_t *functions;    /* Function evaluation table (NULL if using global) */

    /* Syntax configuration */
    struct {
        char say_token;         /* Token for say command ('"' for deMUSE) */
        char pose_token;        /* Token for pose command (':') */
        char semipose_token;    /* Token for semipose (';') */
        char page_token;        /* Token for page command ('p' or 0 if none) */
        char think_token;       /* Token for think command ('#') */
        int case_sensitive;     /* Case-sensitive command matching */
        int allow_abbreviations; /* Allow command abbreviations */
    } syntax;

    /* Parser limits */
    struct {
        int max_recursion;      /* Function recursion limit */
        int max_command_length; /* Maximum command string length */
        int max_function_invocations; /* Max functions per command */
    } limits;

} parser_t;

/* ============================================================================
 * UNIVERSE STRUCTURE
 * ============================================================================ */

/**
 * universe_t - World instance using a parser
 *
 * A universe is an instance of a game world using a specific parser.
 * Multiple universes can use the same parser but have different rules.
 */
typedef struct universe {
    int id;                     /* Universe ID (index in universes array) */
    const char *name;           /* Universe name (e.g., "Fantasy Realm") */
    const char *description;    /* Player-visible description */

    parser_t *parser;           /* Which parser this universe uses */
    dbref db_object;            /* Config object in DB (or NOTHING) */

    /* Universe-specific configuration */
    struct {
        int allow_combat;       /* Combat allowed in this universe */
        int allow_building;     /* Building allowed */
        int allow_teleport;     /* Teleportation allowed */
        int max_objects_per_player; /* Object creation limit */
        dbref starting_location; /* Where new players start */
        dbref default_zone;     /* Default zone for this universe */
    } config;

    /* Statistics */
    int player_count;           /* Current players in this universe */
    time_t created;             /* When universe was created */

} universe_t;

/* ============================================================================
 * GLOBAL TABLES
 * ============================================================================ */

/* Defined in parser.c */
extern parser_t parsers[MAX_PARSERS];
extern universe_t universes[MAX_UNIVERSES];
extern int num_parsers;
extern int num_universes;

/* ============================================================================
 * INITIALIZATION FUNCTIONS
 * ============================================================================ */

/**
 * init_parsers - Initialize parser definitions
 *
 * Creates parser structures and registers all commands for each parser type.
 * Called once at server startup after database is loaded.
 *
 * MEMORY: Allocates hash tables for command dispatch
 * TIMING: Called from init_game() or server startup
 */
void init_parsers(void);

/**
 * init_universes - Initialize universe instances
 *
 * Creates universe instances and associates them with parsers.
 * Called once at server startup after init_parsers().
 *
 * DEPENDENCY: Must be called after init_parsers()
 * TIMING: Called from init_game() or server startup
 */
void init_universes(void);

/**
 * shutdown_parsers - Clean up parser system
 *
 * Destroys hash tables and frees parser resources.
 * Called at server shutdown.
 */
void shutdown_parsers(void);

/* ============================================================================
 * LOOKUP FUNCTIONS
 * ============================================================================ */

/**
 * get_parser - Get parser by ID
 *
 * @param parser_id Parser ID (0 = deMUSE, etc.)
 * @return Pointer to parser or NULL if invalid
 */
parser_t *get_parser(int parser_id);

/**
 * get_parser_by_name - Get parser by name
 *
 * @param name Parser name (case-insensitive)
 * @return Pointer to parser or NULL if not found
 */
parser_t *get_parser_by_name(const char *name);

/**
 * get_universe - Get universe by ID
 *
 * @param universe_id Universe ID
 * @return Pointer to universe or default universe if invalid
 */
universe_t *get_universe(int universe_id);

/**
 * get_universe_by_name - Get universe by name
 *
 * @param name Universe name (case-insensitive)
 * @return Pointer to universe or NULL if not found
 */
universe_t *get_universe_by_name(const char *name);

/**
 * get_player_universe - Get the universe a player is in
 *
 * Checks player's universe attribute/field.
 * Falls back to default universe if not set.
 *
 * @param player Player dbref
 * @return Universe ID (always valid)
 *
 * SECURITY: Validates player dbref
 */
int get_player_universe(dbref player);

/**
 * set_player_universe - Set which universe a player is in
 *
 * @param player Player dbref
 * @param universe_id Universe ID to assign
 * @return 1 on success, 0 on failure
 *
 * SECURITY: Validates both player and universe_id
 */
int set_player_universe(dbref player, int universe_id);

/* ============================================================================
 * COMMAND LOOKUP FUNCTIONS
 * ============================================================================ */

/**
 * find_command - Find command in parser
 *
 * Looks up command by name with abbreviation support.
 * Tries exact match first (O(1)), then prefix match if needed.
 *
 * @param parser Parser to search
 * @param cmdstr Command string (may be abbreviated)
 * @return Pointer to command entry or NULL if not found
 *
 * PERFORMANCE: O(1) for exact match, O(n) for abbreviations
 */
const command_entry_t *find_command(parser_t *parser, const char *cmdstr);

/**
 * register_command - Register a command in a parser
 *
 * Adds a command to the parser's command hash table.
 * Used during parser initialization.
 *
 * @param parser Parser to add command to
 * @param cmd Command entry to register
 * @return 1 on success, 0 on failure
 *
 * MEMORY: Makes copy of command entry
 */
int register_command(parser_t *parser, const command_entry_t *cmd);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * list_parsers - List all registered parsers
 *
 * @param player Player to send output to
 *
 * USAGE: For @list parsers or similar admin command
 */
void list_parsers(dbref player);

/**
 * list_universes - List all universes
 *
 * @param player Player to send output to
 *
 * USAGE: For @list universes or similar command
 */
void list_universes(dbref player);

/**
 * parser_stats - Show statistics for a parser
 *
 * @param player Player to send output to
 * @param parser Parser to analyze
 *
 * USAGE: For debugging and performance analysis
 */
void parser_stats(dbref player, parser_t *parser);

#endif /* PARSER_H */
