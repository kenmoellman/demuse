/* lstats.c - Login statistics tracking for the game
 * Originally from MAZE v1.0 with permission from itsme
 * Modernized for deMUSE 2.0
 * 
 * Tracks:
 * - Total logins over time
 * - Daily/weekly connection records
 * - Current session counts
 * - Player allowances
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"
#include "externs.h"
#include "sock.h"
#include "db.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#define STATS_DISPLAY_BUF 1024

/* ===================================================================
 * Static Variables
 * =================================================================== */

/* Previous record highs for announcement detection */
static long old_highest_week = 0;
static long old_highest_day = 0;

/* External command buffer */
extern char ccom[1024];


struct num_logins
{
  int highest_day;      /* Most logins in one day                       */
  int highest_week;     /* Most logins in one week                      */
  int highest_atonce;   /* Most players logged in at one time           */
  char date_day[10];    /* Date of the day with the most logins         */
  char date_week[10];   /* Date of the day that broke the week record   */
  char date_atonce[10]; /* Date of the day that broke the atonce record */
  int day;    /* day of week on last dump */
  time_t time; /* time in secs of last dump */
  long total;
  int today;
  int a_sun;
  int a_mon;
  int a_tue;
  int a_wed;
  int a_thu;
  int a_fri;
  int a_sat;
  int b_sun;
  int b_mon;
  int b_tue;
  int b_wed;
  int b_thu;
  int b_fri;
  int b_sat;
  int new_week_flag;
} nl;




/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static void announce_new_record(const char *message);
static int safe_remove_old_backup(long epoch);
static int safe_create_backup(long epoch);
static const char *get_stats_separator(void);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Safely announce a message to all players
 */
static void announce_new_record(const char *message)
{
    if (message && *message) {
        notify_all((char *)message, NOTHING, 1);
    }
}

/**
 * Get the display separator line
 */
static const char *get_stats_separator(void)
{
    return "-------------------------------------------------------------";
}

/**
 * Safely remove old backup file
 * @param epoch Epoch timestamp for old backup
 * @return 1 on success, 0 on failure
 */
static int safe_remove_old_backup(long epoch)
{
    char filename[256];
    int result;
    
    /* Build filename safely */
    result = snprintf(filename, sizeof(filename), 
                     "%s.%ld", LOGINSTATS_FILE, epoch - LOGINSTATS_MAX_BACKUPS);
    
    if (result < 0 || result >= (int)sizeof(filename)) {
        log_error("Filename too long in safe_remove_old_backup");
        return 0;
    }
    
    /* Remove file - ignore errors if it doesn't exist */
    if (unlink(filename) != 0 && errno != ENOENT) {
        log_error(tprintf("Failed to remove old backup: %s", filename));
        return 0;
    }
    
    return 1;
}

/**
 * Safely create backup of current stats file
 * @param epoch Epoch timestamp for backup
 * @return 1 on success, 0 on failure
 */
static int safe_create_backup(long epoch)
{
    char old_name[256];
    char new_name[256];
    int result;
    
    /* Build source filename */
    result = snprintf(old_name, sizeof(old_name), "%s.%ld", 
                     LOGINSTATS_FILE, epoch);
    if (result < 0 || result >= (int)sizeof(old_name)) {
        log_error("Source filename too long in safe_create_backup");
        return 0;
    }
    
    /* Build destination filename */
    result = snprintf(new_name, sizeof(new_name), "%s", LOGINSTATS_FILE);
    if (result < 0 || result >= (int)sizeof(new_name)) {
        log_error("Dest filename too long in safe_create_backup");
        return 0;
    }
    
    /* Copy file using rename (atomic operation) */
    if (rename(old_name, new_name) != 0) {
        log_error(tprintf("Failed to backup stats file: %s to %s", 
                         old_name, new_name));
        return 0;
    }
    
    return 1;
}

/* ===================================================================
 * Display Functions
 * =================================================================== */

/**
 * Display login statistics to a player
 * @param player Player to show stats to
 */
void do_loginstats(dbref player)
{
    struct tm *tim;
    char *day;
    char *suffix;
    char buf[STATS_DISPLAY_BUF];
    int current_logins = 0;
    int total1, total2;
    int hour;
    struct descriptor_data *d;
    const char *separator;
    
    /* Validate player */
    if (!GoodObject(player)) {
        return;
    }
    
    /* Get current time */
    tim = localtime(&now);
    if (!tim) {
        notify(player, "Error getting current time.");
        return;
    }
    
    day = get_day(now);
    suffix = mil_to_stndrd(now);
    separator = get_stats_separator();

    /* Update day check */
    check_newday();
    
    /* Calculate totals */
    total1 = nl.a_sun + nl.a_mon + nl.a_tue + nl.a_wed + nl.a_thu + nl.a_fri + nl.a_sat;
    total2 = nl.b_sun + nl.b_mon + nl.b_tue + nl.b_wed + nl.b_thu + nl.b_fri + nl.b_sat;
    
    /* Count current connections */
    for (d = descriptor_list; d; d = d->next) 
    {
        if (d->state == CONNECTED) 
	{
            current_logins++;
        }
    }
    
    /* Calculate display hour */
    hour = (tim->tm_hour % 12) ? (tim->tm_hour % 12) : 12;
    
    /* Display header */
    snprintf(buf, sizeof(buf), "%s User Login Statistics as of %s %d/%d/%d - %d:%02d:%02d%s",
             muse_name, day, tim->tm_mon + 1, tim->tm_mday, (1900 + tim->tm_year), hour, tim->tm_min, tim->tm_sec, suffix);
    notify(player, buf);
    
    /* Display total logins */
    notify(player, tprintf("\n  Total Logins: %ld", nl.total));
    
    /* Display current connections */
    notify(player, 
           tprintf("  |W!+Connections: ||Y!+Today:||G!+ %d (%3.1f%% of record) ||Y!+ Currently:||G!+ %d (%3.1f%% of record)|",
                  nl.today, (nl.highest_day ? (double)nl.today / (double)nl.highest_day * 100.0 : 0.0),
                  current_logins, (nl.highest_atonce ? (double)current_logins / (double)nl.highest_atonce * 100.0 : 0.0)));
    
    /* Display records */
    notify(player, tprintf("\n  Records:  One Day: %-6d One Week: %-6d At Once: %-6d", nl.highest_day, nl.highest_week, nl.highest_atonce));
    notify(player, tprintf("            (on %s)   (on %s)    (on %s)", nl.date_day, nl.date_week, nl.date_atonce));
    
    /* Display weekly breakdown */
    notify(player, tprintf("\n.%s.", separator));
    notify(player, "|           | Sun | Mon | Tue | Wed | Thu | Fri | Sat | Total |");
    notify(player, "|-----------|-----|-----|-----|-----|-----|-----|-----|-------|");
    
    /* This week */
    snprintf(buf, sizeof(buf), "| This Week | %3d | %3d | %3d | %3d | %3d | %3d | %3d | %5d |",
             nl.a_sun, nl.a_mon, nl.a_tue, nl.a_wed, nl.a_thu, nl.a_fri, nl.a_sat, total1);
    notify(player, buf);
    
    /* Last week */
    snprintf(buf, sizeof(buf),
             "| Last Week | %3d | %3d | %3d | %3d | %3d | %3d | %3d | %5d |",
             nl.b_sun, nl.b_mon, nl.b_tue, nl.b_wed, nl.b_thu, nl.b_fri, nl.b_sat, total2);
    notify(player, buf);
    
    notify(player, tprintf("`%s'", separator));
}

/* ===================================================================
 * File I/O Functions
 * =================================================================== */

/**
 * Write login statistics to file
 * @param epoch Current time for backup naming
 */
void write_loginstats(long epoch)
{
    FILE *fp;
    char filename[256];
    int result;
    
    /* Build filename safely */
    result = snprintf(filename, sizeof(filename), "%s.%ld", 
                     LOGINSTATS_FILE, epoch);
    
    if (result < 0 || result >= (int)sizeof(filename)) {
        log_error("Filename too long in write_loginstats");
        return;
    }
    
    /* Open file for writing */
    fp = fopen(filename, "w");
    if (!fp) {
        log_error(tprintf("Couldn't open \"%s\" for writing", filename));
        return;
    }
    
    /* Write all statistics */
    fprintf(fp, "%ld\n", now);
    fprintf(fp, "%d\n", nl.highest_day);
    fprintf(fp, "%d\n", nl.highest_week);
    fprintf(fp, "%d\n", nl.highest_atonce);
    fprintf(fp, "%s\n", nl.date_day);
    fprintf(fp, "%s\n", nl.date_week);
    fprintf(fp, "%s\n", nl.date_atonce);
    fprintf(fp, "%ld\n", nl.total);
    fprintf(fp, "%d\n", nl.today);
    fprintf(fp, "%d\n", nl.a_sun);
    fprintf(fp, "%d\n", nl.a_mon);
    fprintf(fp, "%d\n", nl.a_tue);
    fprintf(fp, "%d\n", nl.a_wed);
    fprintf(fp, "%d\n", nl.a_thu);
    fprintf(fp, "%d\n", nl.a_fri);
    fprintf(fp, "%d\n", nl.a_sat);
    fprintf(fp, "%d\n", nl.b_sun);
    fprintf(fp, "%d\n", nl.b_mon);
    fprintf(fp, "%d\n", nl.b_tue);
    fprintf(fp, "%d\n", nl.b_wed);
    fprintf(fp, "%d\n", nl.b_thu);
    fprintf(fp, "%d\n", nl.b_fri);
    fprintf(fp, "%d\n", nl.b_sat);
    
    /* Flush and close */
    fflush(fp);
    fclose(fp);
    
    /* Create backup using safe file operations */
    if (!safe_create_backup(epoch)) {
        log_error("Failed to create stats backup");
    }
    
    /* Remove old backup file */
    safe_remove_old_backup(epoch);
}

/**
 * Read login statistics from file
 */
void read_loginstats(void)
{
    FILE *fp;
    char buf[LOGINSTATS_BUF];
    struct tm *tim;
    
    /* Open file for reading */
    fp = fopen(LOGINSTATS_FILE, "r");
    if (!fp) {
        log_error(tprintf("Couldn't open \"%s\" for reading - initializing to zero",
                         LOGINSTATS_FILE));
        /* Initialize all to zero */
        nl.total = 0;
        nl.today = 0;
        nl.a_sun = nl.a_mon = nl.a_tue = nl.a_wed = 0;
        nl.a_thu = nl.a_fri = nl.a_sat = 0;
        nl.b_sun = nl.b_mon = nl.b_tue = nl.b_wed = 0;
        nl.b_thu = nl.b_fri = nl.b_sat = 0;
        nl.highest_day = 0;
        nl.highest_week = 0;
        nl.highest_atonce = 0;
        nl.date_day[0] = '\0';
        nl.date_week[0] = '\0';
        nl.date_atonce[0] = '\0';
        return;
    }
    
    /* Read saved time */
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        nl.time = atol(buf);
        tim = localtime(&nl.time);
        if (tim) {
            nl.day = tim->tm_wday;
        } else {
            nl.day = 0;
        }
    }
    
    /* Read highest day */
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        nl.highest_day = atoi(buf);
        old_highest_day = nl.highest_day;
    }
    
    /* Read highest week */
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        nl.highest_week = atoi(buf);
        old_highest_week = nl.highest_week;
    }
    
    /* Read highest at once */
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        nl.highest_atonce = atoi(buf);
    }
    
    /* Read date strings - safely */
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        buf[strcspn(buf, "\n")] = '\0';  /* Remove newline */
        strncpy(nl.date_day, buf, sizeof(nl.date_day) - 1);
        nl.date_day[sizeof(nl.date_day) - 1] = '\0';
    }
    
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        buf[strcspn(buf, "\n")] = '\0';
        strncpy(nl.date_week, buf, sizeof(nl.date_week) - 1);
        nl.date_week[sizeof(nl.date_week) - 1] = '\0';
    }
    
    if (fgets(buf, LOGINSTATS_BUF, fp)) {
        buf[strcspn(buf, "\n")] = '\0';
        strncpy(nl.date_atonce, buf, sizeof(nl.date_atonce) - 1);
        nl.date_atonce[sizeof(nl.date_atonce) - 1] = '\0';
    }
    
    /* Read statistics */
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.total = atol(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.today = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_sun = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_mon = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_tue = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_wed = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_thu = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_fri = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.a_sat = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_sun = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_mon = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_tue = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_wed = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_thu = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_fri = atoi(buf);
    if (fgets(buf, LOGINSTATS_BUF, fp)) nl.b_sat = atoi(buf);
    
    fclose(fp);
}

/* ===================================================================
 * Login Tracking Functions
 * =================================================================== */

/**
 * Add a login to statistics
 * @param player Player who logged in
 */
void add_login(dbref player)
{
    struct descriptor_data *d;
    struct tm *tim;
    int total;
    char date_buf[32];  /* Larger buffer to avoid truncation warnings */
    
    /* Validate player */
    if (!GoodObject(player)) {
        return;
    }
    
    /* Check for new day */
    check_newday();
    
    /* Get current time */
    tim = localtime(&now);
    if (!tim) {
        log_error("Failed to get localtime in add_login");
        return;
    }
    
    /* Increment appropriate day counter */
    switch (tim->tm_wday) {
        case 0: nl.a_sun++; break;
        case 1: nl.a_mon++; break;
        case 2: nl.a_tue++; break;
        case 3: nl.a_wed++; break;
        case 4: nl.a_thu++; break;
        case 5: nl.a_fri++; break;
        case 6: nl.a_sat++; break;
    }
    
    /* Increment totals */
    nl.total++;
    nl.today++;
    
    /* Check for new daily record */
    if (nl.today > nl.highest_day) {
        nl.highest_day = nl.today;
        snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%02d",
                tim->tm_mon + 1, tim->tm_mday, (tim->tm_year - 100));
        strncpy(nl.date_day, date_buf, sizeof(nl.date_day) - 1);
        nl.date_day[sizeof(nl.date_day) - 1] = '\0';
    }
    
    /* Calculate weekly total */
    total = nl.a_sun + nl.a_mon + nl.a_tue + nl.a_wed + 
            nl.a_thu + nl.a_fri + nl.a_sat;
    
    /* Check for new weekly record */
    if (total > nl.highest_week) {
        nl.highest_week = total;
        snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%02d",
                tim->tm_mon + 1, tim->tm_mday, (tim->tm_year - 100));
        strncpy(nl.date_week, date_buf, sizeof(nl.date_week) - 1);
        nl.date_week[sizeof(nl.date_week) - 1] = '\0';
    }
    
    /* Count currently connected */
    total = 0;
    for (d = descriptor_list; d; d = d->next) {
        total++;
    }
    
    /* Check for new concurrent user record */
    if (total > nl.highest_atonce) {
        /* Announce if player is not hidden */
        if (!*atr_get(player, A_LHIDE)) {
            announce_new_record(
                tprintf("** This is the most players ever connected to %s at once! "
                       "There are currently %d players connected.",
                       muse_name, total));
        }
        
        nl.highest_atonce = total;
        snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%02d",
                tim->tm_mon + 1, tim->tm_mday, (tim->tm_year - 100));
        strncpy(nl.date_atonce, date_buf, sizeof(nl.date_atonce) - 1);
        nl.date_atonce[sizeof(nl.date_atonce) - 1] = '\0';
    }
}

/* ===================================================================
 * Day Change Functions
 * =================================================================== */

/**
 * Check if it's a new day and handle rollover
 */
void check_newday(void)
{
    struct tm *tim;
    static int old_day = 8;  /* Initialize to invalid day */
    time_t new_time;
    int day;
    
    /* Check if game has been down a while */
    if (old_day == 8) {
        /* Game just started - check if stats are recent */
        if (nl.time > now - 604800) {  /* Within past week */
            old_day = nl.day;
        }
    }
    
    /* Get current day */
    new_time = time(NULL);
    tim = localtime(&new_time);
    if (!tim) {
        return;
    }
    
    day = tim->tm_wday;
    
    /* Check if day changed */
    if (day == old_day) {
        return;
    }
    
    /* Handle week rollover */
    if (day < old_day) {
        /* Save this week to last week */
        nl.b_sun = nl.a_sun;
        nl.b_mon = nl.a_mon;
        nl.b_tue = nl.a_tue;
        nl.b_wed = nl.a_wed;
        nl.b_thu = nl.a_thu;
        nl.b_fri = nl.a_fri;
        nl.b_sat = nl.a_sat;
        
        /* Clear this week */
        nl.a_sun = nl.a_mon = nl.a_tue = nl.a_wed = 0;
        nl.a_thu = nl.a_fri = nl.a_sat = 0;
        
        /* Announce if new weekly record */
        if (nl.highest_week > old_highest_week) {
            announce_new_record(
                tprintf("|R!+This was a record-breaking week! Connections this week: ||C!+%ld||R!+ Previous: ||C!+%ld|",
                       nl.highest_week, old_highest_week));
            old_highest_week = nl.highest_week;
        } else {
            announce_new_record("A new week begins!");
        }
    } else {
        /* Just a new day */
        if (nl.highest_day > old_highest_day) {
            announce_new_record(
                tprintf("|R!+This was a record-breaking day! Connections today: ||C!+%ld||R!+ Previous: ||C!+%ld|",
                       nl.highest_day, old_highest_day));
            old_highest_day = nl.highest_day;
        } else {
            announce_new_record("A new day begins!");
        }
    }
    
    /* Give allowances */
    give_allowances();
    
    /* Reset today counter */
    nl.today = 0;
    old_day = day;
    
    /* Dump database */
    log_command("Dumping.");
    fork_and_dump();
    
#ifdef USE_COMBAT
    clear_deathlist();
#endif
}

/* ===================================================================
 * Allowance System
 * =================================================================== */

/**
 * Give daily allowances to connected players
 */
void give_allowances(void)
{
    struct descriptor_data *d;
    struct plist_str {
        dbref player;
        struct plist_str *next;
    } *plist = NULL, *x, *e;
    
    /* Build list of eligible players (avoid duplicates) */
    for (d = descriptor_list; d; d = d->next) {
        int found = 0;
        
        /* Check if player qualifies */
        if (d->state != CONNECTED) {
            continue;
        }
        if (Typeof(d->player) != TYPE_PLAYER) {
            continue;
        }
        if (!power(d->player, POW_MEMBER)) {
            continue;
        }
        if (db[d->player].owner != d->player) {
            continue;  /* Skip puppets */
        }
        
        /* Check if already in list */
        for (x = plist; x; x = x->next) {
            if (x->player == d->player) {
                found = 1;
                break;
            }
        }
        
        if (!found) {
            /* Add to list */
            e = (struct plist_str *)malloc(sizeof(struct plist_str));
            if (!e) {
                log_error("Out of memory in give_allowances");
                break;
            }
            e->player = d->player;
            e->next = plist;
            plist = e;
        }
    }
    
    /* Give allowances and clean up list */
    for (x = plist; x; ) {
        struct plist_str *next = x->next;
        
        giveto(x->player, allowance);
        notify(x->player, tprintf("You collect %d credits.", allowance));
        
        free(x);
        x = next;
    }
}
