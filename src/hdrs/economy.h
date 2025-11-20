/* economy.h - Economic System Declarations
 *
 * Header file for muse/economy.c
 * Declares all public economic/currency functions
 */

#ifndef _ECONOMY_H_
#define _ECONOMY_H_

#include "config.h"
#include "db.h"

/* ========================================================================
 * Currency and Item Transfer Commands
 * ======================================================================== */

/**
 * @giveto - Administrative currency transfer
 * Allows administrators to give currency to any player
 *
 * @param player Administrator executing command
 * @param who Target player name/dbref
 * @param amnt Amount to give (can be negative with POW_STEAL)
 */
extern void do_giveto(dbref player, char *who, char *amnt);

/**
 * give - Transfer credits or objects to another player
 *
 * Supports two modes:
 * 1. "give <player>=<amount>" - Give credits
 * 2. "give <player>=<object>" - Give an object
 *
 * @param player Giver
 * @param recipient Recipient name/dbref
 * @param amnt Amount/object to give
 */
extern void do_give(dbref player, char *recipient, char *amnt);

#endif /* _ECONOMY_H_ */
