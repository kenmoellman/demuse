/* utils.c */
/* $Id: utils.c,v 1.4 1993/08/16 01:57:27 nils Exp $ */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * SAFETY IMPROVEMENTS:
 * - Added GoodObject() validation
 * - Added NULL pointer checks
 * - Improved bounds checking
 *
 * CODE QUALITY:
 * - Full ANSI C compliance
 * - Better organization with section markers
 * - Comprehensive inline documentation
 * - Removed large blocks of commented-out code
 * - Cleaned up unused functions
 *
 * ANSI C COMPLIANCE:
 * - All function declarations use ANSI C prototypes
 * - Proper parameter types
 *
 * SECURITY NOTES:
 * - All database access validated with GoodObject()
 * - List operations handle invalid references
 *
 * DEPRECATED FUNCTIONS:
 * - mktm() and mkxtime() have been removed (commented out)
 * - These time conversion functions are being moved to datetime.c
 * - See datetime.h for replacement functions
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================ */

#include <sys/types.h>
#include <time.h>
#include <ctype.h>
#include "externs.h"
#include "db.h"
#include "config.h"

/* ============================================================================
 * TIMEZONE UTILITIES (DEPRECATED)
 * ============================================================================
 * 
 * NOTE: The timezone functions get_timezone(), mktm(), and mkxtime() have
 * been commented out as they are being migrated to the new datetime.c module.
 * 
 * For time/date functionality, see:
 * - src/hdrs/datetime.h for function declarations
 * - src/time.c for current implementations
 * - Future: src/datetime.c for consolidated time handling
 */

/*
 * Original timezone functions were here:
 * - static long get_timezone() - get local timezone offset
 * - char *mktm(time_t cl, char *tz, dbref thing) - format time with timezone
 * - long mkxtime(char *s, dbref thing, char *tz) - parse time string
 * 
 * These have been removed to avoid duplication with datetime.c
 */

/* ============================================================================
 * ENTRANCE FINDING
 * ============================================================================ */

/**
 * Find the entrance (source room) of an exit
 * 
 * This function returns the room that an exit leads from.
 * Historically this searched through all rooms, but now simply
 * returns the exit's location since that information is tracked.
 * 
 * SECURITY: Validates exit reference
 * 
 * @param door Exit object
 * @return Room the exit is in, or NOTHING if invalid
 */
dbref find_entrance(dbref door)
{
  if (!GoodObject(door)) {
    return NOTHING;
  }

  /* Exit's location is the room it's in */
  return db[door].location;
}

/* ============================================================================
 * LINKED LIST OPERATIONS
 * ============================================================================ */

/**
 * Remove first occurrence of 'what' from a linked list
 * 
 * This function removes the first occurrence of an object from
 * a next-linked list (like contents or exits).
 * 
 * SECURITY: Validates object references
 * 
 * @param first Head of linked list
 * @param what Object to remove from list
 * @return New head of list
 */
dbref remove_first(dbref first, dbref what)
{
  dbref prev;

  if (!GoodObject(what)) {
    return first;
  }

  /* Special case: removing the first element */
  if (first == what) {
    if (!GoodObject(first)) {
      return NOTHING;
    }
    return db[first].next;
  }

  /* Search through list */
  DOLIST(prev, first) {
    if (!GoodObject(prev)) {
      break;
    }
    
    if (db[prev].next == what) {
      db[prev].next = GoodObject(what) ? db[what].next : NOTHING;
      return first;
    }
  }

  return first;
}

/**
 * Remove first occurrence from fighting list
 * 
 * Similar to remove_first() but operates on the next_fighting
 * chain used by the combat system.
 * 
 * SECURITY: Validates object references
 * 
 * @param first Head of fighting list
 * @param what Object to remove
 * @return New head of list
 */
dbref remove_first_fighting(dbref first, dbref what)
{
  dbref prev;

  if (!GoodObject(what)) {
    return first;
  }

  /* Special case: removing the first element */
  if (first == what) {
    if (!GoodObject(first)) {
      return NOTHING;
    }
    return db[first].next_fighting;
  }

  /* Search through list */
  DOLIST(prev, first) {
    if (!GoodObject(prev)) {
      break;
    }
    
    if (db[prev].next_fighting == what) {
      db[prev].next_fighting = GoodObject(what) ? db[what].next_fighting : NOTHING;
      return first;
    }
  }

  return first;
}

/**
 * Check if thing is a member of a linked list
 * 
 * Searches through a next-linked list to determine if
 * an object is present.
 * 
 * SECURITY: Validates object references
 * 
 * @param thing Object to search for
 * @param list Head of list to search in
 * @return 1 if found, 0 if not found
 */
int member(dbref thing, dbref list)
{
  if (!GoodObject(thing)) {
    return 0;
  }

  DOLIST(list, list) {
    if (!GoodObject(list)) {
      break;
    }
    
    if (list == thing) {
      return 1;
    }
  }

  return 0;
}

/**
 * Reverse a linked list
 * 
 * Returns a new list that is the reverse of the input list.
 * This modifies the next pointers of the objects in the list.
 * 
 * SECURITY: Validates object references
 * WARNING: Modifies database next pointers
 * 
 * @param list Head of list to reverse
 * @return Head of reversed list
 */
dbref reverse(dbref list)
{
  dbref newlist;
  dbref rest;

  newlist = NOTHING;
  
  while (list != NOTHING && GoodObject(list)) {
    rest = db[list].next;
    PUSH(list, newlist);
    list = rest;
  }
  
  return newlist;
}

/* End of utils.c */
