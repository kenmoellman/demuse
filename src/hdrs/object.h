/* object.h - Object Creation, Modification, and Destruction Declarations
 *
 * Header file for db/object.c
 * Declares all public object database operation functions
 */

#ifndef _OBJECT_H_
#define _OBJECT_H_

#include "config.h"
#include "db.h"

/* ========================================================================
 * Object Creation Commands
 * ======================================================================== */

/**
 * @open - Create an exit
 * @param player Player creating the exit
 * @param direction Exit name
 * @param linkto Destination room
 * @param pseudo Pseudo-exit flag
 */
extern void do_open(dbref player, char *direction, char *linkto, dbref pseudo);

/**
 * @link - Link an exit to a destination, set home, or set drop-to
 * @param player Player executing command
 * @param name Object to link
 * @param room_name Destination
 */
extern void do_link(dbref player, char *name, char *room_name);

/**
 * @dig - Create a new room
 * @param player Player creating room
 * @param name Room name
 * @param argv Optional exit names [in, out]
 */
extern void do_dig(dbref player, char *name, char *argv[]);

/**
 * @create - Create a new thing
 * @param player Player creating thing
 * @param name Thing name
 * @param cost Pennies to spend
 */
extern void do_create(dbref player, char *name, int cost);

#ifdef USE_UNIV
/**
 * @ucreate - Create a new universe object
 * @param player Player creating object
 * @param name Object name
 * @param cost Pennies to spend
 */
extern void do_ucreate(dbref player, char *name, int cost);
#endif

/**
 * @clone - Create a copy of an object
 * @param player Player cloning object
 * @param arg1 Object to clone
 * @param arg2 Optional new name
 */
extern void do_clone(dbref player, char *arg1, char *arg2);

/**
 * @robot - Create a robot/puppet player
 * @param player Player creating robot
 * @param name Robot name
 * @param pass Robot password
 */
extern void do_robot(dbref player, char *name, char *pass);

/* ========================================================================
 * Object Modification Commands
 * ======================================================================== */

/**
 * @cname - Set colorized name (ANSI color in name)
 * @param player Player executing command
 * @param name Object to rename
 * @param cname New colorized name
 */
extern void do_cname(dbref player, char *name, char *cname);

/**
 * @name - Rename an object or player
 * @param player Player executing command
 * @param name Object to rename
 * @param cname New name
 * @param is_direct Direct call flag
 */
extern void do_name(dbref player, char *name, char *cname, int is_direct);

/**
 * @describe - Set object description
 * @param player Player executing command
 * @param name Object to describe
 * @param description Description text
 */
extern void do_describe(dbref player, char *name, char *description);

/**
 * @unlink - Unlink an exit or clear home/drop-to
 * @param player Player executing command
 * @param name Object to unlink
 */
extern void do_unlink(dbref player, char *name);

/**
 * @chown - Change object ownership
 * @param player Player executing command
 * @param name Object to transfer
 * @param newobj New owner
 */
extern void do_chown(dbref player, char *name, char *newobj);

/**
 * @unlock - Remove all locks from an object
 * @param player Player executing command
 * @param name Object to unlock
 */
extern void do_unlock(dbref player, char *name);

/**
 * @hide - Hide player from WHO list
 * @param player Player to hide
 */
extern void do_hide(dbref player);

/**
 * @unhide - Show player on WHO list
 * @param player Player to unhide
 */
extern void do_unhide(dbref player);

/**
 * mark_hearing - Mark object as hearing
 * @param obj Object to mark
 */
extern void mark_hearing(dbref obj);

/**
 * check_hearing - Process hearing flags for all objects
 */
extern void check_hearing(void);

/* ========================================================================
 * Object Destruction Commands
 * ======================================================================== */

/**
 * @destroy - Schedule an object for destruction
 * @param player Player executing command
 * @param name Object to destroy
 */
extern void do_destroy(dbref player, char *name);

/**
 * destroy_obj - Mark object for destruction (internal)
 * @param obj Object to destroy
 * @param no_seconds Skip delayed destruction if non-zero
 */
extern void destroy_obj(dbref obj, int no_seconds);

/**
 * @undestroy - Cancel scheduled object destruction
 * @param player Player executing command
 * @param arg1 Object to restore
 */
extern void do_undestroy(dbref player, char *arg1);

/**
 * @poof - Mark object for immediate destruction (no delay)
 * Sets GOING flag without A_DOOMSDAY, causing cleanup on next @dbck
 * @param player Player executing command
 * @param name Object to mark for destruction
 */
extern void do_poof(dbref player, char *name);

/**
 * free_get - Get an object from the free list
 * @return Database reference of free object, or NOTHING
 */
extern dbref free_get(void);

/**
 * @empty - Completely destroy an object (bypassing delay)
 * @param thing Object to empty/destroy
 */
extern void do_empty(dbref thing);

#endif /* _OBJECT_H_ */
