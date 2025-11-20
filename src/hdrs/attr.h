/* attr.h - Object Attribute Operations Declarations
 *
 * Header file for db/attr.c
 * Declares all public attribute and flag operations
 */

#ifndef _ATTR_H_
#define _ATTR_H_

#include "config.h"
#include "db.h"

/* ========================================================================
 * Attribute and Flag Commands
 * ======================================================================== */

/**
 * @set - Set object attributes or flags
 * @param player Player executing command
 * @param name Object to modify
 * @param flag Attribute or flag to set
 * @param allow_commands Allow command evaluation flag
 */
extern void do_set(dbref player, char *name, char *flag, int allow_commands);

/**
 * test_set - Check for abbreviated set command
 * @param player Player executing command
 * @param command Command string to test
 * @return 1 if handled, 0 otherwise
 */
extern int test_set(dbref player, char *command);

/**
 * parse_attrib - Parse object/attribute specification
 * @param player Player requesting parse
 * @param str String to parse (object/attribute format)
 * @param thing_out Output: parsed object dbref
 * @param locktype Attribute type for locking
 * @return Parsed attribute or NULL
 */
extern ATTR *parse_attrib(dbref player, char *str, dbref *thing_out, int locktype);

/**
 * @edit - Edit object attributes
 * @param player Player executing command
 * @param it Object to edit
 * @param argv Edit arguments [object, old, new]
 */
extern void do_edit(dbref player, char *it, char *argv[]);

/**
 * @haven - Set haven flag (safe from attacks)
 * @param player Player executing command
 * @param haven Target object
 */
extern void do_haven(dbref player, char *haven);

#endif /* _ATTR_H_ */
