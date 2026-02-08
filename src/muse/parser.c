/* parser.c - Multi-Parser Command Dispatch System Implementation
 *
 * ============================================================================
 * PARSER AND UNIVERSE SYSTEM (2025)
 * ============================================================================
 *
 * This implements the flexible command dispatch system that supports:
 * - Multiple parser types (deMUSE, TinyMUSH3, TinyMUD, etc.)
 * - Multiple universe instances sharing parsers
 * - Hash table-based O(1) command lookup
 * - Standardized command wrappers calling existing do_*() functions
 *
 * INITIALIZATION SEQUENCE:
 * 1. init_parsers() - Create parser definitions, register commands
 * 2. init_universes() - Create universe instances, link to parsers
 *
 * RUNTIME DISPATCH:
 * 1. Get player's universe -> get universe's parser
 * 2. Look up command in parser's hash table
 * 3. Execute command handler wrapper
 * 4. Wrapper calls existing do_*() core function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "parser.h"
#include "hash_table.h"
#include "interface.h"

/* ============================================================================
 * ARGUMENT PACKING CONSTANTS AND PROTOTYPES
 * ============================================================================
 * These are used by deferred command wrappers that need to pack/unpack
 * argv[] and cause parameters through the standard handler signature.
 * ============================================================================ */

#ifdef ARG_DELIMITER
#undef ARG_DELIMITER  /* Undefine config.h version which uses '=' */
#endif
#define ARG_DELIMITER '\x1F'  /* ASCII Unit Separator for argument packing */

#define MAX_PACKED_ARGS 10    /* Maximum argv elements we can pack */
#define MAX_COMMAND_BUFFER 1024  /* Buffer size for command processing */

/* Forward declaration of unpack_argv - defined later in file */
static int unpack_argv(char *packed, dbref *cause_out, char **argv_out);

/* ============================================================================
 * GLOBAL TABLES
 * ============================================================================ */

/* Parser definitions */
parser_t parsers[MAX_PARSERS];
int num_parsers = 0;

/* Universe instances */
universe_t universes[MAX_UNIVERSES];
int num_universes = 0;

/* ============================================================================
 * COMMAND WRAPPERS
 * ============================================================================
 *
 * These wrappers adapt existing do_*() functions to the standardized
 * cmd_handler_t signature: (dbref player, char *arg1, char *arg2)
 *
 * DESIGN:
 * - Thin wrappers, minimal logic
 * - Call existing core implementation functions
 * - No changes to existing function signatures
 * - Can be collapsed later if desired
 * ============================================================================ */

/**
 * cmd_look - Wrapper for look command
 *
 * Core: do_look_at(dbref player, char *arg1)
 */
static void cmd_look(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused - look doesn't take second arg */
    do_look_at(player, arg1);
}

/**
 * cmd_say - Wrapper for say command
 *
 * Core: do_say(dbref player, char *arg1, char *arg2)
 * Already matches signature - but wrap for consistency
 */
static void cmd_say(dbref player, char *arg1, char *arg2)
{
    do_say(player, arg1, arg2);
}

/**
 * cmd_pose - Wrapper for pose command
 *
 * Core: do_pose(dbref player, char *arg1, char *arg2, int possessive)
 */
static void cmd_pose(dbref player, char *arg1, char *arg2)
{
    do_pose(player, arg1, arg2, 0);  /* possessive = 0 for normal pose */
}

/**
 * cmd_semipose - Wrapper for semipose command (possessive pose)
 *
 * Core: do_pose(dbref player, char *arg1, char *arg2, int possessive)
 */
static void cmd_semipose(dbref player, char *arg1, char *arg2)
{
    do_pose(player, arg1, arg2, 1);  /* possessive = 1 for semipose */
}

/**
 * cmd_page - Wrapper for page command
 *
 * Core: do_page(dbref player, char *arg1, char *arg2)
 */
static void cmd_page(dbref player, char *arg1, char *arg2)
{
    do_page(player, arg1, arg2);
}

/**
 * cmd_whisper - Wrapper for whisper command
 *
 * Core: do_whisper(dbref player, char *arg1, char *arg2)
 */
static void cmd_whisper(dbref player, char *arg1, char *arg2)
{
    do_whisper(player, arg1, arg2);
}

/**
 * cmd_think - Wrapper for think command
 *
 * Core: do_think(dbref player, char *arg1, char *arg2)
 */
static void cmd_think(dbref player, char *arg1, char *arg2)
{
    do_think(player, arg1, arg2);
}

/**
 * cmd_examine - Wrapper for examine command
 *
 * Core: do_examine(dbref player, char *arg1, char *arg2)
 */
static void cmd_examine(dbref player, char *arg1, char *arg2)
{
    do_examine(player, arg1, arg2);
}

/**
 * cmd_inventory - Wrapper for inventory command
 *
 * Core: do_inventory(dbref player)
 */
static void cmd_inventory(dbref player, char *arg1, char *arg2)
{
    (void)arg1;  /* Unused */
    (void)arg2;  /* Unused */
    do_inventory(player);
}

/**
 * cmd_score - Wrapper for score command
 *
 * Core: do_score(dbref player)
 */
static void cmd_score(dbref player, char *arg1, char *arg2)
{
    (void)arg1;  /* Unused */
    (void)arg2;  /* Unused */
    do_score(player);
}

/**
 * cmd_get - Wrapper for get command
 *
 * Core: do_get(dbref player, char *arg1)
 */
static void cmd_get(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_get(player, arg1);
}

/**
 * cmd_drop - Wrapper for drop command
 *
 * Core: do_drop(dbref player, char *arg1)
 */
static void cmd_drop(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_drop(player, arg1);
}

/**
 * cmd_enter - Wrapper for enter command
 *
 * Core: do_enter(dbref player, char *arg1)
 */
static void cmd_enter(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_enter(player, arg1);
}

/**
 * cmd_leave - Wrapper for leave command
 *
 * Core: do_leave(dbref player)
 */
static void cmd_leave(dbref player, char *arg1, char *arg2)
{
    (void)arg1;  /* Unused */
    (void)arg2;  /* Unused */
    do_leave(player);
}

/**
 * cmd_give - Wrapper for give command
 *
 * Core: do_give(dbref player, char *arg1, char *arg2)
 */
static void cmd_give(dbref player, char *arg1, char *arg2)
{
    do_give(player, arg1, arg2);
}

/**
 * cmd_use - Wrapper for use command
 *
 * Core: do_use(dbref player, char *arg1)
 */
static void cmd_use(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_use(player, arg1);
}

/**
 * cmd_create - Wrapper for @create command
 *
 * Core: do_create(dbref player, char *name, int cost)
 */
static void cmd_create(dbref player, char *arg1, char *arg2)
{
    int cost = 0;

    if (arg2 && *arg2) {
        cost = (int)atol(arg2);
    }

    do_create(player, arg1, cost);
}

/**
 * cmd_describe - Wrapper for @describe command
 *
 * Core: do_describe(dbref player, char *arg1, char *arg2)
 */
static void cmd_describe(dbref player, char *arg1, char *arg2)
{
    do_describe(player, arg1, arg2);
}

/**
 * cmd_name - Wrapper for @name command
 *
 * Core: do_name(dbref player, char *arg1, char *arg2, int is_direct)
 * Note: is_direct will be set by process_command dispatcher
 */
static void cmd_name(dbref player, char *arg1, char *arg2)
{
    /* For now, always pass 1 for is_direct since hash dispatch is direct */
    do_name(player, arg1, arg2, 1);
}

/**
 * cmd_chown - Wrapper for @chown command
 *
 * Core: do_chown(dbref player, char *arg1, char *arg2)
 */
static void cmd_chown(dbref player, char *arg1, char *arg2)
{
    do_chown(player, arg1, arg2);
}

/**
 * cmd_set - Wrapper for @set command
 *
 * Core: do_set(dbref player, char *arg1, char *arg2, int is_cset)
 */
static void cmd_set(dbref player, char *arg1, char *arg2)
{
    do_set(player, arg1, arg2, 0);  /* is_cset = 0 for normal @set */
}

/**
 * cmd_destroy - Wrapper for @destroy command
 *
 * Core: do_destroy(dbref player, char *arg1)
 */
static void cmd_destroy(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_destroy(player, arg1);
}

/**
 * cmd_poof - Wrapper for @poof command
 *
 * Core: do_poof(dbref player, char *name)
 */
static void cmd_poof(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_poof(player, arg1);
}

/**
 * cmd_teleport - Wrapper for @teleport command
 *
 * Core: do_teleport(dbref player, char *arg1, char *arg2)
 */
static void cmd_teleport(dbref player, char *arg1, char *arg2)
{
    do_teleport(player, arg1, arg2);
}

/**
 * cmd_find - Wrapper for @find command
 *
 * Core: do_find(dbref player, char *arg1)
 */
static void cmd_find(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_find(player, arg1);
}

/**
 * cmd_stats - Wrapper for @stats command
 *
 * Core: do_stats(dbref player, char *arg1)
 */
static void cmd_stats(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_stats(player, arg1);
}

/**
 * cmd_version - Wrapper for @version/+version command
 *
 * Core: do_version(dbref player)
 */
static void cmd_version(dbref player, char *arg1, char *arg2)
{
    (void)arg1;  /* Unused */
    (void)arg2;  /* Unused */
    do_version(player);
}

/**
 * cmd_uptime - Wrapper for +uptime command
 *
 * Core: do_uptime(dbref player)
 */
static void cmd_uptime(dbref player, char *arg1, char *arg2)
{
    (void)arg1;  /* Unused */
    (void)arg2;  /* Unused */
    do_uptime(player);
}

/**
 * cmd_away - Wrapper for +away command
 *
 * Core: do_away(dbref player, char *arg1)
 */
static void cmd_away(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_away(player, arg1);
}

/**
 * cmd_haven - Wrapper for +haven command
 *
 * Core: do_haven(dbref player, char *arg1)
 */
static void cmd_haven(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_haven(player, arg1);
}

/**
 * cmd_clone - Wrapper for @clone command
 *
 * Core: do_clone(dbref player, char *arg1, char *arg2)
 */
static void cmd_clone(dbref player, char *arg1, char *arg2)
{
    do_clone(player, arg1, arg2);
}

/**
 * cmd_link - Wrapper for @link command
 *
 * Core: do_link(dbref player, char *arg1, char *arg2)
 */
static void cmd_link(dbref player, char *arg1, char *arg2)
{
    do_link(player, arg1, arg2);
}

/**
 * cmd_unlink - Wrapper for @unlink command
 *
 * Core: do_unlink(dbref player, char *arg1)
 */
static void cmd_unlink(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_unlink(player, arg1);
}

/**
 * cmd_unlock - Wrapper for @unlock command
 *
 * Core: do_unlock(dbref player, char *arg1)
 */
static void cmd_unlock(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_unlock(player, arg1);
}

/**
 * cmd_emit - Wrapper for @emit command
 *
 * Core: do_emit(dbref player, char *arg1, char *arg2, int noeval)
 */
static void cmd_emit(dbref player, char *arg1, char *arg2)
{
    do_emit(player, arg1, arg2, 0);  /* noeval = 0 */
}

/**
 * cmd_pemit - Wrapper for @pemit command
 *
 * Core: do_emit(dbref player, char *arg1, char *arg2, int noeval)
 * @pemit is an alias for @emit
 */
static void cmd_pemit(dbref player, char *arg1, char *arg2)
{
    do_emit(player, arg1, arg2, 0);  /* noeval = 0 */
}

/**
 * cmd_force - Wrapper for @force command
 *
 * Core: do_force(dbref player, char *arg1, char *arg2)
 */
static void cmd_force(dbref player, char *arg1, char *arg2)
{
    do_force(player, arg1, arg2);
}

/**
 * cmd_halt - Wrapper for @halt command
 *
 * Core: do_halt(dbref player, char *arg1, char *arg2)
 */
static void cmd_halt(dbref player, char *arg1, char *arg2)
{
    do_halt(player, arg1, arg2);
}

/**
 * cmd_sweep - Wrapper for @sweep command
 *
 * Core: do_sweep(dbref player, char *arg1)
 */
static void cmd_sweep(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_sweep(player, arg1);
}

/**
 * cmd_whereis - Wrapper for @whereis command
 *
 * Core: do_whereis(dbref player, char *arg1)
 */
static void cmd_whereis(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_whereis(player, arg1);
}

/**
 * cmd_password - Wrapper for @password command
 *
 * Core: do_password(dbref player, const char *arg1, const char *arg2)
 */
static void cmd_password(dbref player, char *arg1, char *arg2)
{
    do_password(player, (const char *)arg1, (const char *)arg2);
}

/**
 * cmd_boot - Wrapper for @boot command
 *
 * Core: do_boot(dbref player, char *arg1, char *arg2)
 */
static void cmd_boot(dbref player, char *arg1, char *arg2)
{
    do_boot(player, arg1, arg2);
}

/**
 * cmd_idle - Wrapper for +idle command
 *
 * Core: do_idle(dbref player, char *arg1)
 */
static void cmd_idle(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_idle(player, arg1);
}

/**
 * cmd_laston - Wrapper for +laston command
 *
 * Core: do_laston(dbref player, char *arg1)
 */
static void cmd_laston(dbref player, char *arg1, char *arg2)
{
    (void)arg2;  /* Unused */
    do_laston(player, arg1);
}

/**
 * cmd_mail - Wrapper for +mail command
 *
 * Core: do_mail(dbref player, char *arg1, char *arg2)
 */
static void cmd_mail(dbref player, char *arg1, char *arg2)
{
    do_mail(player, arg1, arg2);
}

/**
 * cmd_board - Wrapper for +board command
 *
 * Core: do_board(dbref player, char *arg1, char *arg2)
 */
static void cmd_board(dbref player, char *arg1, char *arg2)
{
    do_board(player, arg1, arg2);
}

/**
 * cmd_com - Wrapper for +com command (channel management)
 *
 * Core: do_com(dbref player, char *arg1, char *arg2)
 */
static void cmd_com(dbref player, char *arg1, char *arg2)
{
    do_com(player, arg1, arg2);
}

/**
 * cmd_channel - Wrapper for +channel command
 *
 * Core: do_channel(dbref player, char *arg1, char *arg2)
 */
static void cmd_channel(dbref player, char *arg1, char *arg2)
{
    do_channel(player, arg1, arg2);
}

/**
 * cmd_who - Wrapper for WHO command
 *
 * Core: dump_users(dbref player, char *arg1, char *arg2, struct descriptor_data *d)
 */
static void cmd_who(dbref player, char *arg1, char *arg2)
{
    dump_users(player, arg1, arg2, NULL);
}

/* ============================================================================
 * PHASE 4 COMMAND WRAPPERS (123 COMMANDS)
 * ============================================================================
 *
 * These wrappers were generated from the remaining commands in the switch
 * statement in process_command(). They complete the migration to hash dispatch.
 * ============================================================================ */
/* ========================================================================== */
/*                           @ COMMAND WRAPPERS                              */
/* ========================================================================== */

/**
 * cmd_addparent - Wrapper for @addparent command
 *
 * Core: do_addparent(player, arg1, arg2)
 */
static void cmd_addparent(dbref player, char *arg1, char *arg2)
{
    do_addparent(player, arg1, arg2);
}

/**
 * cmd_allquota - Wrapper for @allquota command
 *
 * Core: do_allquota(player, arg1)
 * Note: Requires direct command (not triggered)
 */
static void cmd_allquota(dbref player, char *arg1, char *arg2)
{
    (void)arg2; /* TODO: Add requires_direct flag */
    do_allquota(player, arg1);
}

/**
 * cmd_announce - Wrapper for @announce command
 *
 * Core: do_announce(player, arg1, arg2)
 */
static void cmd_announce(dbref player, char *arg1, char *arg2)
{
    do_announce(player, arg1, arg2);
}

/**
 * cmd_as - Wrapper for @as command
 *
 * Core: do_as(player, arg1, arg2)
 */
static void cmd_as(dbref player, char *arg1, char *arg2)
{
    do_as(player, arg1, arg2);
}

/**
 * cmd_at - Wrapper for @at command
 *
 * Core: do_at(player, arg1, arg2)
 */
static void cmd_at(dbref player, char *arg1, char *arg2)
{
    do_at(player, arg1, arg2);
}

/**
 * cmd_broadcast - Wrapper for @broadcast command
 *
 * Core: do_broadcast(player, arg1, arg2)
 */
static void cmd_broadcast(dbref player, char *arg1, char *arg2)
{
    do_broadcast(player, arg1, arg2);
}

/**
 * cmd_cboot - Wrapper for @cboot command
 *
 * Core: do_cboot(player, arg1)
 */
static void cmd_cboot(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_cboot(player, arg1);
}

/**
 * cmd_cemit - Wrapper for @cemit command
 *
 * Core: do_cemit(player, arg1, arg2)
 */
static void cmd_cemit(dbref player, char *arg1, char *arg2)
{
    do_cemit(player, arg1, arg2);
}

/**
 * cmd_check - Wrapper for @check command
 *
 * Core: do_check(player, arg1)
 */
static void cmd_check(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_check(player, arg1);
}

/**
 * cmd_chemit - Wrapper for @chemit command
 *
 * Core: do_chemit(player, arg1, arg2)
 */
static void cmd_chemit(dbref player, char *arg1, char *arg2)
{
    do_chemit(player, arg1, arg2);
}

/**
 * cmd_class - Wrapper for @class command
 *
 * Core: do_class(player, arg1, arg2)
 */
static void cmd_class(dbref player, char *arg1, char *arg2)
{
    do_class(player, arg1, arg2);
}

/**
 * cmd_cname - Wrapper for @cname command
 *
 * Core: do_cname(player, arg1, arg2)
 */
static void cmd_cname(dbref player, char *arg1, char *arg2)
{
    do_cname(player, arg1, arg2);
}

/**
 * cmd_config - Wrapper for @config command
 *
 * Core: do_config(player, arg1, arg2)
 */
static void cmd_config(dbref player, char *arg1, char *arg2)
{
    do_config(player, arg1, arg2);
}

/**
 * cmd_cpaste - Wrapper for @cpaste command
 *
 * Core: do_paste(player, "channel", arg1)
 * Note: Includes deprecation warning
 */
static void cmd_cpaste(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    notify(player, "WARNING: @cpaste antiquated. Use '@paste channel=<channel>'");
    do_paste(player, "channel", arg1);
}

/**
 * cmd_cset - Wrapper for @cset command
 *
 * Core: do_set(player, arg1, arg2, 1)
 * Note: Fourth parameter is 1
 */
static void cmd_cset(dbref player, char *arg1, char *arg2)
{
    do_set(player, arg1, arg2, 1);
}

/**
 * cmd_ctrace - Wrapper for @ctrace command
 *
 * Core: do_ctrace(player)
 */
static void cmd_ctrace(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_ctrace(player);
}

/**
 * cmd_cycle - Wrapper for @cycle command
 *
 * Core: do_cycle(player, arg1, argv)
 * Unpacks argv from arg2 using argument packing system
 */
static void cmd_cycle(dbref player, char *arg1, char *arg2)
{
    char *argv_array[MAX_PACKED_ARGS];
    char arg2_copy[MAX_COMMAND_BUFFER];
    dbref cause_unused;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause_unused, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_cycle(dbref, char *, char **);
    do_cycle(player, arg1, argv_array);
}

/**
 * cmd_dbck - Wrapper for @dbck command
 *
 * Core: do_dbck(player)
 */
static void cmd_dbck(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_dbck(player);
}

/**
 * cmd_dbtop - Wrapper for @dbtop command
 *
 * Core: do_dbtop(player, arg1)
 */
static void cmd_dbtop(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_dbtop(player, arg1);
}

/**
 * cmd_decompile - Wrapper for @decompile command
 *
 * Core: do_decompile(player, arg1, arg2)
 */
static void cmd_decompile(dbref player, char *arg1, char *arg2)
{
    do_decompile(player, arg1, arg2);
}

/**
 * cmd_defattr - Wrapper for @defattr command
 *
 * Core: do_defattr(player, arg1, arg2)
 */
static void cmd_defattr(dbref player, char *arg1, char *arg2)
{
    do_defattr(player, arg1, arg2);
}

/**
 * cmd_delparent - Wrapper for @delparent command
 *
 * Core: do_delparent(player, arg1, arg2)
 */
static void cmd_delparent(dbref player, char *arg1, char *arg2)
{
    do_delparent(player, arg1, arg2);
}

/**
 * cmd_dig - Wrapper for @dig command
 *
 * Core: do_dig(player, arg1, argv)
 * Unpacks argv from arg2 using argument packing system
 */
static void cmd_dig(dbref player, char *arg1, char *arg2)
{
    char *argv_array[MAX_PACKED_ARGS];
    char arg2_copy[MAX_COMMAND_BUFFER];
    dbref cause_unused;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause_unused, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_dig(dbref, char *, char **);
    do_dig(player, arg1, argv_array);
}

/**
 * cmd_dump - Wrapper for @dump command
 *
 * Core: do_dump(dbref player)
 */
static void cmd_dump(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    extern void do_dump(dbref);  /* Not declared in externs.h */
    do_dump(player);
}

/**
 * cmd_echo - Wrapper for @echo command
 *
 * Core: do_echo(player, arg1, arg2, 0)
 * Note: Fourth parameter is 0
 */
static void cmd_echo(dbref player, char *arg1, char *arg2)
{
    do_echo(player, arg1, arg2, 0);
}

/**
 * cmd_edit - Wrapper for @edit command
 *
 * Core: do_edit(player, arg1, argv)
 * Unpacks argv from arg2 using argument packing system
 */
static void cmd_edit(dbref player, char *arg1, char *arg2)
{
    char *argv_array[MAX_PACKED_ARGS];
    char arg2_copy[MAX_COMMAND_BUFFER];
    dbref cause_unused;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause_unused, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_edit(dbref, char *, char **);
    do_edit(player, arg1, argv_array);
}

/**
 * cmd_empower - Wrapper for @empower command
 *
 * Core: do_empower(player, arg1, arg2)
 * Note: Requires direct command (not triggered)
 */
static void cmd_empower(dbref player, char *arg1, char *arg2)
{
    /* TODO: Add requires_direct flag */
    do_empower(player, arg1, arg2);
}

/**
 * cmd_exec - Wrapper for @exec command
 *
 * Core: do_exec(player, arg1, arg2)
 * Note: Only available if ALLOW_EXEC is defined
 */
static void cmd_exec(dbref player, char *arg1, char *arg2)
{
#ifdef ALLOW_EXEC
    do_exec(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "@exec is not enabled on this server.");
#endif
}

/**
 * cmd_foreach - Wrapper for @foreach command
 *
 * Core: do_foreach(player, arg1, arg2, cause)
 * Note: Requires cause parameter
 */
static void cmd_foreach(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause = NOTHING;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause, argv_array);
    }

    extern void do_foreach(dbref, char *, char *, dbref);
    do_foreach(player, arg1, argv_array[0] ? argv_array[0] : "", cause);
}

/**
 * cmd_giveto - Wrapper for @giveto command
 *
 * Core: do_giveto(player, arg1, arg2)
 */
static void cmd_giveto(dbref player, char *arg1, char *arg2)
{
    do_giveto(player, arg1, arg2);
}

/**
 * cmd_guniverse - Wrapper for @guniverse command
 *
 * Core: do_guniverse(player, arg1)
 * Note: Universe commands are always available
 */
static void cmd_guniverse(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_guniverse(player, arg1);
}

/**
 * cmd_gzone - Wrapper for @gzone command
 *
 * Core: do_gzone(player, arg1)
 */
static void cmd_gzone(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_gzone(player, arg1);
}

/**
 * cmd_hide - Wrapper for @hide command
 *
 * Core: do_hide(player)
 */
static void cmd_hide(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_hide(player);
}

/**
 * cmd_info - Wrapper for @info command
 *
 * Core: do_info(player, arg1)
 */
static void cmd_info(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_info(player, arg1);
}

/**
 * cmd_listarea - Wrapper for @listarea command
 *
 * Core: do_listarea(player, arg1)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_listarea(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    (void)arg2;
    do_listarea(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_lockout - Wrapper for @lockout command
 *
 * Core: do_lockout(player, arg1)
 * Note: Requires direct command (not triggered)
 */
static void cmd_lockout(dbref player, char *arg1, char *arg2)
{
    (void)arg2; /* TODO: Add requires_direct flag */
    do_lockout(player, arg1);
}

/**
 * cmd_misc - Wrapper for @misc command
 *
 * Core: do_misc(player, arg1, arg2)
 */
static void cmd_misc(dbref player, char *arg1, char *arg2)
{
    do_misc(player, arg1, arg2);
}

/**
 * cmd_ncset - Wrapper for @ncset command
 *
 * Core: do_set(player, arg1, pure2, 1)
 * Note: Uses pure2 instead of arg2, fourth param is 1
 */
static void cmd_ncset(dbref player, char *arg1, char *arg2)
{
    /* Note: This command uses pure2 which preserves case/spacing */
    do_set(player, arg1, arg2, 1);
}

/**
 * cmd_necho - Wrapper for @necho command
 *
 * Core: do_echo(player, pure, NULL, 1)
 * Note: Uses pure (unparsed arg) and fourth param is 1
 */
static void cmd_necho(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    /* Note: This command uses 'pure' which is the unparsed argument */
    do_echo(player, arg1, NULL, 1);
}

/**
 * cmd_nemit - Wrapper for @nemit command
 *
 * Core: do_emit(player, pure, NULL, 1)
 * Note: Uses pure (unparsed arg) and fourth param is 1
 */
static void cmd_nemit(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    /* Note: This command uses 'pure' which is the unparsed argument */
    do_emit(player, arg1, NULL, 1);
}

/**
 * cmd_nologins - Wrapper for @nologins command
 *
 * Core: do_nologins(player, arg1)
 * Note: Requires direct command (not triggered)
 */
static void cmd_nologins(dbref player, char *arg1, char *arg2)
{
    (void)arg2; /* TODO: Add requires_direct flag */
    do_nologins(player, arg1);
}

/**
 * cmd_noop - Wrapper for @noop command
 *
 * Core: No operation (empty command)
 */
static void cmd_noop(dbref player, char *arg1, char *arg2)
{
    (void)player;
    (void)arg1;
    (void)arg2;
    /* No operation - this command does nothing */
}

/**
 * cmd_nopow_class - Wrapper for @nopow_class command
 *
 * Core: do_nopow_class(player, arg1, arg2)
 * Note: Requires direct command (not triggered)
 */
static void cmd_nopow_class(dbref player, char *arg1, char *arg2)
{
    /* TODO: Add requires_direct flag */
    do_nopow_class(player, arg1, arg2);
}

/**
 * cmd_npage - Wrapper for @npage command
 *
 * Core: do_page(player, arg1, pure2)
 * Note: Uses pure2 (unparsed second arg)
 */
static void cmd_npage(dbref player, char *arg1, char *arg2)
{
    /* Note: This command uses pure2 to preserve formatting */
    do_page(player, arg1, arg2);
}

/**
 * cmd_npaste - Wrapper for @npaste command
 *
 * Core: do_pastecode(player, arg1, arg2)
 */
static void cmd_npaste(dbref player, char *arg1, char *arg2)
{
    do_pastecode(player, arg1, arg2);
}

/**
 * cmd_npemit - Wrapper for @npemit command
 *
 * Core: do_general_emit(player, arg1, pure, 4)
 * Note: Uses pure and emit type 4
 */
static void cmd_npemit(dbref player, char *arg1, char *arg2)
{
    /* Note: This uses 'pure' and emit type 4 */
    do_general_emit(player, arg1, arg2, 4);
}

/**
 * cmd_nset - Wrapper for @nset command
 *
 * Core: do_set(player, arg1, pure2, is_direct)
 * Note: Uses pure2 and is_direct flag
 */
static void cmd_nset(dbref player, char *arg1, char *arg2)
{
    /* Note: This uses pure2 and is_direct - assuming is_direct=1 for direct calls */
    do_set(player, arg1, arg2, 1);
}

/**
 * cmd_nuke - Wrapper for @nuke command
 *
 * Core: do_nuke(player, arg1)
 * Note: Requires direct command (not triggered)
 */
static void cmd_nuke(dbref player, char *arg1, char *arg2)
{
    (void)arg2; /* TODO: Add requires_direct flag */
    do_nuke(player, arg1);
}

/**
 * cmd_oemit - Wrapper for @oemit command
 *
 * Core: do_general_emit(player, arg1, arg2, 2)
 * Note: Emit type 2
 */
static void cmd_oemit(dbref player, char *arg1, char *arg2)
{
    do_general_emit(player, arg1, arg2, 2);
}

/**
 * cmd_open - Wrapper for @open command
 *
 * Core: do_open(player, arg1, arg2, NOTHING)
 * Note: Fourth parameter is NOTHING
 */
static void cmd_open(dbref player, char *arg1, char *arg2)
{
    do_open(player, arg1, arg2, NOTHING);
}

/**
 * cmd_outgoing - Wrapper for @outgoing command
 *
 * Core: do_outgoing(player, arg1, arg2)
 * Note: Only available if USE_OUTGOING is defined
 */
static void cmd_outgoing(dbref player, char *arg1, char *arg2)
{
#ifdef USE_OUTGOING
    do_outgoing(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "@outgoing disabled");
#endif
}

/**
 * cmd_paste - Wrapper for @paste command
 *
 * Core: do_paste(player, arg1, arg2)
 */
static void cmd_paste(dbref player, char *arg1, char *arg2)
{
    do_paste(player, arg1, arg2);
}

/**
 * cmd_pastecode - Wrapper for @pastecode command
 *
 * Core: do_pastecode(player, arg1, arg2)
 */
static void cmd_pastecode(dbref player, char *arg1, char *arg2)
{
    do_pastecode(player, arg1, arg2);
}

/**
 * cmd_pastestats - Wrapper for @pastestats command
 *
 * Core: do_pastestats(player, arg1)
 */
static void cmd_pastestats(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_pastestats(player, arg1);
}

/**
 * cmd_pbreak - Wrapper for @pbreak command
 *
 * Core: do_pstats(player, arg1)
 */
static void cmd_pbreak(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_pstats(player, arg1);
}

/**
 * cmd_pcreate - Wrapper for @pcreate command
 *
 * Core: do_pcreate(player, arg1, arg2)
 */
static void cmd_pcreate(dbref player, char *arg1, char *arg2)
{
    do_pcreate(player, arg1, arg2);
}

/**
 * cmd_Poor - Wrapper for @Poor command
 *
 * Core: do_poor(player, arg1)
 * Note: Requires direct command (not triggered)
 */
static void cmd_Poor(dbref player, char *arg1, char *arg2)
{
    (void)arg2; /* TODO: Add requires_direct flag */
    do_poor(player, arg1);
}

/**
 * cmd_powers - Wrapper for @powers command
 *
 * Core: do_powers(player, arg1)
 */
static void cmd_powers(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_powers(player, arg1);
}

/**
 * cmd_ps - Wrapper for @ps command
 *
 * Core: do_queue(player)
 */
static void cmd_ps(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_queue(player);
}

/**
 * cmd_purge - Wrapper for @purge command
 *
 * Core: do_purge(dbref player)
 */
static void cmd_purge(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    extern void do_purge(dbref);
    do_purge(player);
}

/**
 * cmd_quota - Wrapper for @quota command
 *
 * Core: do_quota(player, arg1, arg2)
 */
static void cmd_quota(dbref player, char *arg1, char *arg2)
{
    do_quota(player, arg1, arg2);
}

/**
 * cmd_racelist - Wrapper for @racelist command
 *
 * Core: do_racelist(player, arg1)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_racelist(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    (void)arg2;
    do_racelist(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_reboot - Wrapper for @reboot command
 *
 * Core: do_reload(dbref player, char *arg1)
 * Note: Deprecated in favor of @reload
 */
static void cmd_reboot(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    notify(player, "It's no longer @reboot. It's @reload.");
    extern void do_reload(dbref, char *);
    do_reload(player, arg1);
}

/**
 * cmd_reload - Wrapper for @reload command
 *
 * Core: do_reload(dbref player, char *arg1)
 */
static void cmd_reload(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    extern void do_reload(dbref, char *);
    do_reload(player, arg1);
}

/**
 * cmd_remit - Wrapper for @remit command
 *
 * Core: do_general_emit(player, arg1, arg2, 1)
 * Note: Emit type 1
 */
static void cmd_remit(dbref player, char *arg1, char *arg2)
{
    do_general_emit(player, arg1, arg2, 1);
}

/**
 * cmd_robot - Wrapper for @robot command
 *
 * Core: do_robot(player, arg1, arg2)
 */
static void cmd_robot(dbref player, char *arg1, char *arg2)
{
    do_robot(player, arg1, arg2);
}

/**
 * cmd_search - Wrapper for @search command
 *
 * Core: do_search(player, arg1, arg2)
 */
static void cmd_search(dbref player, char *arg1, char *arg2)
{
    do_search(player, arg1, arg2);
}

/**
 * cmd_setbit - Wrapper for @setbit command
 *
 * Core: do_setbit(player, arg1, arg2)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_setbit(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    do_setbit(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_showhash - Wrapper for @showhash command
 *
 * Core: do_showhash(player, arg1)
 */
static void cmd_showhash(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_showhash(player, arg1);
}

/**
 * cmd_shrink - Wrapper for @shrink command
 *
 * Core: do_shrinkdbuse(player, arg1)
 * Note: Only available if SHRINK_DB is defined
 */
static void cmd_shrink(dbref player, char *arg1, char *arg2)
{
#ifdef SHRINK_DB
    (void)arg2;
    do_shrinkdbuse(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Database shrinking is not enabled.");
#endif
}

/**
 * cmd_shutdown - Wrapper for @shutdown command
 *
 * Core: do_shutdown(player, arg1)
 */
static void cmd_shutdown(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    extern void do_shutdown(dbref, char *);
    do_shutdown(player, arg1);
}

/**
 * cmd_skillset - Wrapper for @skillset command
 *
 * Core: do_skillset(player, arg1, arg2)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_skillset(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    do_skillset(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_spawn - Wrapper for @spawn command
 *
 * Core: do_spawn(player, arg1, arg2)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_spawn(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    do_spawn(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_su - Wrapper for @su command
 *
 * Core: do_su(player, arg1, arg2, cause)
 * Note: Requires cause parameter
 */
static void cmd_su(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause = NOTHING;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause, argv_array);
    }

    extern void do_su(dbref, char *, char *, dbref);
    do_su(player, arg1, argv_array[0] ? argv_array[0] : "", cause);
}

/**
 * cmd_swap - Wrapper for @swap command
 *
 * Core: do_swap(player, arg1, arg2)
 */
static void cmd_swap(dbref player, char *arg1, char *arg2)
{
    do_swap(player, arg1, arg2);
}

/**
 * cmd_switch - Wrapper for @switch command
 *
 * Core: do_switch(player, arg1, argv, cause)
 * Unpacks argv and cause from arg2 using argument packing system
 */
static void cmd_switch(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause = NOTHING;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_switch(dbref, char *, char **, dbref);
    do_switch(player, arg1, argv_array, cause);
}

/**
 * cmd_text - Wrapper for @text command
 *
 * Core: do_text(player, arg1, arg2, NULL)
 */
static void cmd_text(dbref player, char *arg1, char *arg2)
{
    do_text(player, arg1, arg2, NULL);
}

/**
 * cmd_trigger - Wrapper for @trigger command
 *
 * Core: do_trigger(player, arg1, argv)
 * Unpacks argv from arg2 using argument packing system
 */
static void cmd_trigger(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause_unused;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause_unused, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_trigger(dbref, char *, char **);
    do_trigger(player, arg1, argv_array);
}

/**
 * cmd_tr_as - Wrapper for @tr_as command
 *
 * Core: do_trigger_as(player, arg1, argv)
 * Note: Requires argv parameter
 */
static void cmd_tr_as(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause_unused;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause_unused, argv_array);
    } else {
        for (int i = 0; i < MAX_PACKED_ARGS; i++) {
            argv_array[i] = NULL;
        }
    }

    extern void do_trigger_as(dbref, char *, char **);
    do_trigger_as(player, arg1, argv_array);
}

/**
 * cmd_uconfig - Wrapper for @uconfig command
 *
 * Core: do_uconfig(player, arg1, arg2)
 * Note: Universe commands are always available
 */
static void cmd_uconfig(dbref player, char *arg1, char *arg2)
{
    do_uconfig(player, arg1, arg2);
}

/**
 * cmd_ucreate - Wrapper for @ucreate command
 *
 * Core: do_ucreate(player, arg1, (int)atol(arg2))
 * Note: Universe commands are always available, arg2 is converted to int
 */
static void cmd_ucreate(dbref player, char *arg1, char *arg2)
{
    do_ucreate(player, arg1, (int)atol(arg2));
}

/**
 * cmd_uinfo - Wrapper for @uinfo command
 *
 * Core: do_uinfo(player, arg1)
 * Note: Universe commands are always available
 */
static void cmd_uinfo(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_uinfo(player, arg1);
}

/**
 * cmd_ulink - Wrapper for @ulink command
 *
 * Core: do_ulink(player, arg1, arg2)
 * Note: Universe commands are always available
 */
static void cmd_ulink(dbref player, char *arg1, char *arg2)
{
    do_ulink(player, arg1, arg2);
}

/**
 * cmd_undefattr - Wrapper for @undefattr command
 *
 * Core: do_undefattr(player, arg1)
 */
static void cmd_undefattr(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_undefattr(player, arg1);
}

/**
 * cmd_undestroy - Wrapper for @undestroy command
 *
 * Core: do_undestroy(player, arg1)
 */
static void cmd_undestroy(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_undestroy(player, arg1);
}

/**
 * cmd_unhide - Wrapper for @unhide command
 *
 * Core: do_unhide(player)
 */
static void cmd_unhide(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_unhide(player);
}

/**
 * cmd_unulink - Wrapper for @unulink command
 *
 * Core: do_unulink(player, arg1)
 * Note: Universe commands are always available
 */
static void cmd_unulink(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_unulink(player, arg1);
}

/**
 * cmd_unzlink - Wrapper for @unzlink command
 *
 * Core: do_unzlink(player, arg1)
 */
static void cmd_unzlink(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_unzlink(player, arg1);
}

/**
 * cmd_upfront - Wrapper for @upfront command
 *
 * Core: do_upfront(player, arg1)
 */
static void cmd_upfront(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_upfront(player, arg1);
}

/**
 * cmd_wait - Wrapper for @wait command
 *
 * Core: wait_que(player, (int)strtol(arg1, NULL, 10), arg2, cause)
 * Note: arg1 is converted to int, requires cause parameter
 */
static void cmd_wait(dbref player, char *arg1, char *arg2)
{
    char arg2_copy[MAX_COMMAND_BUFFER];
    char *argv_array[MAX_PACKED_ARGS];
    dbref cause = NOTHING;
    int delay;

    if (arg2 && *arg2) {
        strncpy(arg2_copy, arg2, sizeof(arg2_copy) - 1);
        arg2_copy[sizeof(arg2_copy) - 1] = '\0';
        unpack_argv(arg2_copy, &cause, argv_array);
    }

    delay = (int)strtol(arg1, NULL, 10);
    extern void wait_que(dbref, int, char *, dbref);
    wait_que(player, delay, argv_array[0] ? argv_array[0] : "", cause);
}

/**
 * cmd_wemit - Wrapper for @wemit command
 *
 * Core: do_wemit(player, arg1, arg2)
 */
static void cmd_wemit(dbref player, char *arg1, char *arg2)
{
    do_wemit(player, arg1, arg2);
}

/**
 * cmd_wipeout - Wrapper for @wipeout command
 *
 * Core: do_wipeout(player, arg1, arg2)
 * Note: Requires direct command (not triggered)
 */
static void cmd_wipeout(dbref player, char *arg1, char *arg2)
{
    /* TODO: Add requires_direct flag */
    do_wipeout(player, arg1, arg2);
}

/**
 * cmd_zemit - Wrapper for @zemit command
 *
 * Core: do_general_emit(player, arg1, arg2, 3)
 * Note: Emit type 3
 */
static void cmd_zemit(dbref player, char *arg1, char *arg2)
{
    do_general_emit(player, arg1, arg2, 3);
}

/**
 * cmd_zlink - Wrapper for @zlink command
 *
 * Core: do_zlink(player, arg1, arg2)
 */
static void cmd_zlink(dbref player, char *arg1, char *arg2)
{
    do_zlink(player, arg1, arg2);
}

/* ========================================================================== */
/*                          + COMMAND WRAPPERS                               */
/* ========================================================================== */

/**
 * cmd_ban - Wrapper for +ban command
 *
 * Core: do_ban(player, arg1, arg2)
 */
static void cmd_ban(dbref player, char *arg1, char *arg2)
{
    do_ban(player, arg1, arg2);
}

/**
 * cmd_cmdav - Wrapper for +cmdav command
 *
 * Core: do_cmdav(player)
 */
static void cmd_cmdav(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_cmdav(player);
}

/**
 * cmd_loginstats - Wrapper for +loginstats command
 *
 * Core: do_loginstats(player)
 */
static void cmd_loginstats(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_loginstats(player);
}

/**
 * cmd_motd - Wrapper for +motd command
 *
 * Core: do_plusmotd(player, arg1, arg2)
 */
static void cmd_motd(dbref player, char *arg1, char *arg2)
{
    do_plusmotd(player, arg1, arg2);
}

/**
 * cmd_skills - Wrapper for +skills command
 *
 * Core: do_skills(player, arg1, arg2)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_skills(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    do_skills(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_status - Wrapper for +status command
 *
 * Core: do_status(player, arg1)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_status(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg2;
    do_status(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_unban - Wrapper for +unban command
 *
 * Core: do_unban(player, arg1, arg2)
 */
static void cmd_unban(dbref player, char *arg1, char *arg2)
{
    do_unban(player, arg1, arg2);
}

/* ========================================================================== */
/*                      REGULAR COMMAND WRAPPERS                             */
/* ========================================================================== */

/**
 * cmd_equip - Wrapper for equip command
 *
 * Core: do_equip(player, player, arg1)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_equip(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    (void)arg2;
    do_equip(player, player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_fight - Wrapper for fight command
 *
 * Core: do_fight(player, arg1, arg2)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_fight(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    do_fight(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_flee - Wrapper for flee command
 *
 * Core: do_flee(player)
 * Note: Only available if USE_COMBAT is defined
 */
static void cmd_flee(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT
    (void)arg1;
    (void)arg2;
    do_flee(player);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_goto - Wrapper for goto command
 *
 * Core: do_move(player, arg1)
 */
static void cmd_goto(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_move(player, arg1);
}

/**
 * cmd_gripe - Wrapper for gripe command
 *
 * Core: do_gripe(player, arg1, arg2)
 */
static void cmd_gripe(dbref player, char *arg1, char *arg2)
{
    do_gripe(player, arg1, arg2);
}

/**
 * cmd_help - Wrapper for help command
 *
 * Core: do_text(player, "help", arg1, NULL)
 */
static void cmd_help(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_text(player, "help", arg1, NULL);
}

/**
 * cmd_join - Wrapper for join command
 *
 * Core: do_join(player, arg1)
 */
static void cmd_join(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_join(player, arg1);
}

/**
 * cmd_money - Wrapper for money command
 *
 * Core: do_money(player, arg1, arg2)
 */
static void cmd_money(dbref player, char *arg1, char *arg2)
{
    do_money(player, arg1, arg2);
}

/**
 * cmd_motd_regular - Wrapper for motd command (non-plus version)
 *
 * Core: do_motd(player)
 */
static void cmd_motd_regular(dbref player, char *arg1, char *arg2)
{
    (void)arg1;
    (void)arg2;
    do_motd(player);
}

/**
 * cmd_move - Wrapper for move command
 *
 * Core: do_move(player, arg1)
 */
static void cmd_move(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_move(player, arg1);
}

/**
 * cmd_pray - Wrapper for pray command
 *
 * Core: do_pray(player, arg1, arg2)
 */
static void cmd_pray(dbref player, char *arg1, char *arg2)
{
    do_pray(player, arg1, arg2);
}

/**
 * cmd_read - Wrapper for read command
 *
 * Core: do_look_at(player, arg1)
 */
static void cmd_read(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_look_at(player, arg1);
}

/**
 * cmd_remove - Wrapper for remove command
 *
 * Core: do_remove(player)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_remove(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg1;
    (void)arg2;
    do_remove(player);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_rlpage - Wrapper for rlpage command
 *
 * Core: do_rlpage(player, arg1, arg2)
 * Note: Only available if USE_RLPAGE is defined
 */
static void cmd_rlpage(dbref player, char *arg1, char *arg2)
{
#ifdef USE_RLPAGE
    do_rlpage(player, arg1, arg2);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "rlpage is not enabled on this server.");
#endif
}

/**
 * cmd_slay - Wrapper for slay command
 *
 * Core: do_slay(player, arg1)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_slay(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg2;
    do_slay(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_summon - Wrapper for summon command
 *
 * Core: do_summon(player, arg1)
 */
static void cmd_summon(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_summon(player, arg1);
}

/**
 * cmd_take - Wrapper for take command
 *
 * Core: do_get(player, arg1)
 */
static void cmd_take(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_get(player, arg1);
}

/**
 * cmd_throw - Wrapper for throw command
 *
 * Core: do_drop(player, arg1)
 */
static void cmd_throw(dbref player, char *arg1, char *arg2)
{
    (void)arg2;
    do_drop(player, arg1);
}

/**
 * cmd_to - Wrapper for to command
 *
 * Core: do_to(player, arg1, arg2)
 */
static void cmd_to(dbref player, char *arg1, char *arg2)
{
    do_to(player, arg1, arg2);
}

/**
 * cmd_unwield - Wrapper for unwield command
 *
 * Core: do_unwield(player)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_unwield(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg1;
    (void)arg2;
    do_unwield(player);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_wear - Wrapper for wear command
 *
 * Core: do_wear(player, arg1)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_wear(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg2;
    do_wear(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/**
 * cmd_wield - Wrapper for wield command
 *
 * Core: do_wield(player, arg1)
 * Note: Only available if USE_COMBAT_TM97 is defined
 */
static void cmd_wield(dbref player, char *arg1, char *arg2)
{
#ifdef USE_COMBAT_TM97
    (void)arg2;
    do_wield(player, arg1);
#else
    (void)arg1;
    (void)arg2;
    notify(player, "Combat features are not enabled.");
#endif
}

/* ========================================================================== */
/*                          END OF PHASE 4 WRAPPERS                          */

/* ============================================================================
 * SPECIAL ARGUMENT PACKING FOR DEFERRED COMMANDS
 * ============================================================================
 *
 * Some commands require access to argv[] (variable argument array) or cause
 * (the dbref that caused this command). These cannot be passed through the
 * standard (player, arg1, arg2) handler signature.
 *
 * SOLUTION: Pack argv[] and cause into a delimited string in arg2, then
 * unpack in the wrapper function.
 *
 * DELIMITER: ASCII Unit Separator (0x1F) - guaranteed not to appear in
 * normal user input as it's a non-printable control character.
 *
 * FORMAT: arg2 = "cause_dbref\x1Fargv[0]\x1Fargv[1]\x1F...\x1Fargv[N]"
 * ============================================================================ */

/**
 * unpack_argv - Unpack a delimited string into argv array and cause
 *
 * Takes a packed string created by pack_argv() and reconstructs the
 * argv[] array and cause dbref.
 *
 * @param packed    The packed string from arg2
 * @param cause_out Pointer to store cause dbref (can be NULL)
 * @param argv_out  Array to store argv pointers (must have MAX_PACKED_ARGS elements)
 * @return Number of argv elements unpacked (not including cause)
 *
 * MEMORY: Modifies packed string in place (inserts nulls at delimiters)
 * USAGE: char *my_argv[MAX_PACKED_ARGS]; dbref my_cause;
 *        int argc = unpack_argv(arg2, &my_cause, my_argv);
 */
static int unpack_argv(char *packed, dbref *cause_out, char **argv_out)
{
    int argc = 0;
    char *ptr = packed;
    char *start;

    if (!packed || !argv_out) {
        return 0;
    }

    /* Initialize argv array to NULL */
    for (int i = 0; i < MAX_PACKED_ARGS; i++) {
        argv_out[i] = NULL;
    }

    /* Extract cause dbref (first field before first delimiter) */
    if (cause_out) {
        *cause_out = NOTHING;
        if (*ptr) {
            *cause_out = (dbref)atol(ptr);
        }
    }

    /* Skip to first delimiter */
    while (*ptr && *ptr != ARG_DELIMITER) {
        ptr++;
    }
    if (*ptr == ARG_DELIMITER) {
        ptr++;  /* Move past delimiter */
    }

    /* Extract argv elements */
    start = ptr;
    while (*ptr && argc < MAX_PACKED_ARGS) {
        if (*ptr == ARG_DELIMITER) {
            *ptr = '\0';  /* Null-terminate this argument */
            argv_out[argc++] = start;
            start = ptr + 1;
        }
        ptr++;
    }

    /* Don't forget the last argument if there is one */
    if (start < ptr && argc < MAX_PACKED_ARGS) {
        argv_out[argc++] = start;
    }

    return argc;
}

/**
 * register_command - Register a command in a parser
 *
 * @param parser Parser to add command to
 * @param cmd Command entry to register
 * @return 1 on success, 0 on failure
 */
int register_command(parser_t *parser, const command_entry_t *cmd)
{
    command_entry_t *cmd_copy;

    if (!parser || !cmd || !cmd->name || !cmd->handler) {
        log_error("register_command: Invalid parameters");
        return 0;
    }

    if (!parser->commands) {
        log_error(tprintf("register_command: Parser '%s' has no command table",
                         parser->name));
        return 0;
    }

    /* Allocate a copy of the command entry */
    SAFE_MALLOC(cmd_copy, command_entry_t, 1);
    memcpy(cmd_copy, cmd, sizeof(command_entry_t));

    /* Insert into hash table */
    if (!hash_insert(parser->commands, cmd->name, cmd_copy)) {
        log_error(tprintf("register_command: Failed to register '%s'", cmd->name));
        SMART_FREE(cmd_copy);
        return 0;
    }

    parser->command_count++;
    return 1;
}

/**
 * register_demuse_commands - Register standard deMUSE commands
 *
 * @param parser deMUSE parser to register commands in
 */
static void register_demuse_commands(parser_t *parser)
{
    /* Commands organized by functional category for maintainability */

    static const command_entry_t demuse_commands[] = {
        /* ==================================================================
         * BASIC/INFO COMMANDS - Core gameplay commands
         * ================================================================== */
        {"examine",     cmd_examine,   1, 0, 0, 0, 0},
        {"help",        cmd_help,      1, 0, 0, 0, 0},
        {"i",           cmd_inventory, 1, 0, 0, 0, 0},  /* Alias for inventory */
        {"inventory",   cmd_inventory, 1, 0, 0, 0, 0},
        {"l",           cmd_look,      1, 0, 0, 1, 0},  /* Alias for look */
        {"look",        cmd_look,      1, 0, 0, 1, 0},
        {"score",       cmd_score,     2, 0, 0, 0, 0},
        {"who",         cmd_who,       2, 0, 0, 0, 0},
        {"@info",       cmd_info,      2, 0, 0, 0, 0},

        /* ==================================================================
         * COMMUNICATION COMMANDS - Say, pose, page, etc.
         * ================================================================== */
        {";pose",       cmd_semipose,  2, 0, 0, 0, 0},  /* Semipose variant */
        {":pose",       cmd_pose,      2, 0, 0, 0, 0},  /* Alternate pose */
        {"gripe",       cmd_gripe,     1, 0, 0, 0, 0},
        {"page",        cmd_page,      1, 0, 0, 0, 0},
        {"pose",        cmd_pose,      1, 0, 0, 0, 0},
        {"pray",        cmd_pray,      1, 0, 0, 0, 0},
        {"rlpage",      cmd_rlpage,    1, 0, 0, 0, 0},
        {"say",         cmd_say,       1, 0, 0, 0, 0},
        {"think",       cmd_think,     1, 0, 0, 0, 0},
        {"to",          cmd_to,        1, 0, 0, 0, 0},
        {"whisper",     cmd_whisper,   1, 0, 0, 0, 0},

        /* ==================================================================
         * EMIT COMMANDS - Broadcasting messages
         * ================================================================== */
        {"@announce",   cmd_announce,  2, 0, 0, 0, 0},
        {"@broadcast",  cmd_broadcast, 2, 0, 0, 0, 0},
        {"@cemit",      cmd_cemit,     2, 0, 0, 0, 0},
        {"@chemit",     cmd_chemit,    2, 0, 0, 0, 0},
        {"@echo",       cmd_echo,      2, 0, 0, 0, 0},
        {"@emit",       cmd_emit,      2, 0, 0, 0, 0},
        {"@necho",      cmd_necho,     2, 0, 0, 0, 0},
        {"@nemit",      cmd_nemit,     2, 0, 0, 0, 0},
        {"@npage",      cmd_npage,     2, 0, 0, 0, 0},
        {"@npemit",     cmd_npemit,    2, 0, 0, 0, 0},
        {"@oemit",      cmd_oemit,     2, 0, 0, 0, 0},
        {"@pemit",      cmd_pemit,     2, 0, 0, 0, 0},
        {"@remit",      cmd_remit,     2, 0, 0, 0, 0},
        {"@wemit",      cmd_wemit,     2, 0, 0, 0, 0},
        {"@zemit",      cmd_zemit,     2, 0, 0, 0, 0},

        /* ==================================================================
         * MOVEMENT COMMANDS - Navigation
         * ================================================================== */
        {"enter",       cmd_enter,     1, 0, 0, 0, 0},
        {"goto",        cmd_goto,      1, 0, 0, 0, 0},
        {"join",        cmd_join,      1, 0, 0, 0, 0},
        {"leave",       cmd_leave,     1, 0, 0, 0, 0},
        {"move",        cmd_move,      1, 0, 0, 0, 0},
        {"summon",      cmd_summon,    1, 0, 0, 0, 0},

        /* ==================================================================
         * OBJECT MANIPULATION - Get, drop, give, etc.
         * ================================================================== */
        {"drop",        cmd_drop,      1, 0, 0, 0, 0},
        {"get",         cmd_get,       1, 0, 0, 0, 0},
        {"give",        cmd_give,      1, 0, 0, 0, 0},
        {"read",        cmd_read,      1, 0, 0, 0, 0},
        {"remove",      cmd_remove,    1, 0, 0, 0, 0},
        {"take",        cmd_take,      1, 0, 0, 0, 0},
        {"use",         cmd_use,       1, 0, 0, 0, 0},

        /* ==================================================================
         * COMBAT/EQUIPMENT COMMANDS
         * ================================================================== */
        {"equip",       cmd_equip,     1, 0, 0, 0, 0},
        {"fight",       cmd_fight,     1, 0, 0, 0, 0},
        {"flee",        cmd_flee,      1, 0, 0, 0, 0},
        {"money",       cmd_money,     1, 0, 0, 0, 0},
        {"slay",        cmd_slay,      1, 0, 0, 0, 0},
        {"throw",       cmd_throw,     1, 0, 0, 0, 0},
        {"unwield",     cmd_unwield,   1, 0, 0, 0, 0},
        {"wear",        cmd_wear,      1, 0, 0, 0, 0},
        {"wield",       cmd_wield,     1, 0, 0, 0, 0},

        /* ==================================================================
         * BUILDING COMMANDS - Object creation and modification
         * ================================================================== */
        {"@chown",      cmd_chown,     2, 0, 0, 0, 0},
        {"@clone",      cmd_clone,     2, 0, 0, 0, 0},
        {"@create",     cmd_create,    2, 0, 0, 0, 0},
        {"@describe",   cmd_describe,  2, 0, 0, 0, 0},
        {"@destroy",    cmd_destroy,   2, 0, 0, 0, 0},
        {"@dig",        cmd_dig,       2, 0, 0, 0, 0},  /* Now handled via argument packing */
        {"@link",       cmd_link,      2, 0, 0, 0, 0},
        {"@name",       cmd_name,      2, 0, 0, 0, 0},
        {"@open",       cmd_open,      2, 0, 0, 0, 0},
        {"@poof",       cmd_poof,      2, 0, 0, 0, 0},
        {"@set",        cmd_set,       2, 0, 0, 0, 0},
        {"@unlink",     cmd_unlink,    2, 0, 0, 0, 0},
        {"@unlock",     cmd_unlock,    2, 0, 0, 0, 0},

        /* ==================================================================
         * ATTRIBUTE COMMANDS - Attribute definition and manipulation
         * ================================================================== */
        {"@decompile",  cmd_decompile, 2, 0, 0, 0, 0},
        {"@defattr",    cmd_defattr,   2, 0, 0, 0, 0},
        {"@text",       cmd_text,      2, 0, 0, 0, 0},
        {"@undefattr",  cmd_undefattr, 2, 0, 0, 0, 0},

        /* ==================================================================
         * HIERARCHY COMMANDS - Parent-child relationships
         * ================================================================== */
        {"@addparent",  cmd_addparent, 2, 0, 0, 0, 0},
        {"@delparent",  cmd_delparent, 2, 0, 0, 0, 0},

        /* ==================================================================
         * DATABASE/SEARCH COMMANDS - Database inspection and search
         * ================================================================== */
        {"@check",      cmd_check,     2, 0, 0, 0, 0},
        {"@dbck",       cmd_dbck,      2, 0, 0, 0, 0},
        {"@dbtop",      cmd_dbtop,     2, 0, 0, 0, 0},
        {"@dump",       cmd_dump,      2, 0, 0, 0, 0},
        {"@find",       cmd_find,      2, 0, 0, 0, 0},
        {"@search",     cmd_search,    2, 0, 0, 0, 0},
        {"@showhash",   cmd_showhash,  2, 0, 0, 0, 0},
        {"@stats",      cmd_stats,     2, 0, 0, 0, 0},

        /* ==================================================================
         * PLAYER MANAGEMENT COMMANDS - Player creation and control
         * ================================================================== */
        {"@lockout",    cmd_lockout,   6, 1, 0, 0, 0},  /* requires_direct, min=6 to avoid collision */
        {"@nologins",   cmd_nologins,  2, 1, 0, 0, 0},  /* requires_direct */
        {"@nuke",       cmd_nuke,      2, 1, 0, 0, 0},  /* requires_direct */
        {"@password",   cmd_password,  2, 1, 0, 0, 0},  /* requires_direct */
        {"@pcreate",    cmd_pcreate,   2, 0, 0, 0, 0},
        {"@robot",      cmd_robot,     2, 0, 0, 0, 0},
        {"@undestroy",  cmd_undestroy, 2, 0, 0, 0, 0},

        /* ==================================================================
         * PERMISSION/POWER COMMANDS - Permissions and powers management
         * ================================================================== */
        {"@boot",       cmd_boot,      2, 0, 1, 0, 0},  /* requires_wizard */
        {"@cboot",      cmd_cboot,     2, 0, 0, 0, 0},
        {"@empower",    cmd_empower,   2, 1, 0, 0, 0},  /* requires_direct */
        {"@nopow_class", cmd_nopow_class, 2, 1, 0, 0, 0},  /* requires_direct */
        {"@Poor",       cmd_Poor,      2, 1, 0, 0, 0},  /* requires_direct */
        {"@powers",     cmd_powers,    2, 0, 0, 0, 0},
        {"@setbit",     cmd_setbit,    2, 0, 0, 0, 0},
        {"@upfront",    cmd_upfront,   2, 0, 0, 0, 0},

        /* ==================================================================
         * ADMINISTRATIVE COMMANDS - Server and wizard commands
         * ================================================================== */
        {"@as",         cmd_as,        2, 0, 0, 0, 0},
        {"@at",         cmd_at,        2, 0, 0, 0, 0},
        {"@config",     cmd_config,    2, 0, 0, 0, 0},
        {"@exec",       cmd_exec,      2, 0, 0, 0, 0},
        {"@force",      cmd_force,     2, 1, 0, 0, 0},  /* requires_direct */
        {"@giveto",     cmd_giveto,    2, 0, 0, 0, 0},
        {"@halt",       cmd_halt,      2, 0, 0, 0, 0},
        {"@pbreak",     cmd_pbreak,    2, 0, 0, 0, 0},
        {"@ps",         cmd_ps,        2, 0, 0, 0, 0},
        {"@purge",      cmd_purge,     2, 0, 0, 0, 0},  /* Now non-static */
        {"@reboot",     cmd_reboot,    4, 0, 0, 0, 0},  /* Deprecated, redirects to @reload */
        {"@reload",     cmd_reload,    4, 0, 0, 0, 0},  /* Now non-static */
        {"@shutdown",   cmd_shutdown,  4, 0, 0, 0, 0},  /* Now non-static */
        {"@spawn",      cmd_spawn,     2, 0, 0, 0, 0},
        {"@su",         cmd_su,        3, 0, 0, 0, 0},  /* Now handled via argument packing */
        {"@sweep",      cmd_sweep,     2, 0, 0, 0, 0},
        {"@teleport",   cmd_teleport,  2, 0, 0, 0, 0},
        {"@whereis",    cmd_whereis,   2, 0, 0, 0, 0},
        {"@wipeout",    cmd_wipeout,   2, 1, 0, 0, 0},  /* requires_direct */

        /* ==================================================================
         * ZONE/UNIVERSE COMMANDS - Multi-zone and universe management
         * ================================================================== */
        {"@guniverse",  cmd_guniverse, 2, 0, 0, 0, 0},
        {"@gzone",      cmd_gzone,     2, 0, 0, 0, 0},
        {"@uconfig",    cmd_uconfig,   2, 0, 0, 0, 0},
        {"@ucreate",    cmd_ucreate,   2, 0, 0, 0, 0},
        {"@uinfo",      cmd_uinfo,     2, 0, 0, 0, 0},
        {"@ulink",      cmd_ulink,     2, 0, 0, 0, 0},
        {"@unulink",    cmd_unulink,   2, 0, 0, 0, 0},
        {"@unzlink",    cmd_unzlink,   2, 0, 0, 0, 0},
        {"@zlink",      cmd_zlink,     2, 0, 0, 0, 0},

        /* ==================================================================
         * CHANNEL COMMANDS - Communication channels
         * ================================================================== */
        {"+ban",        cmd_ban,       2, 0, 0, 0, 0},
        {"+channel",    cmd_channel,   2, 0, 0, 0, 0},
        {"+com",        cmd_com,       2, 0, 0, 0, 0},
        {"+unban",      cmd_unban,     2, 0, 0, 0, 0},
        {"@cname",      cmd_cname,     2, 0, 0, 0, 0},
        {"@cpaste",     cmd_cpaste,    2, 0, 0, 0, 0},
        {"@cset",       cmd_cset,      2, 0, 0, 0, 0},
        {"@ctrace",     cmd_ctrace,    2, 0, 0, 0, 0},
        {"@ncset",      cmd_ncset,     2, 0, 0, 0, 0},

        /* ==================================================================
         * MAIL/BOARD COMMANDS - Mail and bulletin board systems
         * ================================================================== */
        {"+board",      cmd_board,     2, 0, 0, 0, 0},
        {"+mail",       cmd_mail,      2, 0, 0, 0, 0},

        /* ==================================================================
         * PASTE/CODE COMMANDS - Code paste and management
         * ================================================================== */
        {"@npaste",     cmd_npaste,    2, 0, 0, 0, 0},
        {"@paste",      cmd_paste,     2, 0, 0, 0, 0},
        {"@pastecode",  cmd_pastecode, 2, 0, 0, 0, 0},
        {"@pastestats", cmd_pastestats, 2, 0, 0, 0, 0},

        /* ==================================================================
         * QUOTA COMMANDS - Resource quota management
         * ================================================================== */
        {"@allquota",   cmd_allquota,  2, 1, 0, 0, 0},  /* requires_direct */
        {"@quota",      cmd_quota,     2, 0, 0, 0, 0},
        {"@shrink",     cmd_shrink,    2, 0, 0, 0, 0},

        /* ==================================================================
         * CLASS/SKILL COMMANDS - Character class and skill system
         * ================================================================== */
        {"+skills",     cmd_skills,    2, 0, 0, 0, 0},
        {"+status",     cmd_status,    2, 0, 0, 0, 0},
        {"@class",      cmd_class,     2, 0, 0, 0, 0},
        {"@racelist",   cmd_racelist,  2, 0, 0, 0, 0},
        {"@skillset",   cmd_skillset,  2, 0, 0, 0, 0},

        /* ==================================================================
         * SOCIAL/STATUS COMMANDS - Player status and social features
         * ================================================================== */
        {"+away",       cmd_away,      2, 0, 0, 0, 0},
        {"+cmdav",      cmd_cmdav,     2, 0, 0, 0, 0},
        {"+haven",      cmd_haven,     2, 0, 0, 0, 0},
        {"+idle",       cmd_idle,      2, 0, 0, 0, 0},
        {"+laston",     cmd_laston,    2, 0, 0, 0, 0},
        {"+loginstats", cmd_loginstats, 2, 0, 0, 0, 0},
        {"+motd",       cmd_motd,      2, 0, 0, 0, 0},
        {"+uptime",     cmd_uptime,    2, 0, 0, 0, 0},
        {"+version",    cmd_version,   2, 0, 0, 0, 0},
        {"motd",        cmd_motd_regular, 1, 0, 0, 0, 0},

        /* ==================================================================
         * UTILITY/MISC COMMANDS - Miscellaneous utility commands
         * ================================================================== */
        {"@hide",       cmd_hide,      2, 0, 0, 0, 0},
        {"@listarea",   cmd_listarea,  2, 0, 0, 0, 0},
        {"@misc",       cmd_misc,      2, 0, 0, 0, 0},
        {"@noop",       cmd_noop,      2, 0, 0, 0, 0},
        {"@nset",       cmd_nset,      2, 0, 0, 0, 0},
        {"@outgoing",   cmd_outgoing,  2, 0, 0, 0, 0},
        {"@swap",       cmd_swap,      2, 0, 0, 0, 0},
        {"@unhide",     cmd_unhide,    2, 0, 0, 0, 0},

        /* ==================================================================
         * CONTROL FLOW COMMANDS - Loops, conditionals, triggers
         * Previously deferred, now handled via argument packing system
         * ================================================================== */
        {"@cycle",      cmd_cycle,     2, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@edit",       cmd_edit,      2, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@foreach",    cmd_foreach,   5, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@switch",     cmd_switch,    3, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@trigger",    cmd_trigger,   3, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@tr_as",      cmd_tr_as,     5, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */
        {"@wait",       cmd_wait,      2, 0, 0, 0, 0},  /* Arguments packed with \x1F delimiter */

        /* End marker */
        {NULL, NULL, 0, 0, 0, 0, 0}
    };

    const command_entry_t *cmd;

    log_important(tprintf("Registering deMUSE commands in parser '%s'", parser->name));

    for (cmd = demuse_commands; cmd->name != NULL; cmd++) {
        if (!register_command(parser, cmd)) {
            log_error(tprintf("Failed to register command: %s", cmd->name));
        }
    }

    log_important(tprintf("Registered %d deMUSE commands", parser->command_count));
}

/* ============================================================================
 * PARSER INITIALIZATION
 * ============================================================================ */

/**
 * init_parsers - Initialize all parser definitions
 *
 * Creates parser structures and registers commands for each parser type.
 * Called once at server startup.
 */
void init_parsers(void)
{
    parser_t *p;

    log_important("Initializing parser system...");

    /* ===== Parser 0: deMUSE ===== */
    p = &parsers[PARSER_DEMUSE];
    p->name = "deMUSE";
    p->version = "2025";
    p->description = "Standard deMUSE command set with modern enhancements";

    /* Create command hash table */
    p->commands = hash_create("demuse_commands", HASH_SIZE_LARGE, 0, NULL);
    if (!p->commands) {
        log_error("Failed to create deMUSE command table!");
        return;
    }
    p->command_count = 0;

    /* No separate function table yet - use global function system */
    p->functions = NULL;

    /* deMUSE syntax */
    p->syntax.say_token = SAY_TOKEN;      /* '"' */
    p->syntax.pose_token = POSE_TOKEN;    /* ':' */
    p->syntax.semipose_token = NOSP_POSE; /* ';' */
    p->syntax.page_token = 0;             /* No single-char page token */
    p->syntax.think_token = THINK_TOKEN;  /* '#' */
    p->syntax.case_sensitive = 0;         /* Case-insensitive commands */
    p->syntax.allow_abbreviations = 1;    /* Allow abbreviations */

    /* deMUSE limits (from existing code) */
    p->limits.max_recursion = 15000;
    p->limits.max_command_length = 8192;
    p->limits.max_function_invocations = 15000;

    /* Register all deMUSE commands */
    register_demuse_commands(p);

    num_parsers = 1;

    log_important(tprintf("Parser system initialized with %d parsers", num_parsers));
}

/* ============================================================================
 * UNIVERSE INITIALIZATION
 * ============================================================================ */

/**
 * init_universes - Initialize all universe instances
 *
 * Creates universe instances and links them to parsers.
 * Called once at server startup after init_parsers().
 */
void init_universes(void)
{
    universe_t *u;

    log_important("Initializing universe system...");

    /* ===== Universe 0: Default deMUSE World ===== */
    u = &universes[UNIVERSE_DEFAULT];
    u->id = UNIVERSE_DEFAULT;
    u->name = "deMUSE World";
    u->description = "The standard deMUSE environment";

    /* Link to deMUSE parser */
    u->parser = &parsers[PARSER_DEMUSE];

    /* Database object for configuration (existing object #5) */
    u->db_object = 5;

    /* Default configuration */
    u->config.allow_combat = 1;
    u->config.allow_building = 1;
    u->config.allow_teleport = 1;
    u->config.max_objects_per_player = 500;
    u->config.starting_location = 0;  /* Room #0 */
    u->config.default_zone = 0;       /* Zone #0 */

    /* Statistics */
    u->player_count = 0;
    u->created = time(NULL);

    num_universes = 1;

    log_important(tprintf("Universe system initialized with %d universes", num_universes));
}

/**
 * shutdown_parsers - Clean up parser system
 *
 * Destroys hash tables and frees parser resources.
 * Called at server shutdown.
 */
void shutdown_parsers(void)
{
    int i;

    log_important("Shutting down parser system...");

    /* Destroy parser command tables */
    for (i = 0; i < num_parsers; i++) {
        if (parsers[i].commands) {
            hash_destroy(parsers[i].commands);
            parsers[i].commands = NULL;
        }
        if (parsers[i].functions) {
            hash_destroy(parsers[i].functions);
            parsers[i].functions = NULL;
        }
    }

    num_parsers = 0;
    num_universes = 0;

    log_important("Parser system shutdown complete");
}

/* ============================================================================
 * LOOKUP FUNCTIONS
 * ============================================================================ */

/**
 * get_parser - Get parser by ID
 *
 * @param parser_id Parser ID
 * @return Pointer to parser or NULL if invalid
 */
parser_t *get_parser(int parser_id)
{
    if (parser_id >= 0 && parser_id < num_parsers) {
        return &parsers[parser_id];
    }
    return NULL;
}

/**
 * get_parser_by_name - Get parser by name
 *
 * @param name Parser name (case-insensitive)
 * @return Pointer to parser or NULL if not found
 */
parser_t *get_parser_by_name(const char *name)
{
    int i;

    if (!name || !*name) {
        return NULL;
    }

    for (i = 0; i < num_parsers; i++) {
        if (parsers[i].name && !string_compare(name, parsers[i].name)) {
            return &parsers[i];
        }
    }

    return NULL;
}

/**
 * get_universe - Get universe by ID
 *
 * @param universe_id Universe ID
 * @return Pointer to universe or default universe if invalid
 */
universe_t *get_universe(int universe_id)
{
    if (universe_id >= 0 && universe_id < num_universes) {
        return &universes[universe_id];
    }
    /* Default to universe 0 if invalid */
    return &universes[UNIVERSE_DEFAULT];
}

/**
 * get_universe_by_name - Get universe by name
 *
 * @param name Universe name (case-insensitive)
 * @return Pointer to universe or NULL if not found
 */
universe_t *get_universe_by_name(const char *name)
{
    int i;

    if (!name || !*name) {
        return NULL;
    }

    for (i = 0; i < num_universes; i++) {
        if (universes[i].name && !string_compare(name, universes[i].name)) {
            return &universes[i];
        }
    }

    return NULL;
}

/**
 * get_player_universe - Get the universe a player is in
 *
 * Currently returns default universe.
 * TODO: Check player attribute or db[player].universe field
 *
 * @param player Player dbref
 * @return Universe ID (always valid)
 */
int get_player_universe(dbref player)
{
    /* Validate player */
    if (!GoodObject(player)) {
        return UNIVERSE_DEFAULT;
    }

    /* TODO: In future, check:
     * - db[player].universe field (always available)
     * - A_UNIVERSE attribute
     * - Player's zone's universe
     * For now, everyone is in default universe
     */

    return UNIVERSE_DEFAULT;
}

/**
 * set_player_universe - Set which universe a player is in
 *
 * @param player Player dbref
 * @param universe_id Universe ID to assign
 * @return 1 on success, 0 on failure
 */
int set_player_universe(dbref player, int universe_id)
{
    /* Validate inputs */
    if (!GoodObject(player)) {
        return 0;
    }

    if (universe_id < 0 || universe_id >= num_universes) {
        return 0;
    }

    /* TODO: Implement universe assignment
     * - Set db[player].universe (always available)
     * - Set A_UNIVERSE attribute
     * For now, this is a no-op
     */

    log_important(tprintf("set_player_universe: Player #%" DBREF_FMT " assigned to universe %d",
                         player, universe_id));

    return 1;
}

/* ============================================================================
 * COMMAND LOOKUP
 * ============================================================================ */

/**
 * find_command - Find command in parser
 *
 * Tries exact match first (O(1)), then prefix match for abbreviations (O(n)).
 *
 * @param parser Parser to search
 * @param cmdstr Command string (may be abbreviated)
 * @return Pointer to command entry or NULL if not found
 */
const command_entry_t *find_command(parser_t *parser, const char *cmdstr)
{
    const command_entry_t *cmd;
    hash_iterator_t iter;
    char *key;
    void *value;
    size_t len;

    /* Validate inputs */
    if (!parser || !cmdstr || !*cmdstr) {
        return NULL;
    }

    if (!parser->commands) {
        return NULL;
    }

    /* Try exact match first (fast path - O(1)) */
    cmd = (const command_entry_t *)hash_lookup(parser->commands, cmdstr);
    if (cmd) {
        return cmd;
    }

    /* If abbreviations not allowed, stop here */
    if (!parser->syntax.allow_abbreviations) {
        return NULL;
    }

    /* Try prefix match for abbreviations (slow path - O(n)) */
    len = strlen(cmdstr);

    hash_iterate_init(parser->commands, &iter);
    while (hash_iterate_next(&iter, &key, &value)) {
        cmd = (const command_entry_t *)value;

        if (!cmd) {
            continue;
        }

        /* Check if this is a valid abbreviation */
        /* string_prefix(A, B) returns 1 if B is a prefix of A */
        if (len >= (size_t)cmd->min_length && string_prefix(cmd->name, cmdstr)) {
            return cmd;
        }
    }

    return NULL;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * list_parsers - List all registered parsers
 *
 * @param player Player to send output to
 */
void list_parsers(dbref player)
{
    int i;
    parser_t *p;

    if (!GoodObject(player)) {
        return;
    }

    notify(player, "Registered Parsers:");
    notify(player, "-------------------");

    for (i = 0; i < num_parsers; i++) {
        p = &parsers[i];
        notify(player, tprintf("%d. %s v%s - %d commands",
                              i, p->name, p->version, p->command_count));
        if (p->description) {
            notify(player, tprintf("   %s", p->description));
        }
    }

    if (num_parsers == 0) {
        notify(player, "  (none registered)");
    }

    notify(player, "-------------------");
    notify(player, tprintf("Total: %d parsers", num_parsers));
}

/**
 * list_universes - List all universes
 *
 * @param player Player to send output to
 */
void list_universes(dbref player)
{
    int i;
    universe_t *u;

    if (!GoodObject(player)) {
        return;
    }

    notify(player, "Active Universes:");
    notify(player, "-----------------");

    for (i = 0; i < num_universes; i++) {
        u = &universes[i];
        notify(player, tprintf("%d. %s (uses %s parser)",
                              u->id, u->name,
                              u->parser ? u->parser->name : "none"));
        if (u->description) {
            notify(player, tprintf("   %s", u->description));
        }
        notify(player, tprintf("   Players: %d | Starting location: #%" DBREF_FMT,
                              u->player_count, u->config.starting_location));
    }

    if (num_universes == 0) {
        notify(player, "  (none initialized)");
    }

    notify(player, "-----------------");
    notify(player, tprintf("Total: %d universes", num_universes));
}

/**
 * parser_stats - Show statistics for a parser
 *
 * @param player Player to send output to
 * @param parser Parser to analyze
 */
void parser_stats(dbref player, parser_t *parser)
{
    hash_stats_t stats;

    if (!GoodObject(player)) {
        return;
    }

    if (!parser) {
        notify(player, "Invalid parser");
        return;
    }

    notify(player, tprintf("Parser: %s v%s", parser->name, parser->version));
    notify(player, "-------------------");

    if (parser->description) {
        notify(player, tprintf("Description: %s", parser->description));
    }

    notify(player, tprintf("Commands: %d registered", parser->command_count));

    if (parser->commands) {
        hash_get_stats(parser->commands, &stats);
        notify(player, tprintf("Hash table: %zu entries, %.1f%% load factor",
                              stats.entries,
                              stats.load_factor * 100.0));
        notify(player, tprintf("  Max chain: %zu, Avg chain: %.2f",
                              stats.max_chain_length, stats.avg_chain_length));
    }

    notify(player, tprintf("Syntax: Say='%c' Pose='%c' Semipose='%c' Think='%c'",
                          parser->syntax.say_token,
                          parser->syntax.pose_token,
                          parser->syntax.semipose_token,
                          parser->syntax.think_token));

    notify(player, tprintf("Limits: Recursion=%d CommandLen=%d",
                          parser->limits.max_recursion,
                          parser->limits.max_command_length));
}

/* ============================================================================
 * END OF parser.c
 * ============================================================================ */
