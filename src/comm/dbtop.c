/* dbtop.c - Database statistics and top rankings
 * Located in comm/ directory
 * 
 * This file contains commands for displaying database statistics,
 * showing top rankings by various metrics (credits, quota, memory, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"

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
#ifdef POW_DBTOP
    if (!power(player, POW_DBTOP)) {
        notify(player, "@dbtop is a restricted command.");
        return;
    }
#endif
    
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
