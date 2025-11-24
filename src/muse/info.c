/* info.c - System Information and Statistics Display
 *
 * CONSOLIDATED (2025 reorganization):
 * All info/stats display commands moved to muse/ for proper modularity.
 *
 * From comm/info.c:
 * - do_info() - @info command (config, db, funcs, memory, mail, pid, cpu)
 * - info_cpu(), info_mem(), info_pid() - Helper functions
 *
 * From comm/dbtop.c:
 * - do_dbtop() - @dbtop command (database top rankings)
 * - do_personal_dbtop() - Personal database statistics
 * - Helper functions and structures
 *
 * From comm/pcmds.c:
 * - do_version() - @version command
 * - do_uptime() - @uptime command
 * - do_cmdav() - Command statistics
 *
 * NOTE: Command execution functions (@exec, @at, @as) remain in comm/pcmds.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "net.h"
#include "credits.h"

/* For mallinfo on systems that support it */
#ifdef __GLIBC__
#include <malloc.h>
#endif

/* ========================================================================
 * SECTION 1: @info Command (from comm/info.c)
 * ======================================================================== */

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static void info_cpu(dbref player);
static void info_mem(dbref player);

/* ===================================================================
 * Main Info Command
 * =================================================================== */

/**
 * INFO command - display various system information
 * @param player Player requesting info
 * @param arg1 Type of info to display
 */
void do_info(dbref player, char *arg1)
{
    if (!arg1 || !*arg1) {
        notify(player, "Usage: @info <type>");
        notify(player, "Available types: config, db, funcs, memory, mail"
#ifdef USE_PROC
               ", pid, cpu"
#endif
        );
        return;
    }

    if (!string_compare(arg1, "config")) {
        info_config(player);
    }
    else if (!string_compare(arg1, "db")) {
        info_db(player);
    }
    else if (!string_compare(arg1, "funcs")) {
        info_funcs(player);
    }
    else if (!string_compare(arg1, "memory")) {
        info_mem(player);
    }
    else if (!string_compare(arg1, "mail")) {
        info_mail(player);
    }
#ifdef USE_PROC
    else if (!string_compare(arg1, "pid")) {
        info_pid(player);
    }
    else if (!string_compare(arg1, "cpu")) {
        info_cpu(player);
    }
#endif
    else {
        notify(player, tprintf("Unknown info type: %s", arg1));
        notify(player, "Try: @info (with no arguments) for a list of types.");
    }
}

/* ===================================================================
 * Memory Information
 * =================================================================== */

/**
 * Display memory usage statistics
 * @param player Player to send info to
 */
static void info_mem(dbref player)
{
    notify(player, "=== Memory Statistics ===");

    /* Stack information */
    notify(player, tprintf("Stack Size/Blocks: %zu/%zu",
                          stack_size, number_stack_blocks));

    /* Text block information */
    notify(player, tprintf("Text Block Size/Count: %zu/%zu",
                          text_block_size, text_block_num));

#ifdef __GLIBC__
    /* Use mallinfo2 on newer glibc, mallinfo on older */
    #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)
    struct mallinfo2 m_info = mallinfo2();
    notify(player, tprintf("Total Allocated Memory: %zu bytes", m_info.arena));
    notify(player, tprintf("Free Allocated Memory: %zu bytes", m_info.fordblks));
    notify(player, tprintf("Free Chunks: %zu", m_info.ordblks));
    notify(player, tprintf("Used Memory: %zu bytes", m_info.uordblks));
    #else
    struct mallinfo m_info = mallinfo();
    notify(player, tprintf("Total Allocated Memory: %d bytes", m_info.arena));
    notify(player, tprintf("Free Allocated Memory: %d bytes", m_info.fordblks));
    notify(player, tprintf("Free Chunks: %d", m_info.ordblks));
    notify(player, tprintf("Used Memory: %d bytes", m_info.uordblks));
    #endif
#else
    notify(player, "Detailed memory statistics not available on this platform.");
#endif
}


/* ===================================================================
 * Process Information (Linux /proc filesystem)
 * =================================================================== */

#ifdef USE_PROC

/**
 * Display process ID and memory information from /proc
 * @param player Player to send info to
 */
void info_pid(dbref player)
{
    char filename[256];
    char buf[1024];
    FILE *fp;
    char *p;

    /* Build /proc path */
    snprintf(filename, sizeof(filename), "/proc/%d/status", getpid());

    /* Try to open /proc file */
    fp = fopen(filename, "r");
    if (!fp) {
        notify(player, tprintf("Couldn't open \"%s\" for reading!", filename));
        notify(player, "Process information not available on this system.");
        return;
    }

    /* Search for VmSize line */
    while (fgets(buf, sizeof(buf), fp)) {
        if (!strncmp(buf, "VmSize", 6)) {
            break;
        }
    }

    if (feof(fp)) {
        fclose(fp);
        notify(player, tprintf("Error reading \"%s\"!", filename));
        return;
    }

    fclose(fp);

    /* Parse the VmSize value */
    /* Remove trailing newline */
    buf[strcspn(buf, "\n")] = '\0';

    /* Skip to the value */
    p = buf;
    while (*p && !isspace(*p)) {
        p++;
    }
    while (isspace(*p)) {
        p++;
    }

    /* Display results */
    notify(player, tprintf("=== %s Process Information ===", muse_name));
    notify(player, tprintf("PID: %d", getpid()));
    notify(player, tprintf("Virtual Memory Size: %s", p));
}

/**
 * Display CPU information from /proc/cpuinfo
 * @param player Player to send info to
 */
static void info_cpu(dbref player)
{
    FILE *fp;
    char buf[256];

    fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        notify(player, "CPU information not available on this system.");
        return;
    }

    notify(player, "=== CPU Information ===");

    while (fgets(buf, sizeof(buf), fp)) {
        /* Remove trailing newline */
        buf[strcspn(buf, "\n")] = '\0';
        notify(player, buf);
    }

    fclose(fp);
}

#endif /* USE_PROC */

/* ========================================================================
 * SECTION 2: @dbtop Command (from comm/dbtop.c)
 * ======================================================================== */

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_RANKINGS 30           /* Maximum entries in ranking tables */
#define DISPLAY_RANKINGS 26       /* Number of rankings to display */
#define MIN_DISPLAY_RANKING 1     /* Start display from rank 1 */

/* ===================================================================
 * Data Structures
 * =================================================================== */

/* Structure for holding ranking data */
struct ranking_entry {
    dbref player;
    long value;
};

/* Comparison function type for qsort */
typedef int (*compare_func)(const void *, const void *);

/* Database statistics function type */
typedef long (*stat_func)(dbref);

/* Category definition for statistics */
struct stat_category {
    const char *name;
    stat_func calculator;
    const char *description;
};

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static long dt_mem(dbref obj);
static long dt_cred(dbref obj);
static long dt_cont(dbref obj);
static long dt_exits(dbref obj);
static long dt_quota(dbref obj);
static long dt_obj(dbref obj);
static long dt_numdefs(dbref obj);

static void display_rankings(dbref player, struct ranking_entry *rankings,
                            int count, const char *category_name);
static int compare_rankings(const void *a, const void *b);
static void build_rankings(struct ranking_entry *rankings, stat_func calculator);

/* ===================================================================
 * Statistics Calculation Functions
 * =================================================================== */

/**
 * Calculate number of attribute definitions for an object
 * @param obj Object to check
 * @return Number of attribute definitions
 */
static long dt_numdefs(dbref obj)
{
    ATRDEF *atrdef;
    long count = 0;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    for (atrdef = db[obj].atrdefs; atrdef; atrdef = atrdef->next) {
        count++;
    }

    return count;
}

/**
 * Calculate credits/pennies for an object
 * @param obj Object to check
 * @return Number of pennies, or -1 if not applicable
 */
static long dt_cred(dbref obj)
{
    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    return Pennies(obj);
}

/**
 * Calculate number of contents for an object
 * @param obj Object to check
 * @return Number of contents, or -1 if not applicable
 */
static long dt_cont(dbref obj)
{
    dbref item;
    long count = 0;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    if (Typeof(obj) == TYPE_EXIT || db[obj].contents == NOTHING) {
        return -1;
    }

    DOLIST(item, db[obj].contents) {
        count++;
    }

    return count;
}

/**
 * Calculate number of exits for a room
 * @param obj Object to check
 * @return Number of exits, or -1 if not a room
 */
static long dt_exits(dbref obj)
{
    dbref exit;
    long count = 0;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    if (Typeof(obj) != TYPE_ROOM || Exits(obj) == NOTHING) {
        return -1;
    }

    DOLIST(exit, Exits(obj)) {
        count++;
    }

    return count;
}

/**
 * Calculate remaining quota for a player
 * @param obj Object to check
 * @return Remaining quota, or -1 if not applicable
 */
static long dt_quota(dbref obj)
{
    const char *quota_str;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    if (Typeof(obj) != TYPE_PLAYER) {
        return -1;
    }

    if (power(obj, POW_NOQUOTA)) {
        return -1;  /* Unlimited quota */
    }

    quota_str = atr_get(obj, A_QUOTA);
    if (!quota_str || !*quota_str) {
        return 0;
    }

    return atol(quota_str);
}

/**
 * Calculate number of objects owned by a player
 * @param obj Object to check
 * @return Number of objects owned, or -1 if not a player
 */
static long dt_obj(dbref obj)
{
    const char *quota_str;
    const char *rquota_str;
    long quota, rquota;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    if (Typeof(obj) != TYPE_PLAYER) {
        return -1;
    }

    quota_str = atr_get(obj, A_QUOTA);
    rquota_str = atr_get(obj, A_RQUOTA);

    if (!quota_str || !*quota_str) {
        quota = 0;
    } else {
        quota = atol(quota_str);
    }

    if (!rquota_str || !*rquota_str) {
        rquota = 0;
    } else {
        rquota = atol(rquota_str);
    }

    return quota - rquota;
}

/**
 * Calculate memory usage for an object's owner
 * @param obj Object to check
 * @return Bytes used, or -1 if not applicable
 */
static long dt_mem(dbref obj)
{
    const char *bytes_str;

    if (obj < 0 || obj >= db_top) {
        return -1;
    }

    /* Only count owners, not objects owned by others */
    if (db[obj].owner != obj) {
        return -1;
    }

    bytes_str = atr_get(obj, A_BYTESUSED);
    if (!bytes_str || !*bytes_str) {
        return 0;
    }

    return atol(bytes_str);
}

/**
 * Calculate mail count for a player
 * Note: Requires dt_mail to be defined elsewhere (mail.c)
 * @param obj Object to check
 * @return Number of mail messages, or -1 if not applicable
 */

/* ===================================================================
 * Ranking System
 * =================================================================== */

/**
 * Comparison function for qsort - sorts by value descending
 */
static int compare_rankings(const void *a, const void *b)
{
    const struct ranking_entry *ra = (const struct ranking_entry *)a;
    const struct ranking_entry *rb = (const struct ranking_entry *)b;

    /* Sort by value descending */
    if (rb->value > ra->value) return 1;
    if (rb->value < ra->value) return -1;

    /* If equal, sort by dbref ascending for consistency */
    if (ra->player < rb->player) return -1;
    if (ra->player > rb->player) return 1;

    return 0;
}

/**
 * Build ranking table by scanning database
 * @param rankings Array to populate (must be MAX_RANKINGS size)
 * @param calculator Function to calculate stat for each object
 */
static void build_rankings(struct ranking_entry *rankings, stat_func calculator)
{
    dbref obj;
    long value;
    int i;

    /* Initialize rankings */
    for (i = 0; i < MAX_RANKINGS; i++) {
        rankings[i].player = NOTHING;
        rankings[i].value = -1;
    }

    /* Scan database */
    for (obj = 0; obj < db_top; obj++) {
        /* Skip invalid objects */
        if (Typeof(obj) == NOTYPE || (db[obj].flags & GOING)) {
            continue;
        }

        /* Calculate value */
        value = calculator(obj);

        /* Skip if not applicable or too low */
        if (value < 0) {
            continue;
        }

        /* Check if this value belongs in rankings */
        if (value > rankings[MAX_RANKINGS - 1].value) {
            /* Insert into rankings */
            rankings[MAX_RANKINGS - 1].player = obj;
            rankings[MAX_RANKINGS - 1].value = value;

            /* Re-sort to maintain order */
            qsort(rankings, MAX_RANKINGS, sizeof(struct ranking_entry),
                  compare_rankings);
        }
    }
}

/**
 * Display ranking table to player
 * @param player Player to display to
 * @param rankings Ranking data
 * @param count Number of entries to display
 * @param category_name Name of the category
 */
static void display_rankings(dbref player, struct ranking_entry *rankings,
                            int count, const char *category_name)
{
    int i;
    char header[128];
    int padding;

    if (count > MAX_RANKINGS) {
        count = MAX_RANKINGS;
    }

    /* Build centered header */
    snprintf(header, sizeof(header), " Top Rankings: %s ", category_name);
    padding = (78 - (int)strlen(header)) / 2;

    notify(player, "==============================================================================");
    notify(player, tprintf("%*s%s%*s", padding, "", header,
                          78 - padding - (int)strlen(header), ""));
    notify(player, "------------------------------------------------------------------------------");

    /* Display rankings */
    for (i = 0; i < count && rankings[i].player != NOTHING; i++) {
        /* Skip entries with no value */
        if (rankings[i].value < 0) {
            continue;
        }

        notify(player, tprintf("%2d) %s has %ld %s",
                              i + 1,
                              unparse_object(player, rankings[i].player),
                              rankings[i].value,
                              category_name));
    }

    notify(player, "==============================================================================");
}

/* ===================================================================
 * Public Command
 * =================================================================== */

/**
 * DBTOP command - display database rankings
 * @param player Player requesting rankings
 * @param arg1 Category to display
 */
void do_dbtop(dbref player, char *arg1)
{
    static struct stat_category categories[] = {
        {"numdefs",    dt_numdefs, "Number of attribute definitions"},
        {"credits",    dt_cred,    "Credits/pennies owned"},
        {"contents",   dt_cont,    "Number of contents"},
        {"exits",      dt_exits,   "Number of exits"},
        {"quota",      dt_quota,   "Remaining build quota"},
        {"objects",    dt_obj,     "Number of objects owned"},
        {"memory",     dt_mem,     "Memory bytes used"},
        {"mail",       dt_mail,    "Number of mail messages"},
        {NULL,         NULL,       NULL}
    };

    struct ranking_entry rankings[MAX_RANKINGS];
    struct stat_category *cat;
    int found = 0;

    /* Check permissions */
    if (!power(player, POW_DBTOP)) {
        notify(player, "@dbtop is a restricted command.");
        return;
    }

    /* If no argument or "all", show usage */
    if (!arg1 || !*arg1) {
        notify(player, "Usage: @dbtop <category>");
        notify(player, "");
        notify(player, "Available categories:");
        for (cat = categories; cat->name; cat++) {
            notify(player, tprintf("  %-12s - %s", cat->name, cat->description));
        }
        notify(player, "  all          - Display all categories");
        return;
    }

    /* Process categories */
    for (cat = categories; cat->name; cat++) {
        if (string_prefix(cat->name, arg1) || !strcmp(arg1, "all")) {
            found++;

            /* Build and display rankings for this category */
            build_rankings(rankings, cat->calculator);
            display_rankings(player, rankings, DISPLAY_RANKINGS, cat->name);

            /* Add spacing between categories when showing all */
            if (!strcmp(arg1, "all")) {
                notify(player, "");
            }
        }
    }

    if (found == 0) {
        notify(player, tprintf("Unknown category: %s", arg1));
        notify(player, "Use '@dbtop' with no arguments for a list of categories.");
    }
}

/* ===================================================================
 * Additional Utility Functions
 * =================================================================== */

/**
 * Get ranking position for a specific object in a category
 * @param obj Object to check
 * @param calculator Function to calculate statistic
 * @return Rank (1-based), or 0 if not ranked
 */
int get_object_rank(dbref obj, stat_func calculator)
{
    struct ranking_entry rankings[MAX_RANKINGS];
    int i;

    if (obj < 0 || obj >= db_top) {
        return 0;
    }

    build_rankings(rankings, calculator);

    for (i = 0; i < MAX_RANKINGS; i++) {
        if (rankings[i].player == obj) {
            return i + 1;
        }
    }

    return 0;  /* Not in rankings */
}

/**
 * Display personal statistics for a player
 * @param player Player requesting info
 * @param target Target to display stats for
 */
void do_personal_dbtop(dbref player, dbref target)
{
    struct stat_category categories[] = {
        {"Credits",      dt_cred,    NULL},
        {"Objects",      dt_obj,     NULL},
        {"Quota",        dt_quota,   NULL},
        {"Memory",       dt_mem,     NULL},
        {"Attr Defs",    dt_numdefs, NULL},
        {"Mail",         dt_mail,    NULL},
        {NULL,           NULL,       NULL}
    };
    struct stat_category *cat;
    long value;
    int rank;

    if (!controls(player, target, POW_EXAMINE)) {
        notify(player, "Permission denied.");
        return;
    }

    notify(player, tprintf("=== Statistics for %s ===",
                          unparse_object(player, target)));

    for (cat = categories; cat->name; cat++) {
        value = cat->calculator(target);
        rank = get_object_rank(target, cat->calculator);

        if (value >= 0) {
            if (rank > 0) {
                notify(player, tprintf("%-12s: %ld (Rank #%d)",
                                      cat->name, value, rank));
            } else {
                notify(player, tprintf("%-12s: %ld (Not ranked)",
                                      cat->name, value));
            }
        } else {
            notify(player, tprintf("%-12s: N/A", cat->name));
        }
    }
}

/* ========================================================================
 * SECTION 3: @version, @uptime, @cmdav (from comm/pcmds.c)
 * ======================================================================== */

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_RECURSION_DEPTH 10
#define COMMAND_WINDOW_SECONDS (60 * 5)  /* 5 minute window */
#define MAX_VERSION_STRING 1024
#define MAX_FORMAT_STRING 128

/* ===================================================================
 * External Variables
 * =================================================================== */

extern time_t muse_up_time;
extern time_t muse_reboot_time;

/* ===================================================================
 * Static Variables for Version and Command Tracking
 * =================================================================== */

char *upgrade_date = "01/01/25";  /* MM/DD/YY format - current modernization date */
char *base_date = "01/01/91";     /* MM/DD/YY format - original TinyMUSE base */
int day_release = 1;

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
