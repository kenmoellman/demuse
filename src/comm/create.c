/* create.c - Object creation and linking commands
 * Located in comm/ directory
 * 
 * This file contains commands for creating and linking objects:
 * rooms, exits, things, players, and their interconnections.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"

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
 * Zone Linking
 * =================================================================== */

#ifdef USE_UNIV
/**
 * ULINK command - link to universe
 * @param player Player doing linking
 * @param arg1 Object to link
 * @param arg2 Universe to link to
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
#endif /* USE_UNIV */

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

#ifdef USE_UNIV
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
#endif /* USE_UNIV */

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

/**
 * Initialize universe-specific data structures
 * @param o Object structure to initialize
 */
void init_universe(struct object *o)
{
    int i;
    
    if (!o) {
        return;
    }
    
//    o->ua_string = (char **)malloc(NUM_UA * sizeof(char *));
//    o->ua_float = (float *)malloc(NUM_UA * sizeof(float));
//    o->ua_int = (int *)malloc(NUM_UA * sizeof(int));
    
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
//                o->ua_string[i] = malloc(strlen(univ_config[i].def) + 1);
                SAFE_MALLOC(o->ua_string[i], char, strlen(univ_config[i].def) + 1);
                if (o->ua_string[i]) {
                    strcpy(o->ua_string[i], univ_config[i].def);
                }
                break;
        }
    }
}
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
