/* pcmds.c - Player Command Execution Utilities
 *
 * NOTE: Many functions have been moved during 2025 reorganization:
 * - Version/uptime/stats commands → muse/info.c
 *   (do_version, do_uptime, do_cmdav, inc_qcmdc, inc_pcmdc)
 * - See info.h for those declarations
 *
 * This file now focuses on:
 * - Command execution utilities (@exec, @at, @as)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "credits.h"
#include "db.h"
#include "config.h"
#include "externs.h"
#include "net.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_RECURSION_DEPTH 10

/* Temporary const casting for old APIs */
#define CONST_CAST(type, ptr) ((type)(uintptr_t)(ptr))

/* ===================================================================
 * Global Variables (for @as command)
 * =================================================================== */

dbref as_from = NOTHING;
dbref as_to = NOTHING;

/* ===================================================================
 * Administrative Commands
 * =================================================================== */

#ifdef ALLOW_EXEC
/**
 * @EXEC - Execute shell command (DISABLED FOR SECURITY)
 *
 * SECURITY WARNING: This command is permanently disabled.
 * It allowed arbitrary shell command execution which is a critical
 * remote code execution vulnerability. Never re-enable this.
 *
 * @param player Player attempting command
 * @param arg1 Target object
 * @param arg2 Command to execute
 */
void do_exec(dbref player, const char *arg1, const char *arg2)
{
    /* SECURITY: Permanently disabled due to RCE vulnerability */
    notify(player, "This command has been permanently disabled for security reasons.");
    log_security(tprintf("Attempted use of disabled @exec by %s with args: %s %s",
                        unparse_object_a(player, player),
                        arg1 ? arg1 : "(null)",
                        arg2 ? arg2 : "(null)"));
    return;

    /* Original dangerous code commented out - DO NOT UNCOMMENT
    dbref thing;
    int fd;
    int mypipe[2];
    int forkno;
    char buf[1024];
    char *bufptr;
    char *curptr;
    FILE *f;
    char fbuf[1024];

    thing = match_thing(player, arg1);
    if (thing == NOTHING) {
        return;
    }

    if (!power(player, POW_EXEC)) {
        notify(player, perm_denied());
        return;
    }

    // ... rest of dangerous code removed ...
    */
}
#endif /* ALLOW_EXEC */

/**
 * @AT - Execute command at another location
 *
 * @param player Player executing command
 * @param arg1 Target location
 * @param arg2 Command to execute
 */
void do_at(dbref player, const char *arg1, const char *arg2)
{
    dbref newloc, oldloc;
    static int depth = 0;

    if (!arg1 || !*arg1 || !arg2 || !*arg2) {
        notify(player, "Usage: @at <location>=<command>");
        return;
    }

    oldloc = db[player].location;

    if ((Typeof(player) != TYPE_PLAYER && Typeof(player) != TYPE_THING) ||
        oldloc == NOTHING || depth > MAX_RECURSION_DEPTH) {
        notify(player, perm_denied());
        return;
    }

    newloc = match_controlled(player, CONST_CAST(char *, arg1), POW_TELEPORT);
    if (newloc == NOTHING) {
        return;
    }

    /* Move player to new location */
    db[oldloc].contents = remove_first(db[oldloc].contents, player);
    PUSH(player, db[newloc].contents);
    db[player].location = newloc;

    /* Execute command */
    depth++;
    process_command(player, CONST_CAST(char *, arg2), player);
    depth--;

    /* Move back to original location */
    newloc = db[player].location;
    db[newloc].contents = remove_first(db[newloc].contents, player);
    PUSH(player, db[oldloc].contents);
    db[player].location = oldloc;
}

/**
 * @AS - Execute command as another object
 *
 * @param player Player executing command
 * @param arg1 Target object to execute as
 * @param arg2 Command to execute
 */
void do_as(dbref player, const char *arg1, const char *arg2)
{
    dbref who;
    static int depth = 0;

    if (!arg1 || !*arg1 || !arg2 || !*arg2) {
        notify(player, "Usage: @as <object>=<command>");
        return;
    }

    who = match_controlled(player, CONST_CAST(char *, arg1), POW_MODIFY);
    if (who == NOTHING) {
        return;
    }

    if (depth > 0) {
        notify(player, perm_denied());
        return;
    }

    /* Log cross-owner @as usage */
    if (db[who].owner != db[player].owner) {
        log_force(tprintf("%s uses @as on %s to execute: %s",
                         unparse_object_a(player, player),
                         unparse_object_a(who, who),
                         arg2));
    }

    /* Execute as target */
    as_from = who;
    as_to = player;
    depth++;
    process_command(who, CONST_CAST(char *, arg2), player);
    depth--;
    as_to = as_from = NOTHING;
}
