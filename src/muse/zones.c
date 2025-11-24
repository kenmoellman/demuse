/* zones.c - Zone and Universe Management
 *
 * This file consolidates all zone and universe related functions:
 * - Zone iteration and hierarchy
 * - Zone linking/unlinking commands
 * - Universe linking/unlinking commands
 * - Global zone/universe management
 * - Universe object initialization
 *
 * Note: Universe parser functions (init_universes, get_player_universe, etc.)
 * remain in parser.c as they're tightly coupled with the parser system.
 */

#include "copyright.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "match.h"
#include "externs.h"
#include "interface.h"

/* ========================================================================
 * SECTION 1: Zone Iteration
 * ======================================================================== */

/**
 * get_zone_first - Get the first zone in the hierarchy for an object
 *
 * Walks up the location chain to find the first zone object.
 * If no zone is found in the location hierarchy, returns the global zone.
 *
 * Improvements in 2025 modernization:
 * - Uses local depth variable to avoid shadowing global
 * - Validates all object references
 * - Prevents infinite loops with depth limit
 *
 * @param player The object to get the zone for
 * @return The first zone object, or db[0].zone if none found
 */
dbref get_zone_first(dbref player)
{
  int zone_depth = 10;  /* Local variable to avoid shadowing global 'depth' */
  dbref location;

  for (location = player;
       zone_depth && location != NOTHING;
       zone_depth--, location = db[location].location) {

    /* Validate object */
    if (!GoodObject(location)) {
      break;
    }

    /* Set default zone if needed */
    if (db[location].zone == NOTHING &&
        (Typeof(location) == TYPE_THING || Typeof(location) == TYPE_ROOM) &&
        location != 0 && location != db[0].zone) {
      db[location].zone = db[0].zone;
    }

    /* Return zone if found */
    if (location == db[0].zone) {
      return db[0].zone;
    } else if (db[location].zone != NOTHING) {
      return db[location].zone;
    }
  }

  return db[0].zone;
}

/**
 * get_zone_next - Get the next zone in the hierarchy
 *
 * @param player The current zone object
 * @return The next zone object, or -1 if none
 */
dbref get_zone_next(dbref player)
{
  if (!valid_player(player)) {
    return -1;
  }

  if (db[player].zone == NOTHING && player != db[0].zone) {
    return db[0].zone;
  } else {
    return db[player].zone;
  }
}

/* ========================================================================
 * SECTION 2: Zone Management Commands
 * ======================================================================== */

/**
 * ZLINK command - link to zone
 * @param player Player doing linking
 * @param arg1 Room to link
 * @param arg2 Zone object
 */
void do_zlink(dbref player, char *arg1, char *arg2)
{
    dbref room;
    dbref zone_obj;

    if (!arg1 || !*arg1 || !arg2 || !*arg2) {
        notify(player, "Usage: @zlink <room>=<zone object>");
        return;
    }

    /* Match room */
    init_match(player, arg1, TYPE_ROOM);
    match_here();
    match_absolute();

    room = noisy_match_result();
    if (room == NOTHING) {
        return;
    }

    /* Match zone object */
    init_match(player, arg2, TYPE_THING);
    match_neighbor();
    match_possession();
    match_absolute();

    zone_obj = noisy_match_result();
    if (zone_obj == NOTHING) {
        return;
    }

    /* Check permissions */
    if (!controls(player, room, POW_MODIFY) ||
        !controls(player, zone_obj, POW_MODIFY) ||
        (Typeof(room) != TYPE_ROOM && player != root)) {
        notify(player, perm_denied());
        return;
    }

    if (is_in_zone(zone_obj, room)) {
        notify(player, "Already linked to that zone.");
        return;
    }

    /* Ensure zone object has a zone set */
    if (db[zone_obj].zone == NOTHING && zone_obj != db[0].zone) {
        db[zone_obj].zone = db[0].zone;
    }

    db[room].zone = zone_obj;
    notify(player, tprintf("%s zone set to %s",
                          db[room].name, db[zone_obj].name));
}

/**
 * UNZLINK command - unlink from zone
 * @param player Player doing unlinking
 * @param arg1 Room to unlink
 */
void do_unzlink(dbref player, char *arg1)
{
    dbref room;

    if (!arg1 || !*arg1) {
        notify(player, "Usage: @unzlink <room>");
        return;
    }

    init_match(player, arg1, TYPE_ROOM);
    match_here();
    match_absolute();

    room = noisy_match_result();
    if (room == NOTHING) {
        return;
    }

    if (!controls(player, room, POW_MODIFY)) {
        notify(player, perm_denied());
        return;
    }

    if (Typeof(room) == TYPE_ROOM) {
        db[room].zone = db[0].zone;
    } else {
        db[room].zone = NOTHING;
    }

    notify(player, "Zone unlinked.");
}

/**
 * GZONE command - set global zone (root only)
 * @param player Player (must be root)
 * @param arg1 Zone object
 */
void do_gzone(dbref player, char *arg1)
{
    dbref thing;
    dbref oldu;
    dbref obj;

    if (player != root) {
        notify(player, "You don't have the authority. So sorry.");
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Usage: @gzone <zone object>");
        return;
    }

    init_match(player, arg1, TYPE_THING);
    match_possession();
    match_neighbor();
    match_absolute();

    thing = noisy_match_result();
    if (thing == NOTHING) {
        return;
    }

    oldu = db[0].zone;
    db[0].zone = thing;

    /* Update all rooms with old zone */
    for (obj = 0; obj < db_top; obj++) {
        if ((Typeof(obj) == TYPE_ROOM) && !(db[obj].flags & GOING) &&
            ((db[obj].zone == oldu) || (db[obj].zone == NOTHING))) {
            db[obj].zone = thing;
        }
    }

    db[thing].zone = NOTHING;
    notify(player, tprintf("Global zone set to %s.", db[thing].name));
}

/* ========================================================================
 * SECTION 3: Universe Management Commands
 * ======================================================================== */

/**
 * ULINK command - link to universe
 * @param player Player doing linking
 * @param arg1 Object to link
 * @param arg2 Universe object
 */
void do_ulink(dbref player, char *arg1, char *arg2)
{
    dbref object;
    dbref univ;

    if (!arg1 || !*arg1 || !arg2 || !*arg2) {
        notify(player, "Usage: @ulink <object>=<universe>");
        return;
    }

    /* Match object */
    init_match(player, arg1, TYPE_THING);
    match_neighbor();
    match_possession();
    match_absolute();

    object = noisy_match_result();
    if (object == NOTHING) {
        return;
    }

    /* Match universe */
    init_match(player, arg2, TYPE_UNIVERSE);
    match_neighbor();
    match_possession();
    match_absolute();

    univ = noisy_match_result();
    if (univ == NOTHING) {
        return;
    }

    if (Typeof(univ) != TYPE_UNIVERSE) {
        notify(player, "That is not a valid Universe.");
        return;
    }

    if (!controls(player, univ, POW_MODIFY) ||
        !controls(player, object, POW_MODIFY)) {
        notify(player, perm_denied());
        return;
    }

    if (db[object].universe == univ) {
        notify(player, "Already linked to that universe.");
        return;
    }

    db[object].universe = univ;
    notify(player, tprintf("%s(#%ld) universe set to %s(#%ld)",
                          db[object].name, object, db[univ].name, univ));
}

/**
 * UNUNLINK command - unlink from universe
 * @param player Player doing unlinking
 * @param arg1 Object to unlink
 */
void do_unulink(dbref player, char *arg1)
{
    dbref thing;

    if (!arg1 || !*arg1) {
        notify(player, "Usage: @ununlink <object>");
        return;
    }

    init_match(player, arg1, TYPE_THING);
    match_neighbor();
    match_possession();
    match_absolute();

    thing = noisy_match_result();
    if (thing == NOTHING) {
        return;
    }

    if (!controls(player, thing, POW_MODIFY)) {
        notify(player, perm_denied());
        return;
    }

    db[thing].universe = db[0].universe;
    notify(player, "Universe unlinked.");
}

/**
 * GUNIVERSE command - set global universe (root only)
 * @param player Player (must be root)
 * @param arg1 Universe object
 */
void do_guniverse(dbref player, char *arg1)
{
    dbref thing;
    dbref oldu;
    dbref obj;

    if (player != root) {
        notify(player, perm_denied());
        return;
    }

    if (!arg1 || !*arg1) {
        notify(player, "Usage: @guniverse <universe object>");
        return;
    }

    init_match(player, arg1, TYPE_UNIVERSE);
    match_possession();
    match_neighbor();
    match_absolute();

    thing = noisy_match_result();
    if (thing == NOTHING) {
        return;
    }

    if (Typeof(thing) != TYPE_UNIVERSE) {
        notify(player, "That is not a valid Universe.");
        return;
    }

    oldu = db[0].universe;

    /* Update all objects with old universe */
    for (obj = 0; obj < db_top; obj++) {
        if (!(db[obj].flags & GOING) &&
            ((db[obj].universe == oldu) || (db[obj].universe == NOTHING))) {
            db[obj].universe = thing;
        }
    }

    notify(player, tprintf("Global universe set to #%ld.", thing));
}

/**
 * init_universe - Initialize universe-specific data structures
 * @param o Object structure to initialize
 */
void init_universe(struct object *o)
{
    int i;

    if (!o) {
        return;
    }

    SAFE_MALLOC(o->ua_string, char *, NUM_UA);
    SAFE_MALLOC(o->ua_float, float, NUM_UA);
    SAFE_MALLOC(o->ua_int, int, NUM_UA);

    if (!o->ua_string || !o->ua_float || !o->ua_int) {
        log_error("init_universe: malloc failed");
        if (o->ua_string) SMART_FREE(o->ua_string);
        if (o->ua_float) SMART_FREE(o->ua_float);
        if (o->ua_int) SMART_FREE(o->ua_int);
        return;
    }

    for (i = 0; i < NUM_UA; i++) {
        switch (univ_config[i].type) {
            case UF_BOOL:
            case UF_INT:
                o->ua_int[i] = atoi(univ_config[i].def);
                o->ua_string[i] = NULL;
                break;

            case UF_FLOAT:
                o->ua_float[i] = (float)atof(univ_config[i].def);
                o->ua_string[i] = NULL;
                break;

            case UF_STRING:
                SAFE_MALLOC(o->ua_string[i], char, strlen(univ_config[i].def) + 1);
                if (o->ua_string[i]) {
                    strcpy(o->ua_string[i], univ_config[i].def);
                }
                break;
        }
    }
}
