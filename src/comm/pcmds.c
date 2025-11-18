/* pcmds.c - Player utility commands
 * 
 * Version information, uptime tracking, command statistics,
 * and administrative commands (@at, @as, @exec)
 */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#define COMMAND_WINDOW_SECONDS (60 * 5)  /* 5 minute window */
#define MAX_VERSION_STRING 1024
#define MAX_FORMAT_STRING 128

/* Temporary const casting for old APIs */
#define CONST_CAST(type, ptr) ((type)(uintptr_t)(ptr))

/* ===================================================================
 * External Variables
 * =================================================================== */

extern time_t muse_up_time;
extern time_t muse_reboot_time;



char *upgrade_date = "01/01/25";  /* MM/DD/YY format - current modernization date */
char *base_date = "01/01/91";     /* MM/DD/YY format - original TinyMUSE base */
int day_release = 1;
dbref as_from = NOTHING;
dbref as_to = NOTHING;

/* ===================================================================
 * Static Variables for Command Tracking
 * =================================================================== */

static int cpos = 0;
static int qcmdcnt[COMMAND_WINDOW_SECONDS];
static int pcmdcnt[COMMAND_WINDOW_SECONDS];

/* ===================================================================
 * Safe Time Calculation Helpers
 * =================================================================== */

/**
 * Safely calculate absolute day number
 * @return Absolute day number or -1 on error
 */
static int calculate_abs_day(void)
{
    int abs_day;
    int upgrade_month, upgrade_day, upgrade_year;
    int base_month, base_day, base_year;
    
    /* Parse upgrade_date (MM/DD/YY format) */
    if (sscanf(upgrade_date, "%d/%d/%d", 
               &upgrade_month, &upgrade_day, &upgrade_year) != 3) {
        return -1;
    }
    
    /* Parse base_date */
    if (sscanf(base_date, "%d/%d/%d", 
               &base_month, &base_day, &base_year) != 3) {
        return -1;
    }
    
    /* Calculate days (simplified algorithm) */
    abs_day = (upgrade_year - 91) * 372;
    abs_day += (upgrade_month - 1) * 31;
    abs_day += upgrade_day;
    
    abs_day -= (base_year - 91) * 372;
    abs_day -= (base_month - 1) * 31;
    abs_day -= base_day;
    
    return abs_day;
}

/* ===================================================================
 * Version Information
 * =================================================================== */

/**
 * Get version string
 * @return Static version string (do not free)
 */
static const char *get_version(void)
{
    static char buf[MAX_VERSION_STRING];
    static int initialized = 0;
    static int abs_day = 0;
    
    if (!initialized) {
        abs_day = calculate_abs_day();
        if (abs_day < 0) {
            abs_day = 0;  /* Fallback */
        }
        
        snprintf(buf, sizeof(buf), "%s.%d.%d%s%s", 
                BASE_VERSION, 
                abs_day, 
                day_release - 1,
#ifdef MODIFIED
                "M",
#else
#ifdef BETA
                " beta",
#else
                "",
#endif
#endif
                BASE_REVISION);
        
        initialized = 1;
    }
    
    return buf;
}

/**
 * Display version information
 * @param player Player requesting version info
 */
void do_version(dbref player)
{
    notify(player, tprintf("%s Version Information:", muse_name));
    notify(player, tprintf("   Last Code Upgrade: %s", UPGRADE_DATE));
    notify(player, tprintf("   Version reference: %s", get_version()));
    notify(player, tprintf("   DB Format Version: v%d", DB_VERSION));
}

/* ===================================================================
 * Uptime Tracking
 * =================================================================== */

/**
 * Display server uptime
 * @param player Player requesting uptime
 */
void do_uptime(dbref player)
{
    int index = 28;
    long a, b = 0, c = 0, d = 0, t;
    static const char pattern[] = "%d days, %d hrs, %d min and %d sec";
    char format[MAX_FORMAT_STRING];
    
    /* Validate time values */
    if (now < muse_up_time) {
        notify(player, "Error: Invalid uptime data.");
        return;
    }
    
    /* Get current runtime */
    a = now - muse_up_time;
    
    /* Validate result */
    if (a < 0) {
        notify(player, "Error: Time calculation error.");
        return;
    }
    
    /* Calculate time components */
    t = a / 60L;
    if (t > 0L) {
        b = a % 60L;
        a = t;
        if (a > 0L) {
            index = 17;
        }
        
        /* Calculate minutes */
        t = a / 60L;
        if (t > 0) {
            c = b;
            b = a % 60L;
            a = t;
            if (a > 0) {
                index = 9;
            }
            
            /* Calculate hours and days */
            t = a / 24L;
            if (t > 0) {
                d = c;
                c = b;
                b = a % 24L;
                a = t;
                if (a > 0) {
                    index = 0;
                }
            }
        }
    }
    
    /* Build format string safely */
    snprintf(format, sizeof(format), "%%s %s", &pattern[index]);
    
    /* Display uptime information */
    notify(player, tprintf("%s runtime stats:", muse_name));
    notify(player, tprintf("    Muse boot time..: %s",
                          mktm(muse_up_time, "D", player)));
    notify(player, tprintf("    Last reload.....: %s",
                          mktm(muse_reboot_time, "D", player)));
    notify(player, tprintf("    Current time....: %s",
                          mktm(now, "D", player)));
    notify(player, tprintf(format, "    In operation for:",
                          (int)a, (int)b, (int)c, (int)d));
}

/* ===================================================================
 * Command Tracking
 * =================================================================== */

/**
 * Increment queue command counter
 */
void inc_qcmdc(void)
{
    if (cpos < 0 || cpos >= COMMAND_WINDOW_SECONDS) {
        cpos = 0;  /* Safety */
    }
    qcmdcnt[cpos]++;
    pcmdcnt[cpos]--;  /* Will be incremented again in process_command */
}

/**
 * Update time tracking for command statistics
 */
static void check_time(void)
{
    static struct timeval t, last;
    static struct timezone tz;
    
    gettimeofday(&t, &tz);
    
    while (t.tv_sec != last.tv_sec) {
        if (last.tv_sec - t.tv_sec > COMMAND_WINDOW_SECONDS || 
            t.tv_sec - last.tv_sec > COMMAND_WINDOW_SECONDS) {
            last.tv_sec = t.tv_sec;
        } else {
            last.tv_sec++;
        }
        
        cpos++;
        if (cpos >= COMMAND_WINDOW_SECONDS) {
            cpos = 0;
        }
        
        qcmdcnt[cpos] = pcmdcnt[cpos];
    }
}

/**
 * Increment player command counter
 */
void inc_pcmdc(void)
{
    check_time();
    
    if (cpos < 0 || cpos >= COMMAND_WINDOW_SECONDS) {
        cpos = 0;  /* Safety */
    }
    
    pcmdcnt[cpos]++;
}

/**
 * Display command average statistics
 * @param player Player requesting statistics
 */
void do_cmdav(dbref player)
{
    int len;
    
    notify(player, "Seconds  Player cmds/s   Queue cmds/s    Tot cmds/s");
    
    for (len = 5; len != 0; 
         len = ((len == 5) ? 30 : ((len == 30) ? COMMAND_WINDOW_SECONDS : 0))) {
        int i;
        int cnt;
        double pcmds = 0, qcmds = 0;
        char buf[1024];
        
        i = cpos - 1;
        cnt = len;
        
        while (cnt) {
            if (i <= 0) {
                i = COMMAND_WINDOW_SECONDS - 1;
            }
            pcmds += pcmdcnt[i];
            qcmds += qcmdcnt[i];
            i--;
            cnt--;
        }
        
        snprintf(buf, sizeof(buf), 
                "%-8d %-14f  %-14f  %f", 
                len, 
                pcmds / len, 
                qcmds / len, 
                (pcmds + qcmds) / len);
        notify(player, buf);
    }
}

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
