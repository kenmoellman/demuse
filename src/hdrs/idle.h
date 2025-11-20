/* idle.h - Player Idle and Away Status Declarations
 *
 * Header file for muse/idle.c
 * Declares all public idle/away status management functions
 */

#ifndef _IDLE_H_
#define _IDLE_H_

#include "config.h"
#include "db.h"
#include <time.h>

/* ========================================================================
 * Idle and Away Status Commands
 * ======================================================================== */

/**
 * @idle - Set player idle status
 * @param player Player executing command
 * @param idle Idle message/target
 */
extern void do_idle(dbref player, char *idle);

/**
 * @away - Set player away status
 * @param player Player executing command
 * @param away Away message
 */
extern void do_away(dbref player, char *away);

/**
 * set_idle_command - Set idle command wrapper
 * @param player Player to set idle
 * @param arg1 First argument
 * @param arg2 Second argument
 */
extern void set_idle_command(dbref player, char *arg1, char *arg2);

/**
 * set_idle - Internal idle status setter
 * @param player Player to mark idle
 * @param cause Cause of idle (player or object)
 * @param time Time when idle started
 * @param msg Idle message
 */
extern void set_idle(dbref player, dbref cause, time_t time, char *msg);

/**
 * set_unidle - Internal unidle status handler
 * @param player Player to mark unidle
 * @param lasttime Last idle time
 */
extern void set_unidle(dbref player, time_t lasttime);

#endif /* _IDLE_H_ */
