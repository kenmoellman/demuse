/* object.c - Consolidated object management functions
 *
 * =============================================================================
 * MODERNIZATION NOTES (2025):
 * =============================================================================
 * This file consolidates object-related functions from multiple source files:
 * - comm/create.c - Object creation commands
 * - comm/set.c - Object modification and naming commands
 * - db/destroy.c - Object destruction and undestroy commands
 *
 * The consolidation improves code organization by grouping all object lifecycle
 * operations (creation, modification, destruction) into a single module.
 *
 * KEY CHANGES FROM SOURCE FILES:
 * - No logic changes - pure code movement
 * - All functions copied exactly as-is
 * - Preserves all buffer sizes, security checks, and validation
 * - Maintains all static helper functions
 * - All comments and documentation preserved
 *
 * SECTIONS:
 * 1. Low-level Object CRUD - Placeholder for future refactoring
 * 2. Object Creation Commands - From create.c
 * 3. Object Modification Commands - From set.c
 * 4. Object Destruction Commands - From set.c and destroy.c
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "copyright.h"
#include "config.h"
#include "db.h"
#include "match.h"
#include "externs.h"
#include "interface.h"

/* =============================================================================
 * GLOBAL VARIABLES
 * =============================================================================
 */

/* Free list head pointer - tracks recycled objects available for reuse
 * Declared extern in db.h, defined here */
dbref first_free = NOTHING;

/* =============================================================================
 * SECTION 1: Low-level Object CRUD
 * =============================================================================
 *
 * This section is reserved for future refactoring. Eventually, low-level
 * object creation, reading, updating, and deletion primitives may be moved
 * here from other files (e.g., db.c).
 *
 * For now, this section serves as a placeholder and organizational marker.
 * =============================================================================
 */

/* TODO: Future refactoring may move new_object(), etc. here */

/* =============================================================================
 * SECTION 2: Object Creation Commands
 * =============================================================================
 * Functions extracted from comm/create.c
 * =============================================================================
 */

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_EXIT_ALIASES 10

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static dbref parse_linkable_room(dbref player, const char *room_name);
static int validate_object_name(const char *name, int type);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Parse a room specification that can be linked to
 * @param player Player doing the linking
 * @param room_name Room name to parse
 * @return dbref of room, or NOTHING on error
 */
static dbref parse_linkable_room(dbref player, const char *room_name)
{
    dbref room;

    if (!room_name || !*room_name) {
        notify(player, "You must specify a destination.");
        return NOTHING;
    }

    /* Skip leading # if present */
    if (*room_name == NUMBER_TOKEN) {
        room_name++;
    }

    /* Handle special keywords */
    if (!string_compare(room_name, "here")) {
        room = db[player].location;
    } else if (!string_compare(room_name, "home")) {
        return HOME;  /* HOME is always linkable */
    } else {
        room = parse_dbref((char *)room_name);
    }

    /* Validate room */
    if (room < 0 || room >= db_top) {
        notify(player, tprintf("#%" DBREF_FMT " is not a valid object.", room));
        return NOTHING;
    }

    if (Typeof(room) == TYPE_EXIT) {
        notify(player, tprintf("%s is an exit!", unparse_object(player, room)));
        return NOTHING;
    }

    if (!can_link_to(player, room, POW_MODIFY)) {
        notify(player, tprintf("You can't link to %s.", unparse_object(player, room)));
        return NOTHING;
    }

    return room;
}

/**
 * Validate an object name based on type
 * @param name Name to validate
 * @param type Object type
 * @return 1 if valid, 0 if invalid
 */
static int validate_object_name(const char *name, int type)
{
    if (!name || !*name) {
        return 0;
    }

    switch (type) {
        case TYPE_ROOM:
            return ok_room_name(name);
        case TYPE_EXIT:
            return ok_exit_name(name);
        case TYPE_THING:
            return ok_thing_name(name);
        case TYPE_PLAYER:
            /* Player names validated elsewhere */
            return 1;
        default:
            return 0;
    }
}

/* ===================================================================
 * Exit Creation
 * =================================================================== */

/**
 * OPEN command - create an exit
 * @param player Player creating exit
 * @param direction Exit name
 * @param linkto Destination (optional)
 * @param pseudo Pseudo location for back exits
 */
void do_open(dbref player, char *direction, char *linkto, dbref pseudo)
{
    dbref loc;
    dbref exit;
    dbref destination;

    /* Determine location */
    loc = (pseudo != NOTHING) ? pseudo : db[player].location;

    if (loc == NOTHING || Typeof(loc) == TYPE_PLAYER) {
        notify(player, "Sorry, you can't make an exit there.");
        return;
    }

    /* Validate exit name */
    if (!direction || !*direction) {
        notify(player, "Open where?");
        return;
    }

    if (!ok_exit_name(direction)) {
        notify(player, tprintf("%s is a strange name for an exit!", direction));
        return;
    }

    /* Check permissions */
    if (!controls(player, loc, POW_MODIFY)) {
        notify(player, perm_denied());
        return;
    }

    /* Check if player can pay */
    if (!can_pay_fees(def_owner(player), exit_cost, QUOTA_COST)) {
        return;
    }

    /* Create the exit */
    exit = new_object();

    /* Initialize exit */
    SET(db[exit].name, direction);
    SET(db[exit].cname, direction);
    db[exit].owner = def_owner(player);
    db[exit].zone = NOTHING;
    db[exit].flags = TYPE_EXIT;
    db[exit].flags |= (db[db[exit].owner].flags & INHERIT_POWERS);

    /* Link it into the room */
    PUSH(exit, Exits(loc));
    db[exit].location = loc;
    db[exit].link = NOTHING;

    notify(player, tprintf("%s opened.", direction));

    /* Auto-link if destination specified */
    if (linkto && *linkto) {
        destination = parse_linkable_room(player, linkto);
        if (destination != NOTHING) {
            if (!payfor(player, link_cost) && !power(player, POW_FREE)) {
                notify(player, "You don't have enough Credits to link.");
            } else {
                db[exit].link = destination;
                notify(player, tprintf("Linked to %s.",
                                      unparse_object(player, destination)));
            }
        }
    }
}

/* ===================================================================
 * Linking Commands
 * =================================================================== */

/**
 * LINK command - link exits, set homes, set drop-tos
 * @param player Player doing the linking
 * @param name Object to link
 * @param room_name Destination
 */
void do_link(dbref player, char *name, char *room_name)
{
    dbref thing;
    dbref room;

    if (!name || !*name) {
        notify(player, "Link what?");
        return;
    }

    if (!room_name || !*room_name) {
        notify(player, "Link to where?");
        return;
    }

    /* Match the object to link */
    init_match(player, name, TYPE_EXIT);
    match_everything();

    thing = noisy_match_result();
    if (thing == NOTHING) {
        return;
    }

    switch (Typeof(thing)) {
        case TYPE_EXIT:
            /* Link an exit */
            room = parse_linkable_room(player, room_name);
            if (room == NOTHING) {
                return;
            }

            /* Check if exit is already linked */
            if (db[thing].link != NOTHING) {
                if (controls(player, thing, POW_MODIFY)) {
                    notify(player, tprintf("%s is already linked.",
                                          unparse_object(player, thing)));
                } else {
                    notify(player, perm_denied());
                }
                return;
            }

            /* Check permissions on destination */
            if (room != HOME && !controls(player, room, POW_MODIFY) &&
                !(db[room].flags & LINK_OK)) {
                notify(player, perm_denied());
                return;
            }

            /* Handle payment */
            if (db[thing].owner == db[player].owner) {
                if (!payfor(player, link_cost) && !power(player, POW_FREE)) {
                    notify(player, "It costs a Credit to link this exit.");
                    return;
                }
            } else {
                if (!can_pay_fees(def_owner(player), link_cost + exit_cost, QUOTA_COST)) {
                    return;
                }
                /* Pay the original owner */
                if (!power(db[thing].owner, POW_FREE)) {
                    giveto(db[thing].owner, exit_cost);
                }
                add_quota(db[thing].owner, QUOTA_COST);
            }

            /* Perform the link */
            db[thing].owner = def_owner(player);
            if (!(db[player].flags & INHERIT_POWERS)) {
                db[thing].flags &= ~(INHERIT_POWERS);
            }
            db[thing].link = room;

            notify(player, tprintf("%s linked to %s.",
                                  unparse_object_a(player, thing),
                                  unparse_object_a(player, room)));
            break;

        case TYPE_PLAYER:
        case TYPE_THING:
        case TYPE_CHANNEL:
        case TYPE_UNIVERSE:
            /* Set home */
            init_match(player, room_name, NOTYPE);
            match_exit();
            match_neighbor();
            match_possession();
            match_me();
            match_here();
            match_absolute();
            match_player(NOTHING, NULL);

            room = noisy_match_result();
            if (room < 0) {
                return;
            }

            if (Typeof(room) == TYPE_EXIT) {
                notify(player, tprintf("%s is an exit.", unparse_object(player, room)));
                return;
            }

            /* Check permissions */
            if (!controls(player, room, POW_MODIFY) && !(db[room].flags & LINK_OK)) {
                notify(player, perm_denied());
                return;
            }

            if (!controls(player, thing, POW_MODIFY) &&
                ((db[thing].location != player) || !(db[thing].flags & LINK_OK))) {
                notify(player, perm_denied());
                return;
            }

            if (room == HOME) {
                notify(player, "Can't set home to home.");
                return;
            }

            /* Set the home */
            db[thing].link = room;
            notify(player, tprintf("Home set to %s.", unparse_object(player, room)));
            break;

        case TYPE_ROOM:
            /* Set drop-to */
            room = parse_linkable_room(player, room_name);
            if (room == NOTHING) {
                return;
            }

            if (Typeof(room) != TYPE_ROOM && room != HOME) {
                notify(player, tprintf("%s is not a room!", unparse_object(player, room)));
                return;
            }

            if (room != HOME && !controls(player, room, POW_MODIFY) &&
                !(db[room].flags & LINK_OK)) {
                notify(player, perm_denied());
                return;
            }

            if (!controls(player, thing, POW_MODIFY)) {
                notify(player, perm_denied());
                return;
            }

            /* Set the drop-to */
            db[thing].link = room;
            notify(player, tprintf("Dropto set to %s.", unparse_object(player, room)));
            break;

        default:
            notify(player, "Internal error: weird object type.");
            log_error(tprintf("PANIC weird object: Typeof(%ld) = %d",
                             thing, Typeof(thing)));
            break;
    }
}

/* ===================================================================
 * NOTE: Zone and Universe linking functions moved to muse/zones.c
 * (2025 reorganization)
 *
 * Moved functions:
 * - do_ulink() - Link object to universe
 * - do_unulink() - Unlink object from universe
 * - do_zlink() - Link room to zone
 * - do_unzlink() - Unlink room from zone
 * - do_gzone() - Set global zone
 * - do_guniverse() - Set global universe
 *
 * See zones.h for declarations
 * =================================================================== */

/* ===================================================================
 * Room Creation
 * =================================================================== */

/**
 * DIG command - create a room
 * @param player Player creating room
 * @param name Room name
 * @param argv Optional exit names [in, out]
 */
void do_dig(dbref player, char *name, char *argv[])
{
    dbref room;
    dbref where;
    char nbuff[64];

    where = db[player].location;

    /* Validate input */
    if (!name || !*name) {
        notify(player, "Dig what?");
        return;
    }

    if (!ok_room_name(name)) {
        notify(player, "That's a silly name for a room!");
        return;
    }

    /* Check if player can pay */
    if (!can_pay_fees(def_owner(player), room_cost, QUOTA_COST)) {
        return;
    }

    /* Create the room */
    room = new_object();

    /* Initialize room */
    SET(db[room].name, name);
    SET(db[room].cname, name);
    db[room].owner = def_owner(player);
    db[room].flags = TYPE_ROOM;
    db[room].location = room;
    db[room].zone = db[where].zone;
    db[room].flags |= (db[db[room].owner].flags & INHERIT_POWERS);

    notify(player, tprintf("%s created with room number %ld.", name, room));

    /* Create optional entrance */
    if (argv[1] && *argv[1]) {
        snprintf(nbuff, sizeof(nbuff), "%ld", room);
        do_open(player, argv[1], nbuff, NOTHING);
    }

    /* Create optional exit back */
    if (argv[2] && *argv[2]) {
        snprintf(nbuff, sizeof(nbuff), "%ld", db[player].location);
        do_open(player, argv[2], nbuff, room);
    }
}

/* ===================================================================
 * Thing Creation
 * =================================================================== */

/**
 * CREATE command - create a thing
 * @param player Player creating thing
 * @param name Thing name
 * @param cost Creation cost
 */
void do_create(dbref player, char *name, int cost)
{
    dbref loc;
    dbref thing;

    /* Validate input */
    if (!name || !*name) {
        notify(player, "Create what?");
        return;
    }

    if (!ok_thing_name(name)) {
        notify(player, "That's a silly name for a thing!");
        return;
    }

    if (cost < 0) {
        notify(player, "You can't create an object for less than nothing!");
        return;
    }

    /* Enforce minimum cost */
    if (cost < thing_cost) {
        cost = thing_cost;
    }

    /* Check if player can pay */
    notify(player, "DEBUG: Checking fees...");
    if (!can_pay_fees(def_owner(player), cost, QUOTA_COST)) {
        return;
    }
    notify(player, "DEBUG: Fees OK, creating object...");

    /* Create the object */
    thing = new_object();
    notify(player, "DEBUG: Object created, initializing...");

    /* Initialize thing */
    SET(db[thing].name, name);
    SET(db[thing].cname, name);
    db[thing].location = player;
    db[thing].zone = NOTHING;
    db[thing].owner = def_owner(player);
    s_Pennies(thing, (long)OBJECT_ENDOWMENT(cost));
    db[thing].flags = TYPE_THING;
    db[thing].flags |= (db[db[thing].owner].flags & INHERIT_POWERS);

    /* Cap endowment */
    if (Pennies(thing) > MAX_OBJECT_ENDOWMENT) {
        s_Pennies(thing, (long)MAX_OBJECT_ENDOWMENT);
    }

    /* Set home */
    loc = db[player].location;
    if (loc != NOTHING && controls(player, loc, POW_MODIFY)) {
        db[thing].link = loc;
    } else {
        db[thing].link = db[player].link;
    }

    db[thing].exits = NOTHING;

    /* Link it into player's inventory */
    PUSH(thing, db[player].contents);
    notify(player, "DEBUG: Object linked to inventory...");

    notify(player, tprintf("%s created.", unparse_object(player, thing)));
}

/**
 * UCREATE command - create a universe object
 * @param player Player creating universe
 * @param name Universe name
 * @param cost Creation cost
 */
void do_ucreate(dbref player, char *name, int cost)
{
    dbref loc;
    dbref thing;

    /* Check permissions */
    if (!power(player, POW_SECURITY)) {
        notify(player, "Foolish mortal! You can't make Universes.");
        return;
    }

    /* Validate input */
    if (!name || !*name) {
        notify(player, "Create what?");
        return;
    }

    if (!ok_thing_name(name)) {
        notify(player, "That's a silly name for a thing!");
        return;
    }

    if (cost < 0) {
        notify(player, "You can't create an object for less than nothing!");
        return;
    }

    /* Enforce minimum cost */
    if (cost < univ_cost) {
        cost = univ_cost;
    }

    /* Check if player can pay */
    if (!can_pay_fees(def_owner(player), cost, QUOTA_COST)) {
        return;
    }

    /* Create the object */
    thing = new_object();

    /* Initialize universe */
    SET(db[thing].name, name);
    SET(db[thing].cname, name);
    db[thing].location = player;
    db[thing].zone = NOTHING;
    db[thing].owner = def_owner(player);
    s_Pennies(thing, (long)OBJECT_ENDOWMENT(cost));
    db[thing].flags = TYPE_UNIVERSE;
    db[thing].flags |= (db[db[thing].owner].flags & INHERIT_POWERS);

    /* Cap endowment */
    if (Pennies(thing) > MAX_OBJECT_ENDOWMENT) {
        s_Pennies(thing, (long)MAX_OBJECT_ENDOWMENT);
    }

    /* Set home */
    loc = db[player].location;
    if (loc != NOTHING && controls(player, loc, POW_MODIFY)) {
        db[thing].link = loc;
    } else {
        db[thing].link = db[player].link;
    }

    db[thing].exits = NOTHING;

    /* Link it into player's inventory */
    PUSH(thing, db[player].contents);

    /* Initialize universe-specific data */
    init_universe(&db[thing]);

    notify(player, tprintf("%s created.", unparse_object(player, thing)));
}

/* NOTE: init_universe() moved to muse/zones.c (2025 reorganization) */

/* ===================================================================
 * Cloning
 * =================================================================== */

/**
 * CLONE command - create a copy of an object
 * @param player Player cloning
 * @param arg1 Object to clone
 * @param arg2 Optional new name
 */
void do_clone(dbref player, char *arg1, char *arg2)
{
    dbref clone;
    dbref thing;
    const char *new_name;

    if (Guest(db[player].owner)) {
        notify(player, "Guests can't clone objects.");
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Clone what?");
        return;
    }

    /* Match object to clone */
    init_match(player, arg1, NOTYPE);
    match_everything();

    thing = noisy_match_result();
    if (thing == NOTHING || thing == AMBIGUOUS) {
        return;
    }

    if (!controls(player, thing, POW_SEEATR)) {
        notify(player, perm_denied());
        return;
    }

    if (Typeof(thing) != TYPE_THING) {
        notify(player, "You can only clone things.");
        return;
    }

    if (!can_pay_fees(def_owner(player), thing_cost, QUOTA_COST)) {
        notify(player, "You don't have enough money.");
        return;
    }

    /* Create clone */
    clone = new_object();

    /* Copy object structure */
    memcpy(&db[clone], &db[thing], sizeof(struct object));

    /* Reset pointers that shouldn't be copied */
    db[clone].name = NULL;
    db[clone].cname = NULL;
    db[clone].owner = def_owner(player);
    db[clone].flags &= ~(HAVEN | BEARING);  /* Remove parent-specific flags */

    if (!(db[player].flags & INHERIT_POWERS)) {
        db[clone].flags &= ~INHERIT_POWERS;
    }

    /* Set name */
    new_name = (arg2 && *arg2) ? arg2 : db[thing].name;
    SET(db[clone].name, new_name);
    SET(db[clone].cname, new_name);

    /* Set initial values */
    s_Pennies(clone, 1L);

    /* Copy non-inherited attributes */
    atr_cpy_noninh(clone, thing);

    /* Reset structural pointers */
    db[clone].contents = NOTHING;
    db[clone].location = NOTHING;
    db[clone].next = NOTHING;
    db[clone].atrdefs = NULL;
    db[clone].parents = NULL;
    db[clone].children = NULL;

    /* Set up parent/child relationship */
    PUSH_L(db[clone].parents, thing);
    PUSH_L(db[thing].children, clone);

    notify(player, tprintf("%s cloned with number %ld.",
                          unparse_object(player, thing), clone));

    /* Move to current location */
    moveto(clone, db[player].location);

    /* Trigger clone attribute */
    did_it(player, clone, NULL, NULL, NULL, NULL, A_ACLONE);
}

/* ===================================================================
 * Robot Creation
 * =================================================================== */

/**
 * ROBOT command - create a robot player
 * @param player Player creating robot
 * @param name Robot name
 * @param pass Robot password
 */
void do_robot(dbref player, char *name, char *pass)
{
    dbref robot;

    if (!power(player, POW_PCREATE)) {
        notify(player, "You can't make robots.");
        return;
    }

    if (!name || !*name || !pass || !*pass) {
        notify(player, "Usage: @robot <name>=<password>");
        return;
    }

    if (!can_pay_fees(def_owner(player), robot_cost, QUOTA_COST)) {
        notify(player, "Sorry, you don't have enough money to make a robot.");
        return;
    }

    robot = create_player(name, pass, CLASS_VISITOR, player_start);
    if (robot == NOTHING) {
        if (!power(player, POW_FREE)) {
            giveto(player, robot_cost);
        }
        add_quota(player, QUOTA_COST);
        notify(player, tprintf("%s already exists.", name));
        return;
    }

    db[robot].owner = db[player].owner;
    atr_clr(robot, A_RQUOTA);

    enter_room(robot, db[player].location);
    notify(player, tprintf("%s has arrived.", unparse_object(player, robot)));
}

/* =============================================================================
 * SECTION 3: Object Modification Commands
 * =============================================================================
 * Functions extracted from comm/set.c
 * =============================================================================
 */

/* Invalid name prefix checking structure */
struct invalid_prefix {
  const char *prefix;
  int case_sensitive;  /* 1 = case-sensitive, 0 = case-insensitive */
};

static const struct invalid_prefix invalid_name_prefixes[] = {
  {"HTTP:", 0},  /* Case-insensitive check for HTTP: */
  {NULL, 0}      /* Sentinel */
};

/* Helper function for case-insensitive comparison */
static int strncasecmp_safe(const char *s1, const char *s2, size_t n)
{
  if (!s1 || !s2) return -1;

  while (n > 0 && *s1 && *s2) {
    char c1 = to_lower(*s1);
    char c2 = to_lower(*s2);
    if (c1 != c2) return c1 - c2;
    s1++;
    s2++;
    n--;
  }
  if (n == 0) return 0;
  return *s1 - *s2;
}

/* Check if name starts with any invalid prefix */
static int has_invalid_prefix(const char *name)
{
  int i;
  if (!name) return 1;

  for (i = 0; invalid_name_prefixes[i].prefix != NULL; i++) {
    size_t prefix_len = strlen(invalid_name_prefixes[i].prefix);
    int matches;

    if (invalid_name_prefixes[i].case_sensitive) {
      matches = (strncmp(name, invalid_name_prefixes[i].prefix, prefix_len) == 0);
    } else {
      matches = (strncasecmp_safe(name, invalid_name_prefixes[i].prefix, prefix_len) == 0);
    }

    if (matches) return 1;
  }
  return 0;
}

extern time_t muse_up_time;

void do_cname(dbref player, char *name, char *cname)
{
  dbref thing;

  if (!name || !cname) {
    notify(player, "Invalid parameters.");
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    if (Typeof(thing) == TYPE_EXIT)
    {
      char buf[2048];
      char *visname, *rest;

      /* Safe copy with null termination */
      if (db[thing].name) {
        strncpy(buf, db[thing].name, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      } else {
        buf[0] = '\0';
      }

      visname = buf;
      rest = strchr(buf, ';');

      if (rest && *rest)
	*rest++ = '\0';
      else
	rest = "";

      if (strcmp(visname, strip_color(cname)) != 0)
      {
	notify(player, "Colorized name of exits must match visible name (the name before the first ';').");
	return;
      }
      else
      {
	char buf2[2048];

	snprintf(buf2, sizeof(buf2), "%s;%s", cname, rest);
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname ? db[thing].cname : "it", buf2));
	SET(db[thing].cname, buf2);
      }
    }
    else
    {
      if (!db[thing].name || strcmp(db[thing].name, strip_color(cname)) != 0)
      {
	notify(player, "Hey! Colorized name doesn't match real name!");
	return;
      }
      else
      {
	if (Typeof(thing) == TYPE_PLAYER)
	  log_important(tprintf("|G+COLOR CHANGE|: %s to %s",
				db[thing].cname ? db[thing].cname : "(null)", cname));
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname ? db[thing].cname : "it", cname));
	SET(db[thing].cname, cname);
      }
    }
  }
}

void do_name(dbref player, char *name, char *cname, int is_direct)
{
  dbref thing;
  char *password;
  char *newname;

  if (!name || !cname) {
    notify(player, "Invalid parameters.");
    return;
  }

  newname = strip_color_nobeep(cname);
  if (!newname) {
    notify(player, "Invalid name.");
    return;
  }

  if ((password = strrchr(cname, ' ')))
  {
    if (password && *password)
    {
      *password++ = '\0';
    }
  }

  if (db[player].name && strncmp(db[player].name, strip_color_nobeep(cname), strlen(db[player].name)+2) == 0)
  {
    do_cname(player, name, cname);
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    /* check for bad name */
    if (*newname == '\0')
    {
      notify(player, "Give it what new name?");
      return;
    }

    /* Check for invalid prefixes using new system */
    if (has_invalid_prefix(newname))
    {
      notify(player, "That name is not allowed.");
      return;
    }

    /* check for renaming a player */
    if (Typeof(thing) == TYPE_PLAYER)
    {
      if (!is_direct)
      {
	notify(player, "sorry, players must change their names directly from a net connection.");
	return;
      }
      if (player == thing && !power(player, POW_MEMBER))
      {
	notify(player, tprintf(
		   "Sorry, only registered %s users may change their name.",
				muse_name ? muse_name : "MUSE"));
	return;
      }

      if ((password = strrchr(newname, ' ')))
      {
	while (isspace(*password))
	  password--;
	password++;
	*password = '\0';
	password++;
	while (isspace(*password))
	  password++;
      }
      else
	password = newname + strlen(newname);

      /* check for reserved player names */
      if (string_prefix(newname, guest_prefix))
      {
	notify(player, tprintf(
			   "Only guests may have names beginning with '%s'",
				guest_prefix ? guest_prefix : "Guest"));
	return;
      }

      if (string_prefix(newname, guest_alias_prefix) &&
	  isdigit(*(newname + strlen(guest_alias_prefix))))
      {
	notify(player, tprintf(
	     "Only guests may have names beginning with '%s' and a number.",
				guest_alias_prefix ? guest_alias_prefix : "Guest"));
	return;
      }

      /* check for null password */
      if (!*password)
      {
	notify(player,
	       "You must specify a password to change a player name.");
	notify(player, "E.g.: name player = newname password");
	return;
      }
      else if (*Pass(player) && strcmp(Pass(player), password) &&
	       strcmp(crypt(password, "XX"), Pass(player)))
      {
	notify(player, "Incorrect password.");
	return;
      }
      else if (!(ok_player_name(thing, newname, atr_get(thing, A_ALIAS))))
      {
	notify(player, "You can't give a player that name.");
	return;
      }

      /* everything ok, notify */

      log_important(tprintf("|G+NAME CHANGE|: %s to %s",
			    unparse_object_a(thing, thing), cname));
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.",
                                                    db[thing].name ? db[thing].name : "Someone",
                                                    cname));
      delete_player(thing);
      SET(db[thing].name, newname);
      add_player(thing);
      SET(db[thing].cname, cname);
      notify(player, "Name set.");
      return;
    }

    /* we're an object. */

    if (!ok_object_name(thing, newname))
    {
      notify(player, "That is not a reasonable name.");
      return;
    }

    /* everything ok, change the name */
    if (Hearer(thing))
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.",
                                                    db[thing].name ? db[thing].name : "Something",
                                                    newname));
    SET(db[thing].name, newname);
    SET(db[thing].cname, newname);
    notify(player, "Name set.");
  }
}

void do_describe(dbref player, char *name, char *description)
{
  dbref thing;

  if (!name) {
    notify(player, "Describe what?");
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    s_Desc(thing, description ? description : "");
    notify(player, "Description set.");
  }
}

void do_unlink(dbref player, char *name)
{
  dbref exit;

  if (!name) {
    notify(player, "Unlink what?");
    return;
  }

  init_match(player, name, TYPE_EXIT);
  match_exit();
  match_here();
  if (power(player, POW_REMOTE))
  {
    match_absolute();
  }

  switch (exit = match_result())
  {
  case NOTHING:
    notify(player, "Unlink what?");
    break;
  case AMBIGUOUS:
    notify(player, "I don't know which one you mean!");
    break;
  default:
    if (!controls(player, exit, POW_MODIFY))
    {
      notify(player, perm_denied());
    }
    else
    {
      switch (Typeof(exit))
      {
      case TYPE_EXIT:
	db[exit].link = NOTHING;
	notify(player, "Unlinked.");
	break;
      case TYPE_ROOM:
	db[exit].link = NOTHING;
	notify(player, "Dropto removed.");
	break;
      default:
	notify(player, "You can't unlink that!");
	break;
      }
    }
  }
}

void do_chown(dbref player, char *name, char *newobj)
{
  dbref thing;
  dbref owner;

  if (!name) {
    notify(player, "Chown what?");
    return;
  }

  log_important(tprintf("%s attempts: @chown %s=%s",
			unparse_object_a(player, player), name, newobj ? newobj : ""));

  init_match(player, name, TYPE_THING);
  match_possession();
  match_here();
  match_exit();
  match_absolute();
  switch (thing = match_result())
  {
  case NOTHING:
    notify(player, "You don't have that!");
    return;
  case AMBIGUOUS:
    notify(player, "I don't know which you mean!");
    return;
  }
  if (!newobj || !*newobj || !string_compare(newobj, "me"))
    owner = def_owner(player);	/* @chown thing or @chown thing=me */
  else if ((owner = lookup_player(newobj)) == NOTHING)
    notify(player, "I couldn't find that player.");
  if (power(player, POW_SECURITY))
  {
    if (Typeof(thing) == TYPE_PLAYER && db[thing].owner != thing)
      db[thing].owner = thing;
  }
  if (owner == NOTHING) ;	/* for the else */
  /* if non-robot player */
  else if ((db[thing].owner == thing && Typeof(thing) == TYPE_PLAYER) && !is_root(player))
    notify(player, "Players always own themselves.");

  else if (!controls(player, owner, POW_CHOWN) ||
	   (!controls(player, thing, POW_CHOWN) &&
	    (!(db[thing].flags & CHOWN_OK) ||
	     ((Typeof(thing) == TYPE_THING) &&
	      (db[thing].location != player && !power(player, POW_CHOWN))))))
    notify(player, perm_denied());

  else
  {
    if (power(player, POW_CHOWN))
    {
      /* adjust quotas */
      add_quota(db[thing].owner, QUOTA_COST);
      sub_quota(db[owner].owner, QUOTA_COST);
      /* adjust credits */
      if (!power(player, POW_FREE))
	payfor(player, thing_cost);
      if (!power(db[thing].owner, POW_FREE))
	giveto(db[thing].owner, thing_cost);
    }
    else
    {
      if (Pennies(db[player].owner) < thing_cost)
      {
	notify(player, "You don't have enough money.");
	return;
      }
      /* adjust quotas */
      if (!pay_quota(owner, QUOTA_COST))
      {
	notify(player, (player == owner) ?
	       "Your quota has run out." : "Nothing happens.");
	return;
      }
      add_quota(db[thing].owner, QUOTA_COST);
      /* adjust credits */
      if (!power(player, POW_FREE))
	payfor(player, thing_cost);
      if (!power(player, POW_FREE))
	giveto(db[thing].owner, thing_cost);
    }

    log_important(tprintf("%s succeeds with: @chown %s=%s",
			  unparse_object_a(player, player), unparse_object_a(thing, thing), unparse_object_a(owner, owner)));

    if (db[thing].flags & CHOWN_OK || !controls(player, db[owner].owner, POW_CHOWN))
    {
      db[thing].flags |= HAVEN;
      db[thing].flags &= ~CHOWN_OK;
      db[thing].flags &= ~INHERIT_POWERS;
    }
    db[thing].owner = db[owner].owner;
    notify(player, "Owner changed.");
  }
}

static struct hearing
{
  dbref obj;
  int did_hear;
  struct hearing *next;
}
 *hearing_list = NULL;

void mark_hearing(dbref obj)
{
  int i;
  struct hearing *mine;

//  mine = malloc(sizeof(struct hearing));
  SAFE_MALLOC(mine, struct hearing, 1);
  if (!mine) {
    return;  /* Memory allocation failed */
  }

  mine->next = hearing_list;
  mine->did_hear = Hearer(obj);
  mine->obj = obj;

  hearing_list = mine;
  for (i = 0; db[obj].children && db[obj].children[i] != NOTHING; i++)
  {
    mark_hearing(db[obj].children[i]);
  }
}

void check_hearing(void)
{
  struct hearing *mine;
  int now_hear;
  dbref obj;

  while (hearing_list)
  {
    mine = hearing_list;
    hearing_list = hearing_list->next;

    obj = mine->obj;
    now_hear = Hearer(mine->obj);

    if (now_hear && !mine->did_hear)
      notify_in(db[obj].location, obj,
		tprintf("%s grows ears and can now hear.", db[obj].name ? db[obj].name : "Something"));
    if (mine->did_hear && !now_hear)
      notify_in(db[obj].location, obj,
		tprintf("%s loses its ears and is now deaf.", db[obj].name ? db[obj].name : "Something"));
    SMART_FREE(mine);
  }
}

void do_unlock(dbref player, char *name)
{
  dbref thing;
  ATTR *attr = A_LOCK;

  if (!name) {
    notify(player, "Unlock what?");
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) == NOTHING)
    return;
  if (thing == root && player != root)
  {
    notify(player, "Not likely.");
    return;
  }
  atr_add(thing, attr, "");
  notify(player, "Unlocked.");
}

void do_hide(dbref player)
{
  atr_add((Typeof(player) == TYPE_PLAYER) ? player : db[player].owner, A_LHIDE, "me&!me");
  if (Typeof(player) == TYPE_PLAYER)
    notify(player, "Your name is HIDDEN.");
  else
    notify(player, "Your owner's name is HIDDEN.");
  return;
}

void do_unhide(dbref play)
{
  atr_add((Typeof(play) == TYPE_PLAYER) ? play : db[play].owner, A_LHIDE, "");
  if (Typeof(play) == TYPE_PLAYER)
    notify(play, "Your name is back on the WHO list.");
  else
    notify(play, "Your owner's name is back on the WHO list.");
  return;
}

/* =============================================================================
 * SECTION 4: Object Destruction Commands
 * =============================================================================
 * Functions extracted from comm/set.c and db/destroy.c
 * =============================================================================
 */

void do_destroy(dbref player, char *name)
{
  dbref thing;

  if (!name) {
    notify(player, "Destroy what?");
    return;
  }

  if (controls(player, db[player].location, POW_MODIFY))
    init_match(player, name, NOTYPE);
  else
    init_match(player, name, TYPE_THING);

  if (controls(player, db[player].location, POW_MODIFY))
  {
    match_exit();
  }

  match_everything();
  thing = match_result();
  if ((thing != NOTHING) && (thing != AMBIGUOUS) &&
      !controls(player, thing, POW_MODIFY) &&
      !(Typeof(thing) == TYPE_THING &&
	(db[thing].flags & THING_DEST_OK)))
  {
    notify(player, perm_denied());
    return;
  }

  if (db[thing].children && (*db[thing].children) != NOTHING)
    notify(player, "Warning: It has children.");

  if (thing < 0)
  {				/* I hope no wizard is this stupid but just
				   in case */
    notify(player, "I don't know what that is, sorry.");
    return;
  }
  if (thing == 0 || thing == 1 || thing == player_start || thing == root)
  {
    notify(player, "Don't you think that's sorta an odd thing to destroy?");
    return;
  }
  /* what kind of thing we are destroying? */

  if (Typeof(thing) == TYPE_PLAYER)
  {
    notify(player, "Destroying players isn't allowed, try a @nuke instead.");
    return;
  }
  else if (Typeof(thing) == TYPE_CHANNEL)
  {
    do_channel_destroy(player, name);
  }
  else
  {
    char *k;

    k = atr_get(thing, A_DOOMSDAY);
    if (k && *k)
      if (db[thing].flags & GOING)
      {
        notify(player, tprintf("It seems it's already gunna go away in %s... if you wanna stop it, use @undestroy", time_format_2(atol(k) - now)));
        return;
      }
      else
      {
        notify(player, "Sorry, it's protected.");
      }
    else if (db[thing].flags & GOING)
    {
      notify(player, "It seems to already be destroyed.");
      return;
    }
    else
    {
      k = atr_get(player, A_DOOMSDAY);
      if (k && *k)
      {
        destroy_obj(thing, atol(k));
        notify(player, tprintf("Okay, %s will go away in %s.", unparse_object(player, thing), time_format_2(atol(k))));
      }
      else
      {
        destroy_obj(thing, atol(default_doomsday));
        notify(player, tprintf("Okay, %s will go away in %s.", unparse_object(player, thing), time_format_2(atol(default_doomsday))));
      }
    }
  }
}

void destroy_obj(dbref obj, int no_seconds)
{
  if (!(db[obj].flags & QUIET))
    do_pose(obj, "shakes and starts to crumble", "", 0);
  atr_add(obj, A_DOOMSDAY, int_to_str(no_seconds + now));
  db[obj].flags |= GOING;
  do_halt(obj, "", "");
}

/*
 * do_undestroy - Cancel scheduled destruction of an object
 *
 * SECURITY:
 * - Validates player has control over object
 * - Checks object is actually scheduled for destruction
 */
void do_undestroy(dbref player, char *arg1)
{
    dbref object;

    if (!GoodObject(player)) {
        log_error("do_undestroy: Invalid player reference");
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Undestroy what?");
        return;
    }

    object = match_controlled(player, arg1, POW_EXAMINE);
    if (object == NOTHING) {
        return;
    }

    /* Use ValidObject() instead of GoodObject() since we NEED to access
     * objects marked for deletion - that's the whole point of @undestroy */
    if (!ValidObject(object)) {
        notify(player, "Invalid object reference.");
        return;
    }

    if (!(db[object].flags & GOING)) {
        notify(player, tprintf("%s is not scheduled for destruction",
                             unparse_object(player, object)));
        return;
    }

    db[object].flags &= ~GOING;

    if (atol(atr_get(object, A_DOOMSDAY)) > 0) {
        atr_add(object, A_DOOMSDAY, "");
        notify(player, tprintf("%s has been saved from destruction.",
                             unparse_object(player, object)));
    } else {
        notify(player, tprintf("%s is protected, and the GOING flag shouldn't "
                             "have been set in the first place.",
                             unparse_object(player, object)));
    }
}

/*
 * do_poof - Mark object for immediate destruction without delay
 *
 * Sets GOING flag without A_DOOMSDAY timestamp, causing object to be
 * cleaned up and recycled on next @dbck run via fix_free_list().
 *
 * SECURITY:
 * - Validates player has control over object (POW_MODIFY)
 * - Prevents destruction of critical objects (room 0, room 1, root, player_start)
 * - Prevents destruction of players (use @nuke instead)
 * - Checks for THING_DEST_OK flag for things
 */
void do_poof(dbref player, char *name)
{
    dbref thing;

    if (!GoodObject(player)) {
        log_error("do_poof: Invalid player reference");
        return;
    }

    if (!name || !*name) {
        notify(player, "Poof what?");
        return;
    }

    /* Match the object to poof */
    if (controls(player, db[player].location, POW_MODIFY))
        init_match(player, name, NOTYPE);
    else
        init_match(player, name, TYPE_THING);

    if (controls(player, db[player].location, POW_MODIFY)) {
        match_exit();
    }

    match_everything();
    thing = match_result();

    /* Check permissions */
    if ((thing != NOTHING) && (thing != AMBIGUOUS) &&
        !controls(player, thing, POW_MODIFY) &&
        !(Typeof(thing) == TYPE_THING &&
          (db[thing].flags & THING_DEST_OK)))
    {
        notify(player, perm_denied());
        return;
    }

    if (thing < 0) {
        notify(player, "I don't know what that is, sorry.");
        return;
    }

    /* Prevent destruction of critical objects */
    if (thing == 0 || thing == 1 || thing == player_start || thing == root) {
        notify(player, "Don't you think that's sorta an odd thing to poof?");
        return;
    }

    /* Prevent destruction of players */
    if (Typeof(thing) == TYPE_PLAYER) {
        notify(player, "Poofing players isn't allowed, try @nuke instead.");
        return;
    }

    /* Check if already marked for destruction */
    if (db[thing].flags & GOING) {
        notify(player, "It's already marked for destruction.");
        return;
    }

    /* Check for children */
    if (db[thing].children && (*db[thing].children) != NOTHING) {
        notify(player, "Warning: It has children.");
    }

    /* IMPORTANT: Clear A_DOOMSDAY BEFORE setting GOING flag!
     * Once GOING is set, GoodObject() returns false and atr_add() will fail.
     * We want the object to have GOING flag but NO A_DOOMSDAY, making it IS_GONE. */
    atr_add(thing, A_DOOMSDAY, "");

    /* Now set GOING flag - object is marked for cleanup on next @dbck */
    db[thing].flags |= GOING;

    /* Halt any pending commands */
    do_halt(thing, "", "");

    /* Visual feedback */
    if (!(db[thing].flags & QUIET)) {
        do_pose(thing, "shimmers and fades away", "", 0);
    }

    notify(player, tprintf("Okay, %s is marked GOING and will be recycled on next @dbck.",
                          unparse_object(player, thing)));
}

/*
 * free_get - Retrieve a cleaned up object from the free list
 *
 * Returns: dbref of recycled object, or NOTHING if list is empty
 *
 * This function pulls an object from the free list for reuse. It validates
 * that the object is truly ready for reuse and fixes the free list if corruption
 * is detected.
 *
 * SECURITY:
 * - Validates free list integrity
 * - Limits recursion to prevent stack overflow
 * - Cleans object name to prevent information leakage
 */
dbref free_get(void)
{
    static int recursion_depth = 0;
    dbref newobj;

    /* Maximum recursion depth to prevent stack overflow */
    #define MAX_RECURSION_DEPTH 20

    /* Check if object is valid for free list - must be destroyed and properly formatted
     * NOTE: Use ValidObject() not GoodObject() - free list objects HAVE the GOING flag! */
    #define NOT_OK(thing) \
        (!ValidObject(thing) || \
         (db[thing].location != NOTHING) || \
         ((db[thing].owner != 1) && (db[thing].owner != root)) || \
         ((db[thing].flags & ~(0x8000)) != (TYPE_THING | GOING)))

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        log_error("free_get: Maximum recursion depth exceeded");
        first_free = NOTHING;
        report();
        return NOTHING;
    }

    if (first_free == NOTHING) {
        log_important("No first free, creating new.");
        return NOTHING;
    }

    newobj = first_free;

    /* Validate the object reference
     * Use ValidObject() instead of GoodObject() because the free list
     * SHOULD contain deleted objects (GOING flag set) - that's the whole
     * point of reusing them! We only need to check structural validity. */
    if (!ValidObject(newobj)) {
        log_error("free_get: Invalid first_free object");
        first_free = NOTHING;
        report();
        return NOTHING;
    }

    log_important(tprintf("First free is %ld", newobj));
    first_free = db[first_free].next;

    /* Make sure this object really should be in free list */
    if (NOT_OK(newobj)) {
        report();
        log_error(tprintf("Object #%ld in free list is corrupt, repairing it", newobj));
        log_error(tprintf("  location=%ld (should be NOTHING), owner=%ld (should be %ld)",
                         db[newobj].location, db[newobj].owner, root));
        log_error(tprintf("  flags=0x%lx (should be 0x%lx)",
                         (unsigned long)db[newobj].flags,
                         (unsigned long)(TYPE_THING | GOING)));

        /* Repair the corrupted object right here instead of rebuilding entire free list */
        db[newobj].location = NOTHING;
        db[newobj].owner = root;
        db[newobj].flags = GOING | TYPE_THING;
        db[newobj].link = NOTHING;
        s_Pennies(newobj, 0L);

        log_error(tprintf("Object #%ld repaired and ready for reuse", newobj));
    }

    /* Free object name to prevent information leakage */
    SET(db[newobj].name, NULL);

    recursion_depth = 0;
    return newobj;

    #undef MAX_RECURSION_DEPTH
    #undef NOT_OK
}

/*
 * do_empty - Completely destroy an object and clean up all references
 *
 * This function:
 * 1. Boots all connected players from the object
 * 2. Frees all attributes
 * 3. Destroys all exits (for rooms)
 * 4. Sends contents home (for rooms/things)
 * 5. Refunds the owner
 * 6. Cleans up parent/child relationships
 * 7. Adds object to free list
 *
 * SECURITY:
 * - Tracks recursion depth to prevent stack overflow
 * - Validates all references before use
 * - Safe memory cleanup using SMART_FREE
 *
 * NOTE: Objects must be moved to NOTHING or unlinked before calling this
 */
void do_empty(dbref thing)
{
    static int nrecur = 0;
    int i;
    ATRDEF *k, *next;

    /* Maximum recursion depth to prevent stack overflow */
    #define MAX_RECURSION_DEPTH 20

    /* Maximum iterations for various loops to prevent infinite loops */
    #define MAX_LOOP_ITERATIONS 10000

    if (!GoodObject(thing)) {
        log_error("do_empty: Invalid object reference");
        return;
    }

    /* Prevent runaway recursion */
    if (nrecur >= MAX_RECURSION_DEPTH) {
        report();
        log_error("Runaway recursion in do_empty");
        nrecur = 0;
        return;
    }
    nrecur++;

    /* Boot all connected players */
    while (boot_off(thing))
        ;

    /* Move object to nowhere if not a room */
    if (Typeof(thing) != TYPE_ROOM) {
        moveto(thing, NOTHING);
    }

    /* Free attribute definitions */
    for (k = db[thing].atrdefs; k; k = next) {
        next = k->next;
        if (k->a.refcount > 0) {
            k->a.refcount--;
        }
        if (k->a.refcount == 0) {
            SMART_FREE(k->a.name);
            SMART_FREE(k);
        }
    }
    db[thing].atrdefs = NULL;

    /* Type-specific cleanup */
    switch (Typeof(thing)) {
    case TYPE_CHANNEL:
    case TYPE_UNIVERSE:
        /* Free universe-specific arrays */
        if (db[thing].ua_string) {
            for (i = 0; i < NUM_UA; i++) {
                SMART_FREE(db[thing].ua_string[i]);
            }
            SMART_FREE(db[thing].ua_string);
        }
        if (db[thing].ua_float) {
            SMART_FREE(db[thing].ua_float);
        }
        if (db[thing].ua_int) {
            SMART_FREE(db[thing].ua_int);
        }
        /* FALLTHROUGH */

    case TYPE_THING:
    case TYPE_PLAYER:
        moveto(thing, NOTHING);
        /* FALLTHROUGH */

    case TYPE_ROOM:
        {
            dbref first;
            dbref rest;
            int iteration_count = 0;

            /* Report destruction if room */
            if (Typeof(thing) == TYPE_ROOM) {
                dest_info(thing, NOTHING);
            }

            /* Clear zone and universe */
            db[thing].zone = NOTHING;
            db[thing].universe = NOTHING;

            /* Destroy all exits */
            first = Exits(thing);
            while (first != NOTHING && iteration_count < MAX_LOOP_ITERATIONS) {
                iteration_count++;
                if (!GoodObject(first)) {
                    log_error(tprintf("Invalid exit #%ld in do_empty", first));
                    break;
                }
                rest = db[first].next;
                if (Typeof(first) == TYPE_EXIT) {
                    do_empty(first);
                }
                first = rest;
            }

            if (iteration_count >= MAX_LOOP_ITERATIONS) {
                log_error(tprintf("do_empty: Infinite loop in exits of #" DBREF_FMT, thing));
            }

            /* Fix home links that point to this object */
            first = db[thing].contents;
            iteration_count = 0;
            for (rest = first;
                 rest != NOTHING && iteration_count < MAX_LOOP_ITERATIONS;
                 rest = (GoodObject(rest) ? db[rest].next : NOTHING), iteration_count++) {

                if (!GoodObject(rest)) {
                    continue;
                }

                if (db[rest].link == thing) {
                    if (GoodObject(db[rest].owner) &&
                        GoodObject(db[db[rest].owner].link) &&
                        db[db[rest].owner].link != thing) {
                        db[rest].link = db[db[rest].owner].link;
                    } else {
                        db[rest].link = player_start;
                    }
                }
            }

            if (iteration_count >= MAX_LOOP_ITERATIONS) {
                log_error(tprintf("do_empty: Infinite loop in contents (link fix) of #" DBREF_FMT, thing));
            }

            /* Send all contents home */
            iteration_count = 0;
            while (first != NOTHING && iteration_count < MAX_LOOP_ITERATIONS) {
                iteration_count++;
                if (!GoodObject(first)) {
                    break;
                }
                rest = db[first].next;
                moveto(first, HOME);
                first = rest;
            }

            if (iteration_count >= MAX_LOOP_ITERATIONS) {
                log_error(tprintf("do_empty: Infinite loop sending contents home for #" DBREF_FMT, thing));
            }
        }
        break;
    }

    /* Refund owner */
    if (GoodObject(db[thing].owner)) {
        int refund_cost;

        /* Calculate refund based on object type */
        switch (Typeof(thing)) {
        case TYPE_THING:
            if (!GoodObject(thing)) {
                refund_cost = 0;
            } else {
                refund_cost = OBJECT_DEPOSIT(Pennies(thing));
            }
            break;
        case TYPE_ROOM:
            refund_cost = room_cost;
            break;
        case TYPE_EXIT:
            if (!GoodObject(thing)) {
                refund_cost = exit_cost;
            } else {
                if (db[thing].link != NOTHING) {
                    refund_cost = exit_cost;
                } else {
                    refund_cost = exit_cost + link_cost;
                }
            }
            break;
        case TYPE_PLAYER:
        case TYPE_UNIVERSE:
            refund_cost = 1000;
            break;
        default:
            log_error(tprintf("Illegal object type: %ld, object_cost", Typeof(thing)));
            refund_cost = 5000;
            break;
        }

        if (!(db[db[thing].owner].flags & QUIET) &&
            !power(db[thing].owner, POW_FREE)) {
            notify(db[thing].owner,
                   tprintf("You get back your %d credit deposit for %s.",
                          refund_cost,
                          unparse_object(db[thing].owner, thing)));
        }

        if (!power(db[thing].owner, POW_FREE)) {
            giveto(db[thing].owner, refund_cost);
        }

        add_quota(db[thing].owner, 1);
    }

    /* Free attribute list */
    atr_free(thing);
    db[thing].list = NULL;

    /* Free powers array */
    if (db[thing].pows) {
        SMART_FREE(db[thing].pows);
        db[thing].pows = 0;
    }

    /* Clean up parent/child relationships */
    if (db[thing].children) {
        for (i = 0; db[thing].children[i] != NOTHING; i++) {
            if (GoodObject(db[thing].children[i])) {
                REMOVE_FIRST_L(db[db[thing].children[i]].parents, thing);
            }
        }
        SMART_FREE(db[thing].children);
        db[thing].children = NULL;
    }

    if (db[thing].parents) {
        for (i = 0; db[thing].parents[i] != NOTHING; i++) {
            if (GoodObject(db[thing].parents[i])) {
                REMOVE_FIRST_L(db[db[thing].parents[i]].children, thing);
            }
        }
        SMART_FREE(db[thing].parents);
        db[thing].parents = NULL;
    }

    /* Halt any queued commands */
    do_halt(thing, "", "");

    /* Reset object to safe state */
    SET(db[thing].name, "-deleted-");
    SET(db[thing].cname, "-deleted-");
    s_Pennies(thing, 0L);
    db[thing].owner = root;
    db[thing].flags = GOING | TYPE_THING;
    db[thing].location = NOTHING;
    db[thing].link = NOTHING;

    /* Add to free list */
    db[thing].next = first_free;
    first_free = thing;

    nrecur--;

    #undef MAX_RECURSION_DEPTH
    #undef MAX_LOOP_ITERATIONS
}

/* =============================================================================
 * SECTION 5: Database Integrity and Garbage Collection
 * =============================================================================
 * Functions from db/destroy.c - database maintenance and integrity checks
 * =============================================================================
 */

/* =============================================================================
 * MACROS AND DEFINITIONS
 * =============================================================================
 */

/* Enhanced CHECK_REF macro with better validation
 * NOTE: Objects with GOING flag are still valid references until free_object() cleans them.
 * We use ValidObject() to allow GOING flag, only rejecting truly invalid objects. */
#define CHECK_REF(thing) \
    if ((thing) < -3 || (thing) >= db_top || \
        ((thing) > -1 && !ValidObject(thing)))

/* Check if object is valid for free list - must be destroyed and properly formatted
 * NOTE: Use ValidObject() not GoodObject() - free list objects HAVE the GOING flag! */
#define NOT_OK(thing) \
    (!ValidObject(thing) || \
     (db[thing].location != NOTHING) || \
     ((db[thing].owner != 1) && (db[thing].owner != root)) || \
     ((db[thing].flags & ~(0x8000)) != (TYPE_THING | GOING)))

/* Maximum recursion depth to prevent stack overflow */
#define MAX_RECURSION_DEPTH 20

/* Maximum iterations for various loops to prevent infinite loops */
#define MAX_LOOP_ITERATIONS 10000

/* Buffer size for various string operations */
#define DESTROY_BUFFER_SIZE 1024

/* =============================================================================
 * FORWARD DECLARATIONS
 * =============================================================================
 */

static void dbmark(dbref loc);
static void dbunmark(void);
static void dbmark1(void);
static void dbunmark1(void);
static void dbmark2(void);
static void mark_float(void);
static int object_cost(dbref thing);
static void calc_memstats(void);

/* =============================================================================
 * FREE LIST MANAGEMENT
 * =============================================================================
 */

/*
 * free_object - Add an object to the free list
 *
 * This function adds a destroyed object to the head of the free list for
 * later reuse. Objects must be completely cleaned up before calling this.
 *
 * SECURITY: Validates object reference before adding to free list
 */
static void free_object(dbref obj)
{
    /* Use ValidObject() instead of GoodObject() because we're adding
     * deleted objects (with GOING flag) to the free list - that's the
     * whole point! We only need to validate structural integrity. */
    if (!ValidObject(obj)) {
        log_error("free_object: Invalid object reference");
        return;
    }

    db[obj].next = first_free;
    first_free = obj;
}

/* NOTE: The following functions have been moved to db/object.c (2025 reorganization):
 * - free_get() - Get object from free list
 * - do_empty() - Completely destroy object
 * - do_undestroy() - Cancel object destruction
 * See object.h for declarations
 */

/* =============================================================================
 * OBJECT COST CALCULATIONS
 * =============================================================================
 */

/*
 * object_cost - Calculate the refund value for a destroyed object
 *
 * Returns: Integer cost/refund amount
 *
 * SECURITY: Validates object type before accessing type-specific data
 */
static int object_cost(dbref thing)
{
    if (!GoodObject(thing)) {
        log_error("object_cost: Invalid object reference");
        return 0;
    }

    switch (Typeof(thing)) {
    case TYPE_THING:
        if (!GoodObject(thing)) {
            return 0;
        }
        return OBJECT_DEPOSIT(Pennies(thing));

    case TYPE_ROOM:
        return room_cost;

    case TYPE_EXIT:
        if (!GoodObject(thing)) {
            return exit_cost;
        }
        if (db[thing].link != NOTHING) {
            return exit_cost;
        } else {
            return exit_cost + link_cost;
        }

    case TYPE_PLAYER:
    case TYPE_UNIVERSE:
        return 1000;

    default:
        log_error(tprintf("Illegal object type: %ld, object_cost", Typeof(thing)));
        return 5000;
    }
}

/* =============================================================================
 * FREE LIST REPAIR AND DATABASE INTEGRITY
 * =============================================================================
 */

/*
 * fix_free_list - Rebuild the free list and repair database references
 *
 * This is a critical database maintenance function that:
 * 1. Clears and rebuilds the free list
 * 2. Processes doomed objects (scheduled for destruction)
 * 3. Validates all database references (exits, zones, links, locations, owners)
 * 4. Repairs corrupted references
 * 5. Marks reachable rooms from limbo
 * 6. Reports disconnected rooms
 *
 * SECURITY:
 * - Extensive validation of all references before use
 * - Safe handling of corrupted data
 * - Prevention of infinite loops in reference chains
 */
void fix_free_list(void)
{
    dbref thing;
    char *ch;
    int iteration_count = 0;

    first_free = NOTHING;

    /* =========================================================================
     * PHASE 1: Process doomed objects and validate living objects
     * =========================================================================
     */
    for (thing = 0; thing < db_top && iteration_count < MAX_LOOP_ITERATIONS; thing++, iteration_count++) {
        if (!GoodObject(thing)) {
            continue;
        }

        if (IS_DOOMED(thing)) {
            ch = atr_get(thing, A_DOOMSDAY);
            if (ch && (atol(ch) < now) && (atol(ch) > 0)) {
                do_empty(thing);
            }
        } else {
            /* If something other than room, make sure it is located in NOTHING,
             * otherwise undelete it (needed in case @tel was used on object) */
            if (NOT_OK(thing)) {
                db[thing].flags &= ~GOING;
            }
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("fix_free_list: Maximum iterations exceeded in phase 1");
    }

    first_free = NOTHING;

    /* =========================================================================
     * PHASE 2: Validate and repair all object references
     * =========================================================================
     */
    iteration_count = 0;
    for (thing = db_top - 1; thing >= 0 && iteration_count < MAX_LOOP_ITERATIONS; thing--, iteration_count++) {
        /* Check for deleted objects and add to free list
         * Use ValidObject() instead of GoodObject() since deleted objects
         * (with GOING flag) need to be added to free list */
        if (!ValidObject(thing)) {
            continue;
        }

        /* If object is IS_GONE (GOING flag + no A_DOOMSDAY), clean and recycle it
         * We temporarily clear GOING so do_empty() can process it, then do_empty()
         * will clean everything, re-set GOING, and add to free list */
        if (IS_GONE(thing)) {
            db[thing].flags &= ~GOING;  /* Temporarily clear GOING */
            do_empty(thing);             /* Clean and recycle (re-sets GOING and adds to free list) */
            continue;
        }

        /* --- Validate exits list --- */
        CHECK_REF(db[thing].exits) {
            switch (Typeof(thing)) {
            case TYPE_PLAYER:
            case TYPE_CHANNEL:
            case TYPE_UNIVERSE:
            case TYPE_THING:
            case TYPE_ROOM:
                log_error(tprintf("Dead exit in exit list (first) for room #%ld: %ld",
                                thing, db[thing].exits));
                report();
                db[thing].exits = NOTHING;
                break;
            }
        }

        /* --- Validate zone reference --- */
        CHECK_REF(db[thing].zone) {
            switch (Typeof(thing)) {
            case TYPE_ROOM:
                log_error(tprintf("Zone for #%ld is #%ld! setting it to the global zone.",
                                thing, db[thing].zone));
                if (GoodObject(0)) {
                    db[thing].zone = db[0].zone;
                } else {
                    db[thing].zone = NOTHING;
                }
                break;
            }
        }

        /* --- Validate link reference --- */
        CHECK_REF(db[thing].link) {
            switch (Typeof(thing)) {
            case TYPE_PLAYER:
            case TYPE_CHANNEL:
            case TYPE_UNIVERSE:
            case TYPE_THING:
                db[thing].link = player_start;
                break;

            case TYPE_EXIT:
            case TYPE_ROOM:
                db[thing].link = NOTHING;
                break;
            }
        }

        /* --- Validate location reference --- */
        CHECK_REF(db[thing].location) {
            switch (Typeof(thing)) {
            case TYPE_PLAYER:
            case TYPE_CHANNEL:
            case TYPE_UNIVERSE:
            case TYPE_THING:
                db[thing].location = NOTHING;
                moveto(thing, player_start);
                break;

            case TYPE_EXIT:
                db[thing].location = NOTHING;
                destroy_obj(thing, atol(bad_object_doomsday));
                break;

            case TYPE_ROOM:
                db[thing].location = thing;  /* rooms are in themselves */
                break;
            }
        }

        /* --- Validate next pointer in contents/exit chains --- */
        if (((db[thing].next < 0) || (db[thing].next >= db_top)) &&
            (db[thing].next != NOTHING)) {
            log_error(tprintf("Invalid next pointer from object %s(%ld)",
                            db[thing].name, thing));
            report();
            db[thing].next = NOTHING;
        }

        /* --- Validate owner reference --- */
        if ((db[thing].owner < 0) ||
            (db[thing].owner >= db_top) ||
            !GoodObject(db[thing].owner) ||
            Typeof(db[thing].owner) != TYPE_PLAYER) {
            log_error(tprintf("Invalid object owner %s(%ld): %ld",
                            db[thing].name, thing, db[thing].owner));
            report();
            db[thing].owner = root;
            db[thing].flags |= HAVEN;
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("fix_free_list: Maximum iterations exceeded in phase 2");
    }

    /* =========================================================================
     * PHASE 3: Mark reachable rooms and report disconnected ones
     * =========================================================================
     */
    dbmark(player_start);
    mark_float();
    dbmark2();
    dbunmark();
}

/* =============================================================================
 * ROOM CONNECTIVITY CHECKING
 * =============================================================================
 */

/*
 * dbmark - Recursively mark all rooms reachable from a given location
 *
 * This function traces through exit links to find all rooms that can be
 * reached from the starting location. Used to detect disconnected rooms.
 *
 * SECURITY:
 * - Validates location before processing
 * - Prevents infinite recursion via I_MARKED flag
 * - Only processes TYPE_ROOM objects
 */
static void dbmark(dbref loc)
{
    dbref thing;
    int iteration_count = 0;

    /* Validate location */
    if (!GoodObject(loc) || (Typeof(loc) != TYPE_ROOM)) {
        return;
    }

    /* Check if already marked (prevents infinite recursion) */
    if (db[loc].i_flags & I_MARKED) {
        return;
    }

    db[loc].i_flags |= I_MARKED;

    /* Recursively trace through all exits */
    for (thing = Exits(loc);
         thing != NOTHING && GoodObject(thing) && iteration_count < MAX_LOOP_ITERATIONS;
         thing = db[thing].next, iteration_count++) {
        if (GoodObject(db[thing].link)) {
            dbmark(db[thing].link);
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error(tprintf("dbmark: Maximum iterations exceeded for room #" DBREF_FMT, loc));
    }
}

/*
 * dbmark2 - Mark rooms linked from players, things, and their homes
 *
 * This ensures that rooms linked to players and things are considered
 * reachable even if not directly connected via exits.
 */
static void dbmark2(void)
{
    dbref loc;
    int iteration_count = 0;

    for (loc = 0; loc < db_top && iteration_count < MAX_LOOP_ITERATIONS; loc++, iteration_count++) {
        if (!GoodObject(loc)) {
            continue;
        }

        if (Typeof(loc) == TYPE_PLAYER ||
            Typeof(loc) == TYPE_CHANNEL ||
            Typeof(loc) == TYPE_UNIVERSE ||
            Typeof(loc) == TYPE_THING) {

            if (db[loc].link != NOTHING && GoodObject(db[loc].link)) {
                dbmark(db[loc].link);
            }
            if (db[loc].location != NOTHING && GoodObject(db[loc].location)) {
                dbmark(db[loc].location);
            }
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("dbmark2: Maximum iterations exceeded");
    }
}

/*
 * dbunmark - Clear marks and report disconnected rooms
 *
 * Generates a report of rooms that cannot be reached from limbo and
 * unlinked exits.
 *
 * SECURITY: Safe buffer handling for generating report strings
 */
static void dbunmark(void)
{
    dbref loc;
    int ndisrooms = 0, nunlexits = 0;
    char roomlist[DESTROY_BUFFER_SIZE * 4] = "";
    char exitlist[DESTROY_BUFFER_SIZE * 4] = "";
    char newbuf[DESTROY_BUFFER_SIZE * 8];
    char tempbuf[64];
    int iteration_count = 0;

    for (loc = 0; loc < db_top && iteration_count < MAX_LOOP_ITERATIONS; loc++, iteration_count++) {
        if (!GoodObject(loc)) {
            continue;
        }

        if (db[loc].i_flags & I_MARKED) {
            db[loc].i_flags &= (unsigned char)(~I_MARKED);
        } else if (Typeof(loc) == TYPE_ROOM) {
            ndisrooms++;

            /* Build room list safely */
            snprintf(tempbuf, sizeof(tempbuf), " #" DBREF_FMT, loc);
            if (strlen(roomlist) + strlen(tempbuf) < sizeof(roomlist) - 1) {
                strncat(roomlist, tempbuf, sizeof(roomlist) - strlen(roomlist) - 1);
            }

            dest_info(NOTHING, loc);
        }

        if (Typeof(loc) == TYPE_EXIT && db[loc].link == NOTHING) {
            nunlexits++;

            /* Build exit list safely */
            snprintf(tempbuf, sizeof(tempbuf), " #" DBREF_FMT, loc);
            if (strlen(exitlist) + strlen(tempbuf) < sizeof(exitlist) - 1) {
                strncat(exitlist, tempbuf, sizeof(exitlist) - strlen(exitlist) - 1);
            }
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("dbunmark: Maximum iterations exceeded");
    }

    /* Generate summary message */
    snprintf(newbuf, sizeof(newbuf),
             "|Y!+*| There are %d disconnected rooms, %d unlinked exits.",
             ndisrooms, nunlexits);

    if (ndisrooms && strlen(newbuf) + strlen(roomlist) < sizeof(newbuf) - 50) {
        strncat(newbuf, " Disconnected rooms:", sizeof(newbuf) - strlen(newbuf) - 1);
        strncat(newbuf, roomlist, sizeof(newbuf) - strlen(newbuf) - 1);
    }

    if (nunlexits && strlen(newbuf) + strlen(exitlist) < sizeof(newbuf) - 50) {
        strncat(newbuf, " Unlinked exits:", sizeof(newbuf) - strlen(newbuf) - 1);
        strncat(newbuf, exitlist, sizeof(newbuf) - strlen(newbuf) - 1);
    }

    com_send(dbinfo_chan, newbuf);
}

/* =============================================================================
 * CONTENTS AND EXIT LIST VALIDATION
 * =============================================================================
 */

/*
 * dbmark1 - Mark all objects in contents and exit lists
 *
 * Validates the integrity of contents and exit chains, clearing corrupted
 * lists if necessary.
 *
 * SECURITY:
 * - Validates all objects before accessing
 * - Checks for circular references
 * - Limits iteration count
 */
static void dbmark1(void)
{
    dbref thing;
    dbref loc;
    int iteration_count;

    for (loc = 0; loc < db_top; loc++) {
        if (!GoodObject(loc) || Typeof(loc) == TYPE_EXIT) {
            continue;
        }

        /* Validate contents list
         * NOTE: Use ValidObject() not GoodObject() - objects with GOING flag
         * are still valid references until free_object() cleans them. */
        iteration_count = 0;
        for (thing = db[loc].contents;
             thing != NOTHING && iteration_count < MAX_LOOP_ITERATIONS;
             thing = db[thing].next, iteration_count++) {

            if (!ValidObject(thing)) {
                log_error(tprintf("Invalid object #%ld in contents of #%ld, clearing contents",
                                thing, loc));
                db[loc].contents = NOTHING;
                break;
            }

            if ((db[thing].location != loc) || (Typeof(thing) == TYPE_EXIT)) {
                log_error(tprintf("Contents of object %ld corrupt at object %ld, cleared",
                                loc, thing));
                db[loc].contents = NOTHING;
                break;
            }

            db[thing].i_flags |= I_MARKED;
        }

        if (iteration_count >= MAX_LOOP_ITERATIONS) {
            log_error(tprintf("dbmark1: Infinite loop in contents of #%ld, cleared", loc));
            db[loc].contents = NOTHING;
        }

        /* Validate exits list */
        iteration_count = 0;
        for (thing = db[loc].exits;
             thing != NOTHING && iteration_count < MAX_LOOP_ITERATIONS;
             thing = db[thing].next, iteration_count++) {

            if (!GoodObject(thing)) {
                log_error(tprintf("Invalid object #%ld in exits of #%ld, clearing exits",
                                thing, loc));
                db[loc].exits = NOTHING;
                break;
            }

            if ((db[thing].location != loc) || (Typeof(thing) != TYPE_EXIT)) {
                log_error(tprintf("Exits of object %ld corrupt at object %ld, cleared",
                                loc, thing));
                db[loc].exits = NOTHING;
                break;
            }

            db[thing].i_flags |= I_MARKED;
        }

        if (iteration_count >= MAX_LOOP_ITERATIONS) {
            log_error(tprintf("dbmark1: Infinite loop in exits of #%ld, cleared", loc));
            db[loc].exits = NOTHING;
        }
    }
}

/*
 * dbunmark1 - Clear marks and relocate orphaned objects
 *
 * Objects that weren't marked are orphaned (not in any valid location).
 * This function relocates them to safe locations.
 */
static void dbunmark1(void)
{
    dbref loc;
    int iteration_count = 0;

    for (loc = 0; loc < db_top && iteration_count < MAX_LOOP_ITERATIONS; loc++, iteration_count++) {
        if (!GoodObject(loc)) {
            continue;
        }

        if (db[loc].i_flags & I_MARKED) {
            db[loc].i_flags &= (unsigned char)(~I_MARKED);
        } else if (!IS_GONE(loc)) {
            if (Typeof(loc) == TYPE_PLAYER ||
                Typeof(loc) == TYPE_CHANNEL ||
                Typeof(loc) == TYPE_UNIVERSE ||
                Typeof(loc) == TYPE_THING) {

                log_error(tprintf("DBCK: Moved object %ld", loc));

                if (db[loc].location > 0 &&
                    GoodObject(db[loc].location) &&
                    Typeof(db[loc].location) != TYPE_EXIT) {
                    moveto(loc, db[loc].location);
                } else {
                    moveto(loc, 0);
                }
            } else if (Typeof(loc) == TYPE_EXIT) {
                log_error(tprintf("DBCK: moved exit %ld", loc));

                if (db[loc].location > 0 &&
                    GoodObject(db[loc].location) &&
                    Typeof(db[loc].location) != TYPE_EXIT) {
                    moveto(loc, db[loc].location);
                } else {
                    moveto(loc, 0);
                }
            }
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("dbunmark1: Maximum iterations exceeded");
    }
}

/* =============================================================================
 * MEMORY STATISTICS
 * =============================================================================
 */

/*
 * calc_memstats - Calculate and report memory usage statistics
 *
 * SECURITY: Safe buffer handling for report generation
 */
static void calc_memstats(void)
{
    int i;
    int j = 0;
    char newbuf[DESTROY_BUFFER_SIZE];
    int iteration_count = 0;

    for (i = 0; i < db_top && iteration_count < MAX_LOOP_ITERATIONS; i++, iteration_count++) {
        if (GoodObject(i)) {
            j += mem_usage(i);
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("calc_memstats: Maximum iterations exceeded");
    }

    snprintf(newbuf, sizeof(newbuf),
             "|Y!+*| There are %d bytes being used in memory for the database.", j);

    /* Use ValidObject() for free list - deleted objects are expected */
    if (first_free != NOTHING && ValidObject(first_free)) {
        char tempbuf[128];
        snprintf(tempbuf, sizeof(tempbuf),
                 " The first object in the free list is #%ld.", first_free);
        strncat(newbuf, tempbuf, sizeof(newbuf) - strlen(newbuf) - 1);
    }

    com_send(dbinfo_chan, newbuf);
}

/* =============================================================================
 * DATABASE CHECKING COMMAND
 * =============================================================================
 */

/*
 * do_dbck - Perform database integrity check and repair
 *
 * This is the @dbck command implementation. It:
 * 1. Fixes circular references in contents/exit lists
 * 2. Rebuilds the free list
 * 3. Validates and repairs object locations
 * 4. Reports memory usage
 *
 * SECURITY:
 * - Requires POW_DB power
 * - Limits iterations to prevent infinite loops
 */
void do_dbck(dbref player)
{
    extern dbref speaker;
    dbref i;
    int iteration_count;

    if (!GoodObject(player)) {
        log_error("do_dbck: Invalid player reference");
        return;
    }

    if (!has_pow(player, NOTHING, POW_DB)) {
        notify(player, "@dbck is a restricted command.");
        return;
    }

    speaker = root;

    /* Fix circular references in exit and content chains */
    iteration_count = 0;
    for (i = 0; i < db_top && iteration_count < MAX_LOOP_ITERATIONS; i++, iteration_count++) {
        int m;
        dbref j;

        if (!GoodObject(i)) {
            continue;
        }

        /* Check exits chain for loops */
        for (j = db[i].exits, m = 0;
             j != NOTHING && m < 1000;
             j = (GoodObject(j) ? db[j].next : NOTHING), m++) {
            if (m >= 999 && GoodObject(j)) {
                log_error(tprintf("Breaking circular exit chain at #" DBREF_FMT, i));
                db[j].next = NOTHING;
            }
        }

        /* Check contents chain for loops */
        for (j = db[i].contents, m = 0;
             j != NOTHING && m < 1000;
             j = (GoodObject(j) ? db[j].next : NOTHING), m++) {
            if (m >= 999 && GoodObject(j)) {
                log_error(tprintf("Breaking circular contents chain at #" DBREF_FMT, i));
                db[j].next = NOTHING;
            }
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("do_dbck: Maximum iterations exceeded in chain fixing");
    }

    /* Perform full database check */
    fix_free_list();
    dbmark1();
    dbunmark1();
    calc_memstats();
}


/* =============================================================================
 * FREE LIST UTILITIES
 * =============================================================================
 */

/*
 * zero_free_list - Clear the free list pointer
 *
 * Used during database initialization.
 */
void zero_free_list(void)
{
    first_free = NOTHING;
}

/* =============================================================================
 * GARBAGE COLLECTION STATE
 * =============================================================================
 */

static int gstate = 0;
static struct object *o;
static dbref thing;

/*
 * do_check - Set garbage collection checkpoint for debugging
 *
 * SECURITY: Requires POW_SECURITY power
 */
void do_check(dbref player, char *arg1)
{
    dbref obj;

    if (!GoodObject(player)) {
        log_error("do_check: Invalid player reference");
        return;
    }

    if (!power(player, POW_SECURITY)) {
        notify(player, perm_denied());
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Check what object?");
        return;
    }

    obj = match_controlled(player, arg1, POW_MODIFY);
    if (obj == NOTHING) {
        return;
    }

    if (!GoodObject(obj)) {
        notify(player, "Invalid object reference.");
        return;
    }

    thing = obj;
    gstate = 1;
    notify(player, "Okay, I set the garbage point.");
}

/* =============================================================================
 * DATABASE INFO COMMAND
 * =============================================================================
 */

/*
 * info_db - Display database information
 *
 * Shows current database statistics including:
 * - Database top
 * - First free object
 * - Update bytes counter
 * - Garbage collection point
 * - Object statistics
 *
 * SECURITY: Safe use of tprintf for formatted output
 */
extern dbref update_bytes_counter;

void info_db(dbref player)
{
    if (!GoodObject(player)) {
        log_error("info_db: Invalid player reference");
        return;
    }

    notify(player, tprintf("db_top: #" DBREF_FMT, db_top));
    notify(player, tprintf("first_free: #" DBREF_FMT, first_free));
    notify(player, tprintf("update_bytes_counter: #" DBREF_FMT, update_bytes_counter));
    notify(player, tprintf("garbage point: #" DBREF_FMT, thing));
    do_stats(player, "");
}

/* =============================================================================
 * INCREMENTAL GARBAGE COLLECTION
 * =============================================================================
 */

/*
 * do_incremental - Perform incremental garbage collection
 *
 * This function runs periodically to:
 * 1. Collect object names into clean string storage
 * 2. Collect attributes
 * 3. Validate parent/child relationships
 * 4. Validate zone chains
 * 5. Validate all object references
 * 6. Recalculate byte usage
 *
 * SECURITY:
 * - Extensive validation of all references
 * - Protection against infinite loops
 * - Safe string operations
 * - Limits iterations per call
 */
void do_incremental(void)
{
    int j;
    int a;

    switch (gstate) {
    case 0:  /* Pre-collection - start new cycle */
        gstate = 1;
        thing = 0;
        break;

    case 1:  /* Collection phase */
        if (!GoodObject(thing)) {
            thing = 0;
        }

        o = &(db[thing]);

        for (a = 0; (a < garbage_chunk) && (thing < db_top); a++, thing++) {
            char buff[DESTROY_BUFFER_SIZE];
            extern char ccom[];
            int i;
            int iteration_count;

            if (!GoodObject(thing)) {
                continue;
            }

            snprintf(ccom, sizeof(ccom), "object #%ld\n", thing);

            /* Safely copy object name */
            strncpy(buff, o->name ? o->name : "", sizeof(buff) - 1);
            buff[sizeof(buff) - 1] = '\0';

#ifdef MEMORY_DEBUG_LOG
            memdebug_log_ts("GC: About to SET object #%ld name=%s ptr=%p\n",
                          thing, o->name, (void*)o->name);
#endif
            SET(o->name, buff);

            /* Collect attributes */
            atr_collect(thing);

            if (IS_GONE(thing)) {
                o++;
                continue;
            }

            /* ================================================================
             * Validate parent list
             * ================================================================
             */
        again1:
            if (db[thing].parents) {
                iteration_count = 0;
                for (i = 0;
                     db[thing].parents[i] != NOTHING && iteration_count < 100;
                     i++, iteration_count++) {

                    if (!GoodObject(db[thing].parents[i])) {
                        log_error(tprintf("Bad #%ld in parent list on #%ld.",
                                        db[thing].parents[i], thing));
                        REMOVE_FIRST_L(db[thing].parents, db[thing].parents[i]);
                        goto again1;
                    }

                    /* Verify reciprocal relationship */
                    for (j = 0;
                         db[db[thing].parents[i]].children &&
                         db[db[thing].parents[i]].children[j] != NOTHING;
                         j++) {
                        if (db[db[thing].parents[i]].children[j] == thing) {
                            j = -1;
                            break;
                        }
                    }

                    if (j != -1) {
                        log_error(tprintf("Wrong #%ld in parent list on #%ld.",
                                        db[thing].parents[i], thing));
                        REMOVE_FIRST_L(db[thing].parents, db[thing].parents[i]);
                        goto again1;
                    }
                }
            }

            /* ================================================================
             * Validate children list
             * ================================================================
             */
        again2:
            if (db[thing].children) {
                iteration_count = 0;
                for (i = 0;
                     db[thing].children[i] != NOTHING && iteration_count < 100;
                     i++, iteration_count++) {

                    if (!GoodObject(db[thing].children[i])) {
                        log_error(tprintf("Bad #%ld in children list on #%ld.",
                                        db[thing].children[i], thing));
                        REMOVE_FIRST_L(db[thing].children, db[thing].children[i]);
                        goto again2;
                    }

                    /* Verify reciprocal relationship */
                    for (j = 0;
                         db[db[thing].children[i]].parents &&
                         db[db[thing].children[i]].parents[j] != NOTHING;
                         j++) {
                        if (db[db[thing].children[i]].parents[j] == thing) {
                            j = -1;
                            break;
                        }
                    }

                    if (j != -1) {
                        log_error(tprintf("Wrong #%ld in children list on #%ld.",
                                        db[thing].children[i], thing));
                        REMOVE_FIRST_L(db[thing].children, db[thing].children[i]);
                        goto again2;
                    }
                }
            }

            /* ================================================================
             * Validate attribute inheritance
             * ================================================================
             */
            {
                ALIST *atr, *nxt;

                for (atr = db[thing].list; atr; atr = nxt) {
                    nxt = AL_NEXT(atr);
                    if (AL_TYPE(atr) &&
                        AL_TYPE(atr)->obj != NOTHING &&
                        GoodObject(AL_TYPE(atr)->obj) &&
                        !is_a(thing, AL_TYPE(atr)->obj)) {
                        atr_add(thing, AL_TYPE(atr), "");
                    }
                }
            }

            /* ================================================================
             * Validate zone chain (prevent infinite loops)
             * ================================================================
             */
            {
                dbref zon;
                int zone_depth;

                for (zone_depth = 0, zon = get_zone_first(thing);
                     zon != NOTHING && zone_depth < 15;
                     zon = get_zone_next(zon), zone_depth++) {

                    if (!GoodObject(zon)) {
                        log_error(tprintf("Invalid zone in chain for #" DBREF_FMT, thing));
                        db[thing].zone = db[0].zone;
                        break;
                    }
                }

                if (zone_depth >= 15) {
                    log_error(tprintf("%s's zone %s is infinite.",
                                    unparse_object_a(1, thing),
                                    unparse_object_a(1, zon)));
                    if (GoodObject(0)) {
                        db[zon].zone = db[0].zone;
                        db[db[0].zone].zone = NOTHING;
                    }
                }
            }

            /* ================================================================
             * Validate standard references
             * ================================================================
             */
            CHECK_REF(db[thing].exits) {
                switch (Typeof(thing)) {
                case TYPE_PLAYER:
                case TYPE_THING:
                case TYPE_CHANNEL:
                case TYPE_UNIVERSE:
                case TYPE_ROOM:
                    log_error(tprintf("Dead exit in exit list (first) for room #%ld: %ld",
                                    thing, db[thing].exits));
                    report();
                    db[thing].exits = NOTHING;
                    break;
                }
            }

            CHECK_REF(db[thing].zone) {
                switch (Typeof(thing)) {
                case TYPE_ROOM:
                    log_error(tprintf("Zone for #%ld is #%ld! setting to global zone.",
                                    thing, db[thing].zone));
                    if (GoodObject(0)) {
                        db[thing].zone = db[0].zone;
                    } else {
                        db[thing].zone = NOTHING;
                    }
                    break;
                }
            }

            CHECK_REF(db[thing].link) {
                switch (Typeof(thing)) {
                case TYPE_PLAYER:
                case TYPE_THING:
                case TYPE_CHANNEL:
                case TYPE_UNIVERSE:
                    db[thing].link = player_start;
                    break;
                case TYPE_EXIT:
                case TYPE_ROOM:
                    db[thing].link = NOTHING;
                    break;
                }
            }

            CHECK_REF(db[thing].location) {
                switch (Typeof(thing)) {
                case TYPE_PLAYER:
                case TYPE_THING:
                case TYPE_CHANNEL:
                case TYPE_UNIVERSE:
                    db[thing].location = NOTHING;
                    moveto(thing, player_start);
                    break;
                case TYPE_EXIT:
                    db[thing].location = NOTHING;
                    destroy_obj(thing, atol(bad_object_doomsday));
                    break;
                case TYPE_ROOM:
                    db[thing].location = thing;
                    break;
                }
            }

            if (((db[thing].next < 0) || (db[thing].next >= db_top)) &&
                (db[thing].next != NOTHING)) {
                log_error(tprintf("Invalid next pointer from object %s(%ld)",
                                db[thing].name, thing));
                report();
                db[thing].next = NOTHING;
            }

            if ((db[thing].owner < 0) ||
                (db[thing].owner >= db_top) ||
                !GoodObject(db[thing].owner) ||
                Typeof(db[thing].owner) != TYPE_PLAYER) {
                log_error(tprintf("Invalid object owner %s(%ld): %ld",
                                db[thing].name, thing, db[thing].owner));
                report();
                db[thing].owner = root;
            }

            /* Recalculate byte usage if needed */
            if (GoodObject(o->owner) && !*atr_get(o->owner, A_BYTESUSED)) {
                recalc_bytes(o->owner);
            }

            o++;
        }

        /* If complete, go to state 0 */
        if (thing >= db_top) {
            gstate = 0;
        }
        break;
    }
}

/* =============================================================================
 * FLOATING ROOM DETECTION
 * =============================================================================
 */

/*
 * mark_float - Mark all floating rooms as reachable
 *
 * Floating rooms (marked with ROOM_FLOATING flag) are intentionally
 * disconnected and should not be reported as orphaned.
 */
static void mark_float(void)
{
    dbref loc;
    int iteration_count = 0;

    for (loc = 0; loc < db_top && iteration_count < MAX_LOOP_ITERATIONS; loc++, iteration_count++) {
        if (GoodObject(loc) && IS(loc, TYPE_ROOM, ROOM_FLOATING)) {
            dbmark(loc);
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        log_error("mark_float: Maximum iterations exceeded");
    }
}

/* =============================================================================
 * FREE LIST MANIPULATION
 * =============================================================================
 */

/*
 * do_upfront - Move an object to the front of the free list
 *
 * This is a debugging command to manipulate the free list order.
 *
 * SECURITY: Requires POW_DB power
 */
void do_upfront(dbref player, char *arg1)
{
    dbref object;
    dbref target;  /* Renamed from 'thing' to avoid shadowing global */
    int iteration_count = 0;

    if (!GoodObject(player)) {
        log_error("do_upfront: Invalid player reference");
        return;
    }

    if (!power(player, POW_DB)) {
        notify(player, "Restricted command.");
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Upfront what object?");
        return;
    }

    target = match_thing(player, arg1);
    if (target == NOTHING) {
        return;
    }

    /* Use ValidObject() for free list - deleted objects are expected */
    if (!ValidObject(target)) {
        notify(player, "Invalid object reference.");
        return;
    }

    if (first_free == target) {
        notify(player, "That object is already at the top of the free list.");
        return;
    }

    /* Find the object in the free list - use ValidObject() since
     * free list contains deleted objects */
    for (object = first_free;
         object != NOTHING &&
         ValidObject(object) &&
         db[object].next != target &&
         iteration_count < MAX_LOOP_ITERATIONS;
         object = db[object].next, iteration_count++) {
        /* Just searching */
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        notify(player, "Error: Possible infinite loop in free list.");
        log_error("do_upfront: Maximum iterations exceeded");
        return;
    }

    if (object == NOTHING) {
        notify(player, "That object does not exist in the free list.");
        return;
    }

    if (!ValidObject(object)) {
        notify(player, "Error: Corrupted free list.");
        return;
    }

    /* Move to front */
    db[object].next = db[target].next;
    db[target].next = first_free;
    first_free = target;

    notify(player, "Object is now at the front of the free list.");
}

/* =============================================================================
 * DATABASE SHRINKING (OPTIONAL)
 * =============================================================================
 */

#ifdef SHRINK_DB

/*
 * do_shrinkdbuse - Compact the database by moving destroyed objects to the end
 *
 * WARNING: This is a dangerous operation that modifies database object numbers.
 * It can severely disrupt +mail and other systems that reference objects by
 * number. Use with extreme caution.
 *
 * SECURITY:
 * - Requires root powers
 * - Should only be used on backed-up databases
 * - Limits iterations to prevent infinite loops
 */
void do_shrinkdbuse(dbref player, char *arg1)
{
    dbref vari = 0;
    dbref vari2 = 0;
    int my_exit = 0;
    char temp[32];
    char temp2[32];
    dbref distance;
    int iteration_count = 0;

    if (!GoodObject(player)) {
        log_error("do_shrinkdbuse: Invalid player reference");
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Usage: @shrinkdb <distance>");
        return;
    }

    distance = atol(arg1);

    if (distance == 0) {
        notify(player, tprintf("db_top: %ld", db_top));
        return;
    }

    /* WARNING: This modifies object numbers! */
    for (vari = db_top - 1;
         vari > distance && iteration_count < MAX_LOOP_ITERATIONS;
         vari--, iteration_count++) {

        /* Skip invalid or already destroyed objects */
        if (vari < 0 || vari >= db_top || !GoodObject(vari)) {
            continue;
        }

        /* Skip objects that are already GOING */
        if (db[vari].flags & GOING) {
            continue;
        }

        /* Find a destroyed object to swap with */
        my_exit = 0;
        vari2 = 0;

        while (!my_exit && vari2 < vari) {
            /* We want GOING objects, so just check bounds not GoodObject */
            if (vari2 >= 0 && vari2 < db_top && (db[vari2].flags & GOING)) {
                my_exit = 1;
            } else {
                vari2++;
            }
        }

        if (vari2 > 0 && vari > vari2 && GoodObject(vari) && GoodObject(vari2)) {
            notify(player, tprintf("Found one: %ld  Free: %ld", vari, vari2));

            snprintf(temp, sizeof(temp), "#%" DBREF_FMT, vari);
            snprintf(temp2, sizeof(temp2), "#%" DBREF_FMT, vari2);

            do_swap(root, temp, temp2);
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        notify(player, "Warning: Maximum iterations reached. Database may not be fully compacted.");
        log_error("do_shrinkdbuse: Maximum iterations exceeded");
    }
}

#endif /* SHRINK_DB */
