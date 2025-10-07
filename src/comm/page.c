/* page.c - Private messaging (page) system
 * Located in comm/ directory
 * 
 * This file contains the page command and related functions.
 * Pages are private messages sent to players anywhere in the game.
 * Supports multiple recipients, idle messages, and various formats.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "player.h"     /* For player utility functions */
#include "datetime.h"   /* For time formatting */

/* ===================================================================
 * Page System Implementation
 * =================================================================== */

/**
 * Send idle notification for a paged player
 * @param pager Player sending the page
 * @param target Player being paged who is idle
 */
static void send_idle_notification(dbref pager, dbref target)
{
    struct descriptor_data *d;
    const char *idle_msg;
    const char *idle_cur;
    time_t idle_time;
    
    /* Check if target has idle messages */
    idle_msg = Idle(target);
    idle_cur = atr_get(target, A_IDLE_CUR);
    
    if (!idle_msg && !idle_cur) {
        return;  /* No idle messages configured */
    }
    
    /* Find target's descriptor to get idle time */
    for (d = descriptor_list; d && (d->player != target); d = d->next)
        ;
    
    if (!d) {
        return;  /* Not connected? */
    }
    
    /* Only send if player is actually idle */
    if (!(db[target].flags & PLAYER_IDLE)) {
        return;
    }
    
    idle_time = now - d->last_time;
    
    /* Send appropriate idle message */
    if (idle_cur && *idle_cur) {
        /* Custom idle message */
        notify(pager,
              tprintf("|C!+Idle message from| %s |R+(||R!+%s||R+)||C!+:| %s", 
                     spname(target), time_format_2(idle_time), idle_cur));
        notify(target,
              tprintf("|W!+Your Idle message| |R+(||R!+%s||R+)||W!+ has been sent to| %s|W!+.|",
                     time_format_2(idle_time), spname(pager)));
    } else {
        /* Default idle notification */
        notify(pager,
              tprintf("%s |C!+is idle ||R+(||R!+%s||R+)|", 
                     spname(target), time_format_2(idle_time)));
        notify(target,
              tprintf("%s |W!+has been told you are ||R!+%s||W!+ idle.|",
                     spname(pager), time_format_2(idle_time)));
    }
}

/**
 * Check if a page can be sent
 * @param pager Player trying to page
 * @param target Target player
 * @return 1 if page allowed, 0 if blocked (with notifications sent)
 */
static int can_page(dbref pager, dbref target)
{
    const char *away_msg;
    const char *haven_msg;
    
    /* Check if target is connected (or a puppet that hears) */
    if ((db[target].owner == target) ? 
        (!(db[target].flags & CONNECT)) :
        !(*atr_get(target, A_APAGE) || Hearer(target))) {
        
        notify(pager, tprintf("%s isn't connected.", db[target].cname));
        
        /* Show away message if set */
        away_msg = Away(target);
        if (away_msg && *away_msg) {
            notify(pager,
                  tprintf("|C!+Away message from %s:| %s", 
                         spname(target), away_msg));
        }
        return 0;
    }
    
    /* Check page lock on target */
    if (!could_doit(pager, target, A_LPAGE)) {
        notify(pager,
              tprintf("|R+%s is not accepting pages.|", spname(target)));
        
        /* Show haven message if set */
        haven_msg = atr_get(target, A_HAVEN);
        if (haven_msg && *haven_msg) {
            notify(pager, 
                  tprintf("|R+Haven message from| %s|R+:| %s",
                         spname(target), haven_msg));
        }
        return 0;
    }
    
    /* Check reverse page lock */
    if (!could_doit(target, pager, A_LPAGE)) {
        notify(pager,
              tprintf("|R!+%s is not allowed to page you, therefore, you can't page them.|", 
                     spname(target)));
        return 0;
    }
    
    return 1;  /* Page allowed */
}

/**
 * Format and send a page message
 * @param pager Player sending the page
 * @param target Target player
 * @param message Message to send (may start with pose tokens)
 * @param hidden String to insert if pager is hidden
 */
static void send_page_message(dbref pager, dbref target, 
                             const char *message, const char *hidden)
{
    char *pager_title = title(pager);
    
    if (!message || !*message) {
        /* Location page */
        notify(target,
              tprintf("You sense that %s%sis looking for you in %s",
                     spname(pager), hidden, 
                     db[db[pager].location].cname));
        notify(pager,
              tprintf("You notified %s of your location.%s", 
                     spname(target), hidden));
        did_it(pager, target, NULL, 0, NULL, 0, A_APAGE);
    }
    else if (*message == POSE_TOKEN) {
        /* Pose page */
        notify(target, 
              tprintf("%s%spage-poses: %s %s", 
                     pager_title, hidden, spname(pager), message + 1));
        notify(pager, 
              tprintf("You page-posed %s with \"%s %s\".%s",
                     db[target].cname, spname(pager), message + 1, hidden));
        
        /* Set wildcard for puppet attribute execution */
        if (db[target].owner != target) {
            wptr[0] = (char *)message;
        }
        did_it(pager, target, NULL, 0, NULL, 0, A_APAGE);
    }
    else if (*message == NOSP_POSE) {
        /* Possessive pose page */
        notify(target, 
              tprintf("%s%spage-poses: %s's %s",
                     pager_title, hidden, spname(pager), message + 1));
        notify(pager, 
              tprintf("You page-posed %s with \"%s's %s\".%s",
                     db[target].cname, spname(pager), message + 1, hidden));
        
        if (db[target].owner != target) {
            wptr[0] = (char *)message;
        }
        did_it(pager, target, NULL, 0, NULL, 0, A_APAGE);
    }
    else if (*message == THINK_TOKEN) {
        /* Think page */
        notify(target, 
              tprintf("%s%spage-thinks: %s . o O ( %s )",
                     pager_title, hidden, spname(pager), message + 1));
        notify(pager, 
              tprintf("You page-thought %s with \"%s . o O ( %s )\".%s",
                     db[target].cname, spname(pager), message + 1, hidden));
        
        if (db[target].owner != target) {
            wptr[0] = (char *)message;
        }
        did_it(pager, target, NULL, 0, NULL, 0, A_APAGE);
    }
    else {
        /* Normal page */
        notify(target, tprintf("%s%spages: %s", 
                              pager_title, hidden, message));
        notify(pager, tprintf("You paged %s with \"%s\".%s",
                             spname(target), message, hidden));
        
        if (db[target].owner != target) {
            wptr[0] = (char *)message;
        }
        did_it(pager, target, NULL, 0, NULL, 0, A_APAGE);
    }
    
    /* Free allocated title string */
    free(pager_title);
}

/**
 * PAGE command - send a private message to another player
 * Supports multiple recipients, various formats, and idle/away messages
 * @param player Sender
 * @param arg1 Target player(s) - can be a list
 * @param arg2 Message to send
 */
void do_page(dbref player, char *arg1, char *arg2)
{
    dbref *targets;
    int k;
    const char *hidden;
    const char *lhide;
    const char *blacklist;
    
    /* Look up target players */
    targets = lookup_players(player, arg1);
    if (targets[0] == 0) {
        return;  /* No valid targets found */
    }
    
    /* Check payment for multiple pages */
    if (!payfor(player, page_cost * targets[0])) {
        notify(player, "You don't have enough Credits.");
        return;
    }
    
    /* Determine if sender is hidden */
    lhide = atr_get(player, A_LHIDE);
    blacklist = atr_get(player, A_BLACKLIST);
    
    if ((lhide && *lhide) || (blacklist && *blacklist)) {
        hidden = " (HIDDEN) ";
    } else {
        hidden = " ";
    }
    
    /* Send page to each target */
    for (k = 1; k <= targets[0]; k++) {
        /* Check if page can be sent */
        if (!can_page(player, targets[k])) {
            continue;  /* Skip this target */
        }
        
        /* Send the page message */
        send_page_message(player, targets[k], arg2, hidden);
        
        /* Send idle notification if applicable */
        send_idle_notification(player, targets[k]);
    }
}

/* ===================================================================
 * Page-related Utility Functions
 * =================================================================== */

/**
 * Set or clear page lock for a player
 * @param player Player to set lock for
 * @param lock Lock expression (NULL to clear)
 */
void do_page_lock(dbref player, const char *lock)
{
    if (lock && *lock) {
        /* Set page lock */
        atr_add(player, A_LPAGE, tprintf("%s", lock));
        notify(player, "Page lock set.");
    } else {
        /* Clear page lock */
        atr_clr(player, A_LPAGE);
        notify(player, "Page lock cleared.");
    }
}

/**
 * Check if player accepts pages from sender
 * @param receiver Player receiving page
 * @param sender Player sending page
 * @return 1 if accepted, 0 if blocked
 */
int page_check(dbref receiver, dbref sender)
{
    /* Check basic page lock */
    if (!could_doit(sender, receiver, A_LPAGE)) {
        return 0;
    }
    
    /* Check reverse lock */
    if (!could_doit(receiver, sender, A_LPAGE)) {
        return 0;
    }
    
    /* Check blacklist */
    /* TODO: Implement blacklist checking */
    
    return 1;
}

/**
 * Send a page without command processing (used by system)
 * @param from Sender
 * @param to Recipient
 * @param message Message text
 */
void page_notify(dbref from, dbref to, const char *message)
{
    if (!is_connected(from, to)) {
        return;
    }
    
    if (!page_check(to, from)) {
        return;
    }
    
    notify(to, tprintf("%s pages: %s", 
                      db[from].cname, message));
}

/* ===================================================================
 * Page History and Last Pager Tracking
 * =================================================================== */

/**
 * Record last person who paged a player
 * This would be called from send_page_message
 * @param target Player who received page
 * @param pager Player who sent page
 */
void record_last_pager(dbref target, dbref pager)
{
    /* Store in attribute for "page last" functionality */
    atr_add(target, A_LASTPAGE, tprintf("#%d", pager));
    
    /* Update timestamp */
    atr_add(target, A_LASTPTIME, tprintf("%ld", (long)time(NULL)));
}

/**
 * Get last person who paged a player
 * @param player Player to check
 * @return dbref of last pager or NOTHING
 */
dbref get_last_pager(dbref player)
{
    const char *lastpage = atr_get(player, A_LASTPAGE);
    
    if (lastpage && *lastpage == '#') {
        return atoi(lastpage + 1);
    }
    
    return NOTHING;
}

/**
 * PAGE/LAST command - page the last person who paged you
 * @param player Player executing command
 * @param message Message to send
 */
void do_page_last(dbref player, const char *message)
{
    dbref last_pager = get_last_pager(player);
    
    if (last_pager == NOTHING) {
        notify(player, "No one has paged you yet.");
        return;
    }
    
    if (!GoodObject(last_pager)) {
        notify(player, "Your last pager no longer exists.");
        return;
    }
    
    /* Use regular page with last pager as target */
    do_page(player, tprintf("#%d", last_pager), (char *)message);
}
