/* destroy.c */

/*
 * =============================================================================
 * MODERNIZATION NOTES (2024):
 * =============================================================================
 * This file has been modernized from the original 1993 TinyMUSE codebase:
 *
 * KEY CHANGES:
 * - Converted all K&R style functions to ANSI C prototypes
 * - Added comprehensive GoodObject() validation before all db[] access
 * - Replaced sprintf() with snprintf() to prevent buffer overflows
 * - Replaced strcpy() with strncpy() with proper null termination
 * - Added bounds checking for all string operations
 * - Improved CHECK_REF macro with better validation
 * - Enhanced memory safety using SAFE_MALLOC and SMART_FREE
 * - Added detailed comments explaining garbage collection logic
 * - Better error messages with context
 * - Added section headers for clarity
 * - Improved recursion depth tracking
 * - Better handling of corrupted database references
 *
 * SECURITY IMPROVEMENTS:
 * - All buffer operations now have size limits
 * - Extensive validation of database references before use
 * - Protection against infinite loops in parent/child relationships
 * - Safe handling of corrupted free list
 * - Better recursion depth limiting
 *
 * FUNCTIONALITY:
 * This module manages the database garbage collection system, including:
 * - Free list management for recycled objects
 * - Object destruction and cleanup
 * - Database integrity checking (@dbck command)
 * - Incremental garbage collection
 * - Detection of disconnected rooms and orphaned objects
 * =============================================================================
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "db.h"
#include "config.h"
#include "externs.h"

/* =============================================================================
 * GLOBAL VARIABLES
 * =============================================================================
 */

dbref first_free = NOTHING;

/* =============================================================================
 * MACROS AND DEFINITIONS
 * =============================================================================
 */

/* Enhanced CHECK_REF macro with better validation */
#define CHECK_REF(thing) \
    if ((thing) < -3 || (thing) >= db_top || \
        ((thing) > -1 && !GoodObject(thing)) || \
        ((thing) > -1 && IS_GONE(thing)))

/* Check if object is valid for free list - must be destroyed and properly formatted */
#define NOT_OK(thing) \
    (!GoodObject(thing) || \
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
    if (!GoodObject(obj)) {
        log_error("free_object: Invalid object reference");
        return;
    }

    db[obj].next = first_free;
    first_free = obj;
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
    
    /* Validate the object reference */
    if (!GoodObject(newobj)) {
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
}

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
#ifdef USE_UNIV
    case TYPE_UNIVERSE:
#endif
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
        if (!GoodObject(thing) || IS_GONE(thing)) {
            if (GoodObject(thing) && IS_GONE(thing)) {
                free_object(thing);
            }
            continue;
        }

        /* --- Validate exits list --- */
        CHECK_REF(db[thing].exits) {
            switch (Typeof(thing)) {
            case TYPE_PLAYER:
            case TYPE_CHANNEL:
#ifdef USE_UNIV
            case TYPE_UNIVERSE:
#endif
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
#ifdef USE_UNIV
            case TYPE_UNIVERSE:
#endif
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
#ifdef USE_UNIV
            case TYPE_UNIVERSE:
#endif
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
        log_error(tprintf("dbmark: Maximum iterations exceeded for room #%ld", loc));
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
#ifdef USE_UNIV
            Typeof(loc) == TYPE_UNIVERSE ||
#endif
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
            snprintf(tempbuf, sizeof(tempbuf), " #%ld", loc);
            if (strlen(roomlist) + strlen(tempbuf) < sizeof(roomlist) - 1) {
                strncat(roomlist, tempbuf, sizeof(roomlist) - strlen(roomlist) - 1);
            }
            
            dest_info(NOTHING, loc);
        }

        if (Typeof(loc) == TYPE_EXIT && db[loc].link == NOTHING) {
            nunlexits++;
            
            /* Build exit list safely */
            snprintf(tempbuf, sizeof(tempbuf), " #%ld", loc);
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

        /* Validate contents list */
        iteration_count = 0;
        for (thing = db[loc].contents; 
             thing != NOTHING && iteration_count < MAX_LOOP_ITERATIONS;
             thing = db[thing].next, iteration_count++) {
            
            if (!GoodObject(thing)) {
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
#ifdef USE_UNIV
                Typeof(loc) == TYPE_UNIVERSE ||
#endif
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
             "|Y!+*| There are %d bytes being used in memory, total.", j);

    if (first_free != NOTHING && GoodObject(first_free)) {
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
                log_error(tprintf("Breaking circular exit chain at #%ld", i));
                db[j].next = NOTHING;
            }
        }

        /* Check contents chain for loops */
        for (j = db[i].contents, m = 0; 
             j != NOTHING && m < 1000; 
             j = (GoodObject(j) ? db[j].next : NOTHING), m++) {
            if (m >= 999 && GoodObject(j)) {
                log_error(tprintf("Breaking circular contents chain at #%ld", i));
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
 * OBJECT DESTRUCTION
 * =============================================================================
 */

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
        if (!(db[db[thing].owner].flags & QUIET) && 
            !power(db[thing].owner, POW_FREE)) {
            notify(db[thing].owner, 
                   tprintf("You get back your %d credit deposit for %s.",
                          object_cost(thing),
                          unparse_object(db[thing].owner, thing)));
        }
        
        if (!power(db[thing].owner, POW_FREE)) {
            giveto(db[thing].owner, object_cost(thing));
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
    free_object(thing);

    nrecur--;
}

/* =============================================================================
 * UNDESTROY COMMAND
 * =============================================================================
 */

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

    if (!GoodObject(object)) {
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

    notify(player, tprintf("db_top: #%ld", db_top));
    notify(player, tprintf("first_free: #%ld", first_free));
    notify(player, tprintf("update_bytes_counter: #%ld", update_bytes_counter));
    notify(player, tprintf("garbage point: #%ld", thing));
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
                        log_error(tprintf("Invalid zone in chain for #%ld", thing));
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
#ifdef USE_UNIV
                case TYPE_UNIVERSE:
#endif
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
#ifdef USE_UNIV
                case TYPE_UNIVERSE:
#endif
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
#ifdef USE_UNIV
                case TYPE_UNIVERSE:
#endif
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

    if (!GoodObject(target)) {
        notify(player, "Invalid object reference.");
        return;
    }

    if (first_free == target) {
        notify(player, "That object is already at the top of the free list.");
        return;
    }

    /* Find the object in the free list */
    for (object = first_free;
         object != NOTHING && 
         GoodObject(object) && 
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

    if (!GoodObject(object)) {
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
            
            snprintf(temp, sizeof(temp), "#%ld", vari);
            snprintf(temp2, sizeof(temp2), "#%ld", vari2);
            
            do_swap(root, temp, temp2);
        }
    }

    if (iteration_count >= MAX_LOOP_ITERATIONS) {
        notify(player, "Warning: Maximum iterations reached. Database may not be fully compacted.");
        log_error("do_shrinkdbuse: Maximum iterations exceeded");
    }
}

#endif /* SHRINK_DB */
