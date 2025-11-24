/* zones.h - Zone and Universe Management Declarations
 *
 * Header file for muse/zones.c
 * Declares all public zone and universe management functions
 */

#ifndef _ZONES_H_
#define _ZONES_H_

#include "config.h"
#include "db.h"

/* ========================================================================
 * Zone Iteration
 * ======================================================================== */

/**
 * Get the first zone in the hierarchy for an object
 * @param player The object to get the zone for
 * @return The first zone object, or db[0].zone if none found
 */
extern dbref get_zone_first(dbref player);

/**
 * Get the next zone in the hierarchy
 * @param player The current zone object
 * @return The next zone object, or -1 if none
 */
extern dbref get_zone_next(dbref player);

/* ========================================================================
 * Zone Management Commands
 * ======================================================================== */

/**
 * @zlink - Link a room to a zone
 * @param player Player executing command
 * @param arg1 Room to link
 * @param arg2 Zone object
 */
extern void do_zlink(dbref player, char *arg1, char *arg2);

/**
 * @unzlink - Unlink a room from its zone
 * @param player Player executing command
 * @param arg1 Room to unlink
 */
extern void do_unzlink(dbref player, char *arg1);

/**
 * @gzone - Set global zone (root only)
 * @param player Player executing command (must be root)
 * @param arg1 Zone object
 */
extern void do_gzone(dbref player, char *arg1);

/* ========================================================================
 * Universe Management Commands
 * ======================================================================== */

/**
 * @ulink - Link an object to a universe
 * @param player Player executing command
 * @param arg1 Object to link
 * @param arg2 Universe object
 */
extern void do_ulink(dbref player, char *arg1, char *arg2);

/**
 * @ununlink - Unlink an object from its universe
 * @param player Player executing command
 * @param arg1 Object to unlink
 */
extern void do_unulink(dbref player, char *arg1);

/**
 * @guniverse - Set global universe (root only)
 * @param player Player executing command (must be root)
 * @param arg1 Universe object
 */
extern void do_guniverse(dbref player, char *arg1);

/**
 * Initialize universe-specific data structures
 * @param o Object structure to initialize
 */
extern void init_universe(struct object *o);

#endif /* _ZONES_H_ */
