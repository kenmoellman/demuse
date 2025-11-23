/* warnings.c - Database topology and security checking system
 * Validates database integrity and warns object owners about potential issues
 * 
 * This module performs incremental checks on database objects to identify:
 * - Missing descriptions and messages
 * - One-way or malformed exits
 * - Security vulnerabilities (wizbugs, privilege escalation)
 * - Topology issues (disconnected rooms, unlinked exits)
 * 
 * Modernization notes:
 * - Uses text stack system from nalloc.c for string management
 * - All tprintf() calls use automatic stack-based memory
 * - No explicit malloc/free needed in this module
 * - Added extensive safety checks and validation
 * - Uses snprintf/strncpy for buffer safety
 * - GoodObject() validation throughout
 */

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "config.h"
#include "externs.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_WARNING_BUFFER 1024

/* ===================================================================
 * Type Definitions
 * =================================================================== */

/* Structure for topology check function table */
struct tcheck_s
{
  char *name;
  void (*func)(dbref);
};

/* ===================================================================
 * Static Variables
 * =================================================================== */

/* Current object being checked (for incremental topology checking) */
static dbref current_object = NOTHING;

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

/* Utility functions */
static void complain(dbref i, char *name, char *desc);
static int lock_type(dbref i, char *str);

/* Individual topology checks */
static void ct_roomdesc(dbref i);
static void ct_onewayexit(dbref i);
static void ct_doubleexit(dbref i);
static void ct_exitmsgs(dbref i);
static void ct_exitdesc(dbref i);
static void ct_playdesc(dbref i);
static void ct_thngdesc(dbref i);
static void ct_thngmsgs(dbref i);
static void ct_exitnames(dbref i);
static void ct_nolinked(dbref i);
static void ct_security(dbref i);

/* Group check functions */
static void ct_none(dbref i);
static void ct_serious(dbref i);
static void ct_normal(dbref i);
static void ct_extra(dbref i);
static void ct_all(dbref i);

/* Main check dispatcher */
static void check_topology_on(dbref i);

/* ===================================================================
 * Check Function Table
 * =================================================================== */

static struct tcheck_s tchecks[] =
{
  /* Group checks - run multiple individual checks */
  {"none", ct_none},
  {"serious", ct_serious},
  {"normal", ct_normal},
  {"extra", ct_extra},
  {"all", ct_all},

  /* Individual checks - can be run standalone */
  {"roomdesc", ct_roomdesc},
  {"onewayexit", ct_onewayexit},
  {"doubleexit", ct_doubleexit},
  {"exitmsgs", ct_exitmsgs},
  {"exitdesc", ct_exitdesc},
  {"thngdesc", ct_thngdesc},
  {"playdesc", ct_playdesc},
  {"thngmsgs", ct_thngmsgs},
  {"exitnames", ct_exitnames},
  {"nolinked", ct_nolinked},
  {"security", ct_security},
  {NULL, NULL}
};

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * lock_type - Determine lock complexity level
 * 
 * Analyzes a lock string to determine its complexity, which affects
 * what warning messages should be displayed for success/failure cases.
 * 
 * @param i Object with the lock
 * @param str Lock string to analyze
 * @return 0=unlocked, 1=simple lock, 2=complex/conditional lock
 */
static int lock_type(dbref i, char *str)
{
  char owner_lock[64];
  char location_lock[64];
  
  /* Empty lock = unlocked */
  if (!str || !*str)
    return 0;
  
  /* Validate object before accessing db[] */
  if (!GoodObject(i))
    return 2;
  
  /* Check for owner-only lock pattern: #123&!#123 */
  snprintf(owner_lock, sizeof(owner_lock), "#%" DBREF_FMT "&!#" DBREF_FMT,
           db[i].owner, db[i].owner);
  if (!strcmp(str, owner_lock))
    return 1;
    
  /* Check for simple owner lock pattern: #123 */
  snprintf(owner_lock, sizeof(owner_lock), "#%" DBREF_FMT, db[i].owner);
  if (!strcmp(str, owner_lock))
    return 1;
  
  /* Check for location-based lock */
  if (GoodObject(db[i].location))
  {
    snprintf(location_lock, sizeof(location_lock), "#%" DBREF_FMT, db[i].location);
    if (!strcmp(str, location_lock))
      return 1;
  }
  
  /* Complex lock - too complicated to analyze, need both success and fail messages */
  return 2;
}

/**
 * complain - Send warning message to object owner
 * 
 * Checks if the warning is inhibited before sending. Warnings can be
 * suppressed per-object using the A_WINHIBIT attribute, which should
 * contain a space-separated list of warning types to ignore.
 * 
 * Also broadcasts to warning channels (warn_<type>) for administrator
 * monitoring of database issues.
 * 
 * @param i Object to warn about
 * @param name Warning type identifier (e.g., "security", "roomdesc")
 * @param desc Human-readable description of the problem
 */
static void complain(dbref i, char *name, char *desc)
{
  char buf[MAX_WARNING_BUFFER];
  char *x;
  char *y;
  
  /* Validate input parameters */
  if (!GoodObject(i) || !name || !desc)
    return;
  
  /* Validate owner before accessing */
  if (!GoodObject(db[i].owner))
    return;
  
  /* Check if this warning is inhibited for this object */
  x = buf;
  strncpy(buf, atr_get(i, A_WINHIBIT), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  
  while ((y = parse_up(&x, ' ')))
  {
    /* Check for specific warning type or "all" */
    if (!string_compare(y, name) || !string_compare(y, "all"))
      return;  /* User doesn't want to hear about this warning */
  }
  
  /* Send warning to object owner */
  snprintf(buf, sizeof(buf), "Warning '%s' for %s: %s", 
           name, unparse_object(db[i].owner, i), desc);
  notify(db[i].owner, buf);

  /* Notify interested parties on warning channels for monitoring */
  snprintf(buf, sizeof(buf), "* %s: %s", 
           unparse_object(db[i].owner, i), desc);
  com_send(tprintf("warn_%s", name), buf);
}

/* ===================================================================
 * Individual Check Functions
 * =================================================================== */

/**
 * ct_roomdesc - Check if room has a description
 * 
 * Rooms without descriptions are confusing to players and indicate
 * incomplete area development.
 */
static void ct_roomdesc(dbref i)
{
  if (!GoodObject(i))
    return;
    
  if ((Typeof(i) == TYPE_ROOM) && !*atr_get(i, A_DESC))
    complain(i, "roomdesc", "Room has no description.");
}

/**
 * ct_onewayexit - Check for exits without return paths
 * 
 * One-way exits can trap players or make navigation confusing.
 * This checks if an exit has a corresponding return exit in the
 * destination room.
 */
static void ct_onewayexit(dbref i)
{
  dbref j;

  if (!GoodObject(i))
    return;

  /* Only check exits that link to rooms (not home, etc.) */
  if ((Typeof(i) != TYPE_EXIT) || 
      (db[i].link == NOTHING) || 
      (Typeof(db[i].link) != TYPE_ROOM) || 
      db[i].link == db[i].location)
    return;
  
  /* Validate the destination */
  if (!GoodObject(db[i].link))
    return;
  
  /* Look for a return exit in the destination room */
  for (j = db[db[i].link].exits; j != NOTHING; j = db[j].next)
  {
    if (!GoodObject(j))
      break;
    if (db[j].link == db[i].location)
      return;  /* Found return exit */
  }
  
  complain(i, "onewayexit", "Exit has no return exit.");
}

/**
 * ct_doubleexit - Check for multiple return exits
 * 
 * Multiple exits between the same two rooms can be confusing.
 * This is typically a building error where someone created
 * redundant exit pairs.
 */
static void ct_doubleexit(dbref i)
{
  dbref j;
  int count = 0;

  if (!GoodObject(i))
    return;

  /* Only check exits that link to rooms */
  if ((Typeof(i) != TYPE_EXIT) || 
      (db[i].link == NOTHING) || 
      (Typeof(db[i].link) != TYPE_ROOM) || 
      db[i].location == db[i].link)
    return;
  
  /* Validate the destination */
  if (!GoodObject(db[i].link))
    return;
  
  /* Count return exits in the destination room */
  for (j = db[db[i].link].exits; j != NOTHING; j = db[j].next)
  {
    if (!GoodObject(j))
      break;
    if (db[j].link == db[i].location)
      count++;
  }
  
  if (count > 1)
    complain(i, "doubleexit", "Exit has multiple return exits.");
}

/**
 * ct_exitmsgs - Check if exit has appropriate success/fail messages
 * 
 * Exits should have appropriate feedback messages based on their lock:
 * - Unlocked/sometimes-locked: need osucc, odrop, succ
 * - Locked/sometimes-locked: need ofail, fail
 * 
 * Dark exits are exempt from this check.
 */
static void ct_exitmsgs(dbref i)
{
  int lt;

  if (!GoodObject(i))
    return;

  /* Skip dark exits - they're intentionally minimal */
  if ((Typeof(i) != TYPE_EXIT) || (db[i].flags & DARK))
    return;
    
  lt = lock_type(i, atr_get(i, A_LOCK));
  
  /* Check success messages for unlocked or conditionally locked exits */
  if ((lt != 1) && (!*atr_get(i, A_OSUCC) ||
                    !*atr_get(i, A_ODROP) ||
                    !*atr_get(i, A_SUCC)))
    complain(i, "exitmsgs", "Exit is missing one or more of osucc, odrop, succ.");
    
  /* Check failure messages for locked or conditionally locked exits */
  if ((lt != 0) && (!*atr_get(i, A_OFAIL) ||
                    !*atr_get(i, A_FAIL)))
    complain(i, "exitmsgs", "Exit is missing one or more of fail, ofail.");
}

/**
 * ct_exitdesc - Check if exit has a description
 * 
 * Exit descriptions help players understand where the exit leads.
 * Dark exits are exempt from this check.
 */
static void ct_exitdesc(dbref i)
{
  if (!GoodObject(i))
    return;
    
  if ((Typeof(i) != TYPE_EXIT) || (db[i].flags & DARK))
    return;
    
  if (!*atr_get(i, A_DESC))
    complain(i, "exitdesc", "Exit is missing description.");
}

/**
 * ct_playdesc - Check if player has a description
 * 
 * Players without descriptions appear incomplete to other players.
 */
static void ct_playdesc(dbref i)
{
  if (!GoodObject(i))
    return;
    
  if (Typeof(i) != TYPE_PLAYER)
    return;
    
  if (!*atr_get(i, A_DESC))
    complain(i, "playdesc", "Player is missing description.");
}

/**
 * ct_thngdesc - Check if thing has a description
 * 
 * Things in public areas should have descriptions. Things in their
 * owner's inventory are exempt (may be works in progress).
 */
static void ct_thngdesc(dbref i)
{
  if (!GoodObject(i))
    return;
    
  /* Don't check things in their owner's inventory */
  if ((Typeof(i) != TYPE_THING) || (db[i].location == db[i].owner))
    return;
    
  if (!*atr_get(i, A_DESC))
    complain(i, "thngdesc", "Thing is missing description.");
}

/**
 * ct_thngmsgs - Check if thing has appropriate success/fail messages
 * 
 * Things should have appropriate feedback messages based on their lock:
 * - Unlocked/sometimes-locked: need osucc, odrop, succ, drop
 * - Locked/sometimes-locked: need ofail, fail
 * 
 * Things in their owner's inventory are exempt.
 */
static void ct_thngmsgs(dbref i)
{
  int lt;

  if (!GoodObject(i))
    return;

  /* Don't check things in their owner's inventory */
  if ((Typeof(i) != TYPE_THING) || (db[i].location == db[i].owner))
    return;
    
  lt = lock_type(i, atr_get(i, A_LOCK));
  
  /* Check success messages */
  if ((lt != 1) && (!*atr_get(i, A_OSUCC) ||
                    !*atr_get(i, A_ODROP) ||
                    !*atr_get(i, A_SUCC) ||
                    !*atr_get(i, A_DROP)))
    complain(i, "thngmsgs", "Thing is missing one or more of osucc,odrop,succ,drop.");
    
  /* Check failure messages */
  if ((lt != 0) && (!*atr_get(i, A_OFAIL) ||
                    !*atr_get(i, A_FAIL)))
    complain(i, "thngmsgs", "Thing is missing one or more of ofail,fail.");
}

/**
 * ct_exitnames - Check exit naming conventions
 * 
 * TODO: Check for proper exit naming patterns and aliases.
 * This was marked as "soon to be written" in the original code.
 */
static void ct_exitnames(dbref i)
{
  if (!GoodObject(i))
    return;
    
  /* To be implemented */
}

/**
 * ct_nolinked - Check for unlinked exits
 * 
 * SECURITY ISSUE: Unlinked exits can be stolen by anyone who can
 * link them. This is a serious security vulnerability.
 */
static void ct_nolinked(dbref i)
{
  if (!GoodObject(i))
    return;
    
  if ((Typeof(i) == TYPE_EXIT) && (db[i].link == NOTHING))
    complain(i, "nolinked", "Exit is unlinked; anyone can steal it.");
}

/**
 * ct_security - Check for potential security vulnerabilities
 * 
 * This function detects several classes of security issues:
 * 
 * 1. Privilege Escalation via Parents:
 *    If an object controls its parent but the parent doesn't control it,
 *    the parent owner could insert malicious code (wizbugs).
 * 
 * 2. Known Exploit Patterns:
 *    - "$bork *:" pattern (historical draco wizbug)
 *    - Use of %0 or v(0) in command triggers without proper validation
 *    - @force commands that execute arbitrary code from %0
 *    - Dbref-targeted commands with unvalidated %0 parameters
 * 
 * These patterns allow privilege escalation where a lower-privileged
 * player can execute code with higher privileges.
 */
static void ct_security(dbref i)
{
  int j;
  ALIST *al;
  char *x;

  if (!GoodObject(i))
    return;

  /* Check parent relationships for privilege escalation opportunities */
  if (db[i].parents)
  {
    for (j = 0; db[i].parents[j] != NOTHING; j++)
    {
      if (!GoodObject(db[i].parents[j]))
        continue;
        
      /* If child controls parent but parent doesn't control child,
       * parent owner could insert wizbug code */
      if (controls(i, db[i].parents[j], POW_MODIFY) &&
          !controls(db[i].parents[j], i, POW_MODIFY) &&
          !(db[i].flags & HAVEN && !db[i].children))
      {
        complain(i, "security", 
                tprintf("Wizbug may be inserted on parent %s.", 
                        unparse_object(db[db[i].parents[j]].owner, db[i].parents[j])));
      }
    }
  }
  
  /* Check for vulnerable command attributes */
  if (db[i].list && !*atr_get(i, A_ULOCK) && Typeof(i) != TYPE_PLAYER)
  {
    for (al = db[i].list; al; al = AL_NEXT(al))
    {
      /* Validate attribute list entry - AL_STR is always valid if al is valid */
      if (!AL_TYPE(al))
        continue;
        
      /* Only check command triggers ($ and !) */
      if (*AL_STR(al) != '$' && *AL_STR(al) != '!')
        continue;
      
      /* Find command/action separator */
      x = strchr(AL_STR(al), ':');
      if (!x)
        continue;
      
      /* Check for known dangerous "draco wizbug" pattern */
      if (!strncmp(AL_STR(al), "$bork *:", 8))
      {
        complain(i, "security", 
                tprintf("I bet draco has a wizbug on attribute %s.", 
                        unparse_attr(AL_TYPE(al), 0)));
        continue;
      }
      
      x++;  /* Move past the colon */
      
      /* Skip over regex flags if present (format: /regex/) */
      if (*x == '/')
      {
        for (x++; *x != '/' && *x; x++)
          ;
        if (*x)
          x++;
      }
      
      /* Check for direct use of %0 or v(0) without validation */
      if (!strncmp(x, "%0", 2) || !strncmp(x, "[v(0", 4))
      {
        complain(i, "security", 
                tprintf("Wizbug may be present on attribute %s.", 
                        unparse_attr(AL_TYPE(al), 0)));
        continue;
      }
      
      /* Check for @force with unvalidated %0 */
      if (!strncmp(x, "@fo", 3))
      {
        /* Find the = separator in @force command */
        for (; *x && *x != '='; x++)
          ;
        if (!*x)
          continue;
        x++;
        
        /* Check if the forced command uses %0 */
        if (!strncmp(x, "%0", 2) || !strncmp(x, "[v(0", 4))
        {
          complain(i, "security", 
                  tprintf("Wizbug may be present on attribute %s.", 
                          unparse_attr(AL_TYPE(al), 0)));
          continue;
        }
      }
      /* Check for commands targeting specific dbrefs with %0 */
      else if (*x == '#')
      {
        /* Skip to the command after the dbref */
        for (; *x && *x != ' '; x++)
          ;
        if (!*x)
          continue;
        x++;
        
        /* Check if the command uses unvalidated %0 */
        if (!strncmp(x, "%0", 2) || !strncmp(x, "[v(0", 4))
        {
          complain(i, "security", 
                  tprintf("Wizbug may be present on attribute %s.", 
                          unparse_attr(AL_TYPE(al), 0)));
          continue;
        }
      }
    }
  }
}

/* ===================================================================
 * Group Check Functions
 * =================================================================== */

/**
 * ct_none - Perform no checks
 * 
 * Used when a player wants to completely disable warnings.
 */
static void ct_none(dbref i)
{
  /* Do absolutely nothing */
}

/**
 * ct_serious - Perform only serious security checks
 * 
 * Minimal warning level - only reports:
 * - Missing room descriptions
 * - Unlinked exits (security risk)
 * - Security vulnerabilities
 */
static void ct_serious(dbref i)
{
  if (!GoodObject(i))
    return;
    
  ct_roomdesc(i);
  ct_nolinked(i);
  ct_security(i);
}

/**
 * ct_normal - Perform normal level of checks
 * 
 * Default warning level - reports:
 * - Player descriptions
 * - Room descriptions
 * - One-way exits
 * - Multiple return exits
 * - Exit naming issues
 * - Unlinked exits
 * - Security vulnerabilities
 */
static void ct_normal(dbref i)
{
  if (!GoodObject(i))
    return;
    
  ct_playdesc(i);
  ct_roomdesc(i);
  ct_onewayexit(i);
  ct_doubleexit(i);
  ct_exitnames(i);
  ct_nolinked(i);
  ct_security(i);
}

/**
 * ct_extra - Perform extra checks
 * 
 * Enhanced warning level - reports everything in normal plus:
 * - Thing descriptions
 * - Thing messages
 * - Exit messages
 */
static void ct_extra(dbref i)
{
  if (!GoodObject(i))
    return;
    
  ct_roomdesc(i);
  ct_onewayexit(i);
  ct_doubleexit(i);
  ct_playdesc(i);
  ct_exitmsgs(i);
  ct_thngdesc(i);
  ct_thngmsgs(i);
  ct_exitnames(i);
  ct_nolinked(i);
  ct_security(i);
}

/**
 * ct_all - Perform all checks
 * 
 * Maximum warning level - reports everything, including:
 * - All extra checks
 * - Exit descriptions
 */
static void ct_all(dbref i)
{
  if (!GoodObject(i))
    return;
    
  ct_extra(i);
  ct_exitdesc(i);
}

/* ===================================================================
 * Main Check Functions
 * =================================================================== */

/**
 * check_topology_on - Run topology checks on a specific object
 * 
 * Reads the object owner's A_WARNINGS attribute to determine which
 * checks to run. The attribute should contain a space-separated list
 * of check names from the tchecks table.
 * 
 * Default warning level is "normal" if no preference is set.
 * 
 * @param i Object to check
 */
static void check_topology_on(dbref i)
{
  char buf[MAX_WARNING_BUFFER];
  char *x;
  char *y;
  int j;

  if (!GoodObject(i) || !GoodObject(db[i].owner))
    return;

  /* Get warning level preference for this player */
  x = buf;
  strncpy(buf, (*atr_get(db[i].owner, A_WARNINGS)) ? 
          atr_get(db[i].owner, A_WARNINGS) : "normal",
          sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  
  /* Process each warning type in the list */
  while ((y = parse_up(&x, ' ')))
  {
    /* Find and execute the check function */
    for (j = 0; tchecks[j].name; j++)
    {
      if (!string_compare(tchecks[j].name, y))
      {
        (*tchecks[j].func)(i);
        break;
      }
    }
    
    /* Warn about unknown check types (only for players) */
    if (!tchecks[j].name && Typeof(i) == TYPE_PLAYER)
      notify(i, tprintf("Unknown warning: %s", y));
  }
}

/**
 * run_topology - Incremental topology checker main loop
 * 
 * Called periodically by the timer system to check a chunk of database
 * objects for problems. Uses warning_chunk and warning_bonus config
 * values to control checking rate.
 * 
 * Priority system:
 * - Objects owned by connected players get full checks + bonus
 * - Objects owned by privileged (non-PW_NO modify power) players get
 *   security checks only + bonus
 * - Other objects are skipped until their owners connect
 * 
 * This ensures active players get timely warnings while not wasting
 * CPU on inactive players' objects.
 */
void run_topology(void)
{
  int ndone;

  for (ndone = 0; ndone < warning_chunk; ndone++)
  {
    /* Advance to next object (wrap at end of database) */
    current_object++;
    if (current_object >= db_top)
      current_object = (dbref) 0;
      
    /* Skip invalid objects */
    if (!GoodObject(current_object))
      continue;
      
    /* Skip destroyed objects */
    if (db[current_object].flags & GOING)
      continue;
    
    /* Check objects owned by connected players (with bonus) */
    if (db[db[current_object].owner].flags & CONNECT)
    {
      check_topology_on(current_object);
      ndone += warning_bonus;
    }
    /* Check privileged players' objects for security issues only (with bonus) */
    else if (get_pow(db[current_object].owner, POW_MODIFY) != PW_NO)
    {
      ct_security(current_object);
      ndone += warning_bonus;
    }
  }
}
