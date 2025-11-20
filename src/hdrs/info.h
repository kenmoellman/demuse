/* info.h - System Information and Statistics Display Declarations
 *
 * Header file for muse/info.c
 * Declares all public info/stats display functions
 */

#ifndef _INFO_H_
#define _INFO_H_

#include "config.h"
#include "db.h"

/* ========================================================================
 * @info Command (from comm/info.c → muse/info.c)
 * ======================================================================== */

/**
 * @info - Display various system information
 * @param player Player requesting info
 * @param arg1 Type of info (config, db, funcs, memory, mail, pid, cpu)
 */
extern void do_info(dbref player, char *arg1);

/**
 * info_pid - Display process information (Linux /proc)
 * @param player Player requesting info
 */
extern void info_pid(dbref player);

/* ========================================================================
 * @dbtop Command (from comm/dbtop.c → muse/info.c)
 * ======================================================================== */

/**
 * @dbtop - Display database top rankings
 * @param player Player requesting rankings
 * @param arg1 Category to rank (credits, quota, memory, objects, etc.)
 */
extern void do_dbtop(dbref player, char *arg1);

/**
 * @dbtop personal - Display personal statistics for a player
 * @param player Player requesting stats
 * @param target Target player to show stats for
 */
extern void do_personal_dbtop(dbref player, dbref target);

/* ========================================================================
 * Version/Uptime Commands (from comm/pcmds.c → muse/info.c)
 * ======================================================================== */

/**
 * @version - Display version information
 * @param player Player requesting version info
 */
extern void do_version(dbref player);

/**
 * @uptime - Display server uptime
 * @param player Player requesting uptime
 */
extern void do_uptime(dbref player);

/**
 * @cmdav - Display command statistics
 * @param player Player requesting statistics
 */
extern void do_cmdav(dbref player);

/* ========================================================================
 * Command Tracking Functions
 * ======================================================================== */

/**
 * inc_qcmdc - Increment queue command counter
 * Called when a command is queued
 */
extern void inc_qcmdc(void);

/**
 * inc_pcmdc - Increment player command counter
 * Called when a player command is processed
 */
extern void inc_pcmdc(void);

#endif /* _INFO_H_ */
