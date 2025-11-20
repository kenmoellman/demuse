/* economy.c - Economic System (Currency and Item Transfers)
 *
 * RELOCATED (2025 reorganization): comm/rob.c → muse/economy.c
 * Economic functions belong in muse/ as they're game mechanics.
 *
 * This file implements currency and item transfer commands:
 * - @giveto - Administrative currency transfer
 * - give - Player currency/item transfer
 */

#include <ctype.h>

#include "copyright.h"

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

/* ===================================================================
 * Static Helper Functions
 * =================================================================== */

/**
 * Check if a player is a guest
 * @param player Player to check
 * @return 1 if guest, 0 otherwise
 */
static int is_guest_player(dbref player)
{
    return Guest(real_owner(player));
}

/**
 * Validate and match a recipient for currency/item transfer
 * @param player Giver
 * @param recipient_name Name/dbref of recipient
 * @param allow_remote Allow remote matching if player has POW_REMOTE
 * @return Matched dbref or NOTHING/AMBIGUOUS on error
 */
static dbref match_recipient(dbref player, const char *recipient_name, 
                             int allow_remote)
{
    dbref who;
    
    init_match(player, recipient_name, TYPE_PLAYER);
    match_neighbor();
    match_me();
    
    if (allow_remote && power(player, POW_REMOTE)) {
        match_player(NOTHING, NULL);
        match_absolute();
    }
    
    who = match_result();
    
    if (who == NOTHING) {
        notify(player, "Give to whom?");
        return NOTHING;
    }
    
    if (who == AMBIGUOUS) {
        notify(player, "I don't know who you mean!");
        return NOTHING;
    }
    
    return who;
}

/* ===================================================================
 * Public Command Functions
 * =================================================================== */

/**
 * @GIVETO command - Administrative currency transfer
 * Allows administrators to give currency to any player
 * 
 * @param player Administrator executing command
 * @param who Target player name/dbref
 * @param amnt Amount to give (can be negative with POW_STEAL)
 */
void do_giveto(dbref player, char *who, char *amnt)
{
    long amount;
    dbref recipient;
    
    /* Check permission */
    if (!power(player, POW_MEMBER)) {
        notify(player, "You don't have permission to give out currency.");
        return;
    }
    
    /* Match recipient */
    init_match(player, who, TYPE_PLAYER);
    match_player(NOTHING, NULL);
    match_absolute();
    match_neighbor();
    
    recipient = noisy_match_result();
    if (recipient == NOTHING) {
        return;
    }
    
    /* Validate amount */
    amount = atol(amnt);
    if (amount < 1 && !has_pow(player, recipient, POW_STEAL)) {
        notify(player, "You can only give positive amounts.");
        return;
    }
    
    /* Perform transfer */
    if (!payfor(player, amount)) {
        notify(player, "You don't have enough Credits for that transfer.");
        return;
    }
    
    giveto(recipient, amount);
    notify(player, "Credits transferred.");
}

/**
 * GIVE command - Transfer credits or objects to another player
 * 
 * Supports two modes:
 * 1. "give <player>=<amount>" - Give credits
 * 2. "give <player>=<object>" - Give an object
 * 
 * @param player Giver
 * @param recipient Recipient name/dbref
 * @param amnt Amount/object to give
 */
void do_give(dbref player, char *recipient, char *amnt)
{
    dbref who;
    long amount;
    char buf[BUFFER_LEN];
    const char *s;
    
    /* Guest check */
    if (is_guest_player(player)) {
        notify(player, "Guests cannot give currency or items.");
        return;
    }
    
    /* Match recipient */
    who = match_recipient(player, recipient, 1);
    if (who == NOTHING || who == AMBIGUOUS) {
        return;
    }
    
    /* Guest recipient check */
    if (is_guest_player(who)) {
        notify(player, "Guests cannot receive currency or items.");
        return;
    }
    
    /* Check if giving an object or credits */
    /* Scan for non-digit characters (except leading minus) */
    for (s = amnt; *s && (isdigit(*s) || (*s == '-' && s == amnt)); s++)
        ;
    
    /* If non-digit found, assume it's an object */
    if (*s) {
        dbref thing;
        
        init_match(player, amnt, TYPE_THING);
        match_possession();
        match_me();
        
        thing = match_result();
        
        if (thing == NOTHING) {
            notify(player, "You don't have that!");
            return;
        }
        
        if (thing == AMBIGUOUS) {
            notify(player, "I don't know which you mean!");
            return;
        }
        
        /* Validate object transfer */
        if ((Typeof(thing) != TYPE_THING && Typeof(thing) != TYPE_PLAYER)) {
            notify(player, "You can only give things or robots.");
            return;
        }
        
        /* Check permissions */
        if (((db[who].flags & ENTER_OK) && could_doit(player, thing, A_LOCK)) ||
            controls(player, who, POW_TELEPORT)) {
            
            moveto(thing, who);
            
            notify(who, tprintf("%s gave you %s.", 
                               db[player].name, db[thing].name));
            notify(player, "Given.");
            notify(thing, tprintf("%s gave you to %s.", 
                                 db[player].name, db[who].name));
        } else {
            notify(player, "Permission denied.");
        }
        
        return;
    }
    
    /* Giving credits */
    amount = atol(amnt);
    
    /* Validate amount */
    if (amount < 1 && !has_pow(player, who, POW_STEAL)) {
        notify(player, "You must specify a positive number of Credits.");
        return;
    }
    
    /* Check recipient's maximum */
    if (!power(player, POW_STEAL)) {
        if (Pennies(who) + amount > max_pennies) {
            notify(player, "That player doesn't need that many Credits!");
            return;
        }
    }
    
    /* Try to pay */
    if (!payfor(player, amount)) {
        notify(player, "You don't have that many Credits to give!");
        return;
    }
    
    /* Handle objects with @cost */
    if (Typeof(who) == TYPE_THING) {
        long cost = atol(atr_get(who, A_COST));
        
        if (amount < cost) {
            notify(player, "That's not enough Credits.");
            giveto(player, amount);  /* Refund */
            return;
        }
        
        if (cost < 0) {
            giveto(player, amount);  /* Refund */
            return;
        }
        
        /* Calculate change */
        if (amount - cost > 0) {
            snprintf(buf, sizeof(buf), 
                    "You get %ld Credits in change.", amount - cost);
        } else {
            snprintf(buf, sizeof(buf), 
                    "You paid %ld Credits.", amount);
        }
        
        notify(player, buf);
        giveto(player, amount - cost);
        giveto(who, cost);
        did_it(player, who, A_PAY, NULL, A_OPAY, NULL, A_APAY);
        return;
    }
    
    /* Transfer to player */
    notify(player, tprintf("You give %ld Credits to %s.",
                          amount, db[who].name));
    
    if (Typeof(who) == TYPE_PLAYER) {
        notify(who, tprintf("%s gives you %ld Credits.",
                           db[player].name, amount));
    }
    
    giveto(who, amount);
    did_it(player, who, A_PAY, NULL, A_OPAY, NULL, A_APAY);
}
