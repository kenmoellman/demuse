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
        notify(player, tprintf("#%ld is not a valid object.", room));
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
#ifdef USE_UNIV
        case TYPE_UNIVERSE:
#endif
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
    if (!can_pay_fees(def_owner(player), cost, QUOTA_COST)) {
        return;
    }

    /* Create the object */
    thing = new_object();

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

    notify(player, tprintf("%s created.", unparse_object(player, thing)));
}

#ifdef USE_UNIV
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
#endif /* USE_UNIV */

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
        recursion_depth++;

        if (recursion_depth >= MAX_RECURSION_DEPTH) {
            first_free = NOTHING;
            report();
            log_error("Removed free list and continued (max recursion)");
            recursion_depth = 0;
            return NOTHING;
        }

        report();
        log_error(tprintf("Object #%ld shouldn't be free, fixing free list", newobj));
        fix_free_list();

        recursion_depth--;
        return free_get();
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
#ifdef USE_UNIV
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
#endif
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
#ifdef USE_UNIV
            db[thing].universe = NOTHING;
#endif

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
                log_error(tprintf("do_empty: Infinite loop in exits of #%ld", thing));
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
                log_error(tprintf("do_empty: Infinite loop in contents (link fix) of #%ld", thing));
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
                log_error(tprintf("do_empty: Infinite loop sending contents home for #%ld", thing));
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
#ifdef USE_UNIV
        case TYPE_UNIVERSE:
#endif
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
